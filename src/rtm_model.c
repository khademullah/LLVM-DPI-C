#include "rtm.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

int rtm_geom_set(rtm_geom_t *g, int domains, int nports, int spacing,
                 int max_zero_run) {
    if (!g || nports < 1 || nports > RTM_MAX_PORTS || max_zero_run < 1)
        return -1;
    memset(g, 0, sizeof(*g));
    g->domains = domains;
    g->nports = nports;
    g->word_bits = 32;
    g->max_zero_run = max_zero_run;
    for (int i = 0; i < nports; i++)
        g->port_off[i] = i * spacing;
    return 0;
}

int rtm_port_init(rtm_port_t *p, const rtm_geom_t *g, int with_mem) {
    if (!p || !g)
        return -1;
    memset(p, 0, sizeof(*p));
    p->geom = *g;
    if (with_mem) {
        if (g->domains < 1)
            return -1;
        p->mem = (uint32_t *)calloc((size_t)g->domains, sizeof(uint32_t));
        if (!p->mem)
            return -1;
    }
    return 0;
}

void rtm_port_free(rtm_port_t *p) {
    if (!p)
        return;
    free(p->mem);
    p->mem = NULL;
}

int rtm_trace_open(rtm_port_t *p, const char *path) {
    if (!p || !path)
        return -1;
    p->trace = fopen(path, "w");
    if (!p->trace)
        return -1;
    fprintf(p->trace, "cycle,event,head,domain,port,data\n");
    return 0;
}

void rtm_trace_close(rtm_port_t *p) {
    if (p && p->trace) {
        fclose(p->trace);
        p->trace = NULL;
    }
}

int rtm_zero_run_word(uint32_t data, int bits) {
    if (bits < 1)
        return 0;
    if (bits > 32)
        bits = 32;
    int best = 0;
    int run = 0;
    for (int i = 0; i < bits; i++) {
        if (((data >> i) & 1u) == 0) {
            run++;
            if (run > best)
                best = run;
        } else {
            run = 0;
        }
    }
    return best;
}

int rtm_align_distance(const rtm_port_t *p, int domain, int *port_out,
                       int *new_head) {
    long long best = LLONG_MAX;
    int best_port = 0;
    int best_head = p->head;
    for (int i = 0; i < p->geom.nports; i++) {
        int head2 = domain - p->geom.port_off[i];
        long long dist = (long long)head2 - (long long)p->head;
        if (dist < 0)
            dist = -dist;
        if (dist < best || (dist == best && i < best_port)) {
            best = dist;
            best_port = i;
            best_head = head2;
        }
    }
    if (port_out)
        *port_out = best_port;
    if (new_head)
        *new_head = best_head;
    if (best > INT_MAX)
        return INT_MAX;
    return (int)best;
}

int rtm_align(rtm_port_t *p, int domain) {
    int which = 0;
    int new_head = p->head;
    int dist = rtm_align_distance(p, domain, &which, &new_head);
    if (dist > 0) {
        p->shift_ops++;
        p->shifts += (uint64_t)dist;
        if (p->trace) {
            int step = (new_head >= p->head) ? 1 : -1;
            int h = p->head;
            for (int s = 0; s < dist; s++) {
                h += step;
                p->cycle++;
                fprintf(p->trace, "%llu,shift,%d,%d,%d,0\n",
                        (unsigned long long)p->cycle, h, domain, which);
            }
        } else {
            p->cycle += (uint64_t)dist;
        }
    }
    p->head = new_head;
    p->last_port = which;
    return dist;
}

int rtm_access(rtm_port_t *p, int domain, int is_write, uint32_t data) {
    if (p->mem && (domain < 0 || domain >= p->geom.domains)) {
        p->range_faults++;
        p->cycle++;
        return -1;
    }
    int dist = rtm_align(p, domain);
    p->accesses++;
    p->cycle++;
    if (is_write) {
        p->writes++;
        if (rtm_zero_run_word(data, p->geom.word_bits) > p->geom.max_zero_run)
            p->zero_run_faults++;
        if (p->mem)
            p->mem[domain] = data;
    } else {
        p->reads++;
        if (p->mem)
            data = p->mem[domain];
    }
    if (p->trace) {
        fprintf(p->trace, "%llu,%s,%d,%d,%d,0x%08x\n",
                (unsigned long long)p->cycle, is_write ? "write" : "read",
                p->head, domain, p->last_port, data);
    }
    return dist;
}
