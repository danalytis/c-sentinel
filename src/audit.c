/*
 * audit.c - Auditd log parsing and summarisation for C-Sentinel
 * 
 * Uses ausearch for reliable event extraction, then summarises
 * for semantic analysis by LLMs.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>
#include "../include/audit.h"

/* Baseline file location */
#define AUDIT_BASELINE_PATH_USER    ".sentinel/audit_baseline.dat"
#define AUDIT_BASELINE_PATH_SYSTEM  "/var/lib/sentinel/audit_baseline.dat"
#define AUDIT_BASELINE_MAGIC        "SNTLAUDT"
#define AUDIT_BASELINE_VERSION      1

/* EMA smoothing factor - 0.2 means recent data weighted 20% */
#define EMA_ALPHA 0.2f

/* Salt for username hashing (generated once, stored in config) */
static char username_salt[32] = "sentinel_default_salt";

/* ============================================================
 * Event Context Cache - correlate SYSCALL and PATH records
 * ============================================================ */
#define MAX_AUDIT_EVENTS 256

typedef struct {
    int event_id;
    pid_t pid;
    pid_t ppid;                         /* Parent PID - key for chain building */
    char comm[32];
    char exe[256];
    bool used;
} audit_event_ctx_t;

static audit_event_ctx_t event_ctx[MAX_AUDIT_EVENTS];
static int event_ctx_count = 0;

/* Extract event ID from audit line: msg=audit(1767386347.120:631) -> 631 */
static int extract_event_id(const char *line) {
    const char *p = strstr(line, "msg=audit(");
    if (!p) return -1;
    
    p = strchr(p, ':');
    if (!p) return -1;
    
    return atoi(p + 1);
}

/* Find or create context slot for an event ID */
static audit_event_ctx_t* get_event_ctx(int event_id) {
    /* Look for existing */
    for (int i = 0; i < event_ctx_count; i++) {
        if (event_ctx[i].used && event_ctx[i].event_id == event_id) {
            return &event_ctx[i];
        }
    }
    
    /* Create new if space */
    if (event_ctx_count >= MAX_AUDIT_EVENTS) {
        return NULL;
    }
    
    audit_event_ctx_t *ctx = &event_ctx[event_ctx_count++];
    memset(ctx, 0, sizeof(*ctx));
    ctx->event_id = event_id;
    ctx->used = true;
    return ctx;
}

/* Clear event context cache */
static void clear_event_ctx(void) {
    memset(event_ctx, 0, sizeof(event_ctx));
    event_ctx_count = 0;
}

/* Parse SYSCALL records to build event context (pid, ppid, comm, exe) */
static void parse_syscall_context(int window_seconds) {
    char cmd[512];
    char line[2048];
    FILE *fp;
    
    (void)window_seconds;
    
    snprintf(cmd, sizeof(cmd),
             "ausearch -m SYSCALL -ts today --format raw 2>/dev/null");
    
    fp = popen(cmd, "r");
    if (!fp) return;
    
    while (fgets(line, sizeof(line), fp)) {
        int event_id = extract_event_id(line);
        if (event_id < 0) continue;
        
        audit_event_ctx_t *ctx = get_event_ctx(event_id);
        if (!ctx) continue;
        
        /* Extract pid */
        char *pid_str = strstr(line, " pid=");
        if (pid_str) {
            ctx->pid = atoi(pid_str + 5);
        }
        
        /* Extract ppid - KEY FOR CHAIN BUILDING */
        char *ppid_str = strstr(line, " ppid=");
        if (ppid_str) {
            ctx->ppid = atoi(ppid_str + 6);
        }
        
        /* Extract comm="..." */
        char *comm = strstr(line, " comm=\"");
        if (comm) {
            comm += 7;
            int i = 0;
            while (*comm && *comm != '"' && i < 31) {
                ctx->comm[i++] = *comm++;
            }
            ctx->comm[i] = '\0';
        }
        
        /* Extract exe="..." */
        char *exe = strstr(line, " exe=\"");
        if (exe) {
            exe += 6;
            int i = 0;
            while (*exe && *exe != '"' && i < 255) {
                ctx->exe[i++] = *exe++;
            }
            ctx->exe[i] = '\0';
        }
    }
    
    pclose(fp);
}


