/*
 * C-Sentinel - Semantic Observability for UNIX Systems
 * Copyright (c) 2025 William Murray
 *
 * Licensed under the MIT License.
 * See LICENSE file for details.
 *
 * https://github.com/williamofai/c-sentinel
 *
 * main.c - CLI entry point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "sentinel.h"

/* Forward declarations for new modules */
typedef struct baseline baseline_t;
typedef struct deviation_report deviation_report_t;
typedef struct sentinel_config sentinel_config_t;
typedef struct alert alert_t;

/* Baseline functions */
extern void baseline_init(baseline_t *b);
extern int baseline_load(baseline_t *b);
extern int baseline_save(const baseline_t *b);
extern int baseline_learn(baseline_t *b, const fingerprint_t *fp);
extern int baseline_compare(const baseline_t *b, const fingerprint_t *fp, 
                            deviation_report_t *report);
extern void baseline_print_report(const baseline_t *b, const deviation_report_t *report);
extern void baseline_print_info(const baseline_t *b);

/* Config functions */
extern int config_load(void);
extern const sentinel_config_t* config_get(void);
extern int config_create_default(void);
extern void config_print(void);

/* Alert functions - use void* to avoid including full struct */
extern int alert_send_webhook(const char *url, const void *alert);
extern int alert_create_from_analysis(void *alert, const fingerprint_t *fp, 
                                       const quick_analysis_t *analysis, int severity);
extern void alert_print(const void *alert);

/* Default config files to probe if none specified */
static const char *default_configs[] = {
    "/etc/hosts",
    "/etc/passwd",
    "/etc/ssh/sshd_config",
    "/etc/fstab",
    "/etc/resolv.conf",
    NULL
};

/* Global flag for clean shutdown in watch mode */
static volatile int keep_running = 1;

static void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
    fprintf(stderr, "\nShutting down...\n");
}

