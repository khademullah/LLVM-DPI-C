#include "rtm.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

static rtm_port_t Golden;
static int ExpectHead;
static uint32_t ExpectData;
static int ExpectWrite;
static int ExpectDomain;
static int Failures;
static FILE *Trace;
static unsigned Seq;

void dpi_reset(int domains) {
    rtm_port_free(&Golden);
    rtm_geom_t g;
    rtm_geom_set(&g, domains, 1, 0, 8);
    rtm_port_init(&Golden, &g, 1);
    Failures = 0;
    Seq = 0;
    if (Trace)
        fclose(Trace);
    Trace = fopen("build/cosim_trace.csv", "w");
    if (Trace)
        fprintf(Trace, "seq,event,head,domain,data,write\n");
}

void dpi_begin(int domain, int is_write, int data) {
    rtm_access(&Golden, domain, is_write, (uint32_t)data);
    ExpectHead = Golden.head;
    ExpectWrite = is_write;
    ExpectDomain = domain;
    ExpectData = Golden.mem ? Golden.mem[domain] : (uint32_t)data;
}

void dpi_check(int head, int data, int is_write, int domain) {
    Seq++;
    if (head != ExpectHead || domain != ExpectDomain || is_write != ExpectWrite) {
        fprintf(stderr,
                "scoreboard seq %u: rtl head=%d domain=%d write=%d, "
                "model head=%d domain=%d write=%d\n",
                Seq, head, domain, is_write, ExpectHead, ExpectDomain,
                ExpectWrite);
        Failures++;
    } else if (!is_write && (uint32_t)data != ExpectData) {
        fprintf(stderr, "scoreboard seq %u: rtl data=0x%08x model data=0x%08x\n",
                Seq, (unsigned)data, ExpectData);
        Failures++;
    }
    if (Trace) {
        fprintf(Trace, "%u,%s,%d,%d,0x%08x,%d\n", Seq,
                is_write ? "write" : "read", head, domain,
                is_write ? (unsigned)ExpectData : (unsigned)data, is_write);
    }
}

int dpi_failures(void) {
    if (Trace) {
        fclose(Trace);
        Trace = NULL;
    }
    return Failures;
}

#ifdef __cplusplus
}
#endif