/*
 * Hash a username for privacy-preserving output
 * Output format: "user_xxxx" where xxxx is first 4 chars of hash
 */
void hash_username(const char *username, char *output, size_t outsize) {
    if (!username || !output || outsize < HASH_USERNAME_LEN) {
        if (output && outsize > 0) output[0] = '\0';
        return;
    }
    
    /* Combine username with salt */
    char salted[256];
    snprintf(salted, sizeof(salted), "%s:%s", username_salt, username);
    
    /* Use our existing SHA256 */
    char hash[65];
    sha256_string(salted, hash, sizeof(hash));
    
    /* Take first 4 chars for readability */
    snprintf(output, outsize, "user_%.4s", hash);
}


/*
 * Find or add a hashed user to the failure list
 */
static hashed_user_t* find_or_add_user(audit_summary_t *summary, const char *username) {
    char hashed[HASH_USERNAME_LEN];
    hash_username(username, hashed, sizeof(hashed));
    
    /* Look for existing */
    for (int i = 0; i < summary->failure_user_count; i++) {
        if (strcmp(summary->failure_users[i].hash, hashed) == 0) {
            return &summary->failure_users[i];
        }
    }
    
    /* Add new if space */
    if (summary->failure_user_count < MAX_AUDIT_USERS) {
        hashed_user_t *user = &summary->failure_users[summary->failure_user_count++];
        memset(user->hash, 0, sizeof(user->hash));
        memcpy(user->hash, hashed, sizeof(user->hash) - 1);
        user->count = 0;
        return user;
    }
    
    return NULL;
}


/*
 * Parse ausearch output for authentication events
 * Looks for: type=USER_AUTH ... res=failed
 */
static void parse_auth_events(audit_summary_t *summary, int window_seconds) {
    char cmd[512];
    char line[2048];
    FILE *fp;
    
    (void)window_seconds;
    
    /* Use raw format for stable parsing */
    snprintf(cmd, sizeof(cmd), 
             "ausearch -m USER_AUTH -ts today --format raw 2>/dev/null | grep -E 'res=(success|failed)' | tail -100 2>/dev/null");
    
    fp = popen(cmd, "r");
    if (!fp) {
        return;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        /* Check result */
        if (strstr(line, "res=failed")) {
            summary->auth_failures++;
            
            /* Extract username from acct="..." (raw format has quotes) */
            char *acct = strstr(line, "acct=\"");
            if (acct) {
                acct += 6;
                char username[64] = {0};
                int i = 0;
                while (*acct && *acct != '"' && i < 63) {
                    username[i++] = *acct++;
                }
                username[i] = '\0';
                
                if (strlen(username) > 0) {
                    hashed_user_t *user = find_or_add_user(summary, username);
                    if (user) {
                        user->count++;
                    }
                }
            }
        } else if (strstr(line, "res=success")) {
            summary->auth_successes++;
        }
    }
    
    pclose(fp);
    
    /* Detect brute force: >5 failures in the window */
    summary->brute_force_detected = (summary->auth_failures > 5);
}


/*
 * Parse sudo/privilege escalation events
 */
static void parse_priv_events(audit_summary_t *summary, int window_seconds) {
    char cmd[512];
    char line[1024];
    FILE *fp;
    
    (void)window_seconds;
    
    /* Count sudo usage - raw format has exe="/usr/bin/sudo" with quotes */
    snprintf(cmd, sizeof(cmd),
             "ausearch -m USER_CMD -ts today --format raw 2>/dev/null | grep -c 'exe=\"/usr/bin/sudo\"' 2>/dev/null");
    
    fp = popen(cmd, "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            summary->sudo_count = atoi(line);
        }
        pclose(fp);
    }
    
    /* Count su usage */
    snprintf(cmd, sizeof(cmd),
             "ausearch -m USER_CMD -ts today --format raw 2>/dev/null | grep -c 'exe=\"/usr/bin/su\"' 2>/dev/null");
    
    fp = popen(cmd, "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            summary->su_count = atoi(line);
        }
        pclose(fp);
    }
}


