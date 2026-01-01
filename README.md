# C-Sentinel

**Semantic Observability for UNIX Systems**

A lightweight, portable system prober written in C that captures "system fingerprints" for AI-assisted analysis of non-obvious risks.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## The Problem

Modern observability tools like Dynatrace, Datadog, and Prometheus are excellent at metric collection and threshold alerting. But they answer a narrow question: *"Is this metric outside its expected range?"*

They struggle with:
- **Causal reasoning**: *Why* did something fail?
- **Context synthesis**: Connecting a config change last week to today's latency spike
- **Non-obvious degradation**: Things that aren't "broken" but are drifting toward failure
- **The "silent drift"**: Two servers that should be identical but have subtly diverged

C-Sentinel takes a different approach: capture a comprehensive system fingerprint and use LLM reasoning to identify the "ghosts in the machine."

## Why C? (And Why Python Wasn't Enough)

> *"By 2030, we will see a massive shift toward AI-automated infrastructure. C-Sentinel is designed to be the 'Eyes' for those autonomous agents—providing structured, low-overhead system truth."*

This project exists at the intersection of two worlds:

**The Old World**: UNIX systems, `/proc` filesystems, process states, file descriptors. These haven't changed fundamentally in 40 years. They're deterministic, well-understood, and critical.

**The New World**: LLMs that can reason about complex systems, spot patterns humans miss, and suggest fixes. They're powerful but non-deterministic and potentially dangerous.

### Why not just Python?

Python is excellent for API integration and orchestration. But for the *prober*—the component that reads `/proc`, parses process state, and generates the system fingerprint—Python has problems:

| Concern | Python | C |
|---------|--------|---|
| **Dependencies** | Requires Python runtime (~100MB) | Static binary (~40KB) |
| **Startup time** | ~500ms interpreter startup | ~1ms |
| **Memory** | ~30MB baseline | <2MB |
| **Portability** | Needs matching Python version | Runs on any POSIX system |
| **Determinism** | GC pauses, import side effects | Predictable execution |

When you're probing a struggling production server, the last thing you want is your diagnostic tool consuming resources or behaving unpredictably.

### The Hybrid Architecture

C-Sentinel uses each language for what it's best at:

```
┌─────────────────────────────────────────────────────────────┐
│                    Python Orchestration                      │
│  • API communication        • Response parsing              │
│  • Policy validation        • User interface                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     C Foundation                             │
│  • /proc parsing            • JSON serialization            │
│  • Process analysis         • Sanitization                  │
│  • Config checksumming      • Drift detection               │
└─────────────────────────────────────────────────────────────┘
```

This is the same pattern used by successful systems tools: Git (C core, various language bindings), Docker (Go core, REST API), and countless others.

### For AI Architects

If you're building AI systems that interact with infrastructure, consider:

1. **The AI should not be the source of truth** about system state. A deterministic prober should be.
2. **AI suggestions must be validated** before presentation. Our policy engine blocks dangerous commands regardless of how convincing the AI's reasoning is.
3. **Data leaving your network must be sanitized**. The AI doesn't need to see real IP addresses to reason about network topology.

These aren't just good practices—they're the difference between a demo and a production system.

---

## Architecture

```mermaid
flowchart TB
    subgraph "C Layer (Deterministic)"
        A[System Prober] --> B[/proc Parser]
        A --> C[Config Scanner]
        A --> D[Process Analyzer]
        B --> E[Fingerprint Builder]
        C --> E
        D --> E
        E --> F[JSON Serializer]
        F --> G[Sanitizer]
    end
    
    subgraph "Python Layer (Flexible)"
        G --> H[API Client]
        H --> I[LLM API]
        I --> J[Response Parser]
        J --> K[Policy Engine]
        K --> L[Safe Output]
    end
    
    subgraph "Data Flow"
        M[("/proc")] -.-> B
        N[("Config Files")] -.-> C
        O[("Process Table")] -.-> D
    end
```

## Features

- **Lightweight**: <2MB RAM, minimal binary size
- **Portable**: Pure C99, runs on any POSIX system
- **Safe by default**: Read-only probing, no root required
- **Privacy-aware**: Built-in sanitization strips IPs, secrets, and PII before LLM transmission
- **Policy engine**: Deterministic safety gate validates AI suggestions before presentation
- **Drift detection**: Compare "identical" systems to find hidden differences
- **Hybrid architecture**: C for performance-critical probing, Python for AI orchestration

### What it captures

| Category | Data | Purpose |
|----------|------|---------|
| System | Hostname, kernel, uptime, load, memory | Basic health context |
| Processes | Notable processes with metadata | Zombie, leak, stuck detection |
| Configs | File metadata and checksums | Drift detection |

### What it flags

