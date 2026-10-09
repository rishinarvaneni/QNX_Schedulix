# Milestone Documentation: break_workload RT Project

## 1. RTOS Concepts

This project targets **QNX Neutrino RTOS** (indicated by `sys/neutrino.h`, `sys/dispatch.h`, and the `qcc` cross-compiler). Key concepts:

### Processes & Threads
- `break_workload.c` creates a server thread via `name_attach()` and `pthread_setschedparam()` with `SCHED_FIFO` policy at priority 30.
- `break_trigger.c` (now `send_brake_requests()`) is a client that opens a connection via `name_open("brake", 0)` and sends messages via `MsgSend()`.

### Inter-Process Communication (IPC)
- **Pulse-driven messaging**: The server uses `MsgReceive()` to receive brake request messages.
- **Messages**: `brake_request_t` (sequence, brake_request, release_ns) → `brake_response_t` (sequence, decision, received_ns, completed_ns).
- **Name service**: `name_attach(NULL, "brake", 0)` registers the server under the well-known path `/brake`. Clients connect via `name_open("brake", 0)`.
- **Pulses**: `rcvid == 0` indicates a pulse message, which the server skips (`continue`).

### Timing
- `now_ns()` uses `clock_gettime(CLOCK_MONOTONIC, ...)` for nanosecond-resolution timestamps.
- Response time is computed as `(resp.completed_ns - req.release_ns) / 1e6` (milliseconds).

### Startup Sequence
1. Server (`break_workload`): `name_attach()` → set SCHED_FIFO priority → `MsgReceive()` loop.
2. Client (`break_trigger` or `send_brake_requests()`): `name_open("brake", 0)` → loop sending 10 requests via `MsgSend()` → `name_close()`.

---

## 2. Scheduling Analysis

### Priority-Based Preemptive Scheduling
- The server runs with `SCHED_FIFO` at priority **30** (`BRAKE_PRIORITY`).
- FIFO means once the server starts a request, it runs to completion without time-slicing.
- Higher-priority threads can preempt the server; lower-priority threads cannot interrupt.

### Verification on Hardware
```bash
# Check current scheduling policy and priority
cat /proc/self/sched

# Verify the server thread priority
pidin t | grep brake_workload

# List all threads with their priorities
pidin t
```

### Expected Behavior
- Server should maintain priority 30 throughout execution.
- If the system has other threads at priority > 30, they will preempt the server between `MsgReceive` and `MsgReply`.
- The `1-second` delay between requests (`nanosleep`) runs at the scheduled priority.

---

## 3. Instrumentation

The code includes runtime `printf` output for diagnostics:

### Server Output (`break_workload.c:86-92`)
```
BRAKE job=%u decision=%u release_ns=%llu receive_ns=%llu complete_ns=%llu
```

### Client Output (`break_trigger.c:61-63`)
```
Job %u: response time = %.3f ms
```

### Validation Commands
```bash
# Run the server in background
./build/aarch64-debug/break_workload &
SERVER_PID=$!

# Run the client
./build/aarch64-debug/break_trigger

# Kill the server
kill $SERVER_PID
```

### Expected Output Pattern
```
BRAKE server ready: SCHED_FIFO, priority 30
BRAKE job=0 decision=1 release_ns=1234567890 receive_ns=1234567895 complete_ns=1234567900
Job 1: response time = 10.000 ms
...
BRAKE job=9 decision=1 release_ns=... receive_ns=... complete_ns=...
Job 10: response time = 10.000 ms
```

---

## 4. IPC Mechanisms

### Message Passing (QNX Neutrino)
- **`MsgSend()`**: Synchronous send/receive. Client blocks until server replies.
- **`MsgReceive()`**: Server receives messages on its channel chid.
- **`MsgReply()`**: Server sends response back to client's SID.

### Pulse Messages
- Special QNX mechanism where `rcvid == 0`.
- Used for notifications/control signals (not used in current code but the server handles it).

### Name Service
- `name_attach()` registers a channel under a pathname.
- `name_open()` opens a connection to that pathname, returns a connection ID (`coid`).
- Enables location-transparent IPC: clients don't need to know the underlying channel ID.

### Data Structures
- `brake_request_t`: {sequence, brake_request, release_ns} — sent from client to server.
- `brake_response_t`: {sequence, decision, received_ns, completed_ns} — sent from server to client.

---

## 5. CLI Usage

### Build Commands
```bash
# Clean and build
make clean
make all

# Build with different config (release)
make BUILD_PROFILE=release

# Rebuild from scratch
make rebuild
```

### Serial Console
From the `tools/` directory:
```bash
# Interactive mode (default COM6, 115200 8N1)
.\serial_console.ps1

# One-shot command
.\serial_console.ps1 -Command "id; hostname"

# Upload a binary (gzip + base64 over UART)
.\serial_console.ps1 -UploadFile build\aarch64le-debug\break_workload -RemotePath /tmp/break_workload
```

### Verifying the Build Artifact
```bash
# List built binary
ls -la build/aarch64le-debug/break_workload

# Check file type
file build/aarch64le-debug/break_workload

# Run on target (if connected)
./build/aarch64le-debug/break_workload &
./build/aarch64le-debug/break_trigger
```

---

## 6. .kev File Validation (QNX Kernel Event Format)

QNX `.kev` files capture kernel events (scheduling, IPC, timing). To validate instrumentation:

### Generate .kev Trace
The current code does not auto-generate `.kev`, but you can enable tracing:

```bash
# Enable kernel tracing
set -k trace=all

# Run the application
./build/aarch64le-debug/break_workload &
./build/aarch64le-debug/break_trigger

# Disable tracing
set -k trace=-all
```

### View .kev Events
```bash
# Convert .kev to human-readable
kinfo -t sched events.kev
kinfo -t ipc events.kev
kinfo -t timing events.kev
```

### Expected Events in .kev
| Event Type | Meaning |
|---|---|
| `SchedEnter / SchedLeave` | Thread scheduling/unscheduling |
| `MsgSend / MsgReceive` | IPC message send/receive |
| `TimerEnter / TimerExit` | Clock/nanosleep events |
| `NameAttach / NameOpen` | Name service events |

### Validation Steps
1. Run the application under tracing:
   ```bash
   tracetool -o trace.kev ./build/aarch64le-debug/break_workload &
   tracetool -o trace.kev ./build/aarch64le-debug/break_trigger
   ```
2. Analyze with `tracetool` or `procnto -k trace`:
   ```bash
   tracetool -i trace.kev "sched:[pid==%SERVER_PID]"
   tracetool -i trace.kev "ipc:[type==MSGSEND]"
   ```
3. Verify:
   - 10 `MsgSend` events from client to server
   - Corresponding `MsgReply` events from server to client
   - Timer nanoseconds match the `now_ns()` timestamps in the `printf` output
   - Response times are consistent (~10ms per request due to `nanosleep`)

### Using `procnto` with Instrumentation
```bash
# Start procnto with kernel instrumentation
procnto -k trace -i /dev/shm/trace.kev &

# Run the application
./break_trigger

# Stop tracing and export
tracetool -o trace.kev -e
```

### Cross-Reference with `printf` Output
Match the `release_ns`, `received_ns`, `completed_ns` values from `printf` output against the `TimerEnter/TimerExit` and `MsgSend/MsgsReply` events in the `.kev` file to confirm timing correctness.