/*
 * Parse sensitive file access events (from our watch rules)
 */
static void parse_file_events(audit_summary_t *summary, int window_seconds) {
    char cmd[512];
    char line[2048];
    FILE *fp;
    
    (void)window_seconds;
    
    /* Identity files (actual file access) - these have nametype=NORMAL */
    snprintf(cmd, sizeof(cmd),
             "ausearch -k identity -ts today --format raw 2>/dev/null | grep 'type=PATH' | grep 'nametype=NORMAL' 2>/dev/null");
    
    fp = popen(cmd, "r");
    if (!fp) {
        return;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        /* Get event ID for correlation with SYSCALL context */
        int event_id = extract_event_id(line);
        audit_event_ctx_t *ctx = NULL;
        if (event_id >= 0) {
            ctx = get_event_ctx(event_id);
        }
        
        /* Look for name="..." in raw format */
        char *name = strstr(line, "name=\"");
        if (!name) continue;
        
        name += 6;
        
        char path[AUDIT_PATH_LEN] = {0};
        int i = 0;
        while (*name && *name != '"' && i < AUDIT_PATH_LEN - 1) {
            path[i++] = *name++;
        }
        path[i] = '\0';
        
        size_t pathlen = strlen(path);
        if (pathlen > 5 && path[pathlen-1] != '/' && 
            summary->sensitive_file_count < MAX_AUDIT_FILES) {
            
            /* Check if we already have this file */
            bool found = false;
            for (int j = 0; j < summary->sensitive_file_count; j++) {
                if (strcmp(summary->sensitive_files[j].path, path) == 0) {
                    summary->sensitive_files[j].count++;
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                file_access_t *fa = &summary->sensitive_files[summary->sensitive_file_count++];
                memset(fa, 0, sizeof(*fa));
                strncpy(fa->path, path, sizeof(fa->path) - 1);
                strcpy(fa->access_type, "write");
                fa->count = 1;
                
                /* Attach process info from SYSCALL context */
                if (ctx && ctx->comm[0]) {
                    strncpy(fa->process, ctx->comm, sizeof(fa->process) - 1);
                    
                    /* Build process chain:
                     * 1. First entry is the audited process (from audit log, process may be dead)
                     * 2. Then walk from ppid (parent is likely still alive)
                     */
                    process_chain_t *chain = &fa->chain;
                    memset(chain, 0, sizeof(*chain));
                    
                    /* First hop: audited process name from audit log */
                    strncpy(chain->names[0], ctx->comm, sizeof(chain->names[0]) - 1);
                    chain->depth = 1;
                    
                    /* Continue from ppid (parent should still exist) */
                    if (ctx->ppid > 1) {
                        build_process_chain(ctx->ppid, chain);
                    }
                    
                    /* Check for suspicious process chains */
                    const char *reason = NULL;
                    if (is_suspicious_chain(chain, &reason)) {
                        fa->suspicious = true;
                        summary->suspicious_exec_count++;
                    }
                }
                
                /* Also mark shadow/sudoers file access as suspicious */
                if (strstr(path, "shadow") || strstr(path, "sudoers")) {
                    fa->suspicious = true;
                }
            }
        }
    }
    
    pclose(fp);
}


/*
 * Check for executions from suspicious locations (/tmp, /dev/shm)
 */
static void parse_exec_events(audit_summary_t *summary, int window_seconds) {
    char cmd[512];
    char line[1024];
    FILE *fp;
    
    (void)window_seconds;  /* Could use for time filtering later */
    
    /* Look for execve syscalls with paths in /tmp or /dev/shm */
    snprintf(cmd, sizeof(cmd),
             "ausearch -sc execve -ts today -i 2>/dev/null | grep -E 'name=(/tmp/|/dev/shm/)' 2>/dev/null");
    
    fp = popen(cmd, "r");
    if (!fp) {
        return;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        /* Check path location */
        if (strstr(line, "/tmp/")) {
            summary->tmp_executions++;
        }
        if (strstr(line, "/dev/shm/")) {
            summary->devshm_executions++;
        }
    }
    
    pclose(fp);
    
    /* Count shell spawns */
    snprintf(cmd, sizeof(cmd),
             "ausearch -sc execve -ts today -i 2>/dev/null | grep -cE 'name=.*/bin/(ba)?sh' 2>/dev/null");
    
    fp = popen(cmd, "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            summary->shell_spawns = atoi(line);
        }
        pclose(fp);
    }
}


