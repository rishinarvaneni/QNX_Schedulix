# QNX_Schedulix

**Automotive RTOS Performance & Latency Analyzer** - QNX Neutrino RTOS scheduling and IPC analysis project.

[![RTOS](https://img.shields.io/badge/RTOS-QNX_Neutrino_8.0-blue.svg)](https://blackberry.qnx.com)
[![Platform](https://img.shields.io/badge/Platform-aarch64le-green.svg)](https://www.qnx.com/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

## Project Overview

This project implements a brake request/response system using **QNX Neutrino RTOS** primitives to demonstrate:
- Priority-based preemptive scheduling with `SCHED_FIFO`
- Inter-Process Communication (IPC) via QNX message passing (`MsgSend`/`MsgReceive`)
- Name service for location-transparent client-server connectivity
- Nanosecond-resolution timing using `clock_gettime(CLOCK_MONOTONIC)`

The system consists of two processes:
- **Server** (`break_workload`): High-priority (`30`) thread that receives brake requests via name service and responds with timing data
- **Client** (`break_trigger`): Sends 10 brake requests to the server and measures response times

## Phase-Wise Study Coverage

### Phase 1: RTOS Fundamentals
- QNX Neutrino RTOS concepts and architecture
- Processes vs. threads in QNX
- POSIX standards compliance on QNX
- Scheduling policies: `SCHED_FIFO`, `SCHED_RR`, and `SCHED_OTHER`
- Priority-based preemptive scheduling mechanisms
- Thread creation and management with `pthread_setschedparam()`

### Phase 2: Inter-Process Communication (IPC)
- QNX Name Service: `name_attach()`, `name_open()`, `name_close()`
- Message Passing: `MsgSend()`, `MsgReceive()`, `MsgReply()`
- Pulse message handling (`rcvid == 0` detection)
- Well-known pathname `/brake` for server registration
- Location-transparent IPC (clients don't need channel IDs)

### Phase 3: Timing and Instrumentation
- Nanosecond-resolution timestamps via `clock_gettime(CLOCK_MONOTONIC, ...)`
- Response time calculation: `(completed_ns - release_ns) / 1e6` (ms)
- Runtime `printf` diagnostics for server and client
- Trace validation with QNX `.kev` kernel event files

### Phase 4: Scheduling Analysis
- FIFO scheduling: once server starts a request, runs to completion
- Priority preemption: threads > 30 can preempt; threads < 30 cannot interrupt
- Verification commands: `cat /proc/self/sched`, `pidin t | grep brake_workload`
- Expected behavior analysis with multi-priority thread systems

### Phase 5: Build and Execution
- **Build commands**: `make clean`, `make all`, `make rebuild`
- **Build profiles**: debug (`-g -O0`), release (`-O2`), coverage, profile
- **Platform**: `aarch64le` (QNX Neutrino 8.0)
- **Serialization**: `tools/serial_console.ps1` for target board communication

### Phase 6: .kev File Validation (QNX Kernel Events)
- Kernel event tracing with `tracetool` or `procnto -k trace`
- Expected events: `SchedEnter/SchedLeave`, `MsgSend/MsgsReply`, `TimerEnter/TimerExit`, `NameAttach/NameOpen`
- Cross-reference `printf` output values with `.kev` events
- Validation steps for IPC timing correctness

### Phase 7: Data Structures
- `brake_request_t`: `{sequence, brake_request, release_ns}` - client-to-server
- `brake_response_t`: `{sequence, decision, received_ns, completed_ns}` - server-to-client

## Repository Structure

```
Schedulix/
├── break_trigger/          # Client module
│   ├── Makefile
│   ├── src/
│   │   └── break_trigger.c
│   └── docs/
├── break_workload/         # Server module
│   ├── Makefile
│   ├── src/
│   │   └── break_workload.c
│   ├── docs/
│   └── MILESTONE_DOCUMENTATION.md
├── tools/
│   └── serial_console.ps1
├── .gitignore
├── README.md
└── MILESTONE_DOCUMENTATION.md  # Comprehensive phase documentation
```

## Quick Start

```bash
# From break_trigger or break_workload directory
make clean
make all

# Run the server in background
./build/aarch64le-debug/break_workload &
SERVER_PID=$!

# Run the client
./build/aarch64le-debug/break_trigger

# Kill the server
kill $SERVER_PID
```

## Referenced Projects

This project builds upon concepts from my previous works:

- **[Network_Telemetry_SoC](https://github.com/Abdul99Aleem/Network_Telemetry_SoC)**: RISC-V SoC network telemetry and secure packet monitoring - studied hardware packet inspection, AES-128 encryption, and 128-bit record structuring
- **[Schedulix](https://github.com/Abdul99Aleem/Schedulix)**: Original QNX schedulix implementation - studied RTOS scheduling theory and priority-based preemptive scheduling

Both repos follow a natural, ownership-aware design philosophy where hardware/software boundaries are clearly defined and systematically documented.

## License

MIT