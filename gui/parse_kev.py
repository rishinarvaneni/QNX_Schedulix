#!/usr/bin/env python3
"""
KEV (QNX Kernel Event Trace) parser -> Schedulix dashboard JSONs.

Format learned from brake_run.kev (QNX 8, RaspberryPi4B, 4 CPUs, 54 MHz):
  Text header ... TRACE_SYSPAGE_LEN::<N> TRACE_FILE_NAME... TRACE_HEADER_END::<syspage N bytes><16-byte events>
  NOTE: the old version used readline() for the header, which stops at the first
  0x0A byte *inside the binary syspage* -> misaligned events. We instead locate
  b'TRACE_HEADER_END::' and skip exactly SYSPAGE_LEN bytes.

16-byte event layout (little-endian): <hdr u32><ts u32><p1 u32><p2 u32>
  hdr: cpu = (hdr>>30)&3 | cls = (hdr>>10)&0x3F | ev = hdr&0x3FF
  ts:  32-bit cycle counter @ TRACE_CYCLES_PER_SEC (wraps ~79 s @54 MHz; trace is 20 s so u32 deltas are fine)
  Scheduling class: cls==4, ev==1 RUNNING, ev==2 READY, ev 3/4/5.. BLOCKED/SEND/RECEIVE...
    (validated: p1 of RUNNING events matches the pid associated with the
     'break_trigger' process-name record; per-CPU idle sequences match idle_cpu_* threads)
  Thread key: (p1, p2). Kernel idle key is (1,1) running on every CPU.
  Name records: cls==5 groups sharing one timestamp carry NUL-terminated path strings
    across consecutive 8-byte payloads; the group's first event p1 is the pid.

Outputs (all three locations so Qt finds them from Qt Creator *and* deployed exe):
  brake_data.json    - up to 10 brake jobs {sequence, decision, response_time_ms, release_ns, received_ns, completed_ns}
  timeline_data.json - {cpu_lanes:[{label,bars:[{l,c,tc,x,w}]}], can_events:[0..1], irq_events:[0..1]}
  kev_summary.json   - every dashboard metric + method notes
"""
import struct, re, json, shutil, sys
from pathlib import Path
from collections import Counter, defaultdict

GUI_DIR = Path(__file__).resolve().parent
KEV_PATH = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\User\ide-8.0.3-workspace\break_trigger\brake_run.kev"
BUILD_DIR = GUI_DIR / "build" / "Desktop_Qt_6_8_3_MinGW_64_bit_Debug"
DOCS_DIR = Path.home() / "Documents"
SRC_DATA_DIR = GUI_DIR / "data"

CYCLES_PER_SEC_DEFAULT = 54_000_000
DEADLINE_MS = 10.0          # dashboard DL marker + workload ~10 ms claim
JOB_GAP_MS = 50.0           # >50 ms gap between brake sched events => new job (client sleeps 1 s)
MAX_BARS_PER_CPU = 120

def parse_header(raw: bytes):
    m = raw.find(b"TRACE_HEADER_END::")
    if m < 0:
        raise SystemExit("TRACE_HEADER_END not found - not a QNX .kev file")
    hdr_txt = raw[:m].decode("ascii", errors="replace")
    cyc = re.search(r"TRACE_CYCLES_PER_SEC::(\d+)", hdr_txt)
    cpu = re.search(r"TRACE_CPU_NUM::(\d+)", hdr_txt)
    sp = re.search(r"TRACE_SYSPAGE_LEN::(\d+)", hdr_txt)
    cps = int(cyc.group(1)) if cyc else CYCLES_PER_SEC_DEFAULT
    ncpu = int(cpu.group(1)) if cpu else 4
    slen = int(sp.group(1)) if sp else 0
    ev_start = m + len(b"TRACE_HEADER_END::") + slen
    print(f"header: cycles_per_sec={cps} cpus={ncpu} syspage={slen} ev_start={ev_start} file={len(raw)}")
    return cps, ncpu, ev_start

def load_events(raw, ev_start):
    body = raw[ev_start:]
    n = len(body) // 16
    rem = len(body) % 16
    print(f"events: n={n} rem={rem}")
    # fast bulk unpack: 4 x u32 per event
    fmt = "<" + "IIII" * n
    vals = struct.unpack(fmt, body[:n * 16])
    return vals, n