/*
 * Check SELinux/AppArmor status
 */
static void check_security_framework(audit_summary_t *summary) {
    FILE *fp;
    char line[256];
    
    /* Check SELinux */
    fp = fopen("/sys/fs/selinux/enforce", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            summary->selinux_enforcing = (atoi(line) == 1);
        }
        fclose(fp);
        
        /* Count AVC denials */
        fp = popen("ausearch -m AVC -ts today 2>/dev/null | grep -c 'denied' 2>/dev/null", "r");
        if (fp) {
            if (fgets(line, sizeof(line), fp)) {
                summary->selinux_avc_denials = atoi(line);
            }
            pclose(fp);
        }
    }
    
    /* Check AppArmor */
    fp = popen("ausearch -m APPARMOR_DENIED -ts today 2>/dev/null | wc -l 2>/dev/null", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            summary->apparmor_denials = atoi(line);
        }
        pclose(fp);
    }
}


/*
 * Calculate deviation percentage from baseline
 */
float calculate_deviation_pct(float current, float baseline_avg) {
    if (baseline_avg < 0.1f) {
        /* Baseline near zero - any activity is significant */
        return current > 0 ? 100.0f : 0.0f;
    }
    return ((current - baseline_avg) / baseline_avg) * 100.0f;
}


/*
 * Determine significance of deviation
 */
const char* deviation_significance(float deviation_pct) {
    if (deviation_pct > 500.0f) return "CRITICAL";
    if (deviation_pct > 200.0f) return "HIGH";
    if (deviation_pct > 100.0f) return "MEDIUM";
    if (deviation_pct > 50.0f) return "LOW";
    return "NORMAL";
}


/*
 * Add an anomaly to the summary
 */
static void add_anomaly(audit_summary_t *summary, const char *type, 
                       const char *description, const char *severity,
                       float current, float baseline, float deviation) {
    if (summary->anomaly_count >= MAX_AUDIT_ANOMALIES) {
        return;
    }
    
    audit_anomaly_t *a = &summary->anomalies[summary->anomaly_count++];
    strncpy(a->type, type, sizeof(a->type) - 1);
    strncpy(a->description, description, sizeof(a->description) - 1);
    strncpy(a->severity, severity, sizeof(a->severity) - 1);
    a->current_value = current;
    a->baseline_avg = baseline;
    a->deviation_pct = deviation;
    a->timestamp = time(NULL);
}


/*
 * Detect anomalies by comparing against baseline
 */