- 🧟 **Zombie processes**: Always a problem
- 📂 **High FD counts**: Potential descriptor leaks (>100 open)
- ⏰ **Long-running processes**: >30 days without restart
- 🔓 **Permission issues**: World-writable configs
- 💾 **Memory hogs**: Processes >1GB RSS

## Quick Start

**One-liner install:**
```bash
git clone https://github.com/williamofai/c-sentinel.git
cd c-sentinel
./quickstart.sh
```

**Manual setup:**
```bash
# Build
make

# Quick analysis (human-readable)
./bin/sentinel --quick

# Full fingerprint (JSON for LLM)
./bin/sentinel > fingerprint.json

# Probe specific configs
./bin/sentinel /etc/nginx/nginx.conf /etc/mysql/my.cnf

# Compare two systems (drift detection)
./bin/sentinel-diff node_a.json node_b.json

# Full AI-powered analysis (requires ANTHROPIC_API_KEY)
pip install anthropic
export ANTHROPIC_API_KEY="your-key"
./sentinel_analyze.py

# AI analysis with local Ollama (free, private)
ollama pull llama3.2:3b
pip install openai
./sentinel_analyze.py --local
```

📖 **See [SAMPLES.md](SAMPLES.md) for real-world output** from the first production test, including a security anomaly detection story.

### Example Output (Quick Mode)

```
C-Sentinel Quick Analysis
========================
Hostname: prod-web-03
Uptime: 47.3 days
Load: 0.42 0.38 0.35
Processes: 287 total

Potential Issues:
  Zombie processes: 0
  High FD processes: 2
  Long-running (>7d): 23
  Config permission issues: 1
```

## Using with an LLM

The JSON output is designed to be sent directly to an LLM with a system prompt like:

```
You are a Principal UNIX Systems Engineer with 40 years experience.
Analyze this system fingerprint and identify:
1. Non-obvious risks that traditional monitoring would miss
2. Signs of "drift" or degradation
3. Processes that warrant investigation
4. Configuration anomalies

Be specific. Reference the actual process names, PIDs, and values.
```

### Example Python wrapper

```python
#!/usr/bin/env python3
import subprocess
import json
from anthropic import Anthropic

# Capture fingerprint
result = subprocess.run(['./bin/sentinel'], capture_output=True, text=True)
fingerprint = result.stdout

# Send to Claude
client = Anthropic()
response = client.messages.create(
    model="claude-sonnet-4-20250514",
    max_tokens=1024,
    system="You are a Principal UNIX Systems Engineer...",
    messages=[{"role": "user", "content": f"Analyze this fingerprint:\n{fingerprint}"}]
)

print(response.content[0].text)
```

## Building

### Requirements

- GCC or Clang with C99 support
- GNU Make
- Linux (uses `/proc` filesystem)

### Build options

```bash
make              # Release build
make DEBUG=1      # Debug build with symbols
make test         # Run basic tests
make lint         # Static analysis (requires cppcheck)
make install      # Install to /usr/local/bin
```

## Project Structure

```
c-sentinel/
├── include/
│   ├── sentinel.h      # Core data structures
│   ├── policy.h        # Safety gate API
│   └── sanitize.h      # Data sanitization API
├── src/
│   ├── main.c          # CLI entry point
│   ├── prober.c        # System probing functions
│   ├── json_serialize.c # JSON output generation
│   ├── policy.c        # Command validation engine
│   ├── sanitize.c      # PII/secret stripping
│   └── diff.c          # Fingerprint comparison
├── sentinel_analyze.py # Python wrapper for LLM integration
├── Makefile
├── README.md
├── DESIGN_DECISIONS.md # Architectural rationale (read this!)
└── LICENSE
```

## Design Philosophy

See [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) for detailed rationale, but the key principles are:

1. **C for determinism**: The "must not fail" parts are in C
2. **Read-only by design**: We observe, never modify
3. **Filter, don't flood**: Send notable findings, not raw data
4. **Safe for external APIs**: Sanitize before sending anywhere

## Roadmap

- [x] Core system prober
- [x] JSON serialization  
- [x] Policy engine (command validation)
- [x] Sanitizer (PII stripping)
- [x] Drift detection (sentinel-diff)
- [x] Python wrapper with Claude integration
- [ ] SHA256 checksums (replace simple hash)
- [ ] Watch mode (periodic fingerprints)
- [ ] Plugin system for application-specific probes
- [ ] FreeBSD/macOS support
- [ ] Web UI for drift visualization

## Contributing

Contributions welcome! Areas of particular interest:

- **Platform support**: BSD, macOS, Solaris
- **Application probers**: nginx, postgres, redis, etc.
- **Sanitization patterns**: Help identify sensitive data patterns
- **Test coverage**: Edge cases and failure modes

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

This project grew from 30 years of UNIX systems experience and countless hours of asking "why did that break?" The patterns it detects aren't theoretical—they're battle scars.

---

*"The goal isn't to replace monitoring tools—it's to add wisdom to their data."*
