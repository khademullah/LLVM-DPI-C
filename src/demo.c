#include "rtm.h"

#include <stdio.h>

/* Runtime half of the demo. The LLVM pass and the DPI-C co-sim are separate
 * binaries; `make demo` runs all three and prints them in order.
 */

static uint64_t stream_cost(int stride, int n) {
    rtm_geom_t g;
    rtm_geom_set(&g, 0, 1, 0, 8);
    rtm_op_t ops[64];
    for (int i = 0; i < n; i++) {
        ops[i].domain = i * stride;
        ops[i].is_write = 0;
        ops[i].data = 0;
    }
    return rtm_replay(&g, 0, ops, NULL, n);
}

static void print_sequentiality(void) {
    const int n = 64;
    uint64_t row = stream_cost(1, n);
    uint64_t col = stream_cost(n, n);
    printf("1. Sequentiality (one port, head already on the first element)\n");
    printf("   row  stride  1: %5llu shifts over %d accesses\n",
           (unsigned long long)row, n);
    printf("   col  stride 64: %5llu shifts over %d accesses\n",
           (unsigned long long)col, n);
    printf("   ratio %.0fx. This is the term a racetrack cost model adds\n",
           row ? (double)col / (double)row : 0.0);
    printf("   to every loop. Loop interchange or a matching layout removes it.\n\n");
}

static void print_ports(void) {
    rtm_geom_t one, two;
    rtm_geom_set(&one, 64, 1, 0, 8);
    rtm_geom_set(&two, 64, 2, 32, 8);
    rtm_port_t a, b;
    rtm_port_init(&a, &one, 0);
    rtm_port_init(&b, &two, 0);
    int da = rtm_align(&a, 32);
    int db = rtm_align(&b, 32);
    printf("2. Port geometry is a compiler parameter, not just a device fact\n");
    printf("   seek domain 32 on a 64-domain nanowire\n");
    printf("   1 port at 0:          %2d shifts\n", da);
    printf("   2 ports at 0 and 32:  %2d shifts (the far port is already there)\n\n",
           db);
    rtm_port_free(&a);
    rtm_port_free(&b);
}

static void print_gather(void) {
    rtm_geom_t g;
    rtm_geom_set(&g, 64, 1, 0, 8);
    const int addr[] = {0, 10, 1, 11, 2, 12};
    const int n = 6;
    rtm_op_t ops[6];
    for (int i = 0; i < n; i++) {
        ops[i].domain = addr[i];
        ops[i].is_write = 0;
        ops[i].data = 0xFFFFFFFFu;
    }
    int order[6];
    uint64_t scheduled = rtm_schedule(&g, 0, ops, n, order);
    uint64_t program = rtm_replay(&g, 0, ops, NULL, n);

    printf("3. Indirect gathers (descriptor reorder, same idea as a NIC DMA ring)\n");
    printf("   addresses:");
    for (int i = 0; i < n; i++)
        printf(" %d", addr[i]);
    printf("\n   program order:     %llu shifts\n", (unsigned long long)program);
    printf("   nearest-port order:");
    for (int i = 0; i < n; i++)
        printf(" %d", addr[order[i]]);
    printf("\n   scheduled:         %llu shifts\n", (unsigned long long)scheduled);
    printf("   Reads of distinct domains have no dependence, so the port\n");
    printf("   scheduler may reorder them. A write still pins later readers.\n");

    rtm_port_t traced;
    rtm_port_init(&traced, &g, 0);
    if (rtm_trace_open(&traced, "build/la_trace.csv") == 0) {
        for (int i = 0; i < n; i++)
            rtm_access(&traced, ops[order[i]].domain, 0, ops[order[i]].data);
        rtm_trace_close(&traced);
        printf("   logic-analyzer trace: build/la_trace.csv\n\n");
    } else {
        printf("   (could not open build/la_trace.csv; run via `make demo`)\n\n");
    }
    rtm_port_free(&traced);
}

static void print_reliability(void) {
    const int K = 8;
    const size_t in_bits = 256;
    uint8_t raw[32];
    uint8_t enc[64];
    uint8_t dec[40];
    for (int i = 0; i < 32; i++)
        raw[i] = 0;
    /* Sparse pattern: a 1 every 24 bits, typical of a pruned weight stream. */
    for (size_t b = 0; b < in_bits; b += 24) {
        raw[b >> 3] |= (uint8_t)(1u << (b & 7));
    }
    size_t enc_bits = 0, dec_bits = 0;
    if (rtm_rll_encode(raw, in_bits, enc, sizeof(enc) * 8, K, &enc_bits) != 0 ||
        rtm_rll_decode(enc, enc_bits, dec, sizeof(dec) * 8, K, &dec_bits) != 0 ||
        dec_bits != in_bits) {
        printf("4. Reliability: RLL roundtrip failed\n");
        return;
    }
    int raw_run = rtm_max_zero_run(raw, in_bits);
    int enc_run = rtm_max_zero_run(enc, enc_bits);
    printf("4. Reliability (long zero-run == long gap with no skyrmion)\n");
    printf("   sparse 256-bit payload: max zero-run %d\n", raw_run);
    printf("   after bit-stuff (K=%d):  max zero-run %d, %zu bits (%.1f%% overhead)\n",
           K, enc_run, enc_bits,
           100.0 * ((double)enc_bits - (double)in_bits) / (double)in_bits);

    rtm_geom_t g;
    rtm_geom_set(&g, 2, 1, 0, K);
    rtm_port_t p;
    rtm_port_init(&p, &g, 1);
    rtm_access(&p, 0, 1, 0x00000000u);
    uint32_t packed = 0;
    int n = enc_bits < 32 ? (int)enc_bits : 32;
    for (int i = 0; i < n; i++) {
        if ((enc[i >> 3] >> (i & 7)) & 1)
            packed |= 1u << i;
    }
    rtm_access(&p, 1, 1, packed);
    printf("   controller faults a raw 0x00000000 write: %s\n",
           p.zero_run_faults >= 1 ? "yes" : "no");
    printf("   first encoded word 0x%08x faults: %s\n\n", packed,
           rtm_zero_run_word(packed, 32) > K ? "yes" : "no");
    rtm_port_free(&p);
}

int main(void) {
    printf("portwalk: compiler-scheduled access ports for racetrack memory\n\n");
    print_sequentiality();
    print_ports();
    print_gather();
    print_reliability();
    return 0;
}