static void detect_anomalies(audit_summary_t *summary, const audit_baseline_t *baseline) {
    if (!baseline || baseline->sample_count < 5) {
        /* Not enough baseline data yet */
        return;
    }
    
    /* Auth failures */
    summary->auth_baseline_avg = baseline->avg_auth_failures;
    summary->auth_deviation_pct = calculate_deviation_pct(
        (float)summary->auth_failures, baseline->avg_auth_failures);
    
    if (summary->auth_deviation_pct > 100.0f) {
        char desc[128];
        snprintf(desc, sizeof(desc), "%d auth failures (%.0f%% above baseline)",
                summary->auth_failures, summary->auth_deviation_pct);
        add_anomaly(summary, "auth_failure_spike", desc,
                   deviation_significance(summary->auth_deviation_pct),
                   (float)summary->auth_failures, baseline->avg_auth_failures,
                   summary->auth_deviation_pct);
    }
    
    /* Sudo usage */
    summary->sudo_baseline_avg = baseline->avg_sudo_count;
    summary->sudo_deviation_pct = calculate_deviation_pct(
        (float)summary->sudo_count, baseline->avg_sudo_count);
    
    if (summary->sudo_deviation_pct > 200.0f) {
        char desc[128];
        snprintf(desc, sizeof(desc), "%d sudo commands (%.0f%% above baseline)",
                summary->sudo_count, summary->sudo_deviation_pct);
        add_anomaly(summary, "sudo_spike", desc,
                   deviation_significance(summary->sudo_deviation_pct),
                   (float)summary->sudo_count, baseline->avg_sudo_count,
                   summary->sudo_deviation_pct);
    }
    
    /* /tmp executions are always suspicious if non-zero */
    if (summary->tmp_executions > 0) {
        char desc[128];
        snprintf(desc, sizeof(desc), "%d executions from /tmp", summary->tmp_executions);
        add_anomaly(summary, "tmp_execution", desc, "HIGH",
                   (float)summary->tmp_executions, 0, 100.0f);
    }
    
    /* /dev/shm executions are very suspicious */
    if (summary->devshm_executions > 0) {
        char desc[128];
        snprintf(desc, sizeof(desc), "%d executions from /dev/shm", summary->devshm_executions);
        add_anomaly(summary, "devshm_execution", desc, "CRITICAL",
                   (float)summary->devshm_executions, 0, 100.0f);
    }
}


/*
 * Calculate risk score based on findings
 */
void calculate_risk_score(audit_summary_t *summary) {
    int score = 0;
    
    /* Authentication */
    score += summary->auth_failures * 1;
    if (summary->brute_force_detected) score += 10;
    
    /* Apply deviation multiplier */
    if (summary->auth_deviation_pct > 500.0f) {
        score = (int)(score * 5.0f);
    } else if (summary->auth_deviation_pct > 200.0f) {
        score = (int)(score * 3.0f);
    } else if (summary->auth_deviation_pct > 100.0f) {
        score = (int)(score * 2.0f);
    }
    
    /* Privilege escalation */
    /* sudo is generally normal, but high deviation is concerning */
    if (summary->sudo_deviation_pct > 200.0f) {
        score += 5;
    }
    score += summary->su_count * 2;
    
    /* File integrity */
    score += summary->permission_changes * 3;
    score += summary->ownership_changes * 3;
    for (int i = 0; i < summary->sensitive_file_count; i++) {
        score += 2;
        if (summary->sensitive_files[i].suspicious) {
            score += 5;
        }
    }
    
    /* Process activity */
    score += summary->tmp_executions * 4;
    score += summary->devshm_executions * 6;
    score += summary->suspicious_exec_count * 10;
    
    /* Security framework */
    score += summary->selinux_avc_denials * 1;
    score += summary->apparmor_denials * 1;
    
    summary->risk_score = score;
    
    /* Determine level */
    if (score >= 31) {
        strcpy(summary->risk_level, "critical");
    } else if (score >= 16) {
        strcpy(summary->risk_level, "high");
    } else if (score >= 6) {
        strcpy(summary->risk_level, "medium");
    } else {
        strcpy(summary->risk_level, "low");
    }
}


/*
 * Load audit baseline from disk
 */
bool load_audit_baseline(audit_baseline_t *baseline) {
    char path[MAX_PATH_LEN];
    FILE *fp;
    
    /* Try system path first, then user path */
    snprintf(path, sizeof(path), "%s", AUDIT_BASELINE_PATH_SYSTEM);
    fp = fopen(path, "rb");
    
    if (!fp) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/%s", home, AUDIT_BASELINE_PATH_USER);
            fp = fopen(path, "rb");
        }
    }
    
    if (!fp) {
        return false;
    }
    
    if (fread(baseline, sizeof(*baseline), 1, fp) != 1) {
        fclose(fp);
        return false;
    }
    fclose(fp);
    
    /* Validate magic */
    if (memcmp(baseline->magic, AUDIT_BASELINE_MAGIC, 8) != 0) {
        return false;
    }
    
    return true;
}


/*
 * Save audit baseline to disk
 */
