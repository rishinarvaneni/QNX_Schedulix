# Schedulix: Automotive RTOS Performance & Latency Analyzer

[![QNX](https://img.shields.io/badge/RTOS-QNX_Neutrino_8.0-blue.svg)](https://blackberry.qnx.com)
[![License](https://img.shields.io/badge/License-QNX_Target_GUI-green.svg)](LICENSE)

## Project Overview

**Schedulix** is an explainable real-time performance and latency analysis engine designed for safety-critical automotive workloads running on QNX Neutrino RTOS (SDP 8.0).

In modern Software-Defined Vehicles (SDVs), mixed-criticality workloads (e.g., ASIL-D Braking, ASIL-B ADAS perception, and diagnostic telemetry) share multicore SoCs. Traditional CPU utilization graphs show that a system is loaded, but fail to explain why a deadline was missed, which higher-priority task caused preemption, or how microsecond jitter cascaded.

I built Schedulix to answer a concrete question: *Can we quantitatively decompose activation latency so an engineer actually knows what caused a deadline miss?* — instead of just seeing that a system is "loaded."

## What This Project Is

Schedulix combines deterministic, zero-allocation application instrumentation with native QNX kernel trace decoding (`libtraceparser`) to deliver:

- **Quantitative delay attribution**: Response Time = Ready Queue Wait + Preemption Duration + Blocking Time + Execution Time
- **Evidence-backed root-cause engine**: Classifies deadline misses with explicit confidence levels (CONFIRMED vs INFERRED) and identifies the interfering PID, TID, priority, and exact overlap window
- **Hardware-accurate peripherals**: Physical UART (`/dev/ser1` at 115200 baud), GPIO markers at `0xFE200000` for sub-µs oscilloscope verification, and a CAN layer with non-blocking simulated adapter queue + TCP socket injection server
- **Automated S0–S6 benchmark matrix**: Baseline runs, 20%→95% CPU load sweeps, mutex priority inversions, CAN event storms, and core affinity contention

## Core Engineering Highlights

- **Native QNX libtraceparser C API**: Directly decodes raw binary `.kev` trace streams captured by `tracelogger`. Reconstructs 64-bit cycle timestamps using sequential rollover detection — zero brittle regex text parsing.
- **Zero-allocation MPSC ring buffer**: Fixed-size 48-byte records in bounded shared memory with lock-free sequence publication. Guaranteed zero `malloc()` calls in the critical execution path to preserve deterministic RTOS timing.
- **Formal delay attribution model**: Quantitatively decomposes activation latency as Response Time = Ready Queue Wait + Preemption Duration + Blocking Time + Execution Time
- **Evidence-backed root cause engine**: Classifies deadline misses with CONFIRMED vs INFERRED confidence and identifies the interfering process ID, thread ID, priority, and exact overlap window
- **Physical UART**: Real POSIX serial driver (`/dev/ser1` at 115200 baud) for telemetry & external event triggering
- **Hardware GPIO markers**: Direct BCM2711 peripheral register memory mapping (`0xFE200000`) for sub-microsecond physical oscilloscope timing verification
- **CAN layer**: Non-blocking simulated adapter queue + TCP socket injection server
- **Automated S0–S6 Benchmark Matrix**: Built-in test harness executing baseline runs, 20%–95% CPU load sweeps, mutex priority inversions, CAN event storms, and core affinity contention

## System Architecture

```mermaid
graph LR
    subgraph AUTOMOTIVE WORKLOAD LAYER
        direction TB
        BRAKE_CTL[BRAKE_CTL (Prio 20)]
        ADAS_FUSION[ADAS_FUSION (Prio 15)]
        DIAG[DIAG (Prio 10)]
    end

    subgraph MPSC BOUNDED SHARED-MEMORY RING BUFFER
        direction LR
        LB[• Lock-free sequence publication]
        RB[• 48-byte fixed records]
        ZA[• Zero runtime heap allocation]
        PF[• Post-mortem file flush]
    end

    subgraph SCHEDULIX ANALYZER ENGINE
        direction TB
        KEV[• .kev trace decoding]
        RCA[• Delay attribution & RCA]
        JSON[• analysis.json output]
    end

    classDef ASIL-D fill:#ffdddd,stroke:#ff0000,stroke-width:2px;
    classDef ASIL-B fill:#ffffdd,stroke:#cccc00,stroke-width:2px;
    classDef QM fill:#ddffff,stroke:#00ccff,stroke-width:2px;

    class BRAKE_CTL ASIL-D
    class ADAS_FUSION ASIL-B
    class DIAG QM
```

## Real-Time Workload Model

| Task Name | Task ID | Priority | Period | Deadline | Execution Demand | Criticality | Target Role |
|-----------|---------|----------|--------|----------|-----------------|-------------|-------------|
| BRAKE_CTL | 1 | 20 (SCHED_FIFO) | 10 ms | 10 ms | 2.0 ms | ASIL-D | Emergency Braking & Stability Control |
| ADAS_FUSION | 2 | 15 (SCHED_FIFO) | 20 ms | 20 ms | 8.0 ms | ASIL-B | Radar/Camera Sensor Fusion |
| DIAG_POLL | 3 | 10 (SCHED_FIFO) | 50 ms | 50 ms | 5.0 ms | QM | OBD-II / UDS Diagnostics Telemetry |
| STRESS_WORKER | 4-8 | 5 - 22 | Config | Config | Config | Stress | Background Load & Contention Injector |

## Automated Experiment Suite (S0–S6)

| Scenario | Code | Description | Injected Condition | Evaluated Behavior |
|----------|------|-------------|-------------------|-------------------|
| Nominal Baseline | S0 | Clean execution | No background stress | Zero jitter, nominal slack (+7.98 ms) |
| Load Sweep | S1 | Scaled CPU saturation | 20% ➔ 95% background load | Monotonic latency increase, knee detection |
| Mutex Contention | S2 | Priority inversion | Shared resource lock | Priority inheritance & blocking delays |
| CAN Burst | S3 | External I/O load | 10x CAN frame bursts | Event Manager dispatch latency & queueing |
| Preemption Storm | S4 | High-prio preemption | Prio 22 task preemption | Confirmed preemption RCA & lateness metrics |
| Mixed Criticality | S5 | Combined interference | CPU load + CAN + Mutex | Cumulative latency degradation |
| Core Affinity | S6 | Multicore contention | Pinning tasks to Core 0 | Cache thrashing & scheduler migration |

## Directory Structure

```
Schedulix/
├── src/                         # QNX Native C Performance Engine
│   ├── main.c                   # CLI router & command entrypoint
│   ├── workload.c / .h          # Real-time periodic workload threads
│   ├── workload_config.c / .h   # Automotive ECU workload parameters
│   ├── trace_collector.c / .h   # Lock-free MPSC ring buffer collector
│   ├── trace_schema.h           # 48-byte binary trace record schema
│   ├── trace_instrumentation.h  # Zero-overhead timestamp macros
│   ├── qnx_kernel_trace_parser.c/.h # Native libtraceparser .kev parser
│   ├── scheduler_correlator.c/.h# Per-CPU scheduler transition correlator
│   ├── analyzer.c / .h          # Delay attribution & RCA engine
│   ├── stress_generator.c / .h  # Synthetic workload & CPU load generator
│   ├── stress_scenarios.c / .h  # Automated S0-S6 experiment harness
│   ├── uart_adapter.c / .h      # Physical POSIX UART driver (/dev/ser1)
│   ├── gpio_marker.c / .h       # Hardware GPIO memory mapper (0xFE200000)
│   ├── can_decoder.c / .h       # CAN frame ID & payload decoder
│   └── event_manager.c / .h     # Asynchronous event dispatcher
├── gui/                         # Qt 6 / QML Desktop Trace Viewer
│   ├── Main.qml                 # Main window container & navigation
│   ├── main.cpp                 # Qt application runner & backend bridge
│   ├── qml/                     # QML views (Dashboard, Timeline, RCA, Experiments)
│   └── src/                     # C++ trace file parser & QML data models
├── screenshots/                 # High-resolution application screenshots
├── docs/                        # Specifications, architecture, & QNX guides
├── tests/                       # Unit tests & Python verification scripts
├── Makefile                     # QNX SDP 8.0 qcc Makefile (aarch64le & x86_64)
└── Makefile.host                # Host fallback build (Linux/GCC/Clang)
```

## 📖 Documentation

| Document | Purpose |
|---|---|
| `docs/BRINGUP_GUIDE.md` | Complete reproducible procedure: clone, host tests, cross-compile, deploy, verify UART and GPIO, build and install the MCP2515 CAN driver |
| `docs/HACKATHON_RUNBOOK.md` | 48hr hackathon runbook — roles, time-boxed plan, demo script, judge Q&A |
| `docs/HANDOVER.md` | What is verified, what is next, and why the load sweep is flat |
| `docs/PROBLEM_STATEMENT_COMPLIANCE.md` | Requirement-by-requirement scorecard, verified vs. unverified |
| `docs/INCIDENT_SPI_DRIVER.md` | The SPI bring-up incident, what was ruled out, and the resume procedure |
| `docs/architecture.md` | Component design |
| `docs/timing-model.md` | Delay attribution mathematics |
| `docs/gpio.md` · `docs/uart.md` | Per-interface detail |
| `docs/HARDWARE_PROCUREMENT_PLAN.md` | Bill of materials, wiring, CAN bus topology |
| `CURRENT_STATE.md` | Live project state |
| `VALIDATION_LOG.md` | Chronological verification record |

## Target Requirements

| Requirement | Specification |
|---|---|
| **OS** | QNX Neutrino 8.0.0, Quick Start Target Image (QSTI) |
| **Hardware** | Raspberry Pi 4 (BCM2711) |
| **Toolchain** | QNX SDP 8.0 — the SDP, not just the IDE |
| **Serial console** | 115200 8N1, passwordless root at `root@console:/#` |
| **SSH** | `ssh -m hmac-sha2-256 qnxuser@<target-ip>` — the MAC override is mandatory |
| **CAN** | MCP2515 on SPI0/CE0 (Waveshare RS485 CAN HAT, SKU 14882) — seat on the 40-pin header |

## Build & Deployment Guide

### Prerequisites
- QNX Software Development Platform (SDP) 8.0
- QNX Neutrino RTOS Target (Raspberry Pi 4 / 5 or x86_64 QNX VM)
- Qt 6.5+ (for the optional Qt/QML UI viewer)

### 1. Cross-Compiling for QNX Target (aarch64le)

```bash
# Set up QNX SDP environment
source ~/qnx800/qnxsdp-env.sh     # Linux / macOS
# or: C:\Users\User\qnx800\qnxsdp-env.bat  # Windows

# Build release/debug aarch64 binary
make PLATFORM=aarch64le BUILD_PROFILE=debug
# Resulting binary: build/aarch64le-debug/schedulix_can
```

### 2. Deploying to Raspberry Pi 4

```bash
scp build/aarch64le-debug/schedulix_can root@<target-ip>:/tmp/
```

### Running on Target (CLI Commands)

```bash
# SSH into your QNX board as root (uid=0 is required for tracelogger kernel access)
ssh root@<target-ip>
cd /tmp

# Check System Status & Peripherals
./schedulix_can status
# Outputs workload states, physical UART detection (/dev/ser1), GPIO availability, and privileged kernel tracer status.

# Run an Automated Experiment (e.g., Scenario 4: Preemption Storm)
./schedulix_can experiment run S4
# Generates trace_s4.bin, manifest_s4.json, and analysis_s4.json

# Decode Raw QNX Kernel Events (.kev)
./schedulix_can trace decode /tmp/schedulix.kev | head -n 30

# Post-Mortem Trace Analysis
./schedulix_can analyze trace_s4.bin
# Executes the RCA engine and prints percentile stats (p50/p95/p99) and deadline slack
```

## Sample JSON Output (analysis.json)

```json
{
  "trace": {
    "records": 16384,
    "dropped": 0
  },
  "per_task": [
    {
      "task_id": 1,
      "activations": 1923,
      "misses": 0,
      "miss_ratio": 0.0000,
      "mean_ms": 2.012,
      "p50": 2.012,
      "p95": 2.018,
      "p99": 2.024,
      "max": 2.038
    }
  ],
  "activations": [
    {
      "task_id": 1,
      "activation_id": 1352,
      "correlation_id": 1352,
      "response_ns": 2012444,
      "slack_ns": 7987556,
      "cpu_first": 1,
      "root_cause": "HIGH_PRIORITY_PREEMPTION",
      "evidence_level": "CONFIRMED",
      "interferer_pid": 4294967295,
      "interferer_tid": 260,
      "interferer_priority": 22,
      "preemption_duration_ns": 10900000,
      "lateness_ns": 0,
      "context_switches": 42
    }
  ]
}
```

## Hackathon Team & Details

```
Project: Schedulix — Automotive RTOS Performance & Latency Analyzer
Institution: Vasavi College of Engineering
Team Members:
  Abdul Aleem (1602-23-735-001)
  Kritika Giridhar (1602-23-735-018)
  Rishi N. (1602-23-735-033)
Problem Statement: Track 16 — Automotive RTOS Scheduling Analysis, Instrumentation & Observability on QNX / Raspberry Pi
```

---

*Built with QNX SDP 8.0 • Designed for automotive safety-critical mixed-criticality workloads*

---