static void print_usage(const char *prog) {
    fprintf(stderr, "C-Sentinel v%s - Semantic Observability for UNIX Systems\n\n", SENTINEL_VERSION);
    fprintf(stderr, "Usage: %s [OPTIONS] [config_files...]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h, --help           Show this help message\n");
    fprintf(stderr, "  -q, --quick          Only show quick analysis summary\n");
    fprintf(stderr, "  -v, --verbose        Include all processes (not just notable ones)\n");
    fprintf(stderr, "  -j, --json           Output JSON to stdout (even in quick mode)\n");
    fprintf(stderr, "  -w, --watch          Continuous monitoring mode\n");
    fprintf(stderr, "  -i, --interval SEC   Interval between probes in watch mode (default: 60)\n");
    fprintf(stderr, "  -n, --network        Include network probe (listeners, connections)\n");
    fprintf(stderr, "  -b, --baseline       Compare against learned baseline\n");
    fprintf(stderr, "  -l, --learn          Learn current state as baseline\n");
    fprintf(stderr, "  -c, --config         Show current configuration\n");
    fprintf(stderr, "      --init-config    Create default config file\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Exit codes:\n");
    fprintf(stderr, "  0 - No issues detected\n");
    fprintf(stderr, "  1 - Warnings (minor issues)\n");
    fprintf(stderr, "  2 - Critical (zombies, permission issues, unusual ports)\n");
    fprintf(stderr, "  3 - Error (probe failed)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "If no config files are specified, probes common system configs.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Baseline:\n");
    fprintf(stderr, "  First, learn what's normal:    %s --learn --network\n", prog);
    fprintf(stderr, "  Then compare against baseline: %s --baseline --network\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Config file: ~/.sentinel/config\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s --quick                    One-shot quick analysis\n", prog);
    fprintf(stderr, "  %s --quick --network          Include network probe\n", prog);
    fprintf(stderr, "  %s --watch --interval 300     Monitor every 5 minutes\n", prog);
    fprintf(stderr, "  %s --json > fingerprint.json  Save full JSON output\n", prog);
    fprintf(stderr, "  %s --learn --network          Learn current state as baseline\n", prog);
    fprintf(stderr, "  %s --baseline --network       Compare against baseline\n", prog);
}

static void print_timestamp(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    printf("[%s] ", buf);
}

static int run_analysis(const char **configs, int config_count, 
                        int quick_mode, int json_mode, int network_mode) {
    fingerprint_t fp;
    int result = capture_fingerprint(&fp, configs, config_count);
    
    if (result != 0) {
        fprintf(stderr, "Warning: Some probes failed (errors: %d)\n", fp.probe_errors);
    }
    
    /* Probe network if requested */
    if (network_mode) {
        probe_network(&fp.network);
    }
    
    /* Always do quick analysis for exit code calculation */
    quick_analysis_t analysis;
    analyze_fingerprint_quick(&fp, &analysis);
    
    if (json_mode) {
        /* Full JSON output */
        char *json = fingerprint_to_json(&fp);
        if (!json) {
            fprintf(stderr, "Error: Failed to serialize fingerprint to JSON\n");
            return EXIT_ERROR;
        }
        printf("%s", json);
        free(json);
    } else if (quick_mode) {
        /* Quick analysis only */
        printf("C-Sentinel Quick Analysis\n");
        printf("========================\n");
        printf("Hostname: %s\n", fp.system.hostname);
        printf("Uptime: %.1f days\n", fp.system.uptime_seconds / 86400.0);
        printf("Load: %.2f %.2f %.2f\n", 
               fp.system.load_avg[0], fp.system.load_avg[1], fp.system.load_avg[2]);
        printf("Memory: %.1f%% used\n", 
               100.0 * (1.0 - (double)fp.system.free_ram / fp.system.total_ram));
        printf("Processes: %d total\n", fp.process_count);
        printf("\nPotential Issues:\n");
        printf("  Zombie processes: %d%s\n", analysis.zombie_process_count,
               analysis.zombie_process_count > 0 ? " ⚠" : "");
        printf("  High FD processes: %d%s\n", analysis.high_fd_process_count,
               analysis.high_fd_process_count > 5 ? " ⚠" : "");
        printf("  Long-running (>7d): %d\n", analysis.long_running_process_count);
        printf("  Config permission issues: %d%s\n", analysis.config_permission_issues,
               analysis.config_permission_issues > 0 ? " ⚠" : "");
        
        if (network_mode) {
            printf("\nNetwork:\n");
            printf("  Listening ports: %d\n", fp.network.total_listening);
            printf("  Established connections: %d\n", fp.network.total_established);
            printf("  Unusual ports: %d%s\n", analysis.unusual_listeners,
                   analysis.unusual_listeners > 0 ? " ⚠" : "");
            
            /* Show listeners if any */
            if (fp.network.listener_count > 0) {
                printf("\n  Listeners:\n");
                for (int i = 0; i < fp.network.listener_count && i < 10; i++) {
                    net_listener_t *l = &fp.network.listeners[i];
                    printf("    %s:%d (%s) - %s\n", 
                           l->local_addr, l->local_port, l->protocol, l->process_name);
                }
                if (fp.network.listener_count > 10) {
                    printf("    ... and %d more\n", fp.network.listener_count - 10);
                }
            }
        }
    } else {
        /* Full JSON output (default) */
        char *json = fingerprint_to_json(&fp);
        if (!json) {
            fprintf(stderr, "Error: Failed to serialize fingerprint to JSON\n");
            return EXIT_ERROR;
        }
        printf("%s", json);
        free(json);
    }
    
    /* Calculate exit code based on issues */
    if (analysis.zombie_process_count > 0 || 
        analysis.config_permission_issues > 0 ||
        analysis.unusual_listeners > 3) {
        return EXIT_CRITICAL;
    } else if (analysis.high_fd_process_count > 5 ||
               analysis.unusual_listeners > 0) {
        return EXIT_WARNINGS;
    }
    
    return EXIT_OK;
}

int main(int argc, char *argv[]) {
    int quick_mode = 0;
    int json_mode = 0;
    int watch_mode = 0;
    int network_mode = 0;
    int baseline_mode = 0;
    int learn_mode = 0;
    int show_config = 0;
    int init_config = 0;
    int interval = 60;
    int opt;
    
    static struct option long_options[] = {
        {"help",        no_argument,       0, 'h'},
        {"quick",       no_argument,       0, 'q'},
        {"verbose",     no_argument,       0, 'v'},
        {"json",        no_argument,       0, 'j'},
        {"watch",       no_argument,       0, 'w'},
        {"interval",    required_argument, 0, 'i'},
        {"network",     no_argument,       0, 'n'},
        {"baseline",    no_argument,       0, 'b'},
        {"learn",       no_argument,       0, 'l'},
        {"config",      no_argument,       0, 'c'},
        {"init-config", no_argument,       0, 'C'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "hqvjwi:nblcC", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return EXIT_OK;
            case 'q':
                quick_mode = 1;
                break;
            case 'v':
                /* verbose mode - future enhancement */
                break;
            case 'j':
                json_mode = 1;
                break;
            case 'w':
                watch_mode = 1;
                break;
            case 'i':
                interval = atoi(optarg);
                if (interval < 1) interval = 1;
                if (interval > 86400) interval = 86400;
                break;
            case 'n':
                network_mode = 1;
                break;
            case 'b':
                baseline_mode = 1;
                break;
            case 'l':
                learn_mode = 1;
                break;
            case 'c':
                show_config = 1;
                break;
            case 'C':
                init_config = 1;
                break;
            default:
                print_usage(argv[0]);
                return EXIT_ERROR;
        }
    }
    
    /* Handle --init-config */
    if (init_config) {
        if (config_create_default() == 0) {
            printf("Created default config file: ~/.sentinel/config\n");
            return EXIT_OK;
        } else {
            fprintf(stderr, "Failed to create config file\n");
            return EXIT_ERROR;
        }
    }
    
    /* Handle --config */
    if (show_config) {
        config_print();
        return EXIT_OK;
    }
    
    /* Determine config files to probe */
    const char **configs;
    int config_count;
    
    if (optind < argc) {
        /* Use command line specified configs */
        configs = (const char **)&argv[optind];
        config_count = argc - optind;
    } else {
        /* Use defaults */
        configs = default_configs;
        config_count = 0;
        while (default_configs[config_count]) config_count++;
    }
    
    /* Handle --learn */
    if (learn_mode) {
        fingerprint_t fp;
        capture_fingerprint(&fp, configs, config_count);
        if (network_mode) {
            probe_network(&fp.network);
        }
        
        /* Load existing baseline or create new */
        /* Using static allocation to avoid complex header includes */
        static char baseline_buf[8192];  /* Large enough for baseline_t */
        baseline_t *b = (baseline_t *)baseline_buf;
        
        if (baseline_load(b) != 0) {
            baseline_init(b);
            printf("Creating new baseline...\n");
        } else {
            printf("Updating existing baseline...\n");
        }
        
        baseline_learn(b, &fp);
        
        if (baseline_save(b) == 0) {
            printf("Baseline saved to ~/.sentinel/baseline.dat\n");
            baseline_print_info(b);
            return EXIT_OK;
        } else {
            fprintf(stderr, "Failed to save baseline\n");
            return EXIT_ERROR;
        }
    }
    
    /* Handle --baseline */
    if (baseline_mode) {
        static char baseline_buf[8192];
        baseline_t *b = (baseline_t *)baseline_buf;
        
        if (baseline_load(b) != 0) {
            fprintf(stderr, "No baseline found. Run with --learn first.\n");
            return EXIT_ERROR;
        }
        
        fingerprint_t fp;
        capture_fingerprint(&fp, configs, config_count);
        if (network_mode) {
            probe_network(&fp.network);
        }
        
        /* Run quick analysis */
        quick_analysis_t analysis;
        analyze_fingerprint_quick(&fp, &analysis);
        
        /* Show quick summary */
        printf("C-Sentinel Quick Analysis\n");
        printf("========================\n");
        printf("Hostname: %s\n", fp.system.hostname);
        printf("Uptime: %.1f days\n", fp.system.uptime_seconds / 86400.0);
        printf("Load: %.2f %.2f %.2f\n", 
               fp.system.load_avg[0], fp.system.load_avg[1], fp.system.load_avg[2]);
        printf("Processes: %d total\n", fp.process_count);
        
        /* Compare against baseline */
        static char report_buf[2048];  /* Large enough for deviation_report_t */
        deviation_report_t *report = (deviation_report_t *)report_buf;
        
        int deviations = baseline_compare(b, &fp, report);
        baseline_print_report(b, report);
        
        if (deviations > 0) {
            return EXIT_CRITICAL;
        }
        return EXIT_OK;
    }
    
    /* Watch mode - continuous monitoring */
    if (watch_mode) {
        /* Setup signal handler for clean shutdown */
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        
        fprintf(stderr, "C-Sentinel v%s - Watch Mode (Ctrl+C to stop)\n", SENTINEL_VERSION);
        fprintf(stderr, "Interval: %d seconds\n\n", interval);
        
        int worst_exit = EXIT_OK;
        
        while (keep_running) {
            print_timestamp();
            int exit_code = run_analysis(configs, config_count, 
                                         quick_mode || 1, json_mode, network_mode);
            
            if (exit_code > worst_exit) worst_exit = exit_code;
            
            if (exit_code == EXIT_CRITICAL) {
                printf(" [CRITICAL]\n");
            } else if (exit_code == EXIT_WARNINGS) {
                printf(" [WARNINGS]\n");
            } else {
                printf(" [OK]\n");
            }
            
            if (keep_running) {
                sleep(interval);
            }
        }
        
        return worst_exit;
    }
    
    /* One-shot mode */
    return run_analysis(configs, config_count, quick_mode, json_mode, network_mode);
}