bool save_audit_baseline(const audit_baseline_t *baseline) {
    char path[MAX_PATH_LEN];
    FILE *fp;
    
    /* Try system path if writable, else user path */
    snprintf(path, sizeof(path), "%s", AUDIT_BASELINE_PATH_SYSTEM);
    fp = fopen(path, "wb");
    
    if (!fp) {
        const char *home = getenv("HOME");
        if (!home) return false;
        
        /* Ensure .sentinel directory exists */
        char dir[MAX_PATH_LEN];
        snprintf(dir, sizeof(dir), "%s/.sentinel", home);
        mkdir(dir, 0700);
        
        snprintf(path, sizeof(path), "%s/%s", home, AUDIT_BASELINE_PATH_USER);
        fp = fopen(path, "wb");
    }
    
    if (!fp) {
        return false;
    }
    
    if (fwrite(baseline, sizeof(*baseline), 1, fp) != 1) {
        fclose(fp);
        return false;
    }
    
    fclose(fp);
    
    /* Set restrictive permissions */
    chmod(path, 0600);
    
    return true;
}


/*
 * Update audit baseline with new sample (Exponential Moving Average)
 */
void update_audit_baseline(audit_baseline_t *baseline, const audit_summary_t *current) {
    if (baseline->sample_count == 0) {
        /* First sample - initialize */
        memcpy(baseline->magic, AUDIT_BASELINE_MAGIC, 8);
        baseline->version = AUDIT_BASELINE_VERSION;
        baseline->created = time(NULL);
        baseline->avg_auth_failures = (float)current->auth_failures;
        baseline->avg_sudo_count = (float)current->sudo_count;
        baseline->avg_sensitive_access = (float)current->sensitive_file_count;
        baseline->avg_tmp_executions = (float)current->tmp_executions;
        baseline->avg_shell_spawns = (float)current->shell_spawns;
    } else {
        /* EMA update */
        baseline->avg_auth_failures = 
            (current->auth_failures * EMA_ALPHA) + (baseline->avg_auth_failures * (1 - EMA_ALPHA));
        baseline->avg_sudo_count = 
            (current->sudo_count * EMA_ALPHA) + (baseline->avg_sudo_count * (1 - EMA_ALPHA));
        baseline->avg_sensitive_access = 
            (current->sensitive_file_count * EMA_ALPHA) + (baseline->avg_sensitive_access * (1 - EMA_ALPHA));
        baseline->avg_tmp_executions = 
            (current->tmp_executions * EMA_ALPHA) + (baseline->avg_tmp_executions * (1 - EMA_ALPHA));
        baseline->avg_shell_spawns = 
            (current->shell_spawns * EMA_ALPHA) + (baseline->avg_shell_spawns * (1 - EMA_ALPHA));
    }
    
    baseline->sample_count++;
    baseline->updated = time(NULL);
}


/*
 * Main probe function - gather all audit data
 */
audit_summary_t* probe_audit(int window_seconds) {
    audit_summary_t *summary = calloc(1, sizeof(audit_summary_t));
    if (!summary) {
        return NULL;
    }
    
    summary->enabled = true;
    summary->period_seconds = window_seconds;
    summary->capture_time = time(NULL);
    
    /* Check if auditd is available */
    if (access("/var/log/audit/audit.log", R_OK) != 0) {
        summary->enabled = false;
        return summary;
    }
    
    /* Build SYSCALL context first (for process correlation) */
    clear_event_ctx();
    parse_syscall_context(window_seconds);
    
    /* Parse various event types */
    parse_auth_events(summary, window_seconds);
    parse_priv_events(summary, window_seconds);
    parse_file_events(summary, window_seconds);
    parse_exec_events(summary, window_seconds);
    check_security_framework(summary);
    
    /* Clean up event context */
    clear_event_ctx();
    
    /* Load baseline and detect anomalies */
    audit_baseline_t baseline = {0};
    if (load_audit_baseline(&baseline)) {
        detect_anomalies(summary, &baseline);
    }
    
    /* Calculate overall risk score */
    calculate_risk_score(summary);
    
    return summary;
}


/*
 * Cleanup
 */
void free_audit_summary(audit_summary_t *summary) {
    free(summary);
}
