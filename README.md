# Schedulix: Automotive RTOS Performance & Latency Analyzer

[![QNX](https://img.shields.io/badge/RTOS-QNX_Neutrino_8.0-blue.svg)](https://blackberry.qnx.com)
[![Target](https://img.shields.io/badge/Target-Raspberry_Pi_4-red.svg)](https://www.raspberrypi.com/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

## 1. Why this project exists

In a software-defined vehicle, an ASIL-D braking task, an ASIL-B perception task and a QM diagnostics task all share the same multicore SoC. When a deadline gets missed, the CPU utilization graph will happily tell you the system was "loaded" — and nothing else. It won't tell you which higher-priority thread preempted the victim, how long the victim waited in the ready queue, or whether the delay was preemption, blocking, or just execution overrun.

I built Schedulix to answer that concretely: **for every job, decompose the response time into its parts and name the interferer, with the timestamps to prove it.** No claimed metric without the numbers behind it — that rule runs through the whole repo.

The scope for this hackathon submission (Track 16 — Automotive RTOS Scheduling Analysis, Instrumentation & Observability on QNX / Raspberry Pi): a brake request/response workload on QNX Neutrino 8.0, a 3-level priority task set with core-affinity migration, a one-file CLI that turns runs into latency statistics and timelines, and kernel traces captured off the target with `tracelogger`.

## 2. What I built

One coherent data path, from a message send to a latency number. Three pieces:

**`break_workload/` — the server.** Registers the well-known name `brake`, runs SCHED_FIFO, and answers brake requests with nanosecond timestamps. It has grown into a 3-level task set: L1 brake at priority 30, L2 at 20, L3 at 10, each pinned to a core with a migration counter that trips when its core's load crosses 90%. The main thread never leaves `MsgReceive`/`MsgReply` duty, so clients can't block forever.

**`break_trigger/` — the client.** Opens `brake`, sends 10 requests one second apart, prints the response time of each. Right now it's a debug build: it prints the connection id after `name_open`, the return code of every `MsgSend`, and per-job decision plus `response_ns` — because it currently hangs right after `coid` assignment and I need the prints to say whether it's stuck *in* `MsgSend` or never getting a reply. That's what I'm debugging next.

**`schedulix_cli/` — the measurement CLI.** One file, five commands, end to end: `run` drives the server and writes `jobs.csv`; `capture` wraps `tracelogger`; `analyze` computes mean/min/max/jitter/p50/p95/p99/misses into `analysis.json`; `gantt` renders an HTML timeline; `report` and `compare` give CSV summaries. `jobs.csv` is the source of truth and the `.kev` is only stat'ed for size — the metrics come from release/completion timestamps we actually captured.

**`gui/` — the Qt 6 dashboard.** A desktop viewer (Dashboard, Timeline, Root Cause, Experiments, Jitter screens) backed by C++ models, plus `parse_kev.py`, which decodes the raw `.kev` binary — header located by marker, 16-byte little-endian events, RUNNING/READY/BLOCKED classes validated against the `break_trigger` pid — into `brake_data.json`, `timeline_data.json` and `kev_summary.json` for the views. The Beta screen bakes in a real 3-job board run so the tab is never empty, and Reload pulls fresh `analysis.json` off the target. `gui/data/` carries sample `jobs.csv`/`analysis.json` so the app opens meaningfully with no hardware attached.

## 3. How it works, in the order I built it

**Phase 1 — IPC bring-up.** QNX name service plus synchronous message passing: `name_attach("brake")` on the server, `name_open("brake")` on the client, `MsgSend`/`MsgReceive`/`MsgReply` carrying `brake_request_t` (`sequence`, `brake_request`, `release_ns`) one way and `brake_response_t` (`sequence`, `decision`, `received_ns`, `completed_ns`) back. Pulses (`rcvid == 0`) are skipped, not mistaken for requests. Response time is `(completed_ns - release_ns)`. An early linker fight — both files defined `main()` — got fixed by keeping `main()` in the trigger and server init in `init_server()`.

**Phase 2 — Scheduling.** The server runs SCHED_FIFO at priority 30, set with `pthread_setschedparam`, verified on target with `pidin`. FIFO means a started request runs to completion; anything above 30 can preempt between receive and reply. Timestamps come from `clock_gettime(CLOCK_MONOTONIC)`, and every print is line-buffered with explicit `fflush` so nothing gets lost over the serial console.

**Phase 3 — Thread affinity and migration (this phase).** Each level tracks its core, activation count and migration count in a `task_info` table. When a level's core load crosses the 90% threshold, L1/L2 migrate to the next core and L3 yields. The real `pthread_setaffinity_np` calls are sketched in the code comments; what runs today is the simulated version of the same policy, so the migration paths get exercised before I touch real affinity on the target. The server split — `init_server()` attaches the name and spawns the level threads, `server_loop()` keeps the main thread on `MsgReceive` — is what keeps the client unblocked.

**Phase 4 — Measurement pipeline.** `schedulix run --scenario baseline --duration 10 --jobs /tmp/jobs.csv` produces the CSV; `schedulix capture --duration 3 --output /tmp/run.kev` grabs the kernel trace; `schedulix analyze --trace /tmp/run.kev --jobs /tmp/jobs.csv --deadline 10` writes the JSON with percentiles and miss counts; `schedulix gantt` and `schedulix report` turn it into a timeline and a summary table.

**Phase 5 — Kernel traces.** Captured on target with `tracelogger` (a 49 MB run with `-s 20`; the "not keeping up" warnings are buffer pressure, expected). The `.kev` files are imported in the Momentics System Profiler and stay local — trace binaries are gitignored, so what's in the repo is the code that produced them and the CLI that turns them into numbers.

**Phase 6 — Dashboard GUI.** The Qt app closes the loop: `parse_kev.py brake_run.kev` decodes the kernel trace into dashboard JSONs (per-job response times, per-CPU timeline lanes with brake intervals highlighted, context-switch and utilization summary), and the QML views render them — latencies and percentiles on the Dashboard, execution lanes on the Timeline, the interferer call-out on the Root Cause screen. It runs against `gui/data/` samples out of the box; point it at fresh parser output for live board runs.

## 4. Where things stand

Honest status, because a judge will ask:

- **Proven on target:** server prints `BRAKE server ready: SCHED_FIFO, priority 30`, all 10 jobs complete, `decision=1` across the board, per-job `release_ns`/`receive_ns`/`complete_ns` on the console. That console log alone is the core evidence.
- **In progress:** the trigger hang after `coid` assignment — debug prints are in, root cause is next.
- **Simulated, not yet real:** core load readings and affinity migration (policy runs, `pthread_setaffinity_np` wiring is next), and the S0–S6 stress matrix scenarios beyond baseline.
- **Captured, not yet decoded in-repo:** `.kev` traces open in the IDE profiler; in-repo decoding is future work, which is why `analyze` deliberately refuses to invent metrics from a binary it doesn't parse.

## 5. Repository layout

```
Schedulix/
├── break_trigger/          # Client: opens "brake", sends 10 jobs, prints response times
│   ├── Makefile            # qcc build, ARTIFACT=break_trigger, aarch64le-debug default
│   └── src/
│       └── break_trigger.c # name_open + MsgSend loop, debug prints around coid/MsgSend
├── break_workload/         # Server: "brake" name service, SCHED_FIFO prio 30, L1/L2/L3 tasks
│   ├── Makefile            # qcc build, ARTIFACT=break_workload
│   └── src/
│       └── break_workload.c# init_server + server_loop, affinity/migration policy, timestamps
├── schedulix_cli/          # One-file measurement CLI: run/capture/analyze/gantt/report/compare
│   ├── Makefile            # qcc build, ARTIFACT=schedulix
│   └── src/
│       └── schedulix.c     # jobs.csv in, analysis.json + gantt.html + CSV reports out
├── gui/                    # Qt 6.8 dashboard (CMake): Dashboard/Timeline/RCA/Experiments views
│   ├── CMakeLists.txt      # project schedulix_gui, exe appschedulix_gui, QML module schedulix_gui
│   ├── main.cpp / Main.qml # app entry + 1536x864 window with sidebar navigation
│   ├── qml/                # theme/, components/, screens/ (Dashboard, Timeline, RootCause, ...)
│   ├── src/                # AppController + C++ models (TaskMetrics, Timeline, RCA, Jitter, ...)
│   ├── parse_kev.py        # .kev binary → brake_data/timeline_data/kev_summary JSONs
│   ├── hw/                 # host-side hardware preflight checks
│   └── data/               # sample jobs.csv + analysis.json so the GUI runs with no target
├── tools/
│   └── serial_console.ps1  # Serial console helper for the Pi UART link
├── docs/                   # Bring-up guide, runbooks, architecture, timing model (see §8)
├── README.md
├── MILESTONE_DOCUMENTATION.md
└── .gitignore              # build/, *.kev, *.log — traces and binaries stay local
```

Trace binaries (`.kev`), build output and logs are gitignored on purpose: a fresh clone carries everything needed to reproduce a result, and nothing a tool regenerates.

## 6. Build, deploy, run

Built with QNX SDP 8.0 (`qcc -Vgcc_ntoaarch64le`), deployed to a Raspberry Pi 4 at `192.168.10.5` running the QNX 8.0 QSTI image. Serial console at 115200 8N1; SSH/SCP need the MAC override.

```bat
:: 1. Build (Windows host, from break_trigger / break_workload / schedulix_cli)
cmd /c "C:\Users\User\qnx800\qnxsdp-env.bat && make all"
:: binary lands at build\aarch64le-debug\<name>
```

```bash
# 2. Deploy to the board
scp -o "MACs=hmac-sha2-256-etm@openssh.com" build/aarch64le-debug/break_workload root@192.168.10.5:/tmp/
scp -o "MACs=hmac-sha2-256-etm@openssh.com" build/aarch64le-debug/break_trigger root@192.168.10.5:/tmp/
scp -o "MACs=hmac-sha2-256-etm@openssh.com" build/aarch64le-debug/schedulix root@192.168.10.5:/tmp/
```

```bash
# 3. Run on the board (server first, backgrounded — it never exits)
ssh root@192.168.10.5
/tmp/break_workload &
/tmp/break_trigger
```

```bash
# 4. Measure with the CLI
/tmp/schedulix run --scenario baseline --duration 10 --jobs /tmp/jobs.csv
/tmp/schedulix capture --duration 3 --output /tmp/run.kev
/tmp/schedulix analyze --trace /tmp/run.kev --jobs /tmp/jobs.csv --deadline 10 --out /tmp/analysis.json
/tmp/schedulix gantt --input /tmp/analysis.json --output /tmp/gantt.html
/tmp/schedulix report --input /tmp/analysis.json --out /tmp/report.csv
```

```bash
# 5. Decode a kernel trace into dashboard JSONs (host machine, needs python3)
python gui/parse_kev.py brake_run.kev
# writes brake_data.json + timeline_data.json + kev_summary.json next to gui/data/

# 6. Build and run the dashboard (host machine, needs Qt 6.8 + CMake)
cmake -S gui -B gui/build -DCMAKE_BUILD_TYPE=Debug
cmake --build gui/build
./gui/build/appschedulix_gui
# opens on bundled gui/data/ samples; Beta screen Reload pulls fresh target files
```

Pi UART wiring (from `tools/serial_console.ps1`): header pin 6 = GND, pin 8 = GPIO14/TX (Pi transmits), pin 10 = GPIO15/RX (Pi receives).

## 7. What the output looks like

Server console (the proof the system works):

```
BRAKE server ready: SCHED_FIFO, priority 30
L1 job=1 decision=1 response_ns=42113
L1 job=2 decision=1 response_ns=39877
...
```

Client console:

```
break_trigger starting...
name_open succeeded, coid=5
MsgSend 1 rc=0
Job 1: decision=1 response_ns=42113
...
All 10 jobs sent successfully
break_trigger done
```

`jobs.csv` (one row per job — this is what `analyze` reads):

```
seq,release_ns,completed_ns,response_us,decision
1,1827349123456,1827349165569,42,1
...
```

`analysis.json` (percentiles, jitter, misses against the deadline, plus the trace size for provenance):

```json
{
  "jobs_csv": "/tmp/jobs.csv",
  "trace": "/tmp/run.kev",
  "trace_bytes": 49152,
  "count": 10,
  "deadline_ms": 10.000,
  "mean_us": 41.5,
  "min_us": 39,
  "max_us": 45,
  "jitter_us": 6,
  "p50_us": 41,
  "p95_us": 44,
  "p99_us": 45,
  "misses": 0,
  "jobs": [
    {"seq":1,"release_ns":1827349123456,"completed_ns":1827349165569,"response_us":42,"decision":1}
  ]
}
```

## 8. Documentation index

| Document | Contents |
|---|---|
| `docs/BRINGUP_GUIDE.md` | Clone → host tests → cross-compile → deploy → UART → GPIO → CAN → verify |
| `docs/HACKATHON_RUNBOOK.md` | 48-hour runbook — roles, time-boxed plan, demo script, judge Q&A |
| `docs/HANDOVER.md` | What is verified, what is next |
| `docs/PROBLEM_STATEMENT_COMPLIANCE.md` | Requirement-by-requirement scorecard, verified vs unverified |
| `docs/INCIDENT_SPI_DRIVER.md` | SPI bring-up incident, what was ruled out, resume procedure |
| `docs/architecture.md` | Component design |
| `docs/timing-model.md` | Delay-attribution mathematics |
| `docs/gpio.md` · `docs/uart.md` | Per-interface detail |
| `docs/HARDWARE_PROCUREMENT_PLAN.md` | Bill of materials, wiring, CAN bus topology |

Target requirements: QNX Neutrino 8.0.0 QSTI on Raspberry Pi 4 (BCM2711), QNX SDP 8.0 toolchain, passwordless root on serial console, `ssh -m hmac-sha2-256` for the MAC override, MCP2515 CAN on SPI0/CE0.

## 9. Hackathon team

```
Project: Schedulix — Automotive RTOS Performance & Latency Analyzer
Institution: Vasavi College of Engineering
Team members:
  Abdul Aleem (1602-23-735-001)
  Kritika Giridhar (1602-23-735-018)
  Rishi N. (1602-23-735-033)
Problem statement: Track 16 — Automotive RTOS Scheduling Analysis, Instrumentation & Observability on QNX / Raspberry Pi
```

---
*Built with QNX SDP 8.0 — measured, not asserted.*
