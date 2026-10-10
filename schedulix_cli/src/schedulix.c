/*
 * schedulix.c - minimal 5-command CLI, one file, end-to-end:
 *   run -> capture -> analyze -> gantt -> report (+ compare)
 *
 * Design rule: jobs.csv is the source of truth. The .kev file is
 * only stat'ed (size/exists) - never binary-parsed. All metrics
 * come from events we actually have: MsgSend release/completion
 * timestamps in jobs.csv. No claimed metric without evidence.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>

#define MAXJ 4096

typedef struct {
    uint32_t sequence;
    uint32_t brake_request;
    uint64_t release_ns;
} brake_request_t;

typedef struct {
    uint32_t sequence;
    uint32_t decision;
    uint64_t received_ns;
    uint64_t completed_ns;
} brake_response_t;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static const char *opt_val(int argc, char **argv, const char *key,
                           const char *dflt)
{
    for (int i = 0; i + 1 < argc; i++) {
        if (strcmp(argv[i], key) == 0) {
            return argv[i + 1];
        }
    }
    return dflt;
}

static void usage(void)
{
    printf("schedulix - minimal RTOS sched analysis CLI\n");
    printf("  schedulix run     --scenario NAME --duration SEC --jobs FILE\n");
    printf("  schedulix capture --duration SEC --output FILE.kev\n");
    printf("  schedulix analyze --trace FILE.kev --jobs FILE.csv [--deadline MS] [--out analysis.json]\n");
    printf("  schedulix gantt   --input analysis.json|jobs.csv --output gantt.html\n");
    printf("  schedulix report  --input analysis.json [--out report.csv]\n");
    printf("  schedulix compare BASE.json STRESS.json\n");
    fflush(stdout);
}

/* ---------------- run ---------------- */
static int cmd_run(int argc, char **argv)
{
    const char *scenario = opt_val(argc, argv, "--scenario", "baseline");
    int duration = atoi(opt_val(argc, argv, "--duration", "10"));
    const char *jobs = opt_val(argc, argv, "--jobs", "/tmp/jobs.csv");
    if (duration <= 0) {
        duration = 10;
    }
    if (duration > MAXJ) {
        duration = MAXJ;
    }

    int coid = name_open("brake", 0);
    if (coid == -1) {
        fprintf(stderr, "run: name_open failed: %s\n", strerror(errno));
        fprintf(stderr, "run: start server first: /tmp/break_workload &\n");
        return 1;
    }

    FILE *f = fopen(jobs, "w");
    if (!f) {
        perror("run: fopen jobs");
        name_close(coid);
        return 1;
    }
    fprintf(f, "seq,release_ns,completed_ns,response_us,decision\n");

    printf("run: scenario=%s jobs=%d -> %s\n", scenario, duration, jobs);
    fflush(stdout);
    for (int i = 1; i <= duration; i++) {
        brake_request_t req;
        brake_response_t resp;
        memset(&resp, 0, sizeof(resp));
        req.sequence = (uint32_t)i;
        req.brake_request = 1;
        req.release_ns = now_ns();

        if (MsgSend(coid, &req, sizeof(req), &resp, sizeof(resp)) == -1) {
            fprintf(stderr, "run: MsgSend %d failed: %s\n", i, strerror(errno));
            fclose(f);
            name_close(coid);
            return 1;
        }
        unsigned long long rus =
            (resp.completed_ns - req.release_ns) / 1000ULL;
        fprintf(f, "%u,%llu,%llu,%llu,%u\n", resp.sequence,
                (unsigned long long)req.release_ns,
                (unsigned long long)resp.completed_ns, rus, resp.decision);
        fflush(f);
        printf("run: job %u response=%llu us\n", resp.sequence, rus);
        fflush(stdout);
        if (i < duration) {
            struct timespec d = { 1, 0 };
            nanosleep(&d, NULL);
        }
    }
    fclose(f);
    name_close(coid);
    printf("run: done scenario=%s file=%s\n", scenario, jobs);
    fflush(stdout);
    return 0;
}

/* ---------------- capture ---------------- */
static int cmd_capture(int argc, char **argv)
{
    int duration = atoi(opt_val(argc, argv, "--duration", "3"));
    const char *out = opt_val(argc, argv, "--output", "/tmp/run.kev");
    char cmd[1024];
    if (duration <= 0) {
        duration = 3;
    }
    snprintf(cmd, sizeof(cmd), "tracelogger -f %s -s %d -w", out, duration);
    printf("capture: + %s\n", cmd);
    printf("capture: 'not keeping up' warnings = buffer pressure (expected)\n");
    fflush(stdout);
    int rc = system(cmd);
    struct stat st;
    if (stat(out, &st) == 0) {
        printf("capture: file=%s bytes=%lld rc=%d\n",
               out, (long long)st.st_size, rc);
    } else {
        printf("capture: MISSING %s rc=%d (%s)\n", out, rc, strerror(errno));
        return 1;
    }
    fflush(stdout);
    return 0;
}

