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


#define BRAKE_PRIORITY 30

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
    return (uint64_t)ts.tv_sec * 1000000000ULL
         + (uint64_t)ts.tv_nsec;
}

name_attach_t *attach;

void init_server(void)
{
    attach = name_attach(NULL, "brake", 0);
    if (attach == NULL) {
        perror("name_attach");
        return;
    }

    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = BRAKE_PRIORITY;

    int rc = pthread_setschedparam(
        pthread_self(), SCHED_FIFO, &param);

    if (rc != EOK) {
        fprintf(stderr, "Scheduling configuration failed: %s\n",
                strerror(rc));
        name_detach(attach, 0);
        return;
    }

    printf("BRAKE server ready: SCHED_FIFO, priority %d\n",
           BRAKE_PRIORITY);

    for (;;) {
        brake_request_t req;
        struct _msg_info info;

        int rcvid = MsgReceive(
            attach->chid, &req, sizeof(req), &info);

        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) {
            /* Pulse: not a regular request message. */
            continue;
        }

        brake_response_t resp = {0};
        resp.sequence = req.sequence;
        resp.received_ns = now_ns();

        /* Simplified simulated braking decision. */
        resp.decision = req.brake_request ? 1U : 0U;

        resp.completed_ns = now_ns();

        printf("BRAKE job=%u decision=%u "
               "release_ns=%llu receive_ns=%llu "
               "complete_ns=%llu\n",
               resp.sequence, resp.decision,
               (unsigned long long)req.release_ns,
               (unsigned long long)resp.received_ns,
               (unsigned long long)resp.completed_ns);

        if (MsgReply(rcvid, EOK, &resp, sizeof(resp)) == -1) {
            perror("MsgReply");
        }
    }
}



int main(void)
{
    init_server();
    return EXIT_SUCCESS;
}
