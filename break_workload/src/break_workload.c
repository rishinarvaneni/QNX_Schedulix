#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include <sys/syspage.h>
#include <unistd.h>


#define BRAKE_PRIORITY 30
#define LEVEL1_PRIORITY 30
#define LEVEL2_PRIORITY 20
#define LEVEL3_PRIORITY 10
#define CPU_LOAD_THRESHOLD 90

#define NUM_CORES 4

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

typedef enum { LEVEL_1, LEVEL_2, LEVEL_3 } task_level_t;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL
         + (uint64_t)ts.tv_nsec;
}

static int get_core_load_percent(int core_id)
{
    /* On QNX, we could read from /dev/cpuinfo or use traceparser.
       For simulation, return a value that demonstrates the threshold logic. */
    /* In a real QNX implementation, use:
       - per-core counter from syspage
       - traceparser for THREAD events
       - ClockCycles profiling
       */
    return (core_id * 25) % 100;
}

typedef struct {
    task_level_t level;
    int core_id;
    uint32_t activations;
    uint32_t migrations;
} task_info_t;

static task_info_t task_info[3];

void init_task_info(void)
{
    for (int i = 0; i < 3; i++) {
        task_info[i].level = (task_level_t)i;
        task_info[i].core_id = i;
        task_info[i].activations = 0;
        task_info[i].migrations = 0;
    }
}

uint64_t compute_response_ns(brake_request_t *req, brake_response_t *resp)
{
    return resp->completed_ns - req->release_ns;
}

void process_brake_request(brake_request_t *req, brake_response_t *resp,
                           task_level_t level)
{
    resp->sequence = req->sequence;
    resp->decision = req->brake_request ? 1U : 0U;
    resp->received_ns = now_ns();
    resp->completed_ns = now_ns();

    uint64_t resp_time = compute_response_ns(req, resp);
    printf("L%u job=%u decision=%u response_ns=%llu\n",
           (unsigned)(level + 1), resp->sequence, resp->decision,
           (unsigned long long)resp_time);

    task_info[level].activations++;
}

void *level1_task(void *arg)
{
    (void)arg;
    /* Set core affinity for Level 1 (critical - brake)
       On QNX, use: pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &mask); */
    /* Simulated: Level 1 runs on core 0, migrates to core 1 if load > 90% */

    for (;;) {
        int load = get_core_load_percent(task_info[LEVEL_1].core_id);
        if (load > CPU_LOAD_THRESHOLD) {
            task_info[LEVEL_1].migrations++;
            /* Migration logic - in real QNX:
               cpu_set_t mask;
               CPU_ZERO(&mask);
               CPU_SET(1, &mask);
               pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);
               */
            task_info[LEVEL_1].core_id = (task_info[LEVEL_1].core_id + 1) % NUM_CORES;
            printf("L1: Migration triggered, core load %d%% - moving to core %d\n",
                   load, task_info[LEVEL_1].core_id);
        }

        /* Simulate critical brake processing */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

void *level2_task(void *arg)
{
    (void)arg;
    /* Set core affinity for Level 2 */
    /* On QNX, use: pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &mask); */
    /* Simulated: Level 2 runs on core 1, migrates to core 2 if load > 90% */

    for (;;) {
        int load = get_core_load_percent(task_info[LEVEL_2].core_id);
        if (load > CPU_LOAD_THRESHOLD) {
            task_info[LEVEL_2].migrations++;
            /* Migration logic - in real QNX:
               cpu_set_t mask;
               CPU_ZERO(&mask);
               CPU_SET(2, &mask);
               pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);
               */
            task_info[LEVEL_2].core_id = (task_info[LEVEL_2].core_id + 1) % NUM_CORES;
            printf("L2: Migration triggered, core load %d%% - moving to core %d\n",
                   load, task_info[LEVEL_2].core_id);
        }

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

void *level3_task(void *arg)
{
    (void)arg;
    /* Set core affinity for Level 3 (audio) */
    /* On QNX, use: pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &mask); */
    /* Simulated: Level 3 runs on core 2, yields if load > 90% */

    for (;;) {
        int load = get_core_load_percent(task_info[LEVEL_3].core_id);
        if (load > CPU_LOAD_THRESHOLD) {
            task_info[LEVEL_3].migrations++;
            /* Level 3 can share core, just yield */
            printf("L3: High load %d%% on core %d - yielding\n",
                   load, task_info[LEVEL_3].core_id);
        }

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 200000 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

name_attach_t *attach;

void init_server(void)
{
    init_task_info();
    attach = name_attach(NULL, "brake", 0);
    if (attach == NULL) {
        perror("name_attach");
        return;
    }

    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = LEVEL1_PRIORITY;

    int rc = pthread_setschedparam(
        pthread_self(), SCHED_FIFO, &param);

    if (rc != EOK) {
        fprintf(stderr, "Scheduling configuration failed: %s\n",
                strerror(rc));
        name_detach(attach, 0);
        return;
    }

    printf("BRAKE server ready: SCHED_FIFO, priority %d\n",
           LEVEL1_PRIORITY);
    fflush(stdout);

    pthread_t t1, t2, t3;

    rc = pthread_create(&t1, NULL, level1_task, NULL);
    if (rc != EOK) {
        fprintf(stderr, "Level 1 task creation failed: %s\n", strerror(rc));
    }

    rc = pthread_create(&t2, NULL, level2_task, NULL);
    if (rc != EOK) {
        fprintf(stderr, "Level 2 task creation failed: %s\n", strerror(rc));
    }

    rc = pthread_create(&t3, NULL, level3_task, NULL);
    if (rc != EOK) {
        fprintf(stderr, "Level 3 task creation failed: %s\n", strerror(rc));
    }

    (void)t1; (void)t2; (void)t3;
    fflush(stdout);
}

/* Main thread = Level-1 BRAKE server: MUST receive, or clients block forever. */
void server_loop(void)
{
    for (;;) {
        brake_request_t req;
        struct _msg_info info;

        int rcvid = MsgReceive(attach->chid, &req, sizeof(req), &info);
        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }
        if (rcvid == 0) {
            continue; /* pulse */
        }

        brake_response_t resp = {0};
        process_brake_request(&req, &resp, LEVEL_1);
        fflush(stdout);

        if (MsgReply(rcvid, EOK, &resp, sizeof(resp)) == -1) {
            perror("MsgReply");
        }
    }
}


int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    init_server();
    if (attach == NULL) {
        return EXIT_FAILURE;
    }
    server_loop();
    return EXIT_SUCCESS;
}