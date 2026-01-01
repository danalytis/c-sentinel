# Design Decisions

This document explains the architectural choices made in C-Sentinel, why they were made, and what trade-offs were considered. It's intended both as documentation and as a demonstration of how a systems architect approaches design.

## Why C?

**Decision**: Use C as the primary language for the system prober.

**Rationale**:
1. **Minimal dependencies**: A C binary can be deployed to virtually any UNIX system without runtime dependencies. No Python interpreter, no Node.js, no container runtime.
2. **Deterministic behaviour**: Memory allocation and deallocation are explicit. There's no garbage collector that might pause at inopportune moments.
3. **Direct system access**: We're reading `/proc`, calling `sysinfo()`, using `stat()`. C is the natural language for this.
4. **Resource efficiency**: The prober should be lightweight enough to run on production systems without impacting performance. A typical run uses <2MB RAM.

**Trade-offs accepted**:
- Longer development time compared to Python
- Manual memory management introduces potential for bugs
- JSON serialization is more tedious without native support

**Mitigations**:
- Strict coding standards (`-Wall -Wextra -Werror`)
- Use of static analysis tools (cppcheck)
- Careful buffer sizing with defined limits

## The Hybrid Architecture

**Decision**: C for the prober, with Python/shell wrapper for API communication.

**Rationale**:
The problem naturally splits into two distinct domains:
1. **System probing**: Low-level, performance-sensitive, needs direct OS access
2. **API communication**: HTTP requests, JSON parsing of responses, error handling

Forcing both into C would mean pulling in `libcurl` and a JSON parsing library, adding complexity and dependencies for the API layer—the opposite of what we want for the lightweight prober.

The Python wrapper can:
- Handle API authentication securely
- Implement retry logic and rate limiting
- Parse and present LLM responses
- Add "policy engine" validation of suggestions

## Fingerprint Design

**Decision**: Capture a "fingerprint" of system state rather than streaming metrics.

**Rationale**:
Traditional monitoring tools (Prometheus, Datadog, Dynatrace) excel at time-series metrics. They answer "what is the CPU doing right now?" C-Sentinel aims to answer a different question: "What is the overall state of this system, and what might be wrong?"

A fingerprint is:
- A point-in-time snapshot
- Comprehensive (system info, processes, configs)
- Structured for semantic analysis
- Suitable for diff-comparison between systems

This enables use cases that streaming metrics cannot address:
- "Compare these two 'identical' non-prod environments"
- "What's changed since last week?"
- "Given this snapshot, what do you predict will fail?"

## What We Include vs. Exclude

### Included in fingerprints:
- **System basics**: hostname, kernel, uptime, load, memory
- **Process list with metadata**: but filtered to "notable" processes
- **Config file metadata**: not content (privacy), but checksums for drift detection

### Explicitly excluded:
- **Raw log content**: Too verbose, often contains PII
- **Network connections**: Potentially sensitive
- **Environment variables**: Often contain secrets
- **Full config file contents**: Could contain credentials

**Rationale**: The fingerprint should be safe to send to an external LLM API without manual review. We'd rather miss some signal than leak credentials.

## "Notable" Process Selection

**Decision**: Don't include all processes in the JSON output—filter to interesting ones.

A system might have 500+ processes. Sending all of them to an LLM is:
- Wasteful of tokens/cost
- Noisy (most processes are uninteresting)
- Potentially revealing (process names can leak information)

**Selection criteria**:
| Flag | Condition | Rationale |
|------|-----------|-----------|
| `zombie` | State = 'Z' | Always a problem |
| `high_fd_count` | >100 open FDs | Potential leak |
| `potentially_stuck` | State 'D' for >5 min | I/O issues |
| `very_long_running` | Running >30 days | Should probably be restarted |
| `high_memory` | RSS >1GB | Resource hog |

**Trade-off**: We might miss genuinely interesting processes that don't trigger these heuristics. Future versions could allow custom filters.

## JSON Serialization Strategy

**Decision**: Hand-rolled JSON serialization rather than using cJSON or similar.

**Rationale**:
- One fewer dependency
- Our output schema is fixed and simple
- Full control over formatting (pretty-printed for readability)
- Educational value (demonstrates string handling in C)

**Trade-offs**:
- More code to maintain
- Potential for subtle escaping bugs
- Would reconsider for more complex schemas

**Mitigation**: The `buf_append_json_string()` function handles all escaping centrally.

## Security Considerations

### Input validation
All paths and strings from external sources are length-limited. Buffer overflows are prevented by:
- Using `snprintf()` instead of `sprintf()`
- Using custom `safe_strcpy()` instead of `strcpy()`
- Defining `MAX_*` constants for all arrays

### Privilege model
The prober reads from `/proc` and specified config files. It requires:
- Read access to `/proc` (standard for all users)
- Read access to config files (may require appropriate group membership)
- **No write access anywhere**
- **No root required** for basic operation

Some probes (like reading all process FDs) may return partial results for non-root users. This is acceptable—we document what we could probe.

### Sanitization
Before sending to LLM:
- Hostnames are preserved (useful context) but could be made optional
- IP addresses in paths/strings should be redacted (TODO)
- Username-like patterns should be redacted (TODO)

## Future Architecture

### Now Implemented:

1. **Policy Engine (C)**: A rules-based validator that approves/rejects LLM suggestions before presenting to user. See `policy.c`.

2. **Sanitizer (C)**: Strips IP addresses, home directories, and secrets before data leaves the system boundary. See `sanitize.c`.

