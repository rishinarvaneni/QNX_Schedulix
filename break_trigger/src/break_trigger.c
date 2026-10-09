/*
 * break_trigger.c
 *
 *  Created on: 09-Oct-2026
 *      Author: User
 */

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
        return;
    }

    for (uint32_t i = 1; i <= 10; ++i) {
        brake_request_t req = {
            .sequence = i,
            .brake_request = 1,
            .release_ns = now_ns()
        };

        brake_response_t resp = {0};

        if (MsgSend(coid, &req, sizeof(req),
                    &resp, sizeof(resp)) == -1) {
            perror("MsgSend");
            name_close(coid);
            return;
        }

        printf("Job %u: response time = %.3f ms\n",
               resp.sequence,
               (resp.completed_ns - req.release_ns) / 1e6);

        struct timespec delay = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&delay, NULL);
    }

    name_close(coid);
}

int main(void)
{
    send_brake_requests();
    return EXIT_SUCCESS;
}
