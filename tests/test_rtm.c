#include "rtm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        fails++;
    }
}

static int replay_identity(const rtm_geom_t *g, const rtm_op_t *ops, int n) {
    return (int)rtm_replay(g, 0, ops, NULL, n);
}

static void test_streams(void) {
    rtm_geom_t g;
    check(rtm_geom_set(&g, 0, 1, 0, 8) == 0, "geom");
    enum { N = 64 };
    rtm_op_t row[N];
    rtm_op_t col[N];
    for (int i = 0; i < N; i++) {
        row[i].domain = i;
        row[i].is_write = 0;
        row[i].data = 0;
        col[i].domain = i * N;
        col[i].is_write = 0;
        col[i].data = 0;
    }
    check(replay_identity(&g, row, N) == 63, "row shifts == 63");
    check(replay_identity(&g, col, N) == 63 * 64, "col shifts == 4032");
}

static void test_ports(void) {
    rtm_geom_t one, two;
    rtm_geom_set(&one, 64, 1, 0, 8);
    rtm_geom_set(&two, 64, 2, 32, 8);
    rtm_port_t a, b;
    rtm_port_init(&a, &one, 0);
    rtm_port_init(&b, &two, 0);
    int da = rtm_align(&a, 32);
    int db = rtm_align(&b, 32);
    check(da == 32, "single port seek 32");
    check(db == 0, "second port already covers domain 32");
    check(b.head == 0, "head stays put when the far port is used");
    rtm_port_free(&a);
    rtm_port_free(&b);
}

static void test_schedule(void) {
    rtm_geom_t g;
    rtm_geom_set(&g, 64, 1, 0, 8);
    const int addr[] = {0, 10, 1, 11, 2, 12};
    enum { N = 6 };
    rtm_op_t ops[N];
    for (int i = 0; i < N; i++) {
        ops[i].domain = addr[i];
        ops[i].is_write = 0;
        ops[i].data = 0;
    }
    int order[N];
    uint64_t scheduled = rtm_schedule(&g, 0, ops, N, order);
    uint64_t program = rtm_replay(&g, 0, ops, NULL, N);
    check(program == 48, "gather program order is 48");
    check(scheduled == 12, "gather scheduled order is 12");
    check(scheduled == rtm_replay(&g, 0, ops, order, N), "schedule matches replay");

    int seen[N] = {0};
    for (int i = 0; i < N; i++) {
        check(order[i] >= 0 && order[i] < N, "order in range");
        seen[order[i]]++;
    }
    for (int i = 0; i < N; i++)
        check(seen[i] == 1, "order is a permutation");
}

static void test_deps(void) {
    rtm_geom_t g;
    rtm_geom_set(&g, 64, 1, 0, 8);
    rtm_op_t ops[4] = {
        {5, 1, 1},
        {1, 0, 0},
        {5, 0, 0},
        {2, 0, 0},
    };
    int order[4];
    uint64_t scheduled = rtm_schedule(&g, 0, ops, 4, order);
    check(scheduled == 5, "dependent schedule costs 5");
    int pos_w = -1, pos_r = -1;
    for (int i = 0; i < 4; i++) {
        if (order[i] == 0)
            pos_w = i;
        if (order[i] == 2)
            pos_r = i;
    }
    check(pos_w >= 0 && pos_r > pos_w, "write of domain 5 stays before its read");
}

static int bits_equal(const uint8_t *a, const uint8_t *b, size_t nbits) {
    for (size_t i = 0; i < nbits; i++) {
        int ba = (a[i >> 3] >> (i & 7)) & 1;
        int bb = (b[i >> 3] >> (i & 7)) & 1;
        if (ba != bb)
            return 0;
    }
    return 1;
}

static void test_rll(void) {
    enum { K = 8, NBITS = 256 };
    uint8_t in[NBITS / 8];
    uint8_t enc[NBITS];
    uint8_t dec[NBITS / 8 + 8];
    uint32_t rng = 0xC0FFEEu;
    for (int i = 0; i < NBITS / 8; i++) {
        rng = rng * 1664525u + 1013904223u;
        in[i] = (uint8_t)(rng >> 24);
    }
    /* Force a long zero run so the encoder has work to do. */
    in[3] = 0;
    in[4] = 0;
    in[5] = 0;

    size_t enc_bits = 0, dec_bits = 0;
    check(rtm_rll_encode(in, NBITS, enc, sizeof(enc) * 8, K, &enc_bits) == 0,
          "encode");
    check(rtm_max_zero_run(enc, enc_bits) <= K, "encoded run limited");
    check(rtm_rll_decode(enc, enc_bits, dec, sizeof(dec) * 8, K, &dec_bits) == 0,
          "decode");
    check(dec_bits == NBITS, "decoded width");
    check(bits_equal(in, dec, NBITS), "roundtrip");

    uint8_t zeros[16] = {0};
    size_t zbits = 0;
    check(rtm_rll_encode(zeros, 64, enc, sizeof(enc) * 8, K, &zbits) == 0,
          "encode zeros");
    check(zbits == 64 + 64 / K, "one stuff bit per K zeros");
    check(rtm_max_zero_run(enc, zbits) == K, "zero input hits the cap exactly");
}

static void test_write_fault(void) {
    rtm_geom_t g;
    rtm_geom_set(&g, 4, 1, 0, 8);
    rtm_port_t p;
    check(rtm_port_init(&p, &g, 1) == 0, "mem port");
    rtm_access(&p, 0, 1, 0x00000000u);
    check(p.zero_run_faults == 1, "raw zero word faults");

    uint8_t zeros[8] = {0};
    uint8_t enc[16] = {0};
    size_t enc_bits = 0;
    check(rtm_rll_encode(zeros, 32, enc, sizeof(enc) * 8, 8, &enc_bits) == 0,
          "encode a word of zeros");
    uint32_t packed = 0;
    int n = enc_bits < 32 ? (int)enc_bits : 32;
    for (int i = 0; i < n; i++) {
        if ((enc[i >> 3] >> (i & 7)) & 1)
            packed |= 1u << i;
    }
    check(rtm_zero_run_word(packed, 32) <= 8, "packed word is legal");
    rtm_access(&p, 1, 1, packed);
    check(p.zero_run_faults == 1, "encoded word does not add a fault");
    rtm_port_free(&p);
}

int main(void) {
    test_streams();
    test_ports();
    test_schedule();
    test_deps();
    test_rll();
    test_write_fault();
    if (fails) {
        fprintf(stderr, "%d check(s) failed\n", fails);
        return 1;
    }
    printf("portwalk tests: PASS\n");
    return 0;
}