/* ---------------- analyze ---------------- */
static int cmp_u64(const void *a, const void *b)
{
    unsigned long long x = *(const unsigned long long *)a;
    unsigned long long y = *(const unsigned long long *)b;
    if (x < y) {
        return -1;
    }
    if (x > y) {
        return 1;
    }
    return 0;
}

static int cmd_analyze(int argc, char **argv)
{
    const char *trace = opt_val(argc, argv, "--trace", "/tmp/run.kev");
    const char *jobs = opt_val(argc, argv, "--jobs", "/tmp/jobs.csv");
    double deadline_ms = atof(opt_val(argc, argv, "--deadline", "10"));
    const char *out = opt_val(argc, argv, "--out", "/tmp/analysis.json");

    FILE *f = fopen(jobs, "r");
    if (!f) {
        fprintf(stderr, "analyze: cannot open %s: %s\n", jobs, strerror(errno));
        return 1;
    }
    char line[256];
    unsigned seq[MAXJ];
    unsigned long long rel[MAXJ], comp[MAXJ], resp[MAXJ], dec[MAXJ];
    int n = 0;
    if (!fgets(line, sizeof(line), f)) {
        fprintf(stderr, "analyze: empty %s\n", jobs);
        fclose(f);
        return 1;
    }
    while (n < MAXJ && fgets(line, sizeof(line), f)) {
        unsigned s = 0, d = 0;
        unsigned long long r = 0, c = 0, u = 0;
        if (sscanf(line, "%u,%llu,%llu,%llu,%u", &s, &r, &c, &u, &d) != 5) {
            continue;
        }
        seq[n] = s;
        rel[n] = r;
        comp[n] = c;
        resp[n] = u;
        dec[n] = d;
        n++;
    }
    fclose(f);
    if (n == 0) {
        fprintf(stderr, "analyze: no jobs in %s\n", jobs);
        return 1;
    }

    unsigned long long mn = resp[0], mx = resp[0], sum = 0;
    int misses = 0;
    for (int i = 0; i < n; i++) {
        if (resp[i] < mn) {
            mn = resp[i];
        }
        if (resp[i] > mx) {
            mx = resp[i];
        }
        sum += resp[i];
        if ((double)resp[i] / 1000.0 > deadline_ms) {
            misses++;
        }
    }
    double mean = (double)sum / (double)n;
    unsigned long long srt[MAXJ];
    memcpy(srt, resp, (size_t)n * sizeof(srt[0]));
    qsort(srt, (size_t)n, sizeof(srt[0]), cmp_u64);
    unsigned long long p50 = srt[(int)(0.50 * (n - 1))];
    unsigned long long p95 = srt[(int)(0.95 * (n - 1))];
    unsigned long long p99 = srt[(int)(0.99 * (n - 1))];

    struct stat st;
    long long tbytes = -1;
    if (stat(trace, &st) == 0) {
        tbytes = (long long)st.st_size;
    } else {
        fprintf(stderr, "analyze: warn: trace %s missing (%s)\n",
                trace, strerror(errno));
    }

    FILE *o = fopen(out, "w");
    if (!o) {
        perror("analyze: fopen out");
        return 1;
    }
    fprintf(o, "{\n  \"jobs_csv\": \"%s\",\n  \"trace\": \"%s\",\n",
            jobs, trace);
    fprintf(o, "  \"trace_bytes\": %lld,\n  \"count\": %d,\n", tbytes, n);
    fprintf(o, "  \"deadline_ms\": %.3f,\n", deadline_ms);
    fprintf(o, "  \"mean_us\": %.1f,\n  \"min_us\": %llu,\n  \"max_us\": %llu,\n",
            mean, mn, mx);
    fprintf(o, "  \"jitter_us\": %llu,\n  \"p50_us\": %llu,\n  \"p95_us\": %llu,\n  \"p99_us\": %llu,\n",
            mx - mn, p50, p95, p99);
    fprintf(o, "  \"misses\": %d,\n  \"jobs\": [\n", misses);
    for (int i = 0; i < n; i++) {
        fprintf(o, "    {\"seq\":%u,\"release_ns\":%llu,\"completed_ns\":%llu,\"response_us\":%llu,\"decision\":%u}%s\n",
                seq[i], rel[i], comp[i], resp[i], (unsigned)dec[i],
                (i + 1 < n) ? "," : "");
    }
    fprintf(o, "  ]\n}\n");
    fclose(o);

    printf("analyze: n=%d mean=%.1f us min=%llu max=%llu jitter=%llu p50=%llu p95=%llu p99=%llu misses=%d trace_bytes=%lld\n",
           n, mean, mn, mx, mx - mn, p50, p95, p99, misses, tbytes);
    printf("analyze: wrote %s\n", out);
    fflush(stdout);
    return 0;
}