3. **Drift Detection**: The `sentinel-diff` tool compares two fingerprints to detect configuration drift between "identical" systems.

### Planned additions:
1. **Policy Engine (C)**: A rules-based validator that can approve/reject LLM suggestions before presenting to user
2. **Comparative mode**: Diff two fingerprints to detect drift
3. **Watch mode**: Periodic fingerprints with delta reporting
4. **Plugin system**: Allow custom probers for specific applications (nginx, mysql, etc.)

### Why not add now:
YAGNI (You Ain't Gonna Need It). Ship the MVP, learn from real usage, then extend.

## Lessons from 30 Years of UNIX

This tool embeds certain assumptions from experience:

1. **Zombies are never okay**: Some monitoring tools ignore them. We don't.
2. **Long-running processes deserve scrutiny**: A process that's been running for 30 days may have accumulated state, leaked memory, or holding stale connections.
3. **Config drift is insidious**: Two "identical" servers with one different sysctl setting have caused countless production incidents.
4. **World-writable configs are never intentional**: This is always either a mistake or a compromise.
5. **File descriptor leaks are slow killers**: The system runs fine until suddenly it doesn't.

These aren't just heuristics—they're battle scars.

## The Policy Engine: AI Safety Gate

**Decision**: Implement a deterministic command validator in C that sits between LLM suggestions and user presentation.

**The Problem**:
LLMs can suggest dangerous commands. Even well-intentioned suggestions like "clean up disk space" might produce `rm -rf /`. We cannot trust AI output without validation.

**Design Principles**:

1. **Deny by default in strict mode**: If we don't explicitly recognize a command as safe, require human review.

2. **No regex**: Regular expressions are a security liability (ReDoS attacks) and difficult to audit. We use simple string matching.

3. **Layered checks**: 
   - First: Check against blocked commands (rm -rf /, fork bombs, etc.)
   - Second: Check for dangerous patterns (pipes to shell, writes to /etc)
   - Third: Apply custom rules
   - Fourth: Check against safe list (in strict mode)
   - Fifth: Warn on state-modifying commands (sudo, systemctl)

4. **Audit trail**: Every decision is logged with the matched rule.

**Battle Scars Encoded**:

| Blocked Pattern | Incident Type |
|----------------|---------------|
| `rm -rf /` | The classic. Always blocked. |
| `curl\|sh` | Supply chain attack vector |
| `> /etc/passwd` | Privilege escalation |
| `--no-preserve-root` | Trying to bypass safeguards |
| `:(){:\|:&};:` | Fork bomb |
| `chmod 777 /` | Security disaster |

**Trade-off**: We might block legitimate commands. That's acceptable—we'd rather have false positives than let a dangerous command through to a production system.

## The Sanitizer: Data Boundary Protection

**Decision**: Strip sensitive data before ANY transmission to external APIs.

**Why This Matters for Enterprise AI**:
You cannot send production system data to external LLM APIs without sanitization. Even non-prod environments contain:
- IP addresses that reveal network topology
- Usernames that could be used in phishing
- Paths that reveal internal project structures
- Environment variables with credentials

**Sanitization Strategy**:

| Data Type | Detection Method | Replacement |
|-----------|-----------------|-------------|
| IPv4 | `\d+\.\d+\.\d+\.\d+` pattern | `[REDACTED-IP]` |
| IPv6 | Multiple colons with hex | `[REDACTED-IP]` |
| Home dirs | `/home/` or `/Users/` prefix | `[REDACTED-PATH]` |
| Secrets | `password=`, `token=`, etc. | `[REDACTED-SECRET]` |
| Env vars | Values of sensitive env vars | `[REDACTED-SECRET]` |

**Design Choice - Visible Redaction**:
We use visible placeholders (`[REDACTED-IP]`) rather than silent removal because:
- Analysts can see that data was present and removed
- The LLM can reason about "there was an IP here" without seeing the actual IP
- Debugging is easier when you know what was removed

**Built-in Secret Detection**:
The sanitizer automatically redacts values of common secret environment variables:
- `AWS_SECRET_ACCESS_KEY`
- `GITHUB_TOKEN`
- `ANTHROPIC_API_KEY`
- `DATABASE_PASSWORD`

## Drift Detection Philosophy

**Decision**: Build fingerprint comparison as a first-class tool (`sentinel-diff`).

**The "Identical Systems" Lie**:
In 30 years of UNIX, I've never seen two systems that Ops claimed were "identical" actually be identical. There's always:
- A kernel parameter that got changed during debugging and never reverted
- A cron job that exists on one but not the other
- A config file that drifted after a failed deployment
- Package versions that don't match

**Why Traditional Tools Miss This**:
Monitoring tools compare each system against its own baseline. They don't compare systems against each other. If both systems have the same bug, neither alerts.

**Diff Design**:
- Exit code indicates drift (0 = no diff, 1 = differences found)
- Significant differences (>10% numeric, any string mismatch) are starred
- Human-readable hints explain why differences might matter

**Example Output**:
```
FIELD                     node-a               node-b               DELTA
-----                     ------               ------               -----
* kernel                  5.4.0-150            5.4.0-148            
* memory_used_percent     45.20                78.30                53.8%
  zombie_count            0.00                 2.00                 200.0%

--- Analysis Hints ---
- Kernel version mismatch: May affect system call behavior
- Memory usage differs: Check for memory leaks or different workloads
```

---

*Last updated: 2025*
*Author: [Your Name]*
