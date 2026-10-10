#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>

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

void send_brake_requests(void)
{
    int coid = name_open("brake", 0);
    if (coid == -1) {
        perror("name_open: start brake_server first");
        exit(EXIT_FAILURE);
    }
    printf("name_open succeeded, coid=%d\n", coid);
    fflush(stdout);

    for (uint32_t i = 1; i <= 10; ++i) {
        brake_request_t req = {
            .sequence = i,
            .brake_request = 1,
            .release_ns = now_ns()
        };

        brake_response_t resp = {0};

        int rc = MsgSend(coid, &req, sizeof(req),
                    &resp, sizeof(resp));
        printf("MsgSend %u rc=%d\n", i, rc);
        fflush(stdout);

        if (rc == -1) {
            perror("MsgSend");
            name_close(coid);
            exit(EXIT_FAILURE);
        }

        printf("Job %u: decision=%u response_ns=%llu\n",
               resp.sequence, resp.decision,
               (unsigned long long)(resp.completed_ns - req.release_ns));
        fflush(stdout);

        struct timespec delay = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&delay, NULL);
    }

    name_close(coid);
    printf("All 10 jobs sent successfully\n");
    fflush(stdout);
    return;
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0); /* line-buffered: every \n flushes */
    printf("break_trigger starting...\n");
    send_brake_requests();
    printf("break_trigger done\n");
    return EXIT_SUCCESS;
}