def extract_names(vals, n):
    """pid -> set(names) from cls==5 string groups."""
    groups = defaultdict(list)  # (ts, ev) -> list of (idx, hdr, pay8)
    for k in range(n):
        hdr = vals[k * 4]
        if ((hdr >> 10) & 0x3F) != 5:
            continue
        e = hdr & 0x3FF
        if e not in (6, 21):
            continue
        ts = vals[k * 4 + 1]
        pay = struct.pack("<II", vals[k * 4 + 2], vals[k * 4 + 3])
        groups[(ts, e)].append((k, hdr, pay))
    pid_names = defaultdict(set)
    for (ts, e), lst in groups.items():
        lst.sort()
        blob = b"".join(p for _, _, p in lst)
        first_p1 = struct.unpack("<I", lst[0][2][:4])[0]
        for frag in blob.split(b"\x00"):
            s = frag.strip(b"\x00 .")
            if len(s) >= 3 and all(32 <= c < 127 or c in (47,) for c in s):
                try:
                    name = s.decode("ascii")
                except UnicodeDecodeError:
                    continue
                if "/" in name or "_" in name or name.isalnum():
                    pid_names[first_p1].add(name[-64:])
    return pid_names

def main():
    raw = Path(KEV_PATH).read_bytes()
    cps, ncpu, ev_start = parse_header(raw)
    vals, n = load_events(raw, ev_start)
    to_ms = lambda c: c / cps * 1000.0
    to_ns = lambda c: int(c / cps * 1e9)

    pid_names = extract_names(vals, n)
    print(f"named pids: {len(pid_names)}")
    brake_pids, idle_p1s = set(), set()
    for pid, names in pid_names.items():
        blob = " ".join(names).lower()
        if "break_workload" in blob or "break_trigger" in blob or ("/brake" in blob and "tmp" not in blob):
            brake_pids.add(pid)
        if "break" in blob and "trigger" in blob:
            brake_pids.add(pid)
        if "idle_cpu_" in blob:
            idle_p1s.add(pid)
    # '/dev/name/local/brake' pid is the name service, not a thread owner - drop pids whose ONLY name is that
    for pid in list(brake_pids):
        names = pid_names.get(pid, set())
        if names and all(v.strip().lower() in ("/dev/name/local/brake", "brake") for v in names):
            brake_pids.discard(pid)
    print("brake_pids:", {p: sorted(pid_names[p]) for p in brake_pids})
    print("idle pids:", {p: sorted(pid_names[p]) for p in list(idle_p1s)[:4]})

    # ---- scheduling scan (cls==4) ----
    SCH = 4
    RUN, RDY = 1, 2
    total_sw = 0
    per_cpu_events = defaultdict(list)   # cpu -> [(ts, ev, p1, p2)]
    ready_q = defaultdict(list)          # key -> [ts READY]
    latencies_ms = []
    send_to_run_ms = []                  # ext-event proxy: SEND(p1 brake or any) -> next RUNNING same key
    last_send_ts = {}
    run_keys = Counter()
    irq_ts = []                          # cls==2 proxy
    min_ts = None
    max_ts = 0
    first_ts = None
    for k in range(n):
        hdr, ts, p1, p2 = vals[k*4], vals[k*4+1], vals[k*4+2], vals[k*4+3]
        cls = (hdr >> 10) & 0x3F
        e = hdr & 0x3FF
        cpu = (hdr >> 30) & 3
        if first_ts is None:
            first_ts = ts
        if min_ts is None or ts < min_ts:
            min_ts = ts
        if ts > max_ts:
            max_ts = ts
        if cls == 2:
            irq_ts.append(ts)
            continue
        if cls != SCH:
            continue
        key = (p1, p2)
        per_cpu_events[cpu].append((ts, e, p1, p2))
        if e == RUN:
            total_sw += 1
            run_keys[key] += 1
            if key in ready_q and ready_q[key]:
                rts = ready_q[key].pop(0)
                if ts >= rts:
                    latencies_ms.append(to_ms(ts - rts))
            if key in last_send_ts and ts >= last_send_ts[key]:
                send_to_run_ms.append(to_ms(ts - last_send_ts[key]))
                del last_send_ts[key]
        elif e == RDY:
            ready_q[key].append(ts)
        elif e == 5:  # SEND / MsgSend - IPC request incl. brake client
            last_send_ts[key] = ts

    # 32-bit wrap-safe duration: trace is 20 s < 79 s wrap, but be safe
    span_cycles = (max_ts - min_ts) % 2**32
    dur_s = span_cycles / cps
    print(f"span: {dur_s:.2f} s  RUNNING events={total_sw}  top RUN keys={run_keys.most_common(8)}")

    IDLE_KEY = (1, 1)
    # ---- per-CPU busy time: interval after each RUNNING until next sched event on that CPU ----
    cpu_busy_ms, cpu_total_ms = {}, {}
    cpu_intervals = {}  # cpu -> [(start_ms, dur_ms, key, is_idle)]
    wrap = 2**32
    for cpu, evs in per_cpu_events.items():
        evs.sort()
        busy = 0.0
        ivs = []
        for i in range(len(evs) - 1):
            ts, e, p1, p2 = evs[i]
            nts = evs[i + 1][0]
            dt = (nts - ts) % wrap
            dms = to_ms(dt)
            if dms > 1000.0:   # drop wrap/hole artefacts
                continue
            if e == RUN:
                idle = (p1, p2) == IDLE_KEY or p1 in idle_p1s
                if not idle:
                    busy += dms
                ivs.append((to_ms((ts - min_ts) % wrap), dms, (p1, p2), idle))
        cpu_busy_ms[cpu] = busy
        cpu_total_ms[cpu] = to_ms(span_cycles)
        cpu_intervals[cpu] = ivs
    avg_util = sum(cpu_busy_ms.values()) / (sum(cpu_total_ms.values()) or 1) * 100.0

    # ---- brake jobs: cluster brake-key sched events by gap ----
    brake_keys = {k for k in run_keys if k[0] in brake_pids} if brake_pids else set()
    if not brake_keys:  # fallback: 2 most active non-idle keys (workload+trigger live here)
        cands = [k for k in run_keys.most_common(12) if k[0] != (1, 1)[0] or k != IDLE_KEY]
        brake_keys = {k for k, _ in cands[:4]}
        print("WARN: no brake pid match - fallback keys:", brake_keys)
    bev = []
    for cpu, evs in per_cpu_events.items():
        for (ts, e, p1, p2) in evs:
            if (p1, p2) in brake_keys:
                bev.append((ts, e, cpu, p1, p2))
    bev.sort()
    print(f"brake sched events: {len(bev)} keys={brake_keys}")
    gap_cyc = int(JOB_GAP_MS / 1000.0 * cps)
    jobs = []
    cur = []
    for t in bev:
        if cur and (t[0] - cur[-1][0]) % wrap > gap_cyc:
            jobs.append(cur)
            cur = []
        cur.append(t)
    if cur:
        jobs.append(cur)
    print(f"job clusters: {len(jobs)} sizes={[len(j) for j in jobs[:15]]}")

    job_rows = []
    for i, j in enumerate(jobs):
        rdy = [t for t in j if t[1] == RDY]
        run = [t for t in j if t[1] == RUN]
        rel = (rdy[0][0] if rdy else j[0][0])
        st = (run[0][0] if run else j[0][0])
        # completion: end of last RUNNING slice on its cpu
        end = j[-1][0]
        if run:
            lts, _, lcpu, _, _ = run[-1]
            nxt = None
            for (ts, e, c, a, b) in per_cpu_events[lcpu]:
                if ts > lts:
                    nxt = ts
                    break
            end = nxt if nxt is not None else (lts + int(0.05 / 1000 * cps))
        resp_ms = to_ms((end - rel) % wrap)
        job_rows.append(dict(sequence=i + 1, decision=1, response_time_ms=round(resp_ms, 3),
                              release_ns=to_ns((rel - first_ts) % wrap),
                              received_ns=to_ns((st - first_ts) % wrap),
                              completed_ns=to_ns((end - first_ts) % wrap),
                              _rel_ms=to_ms((rel - min_ts) % wrap), _resp=resp_ms,
                              _start_lat=to_ms((st - rel) % wrap) if st >= rel else 0.0))
    responses = sorted(j["_resp"] for j in job_rows)
    def pct(v, p):
        if not v:
            return 0.0
        import math
        k = (len(v) - 1) * p / 100.0
        f, c = math.floor(k), math.ceil(k)
        return v[f] if f == c else v[f] + (v[c] - v[f]) * (k - f)
    misses = sum(1 for r in responses if r > DEADLINE_MS)
    rels = [j["_rel_ms"] for j in job_rows]
    intervals = [b - a for a, b in zip(rels, rels[1:])]
    import statistics
    jit = dict(n_jobs=len(job_rows),
               interval_mean_ms=round(statistics.mean(intervals), 3) if intervals else 0.0,
               interval_std_ms=round(statistics.stdev(intervals), 3) if len(intervals) > 1 else 0.0,
               interval_min_ms=round(min(intervals), 3) if intervals else 0.0,
               interval_max_ms=round(max(intervals), 3) if intervals else 0.0,
               start_lat_avg_ms=round(statistics.mean([j["_start_lat"] for j in job_rows]), 4) if job_rows else 0.0,
               start_lat_max_ms=round(max([j["_start_lat"] for j in job_rows]), 4) if job_rows else 0.0)

    def avg(v):
        return sum(v) / len(v) if v else 0.0
    latencies_ms.sort()
    summary = dict(
        file=KEV_PATH, events=n, cycles_per_sec=cps, cpu_count=ncpu,
        duration_s=round(dur_s, 3),
        ctx_switches=total_sw,
        ctx_per_sec=round(total_sw / dur_s, 1) if dur_s else 0,
        preemptions=sum(1 for _ in latencies_ms),  # READY->RUNNING transitions = preempt/resume count proxy
        migrations="n/a (per-(pid,tid) thread keys stay valid; cross-CPU key moves not tracked in v1)",
        task_latency_ready_to_run_ms=dict(n=len(latencies_ms), avg_ms=round(avg(latencies_ms), 4),
                                          p50_ms=round(pct(latencies_ms, 50), 4),
                                          p99_ms=round(pct(latencies_ms, 99), 4),
                                          max_ms=round(max(latencies_ms), 4) if latencies_ms else 0.0),
        jitter=jit,
        cpu_util=dict(per_cpu={str(c): round(cpu_busy_ms.get(c, 0) / (cpu_total_ms.get(c, 1) or 1) * 100, 1) for c in range(ncpu)},
                      avg_pct=round(avg_util, 1)),
        deadlines=dict(deadline_ms=DEADLINE_MS, jobs=len(job_rows), misses=misses,
                       miss_rate_pct=round(misses / len(job_rows) * 100, 2) if job_rows else 0.0,
                       p50_ms=round(pct(responses, 50), 3), p99_ms=round(pct(responses, 99), 3),
                       worst_ms=round(max(responses), 3) if responses else 0.0),
        interrupts=dict(count=len(irq_ts), rate_per_s=round(len(irq_ts) / dur_s, 1) if dur_s else 0,
                        note="class-2 events used as interrupt/IPI-timed proxy (ipi_cpu_*/clock_cpu_* threads present); no separate handler-entry records in this trace"),
        ext_event_latency_ms=dict(n=len(send_to_run_ms), avg_ms=round(avg(send_to_run_ms), 4),
                                  p99_ms=round(pct(sorted(send_to_run_ms), 99), 4) if send_to_run_ms else 0.0,
                                  note="SEND(MsgSend IPC incl. brake client)->next RUNNING of same thread; proxy for CAN/UART event-to-task-start (no CAN ids in this trace)"),
        brake_pids=sorted(brake_pids), brake_keys=[list(k) for k in sorted(brake_keys)],
        methods="hdr=(u32) cpu=(hdr>>30)&3 cls=(hdr>>10)&0x3F ev=hdr&0x3FF; ts=u32 cycles@54MHz; RUN=cls4/ev1 READY=cls4/ev2; jobs clustered by >50ms gap; util=non-idle RUNNING slices/span",
    )

    # ---- outputs ----
    brake_json = [{k: j[k] for k in ("sequence", "decision", "response_time_ms", "release_ns", "received_ns", "completed_ns")} for j in job_rows[:10]]
    while len(brake_json) < 1:
        brake_json = [dict(sequence=1, decision=0, response_time_ms=0.0, release_ns=0, received_ns=0, completed_ns=0)]
        break

    span_ms = to_ms(span_cycles) or 1.0
    lanes = []
    for cpu in range(ncpu):
        ivs = cpu_intervals.get(cpu, [])
        run_ivs = [v for v in ivs if v[1] > 0.002]
        step = max(1, len(run_ivs) // MAX_BARS_PER_CPU)
        bars = []
        for (s_ms, d_ms, key, idle) in run_ivs[::step]:
            is_brake = key in brake_keys
            if idle:
                l, c, tc = "IDLE", "#e5e7eb", "#6b7280"
            elif is_brake:
                l, c = ("BRAKE", "#22c55e")
                tc = "white"
            else:
                l, c, tc = (f"T{key[0] % 1000}", "#60a5fa", "white")
            bars.append(dict(l=l, c=c, tc=tc, x=round(s_ms / span_ms, 5), w=round(max(d_ms / span_ms, 0.0015), 5)))
        lanes.append(dict(label=f"CPU {cpu}", bars=bars))
    can_events = [round(j["_rel_ms"] / span_ms, 5) for j in job_rows[:20]]
    irq_events = []
    if irq_ts:
        irq_sorted = sorted((t - min_ts) % wrap for t in irq_ts)
        step = max(1, len(irq_sorted) // 24)
        irq_events = [round(to_ms(t) / span_ms, 5) for t in irq_sorted[::step]]
    timeline = dict(cpu_lanes=lanes, can_events=can_events, irq_events=irq_events)

    for d in {BUILD_DIR, DOCS_DIR, SRC_DATA_DIR, Path(KEV_PATH).parent}:
        try:
            d.mkdir(parents=True, exist_ok=True)
            (d / "brake_data.json").write_text(json.dumps(brake_json, indent=1))
            (d / "timeline_data.json").write_text(json.dumps(timeline))
            (d / "kev_summary.json").write_text(json.dumps(summary, indent=1))
            print("wrote", d)
        except Exception as e:
            print("skip", d, e)
    print("jobs:", len(job_rows), "misses:", misses, "util:", summary["cpu_util"], "ctx/s:", summary["ctx_per_sec"])
    print("DONE - rebuild/run the app.")

if __name__ == "__main__":
    sys.exit(main())
