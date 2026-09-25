#include "rtm.h"

#include <limits.h>
#include <stdlib.h>

enum { RTM_SCHED_MAX = 4096 };

uint64_t rtm_replay(const rtm_geom_t *geom, int head, const rtm_op_t *ops,
                    const int *order, int n) {
    if (!geom || !ops || n < 0)
        return UINT64_MAX;
    rtm_port_t port;
    if (rtm_port_init(&port, geom, 0) != 0)
        return UINT64_MAX;
    port.head = head;
    for (int step = 0; step < n; step++) {
        int idx = order ? order[step] : step;
        if (idx < 0 || idx >= n) {
            rtm_port_free(&port);
            return UINT64_MAX;
        }
        rtm_access(&port, ops[idx].domain, ops[idx].is_write, ops[idx].data);
    }
    uint64_t shifts = port.shifts;
    rtm_port_free(&port);
    return shifts;
}

uint64_t rtm_schedule(const rtm_geom_t *geom, int head, const rtm_op_t *ops,
                      int n, int *order) {
    if (!geom || !ops || !order || n < 0 || n > RTM_SCHED_MAX)
        return UINT64_MAX;
    if (n == 0)
        return 0;

    int *pred = malloc((size_t)n * sizeof(int));
    unsigned char *done = calloc((size_t)n, 1);
    if (!pred || !done) {
        free(pred);
        free(done);
        return UINT64_MAX;
    }

    for (int j = 0; j < n; j++) {
        pred[j] = -1;
        for (int i = j - 1; i >= 0; i--) {
            if (ops[i].domain == ops[j].domain &&
                (ops[i].is_write || ops[j].is_write)) {
                pred[j] = i;
                break;
            }
        }
    }

    rtm_port_t port;
    if (rtm_port_init(&port, geom, 0) != 0) {
        free(pred);
        free(done);
        return UINT64_MAX;
    }
    port.head = head;

    for (int step = 0; step < n; step++) {
        int best = -1;
        int best_dist = INT_MAX;
        for (int i = 0; i < n; i++) {
            if (done[i])
                continue;
            if (pred[i] >= 0 && !done[pred[i]])
                continue;
            int dist = rtm_align_distance(&port, ops[i].domain, NULL, NULL);
            if (best < 0 || dist < best_dist ||
                (dist == best_dist && i < best)) {
                best = i;
                best_dist = dist;
            }
        }
        if (best < 0) {
            rtm_port_free(&port);
            free(pred);
            free(done);
            return UINT64_MAX;
        }
        rtm_access(&port, ops[best].domain, ops[best].is_write, ops[best].data);
        done[best] = 1;
        order[step] = best;
    }

    uint64_t shifts = port.shifts;
    rtm_port_free(&port);
    free(pred);
    free(done);
    return shifts;
}