/* Load (seq, release, complete, response) from .csv or our own .json */
static int load_jobs(const char *path, unsigned *seq, unsigned long long *rel,
                     unsigned long long *comp, unsigned long long *resp, int cap)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    char head[256];
    if (!fgets(head, sizeof(head), f)) {
        fclose(f);
        return -1;
    }
    int n = 0;
    if (head[0] == '{' || strchr(head, '{') != NULL || strstr(path, ".json") != NULL) {
        /* JSON scan for our own writer format. Re-scan whole file. */
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 8 * 1024 * 1024) {
            fclose(f);
            return -1;
        }
        char *buf = malloc((size_t)sz + 1);
        if (!buf) {
            fclose(f);
            return -1;
        }
        size_t rd = fread(buf, 1, (size_t)sz, f);
        buf[rd] = '\0';
        fclose(f);
        const char *p = buf;
        while (n < cap) {
            p = strstr(p, "\"seq\":");
            if (!p) {
                break;
            }
            unsigned s = 0, d = 0;
            unsigned long long r = 0, c = 0, u = 0;
            if (sscanf(p, "\"seq\":%u,\"release_ns\":%llu,\"completed_ns\":%llu,\"response_us\":%llu,\"decision\":%u",
                       &s, &r, &c, &u, &d) == 5) {
                seq[n] = s;
                rel[n] = r;
                comp[n] = c;
                resp[n] = u;
                n++;
            }
            p += 6;
        }
        free(buf);
        return n;
    }
    /* CSV: first line was header */
    char line[256];
    while (n < cap && fgets(line, sizeof(line), f)) {
        unsigned s = 0, d = 0;
        unsigned long long r = 0, c = 0, u = 0;
        if (sscanf(line, "%u,%llu,%llu,%llu,%u", &s, &r, &c, &u, &d) != 5) {
            continue;
        }
        seq[n] = s;
        rel[n] = r;
        comp[n] = c;
        resp[n] = u;
        n++;
    }
    fclose(f);
    return n;
}

/* ---------------- gantt ---------------- */
static int cmd_gantt(int argc, char **argv)
{
    const char *in = opt_val(argc, argv, "--input", "/tmp/analysis.json");
    const char *out = opt_val(argc, argv, "--output", "/tmp/gantt.html");
    static unsigned seq[MAXJ];
    static unsigned long long rel[MAXJ], comp[MAXJ], resp[MAXJ];
    int n = load_jobs(in, seq, rel, comp, resp, MAXJ);
    if (n <= 0) {
        fprintf(stderr, "gantt: no jobs parsed from %s\n", in);
        return 1;
    }
    unsigned long long t0 = rel[0], t1 = comp[0], mx = resp[0];
    for (int i = 0; i < n; i++) {
        if (rel[i] < t0) {
            t0 = rel[i];
        }
        if (comp[i] > t1) {
            t1 = comp[i];
        }
        if (resp[i] > mx) {
            mx = resp[i];
        }
    }
    if (mx == 0) {
        mx = 1;
    }
    unsigned long long span = (t1 > t0) ? (t1 - t0) : 1;

    FILE *o = fopen(out, "w");
    if (!o) {
        perror("gantt: fopen out");
        return 1;
    }
    fprintf(o, "<!doctype html><html><head><meta charset=\"utf-8\">"
               "<title>Schedulix Gantt</title><style>"
               "body{font-family:sans-serif;margin:24px}"
               ".row{margin:3px 0;font-size:12px}"
               ".bar{display:inline-block;height:14px;background:#2563eb;border-radius:3px;vertical-align:middle}"
               ".lbl{display:inline-block;width:90px}"
               "table{border-collapse:collapse;margin-top:16px;font-size:12px}"
               "td,th{border:1px solid #ccc;padding:3px 8px}</style></head><body>\n");
    fprintf(o, "<h2>Schedulix Gantt: %d jobs (span %.3f ms)</h2>\n",
            n, (double)span / 1e6);
    for (int i = 0; i < n; i++) {
        double left = (double)(rel[i] - t0) / (double)span * 90.0;
        double w = (double)(comp[i] - rel[i]) / (double)span * 90.0;
        if (w < 0.6) {
            w = 0.6;
        }
        fprintf(o, "<div class=\"row\"><span class=\"lbl\">job %u</span>"
                   "<span class=\"bar\" style=\"margin-left:%.2f%%;width:%.2f%%\" "
                   "title=\"resp %llu us\"></span> %llu us</div>\n",
                seq[i], left, w, resp[i], resp[i]);
    }
    fprintf(o, "<table><tr><th>seq</th><th>release_ns</th><th>complete_ns</th><th>resp_us</th></tr>\n");
    for (int i = 0; i < n; i++) {
        fprintf(o, "<tr><td>%u</td><td>%llu</td><td>%llu</td><td>%llu</td></tr>\n",
                seq[i], rel[i], comp[i], resp[i]);
    }
    fprintf(o, "</table></body></html>\n");
    fclose(o);
    printf("gantt: %d jobs -> %s\n", n, out);
    fflush(stdout);
    return 0;
}

