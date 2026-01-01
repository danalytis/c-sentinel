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
- Comprehensive (system info, processes, configs, network)
- Structured for semantic analysis
- Suitable for diff-comparison between systems

This enables use cases that streaming metrics cannot address:
- "Compare these two 'identical' non-prod environments"
- "What's changed since last week?"
- "Given this snapshot, what do you predict will fail?"

## Network Probing Design (v0.3.0)

**Decision**: Parse `/proc/net/tcp`, `/proc/net/tcp6`, `/proc/net/udp`, `/proc/net/udp6` directly rather than using `netstat` or `ss`.

**Rationale**:
1. **No external dependencies**: `netstat` may not be installed; `ss` output format varies
2. **Deterministic parsing**: /proc files have stable, documented formats
3. **Process correlation**: We can map sockets to PIDs by scanning `/proc/[pid]/fd/`

**What we capture**:
- Listening ports (TCP/UDP, IPv4/IPv6)
- Established connections
- Owning process for each socket
- "Unusual" port detection (not in common services list)

**Common ports list**:
```c
22, 25, 53, 80, 110, 143, 443, 465, 587, 993, 995,
3306, 5432, 6379, 8080, 8443, 27017
```

Ports above 32768 are considered ephemeral (normal for outbound).

**Trade-off**: Process lookup for each socket is O(n×m) where n=sockets, m=processes. On systems with many connections, this could be slow. Acceptable for diagnostic use; would optimise for continuous monitoring.

## Baseline Learning (v0.3.0)

**Decision**: Store a binary "baseline" of normal system state for deviation detection.

**Rationale**:
Traditional monitoring compares metrics against static thresholds. But "normal" varies by system:
- A database server with 50 connections is normal; on a static web server, it's suspicious
- 80% memory usage might be fine for a Java app, alarming for a C daemon

Baseline learning solves this by recording what's normal *for this specific system*.

**What we track**:
| Metric | How |
|--------|-----|
| Process count | Min/max/avg range |
| Memory usage | Average and maximum |
| Load average | Maximum observed |
| Expected ports | List of ports that should be listening |
| Config checksums | Detect file changes |

**Learning vs. Comparing**:
- `--learn`: Capture current state, merge with existing baseline
- `--baseline`: Compare current state against learned baseline, report deviations

**Storage format**: Binary struct written to `~/.sentinel/baseline.dat`
- Magic number for validation ("SNTLBASE")
- Version field for future compatibility
- Creation and update timestamps

**Trade-off**: Binary format is not human-readable. Chose this for simplicity; could add `--baseline-export` for JSON output later.

## Configuration File (v0.3.0)

**Decision**: Support `~/.sentinel/config` with INI-style key=value format.

**Rationale**:
Users need to configure:
- API keys (Anthropic, OpenAI, Ollama)
- Thresholds (what counts as "high" memory, FDs, etc.)
- Webhook URLs for alerting
- Default behaviour (always probe network? default interval?)

**Why not YAML/JSON/TOML?**
- INI is simple to parse without external libraries
- Good enough for flat key-value configuration
- Human-readable and editable

**Environment variable fallback**:
API keys can come from environment variables (`ANTHROPIC_API_KEY`, etc.) or the config file. Environment takes precedence—standard practice for secrets.

**Security**:
- Config file created with mode 0600 (owner read/write only)
- API keys displayed as `[set]` not actual values

## Webhook Alerting (v0.3.0)

**Decision**: Support Slack-compatible webhook format for alerts.

**Rationale**:
When running in watch mode, critical findings should notify humans immediately. Slack webhooks are:
- De facto standard (Discord, Teams, etc. accept same format)
- Simple HTTP POST with JSON payload
- No authentication complexity

**Implementation**: Shell out to `curl` rather than implementing HTTP in C.

**Trade-off**: Requires `curl` to be installed. Acceptable—it's near-universal on Linux systems.

**Alert levels**:
- Critical: Zombies, permission issues, many unusual ports
- Warning: Some unusual ports, high FD counts
- Info: Available but not implemented yet

## Exit Codes for CI/CD (v0.3.0)

**Decision**: Return meaningful exit codes for automation.

| Code | Meaning |
|------|---------|
| 0 | No issues detected |
| 1 | Warnings (minor issues) |
| 2 | Critical findings |
| 3 | Error (probe failed) |

**Rationale**:
Enables use in CI/CD pipelines:
```bash
./bin/sentinel --quick --network
if [ $? -eq 2 ]; then
    echo "Critical issues found!"
    exit 1
fi
```

## Watch Mode Design (v0.3.0)

**Decision**: Built-in continuous monitoring rather than relying on cron.

**Rationale**:
- Simpler for users: one command instead of configuring cron
- Clean shutdown handling (SIGINT/SIGTERM)
- Can accumulate worst exit code across runs
- Foundation for future features (webhooks on state change)

**Implementation**:
```c
while (keep_running) {
    run_analysis();
    sleep(interval);
}
```

**Trade-off**: Long-running process vs. cron job. For production, a systemd service with restart-on-failure would be more robust. Added to roadmap.

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
- IP addresses are redacted to `[REDACTED-IP]`
- Home directory paths are redacted
- Known secret environment variables are redacted
- Visible placeholders so analysts know data was present

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

## Drift Detection Philosophy

**Decision**: Build fingerprint comparison as a first-class tool (`sentinel-diff`) and baseline deviation detection.

**The "Identical Systems" Lie**:
In 30 years of UNIX, I've never seen two systems that Ops claimed were "identical" actually be identical. There's always:
- A kernel parameter that got changed during debugging and never reverted
- A cron job that exists on one but not the other
- A config file that drifted after a failed deployment
- Package versions that don't match

**Why Traditional Tools Miss This**:
Monitoring tools compare each system against its own baseline. They don't compare systems against each other. If both systems have the same bug, neither alerts.

**Two Approaches**:
1. **sentinel-diff**: Compare two JSON fingerprints (different systems)
2. **--baseline**: Compare current state against learned normal (same system over time)

Both detect drift; different use cases.

## Lessons from 30 Years of UNIX

This tool embeds certain assumptions from experience:

1. **Zombies are never okay**: Some monitoring tools ignore them. We don't.
2. **Long-running processes deserve scrutiny**: A process that's been running for 30 days may have accumulated state, leaked memory, or holding stale connections.
3. **Config drift is insidious**: Two "identical" servers with one different sysctl setting have caused countless production incidents.
4. **World-writable configs are never intentional**: This is always either a mistake or a compromise.
5. **File descriptor leaks are slow killers**: The system runs fine until suddenly it doesn't.
6. **New listening ports are suspicious**: If a port wasn't open yesterday, ask why it's open today.
7. **Missing services are emergencies**: If a port was supposed to be listening and isn't, something failed.

These aren't just heuristics—they're battle scars.

---

*Last updated: January 2026*
*Author: William Murray*