/* ---------------- report ---------------- */
static double jget_d(const char *buf, const char *key, double dflt)
{
    const char *p = strstr(buf, key);
    double v = dflt;
    if (p) {
        sscanf(p + strlen(key), " %*[: ]%lf", &v);
    }
    return v;
}
static long jget_l(const char *buf, const char *key, long dflt)
{
    const char *p = strstr(buf, key);
    long v = dflt;
    if (p) {
        sscanf(p + strlen(key), " %*[: ]%ld", &v);
    }
    return v;
}

static int cmd_report(int argc, char **argv)
{
    const char *in = opt_val(argc, argv, "--input", "/tmp/analysis.json");
    const char *out = opt_val(argc, argv, "--out", NULL);
    FILE *f = fopen(in, "r");
    if (!f) {
        fprintf(stderr, "report: cannot open %s: %s\n", in, strerror(errno));
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8 * 1024 * 1024) {
        fprintf(stderr, "report: bad size %s\n", in);
        fclose(f);
        return 1;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return 1;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);

    long count = jget_l(buf, "\"count\"", -1);
    long misses = jget_l(buf, "\"misses\"", -1);
    long tbytes = jget_l(buf, "\"trace_bytes\"", -1);
    double mean = jget_d(buf, "\"mean_us\"", -1);
    double p95 = jget_d(buf, "\"p95_us\"", -1);
    double jit = jget_d(buf, "\"jitter_us\"", -1);
    free(buf);
    if (count < 0) {
        fprintf(stderr, "report: cannot parse %s (need schedulix analyze output)\n", in);
        return 1;
    }
    printf("metric,value\ncount,%ld\nmean_us,%.1f\np95_us,%.1f\njitter_us,%.1f\nmisses,%ld\ntrace_bytes,%ld\n",
           count, mean, p95, jit, misses, tbytes);
    fflush(stdout);
    if (out) {
        FILE *o = fopen(out, "w");
        if (!o) {
            perror("report: fopen out");
            return 1;
        }
        fprintf(o, "metric,value\ncount,%ld\nmean_us,%.1f\np95_us,%.1f\njitter_us,%.1f\nmisses,%ld\ntrace_bytes,%ld\n",
                count, mean, p95, jit, misses, tbytes);
        fclose(o);
        printf("report: wrote %s\n", out);
        fflush(stdout);
    }
    return 0;
}

/* ---------------- compare (optional, minimal) ---------------- */
static int cmd_compare(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: schedulix compare BASE.json STRESS.json\n");
        return 1;
    }
    double m[2] = { 0, 0 }, p[2] = { 0, 0 };
    long n[2] = { 0, 0 }, miss[2] = { 0, 0 };
    for (int k = 0; k < 2; k++) {
        FILE *f = fopen(argv[2 + k], "r");
        if (!f) {
            fprintf(stderr, "compare: cannot open %s\n", argv[2 + k]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = malloc((size_t)sz + 1);
        if (!buf) {
            fclose(f);
            return 1;
        }
        size_t rd = fread(buf, 1, (size_t)sz, f);
        buf[rd] = '\0';
        fclose(f);
        n[k] = jget_l(buf, "\"count\"", 0);
        miss[k] = jget_l(buf, "\"misses\"", 0);
        m[k] = jget_d(buf, "\"mean_us\"", 0);
        p[k] = jget_d(buf, "\"p95_us\"", 0);
        free(buf);
    }
    printf("metric,base,stress,delta\n");
    printf("count,%ld,%ld,%ld\n", n[0], n[1], n[1] - n[0]);
    printf("mean_us,%.1f,%.1f,%+.1f\n", m[0], m[1], m[1] - m[0]);
    printf("p95_us,%.1f,%.1f,%+.1f\n", p[0], p[1], p[1] - p[0]);
    printf("misses,%ld,%ld,%+ld\n", miss[0], miss[1], miss[1] - miss[0]);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 2) {
        usage();
        return 1;
    }
    if (strcmp(argv[1], "run") == 0) {
        return cmd_run(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "capture") == 0) {
        return cmd_capture(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "analyze") == 0) {
        return cmd_analyze(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "gantt") == 0) {
        return cmd_gantt(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "report") == 0) {
        return cmd_report(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "compare") == 0) {
        return cmd_compare(argc - 1, argv + 1);
    }
    usage();
    return 1;
}
