#include "rtm.h"

#include <string.h>

static int get_bit(const uint8_t *bytes, size_t i) {
    return (bytes[i >> 3] >> (i & 7)) & 1;
}

static void set_bit(uint8_t *bytes, size_t i, int bit) {
    unsigned mask = 1u << (i & 7);
    if (bit)
        bytes[i >> 3] |= (uint8_t)mask;
    else
        bytes[i >> 3] &= (uint8_t)~mask;
}

int rtm_max_zero_run(const uint8_t *in, size_t nbits) {
    int best = 0;
    int run = 0;
    if (!in)
        return 0;
    for (size_t i = 0; i < nbits; i++) {
        if (get_bit(in, i) == 0) {
            run++;
            if (run > best)
                best = run;
        } else {
            run = 0;
        }
    }
    return best;
}

int rtm_rll_encode(const uint8_t *in, size_t in_bits, uint8_t *out,
                   size_t out_cap_bits, int max_zero, size_t *out_bits) {
    if (!in || !out || !out_bits || max_zero < 1)
        return -1;
    memset(out, 0, (out_cap_bits + 7) / 8);
    size_t o = 0;
    int run = 0;
    for (size_t i = 0; i < in_bits; i++) {
        int bit = get_bit(in, i);
        if (o >= out_cap_bits)
            return -1;
        set_bit(out, o++, bit);
        if (bit == 0) {
            run++;
            if (run == max_zero) {
                if (o >= out_cap_bits)
                    return -1;
                set_bit(out, o++, 1);
                run = 0;
            }
        } else {
            run = 0;
        }
    }
    *out_bits = o;
    return 0;
}

int rtm_rll_decode(const uint8_t *in, size_t in_bits, uint8_t *out,
                   size_t out_cap_bits, int max_zero, size_t *out_bits) {
    if (!in || !out || !out_bits || max_zero < 1)
        return -1;
    memset(out, 0, (out_cap_bits + 7) / 8);
    size_t o = 0;
    int run = 0;
    for (size_t i = 0; i < in_bits; i++) {
        int bit = get_bit(in, i);
        if (run == max_zero) {
            /* This bit is the stuffed 1. Drop it. */
            run = 0;
            continue;
        }
        if (o >= out_cap_bits)
            return -1;
        set_bit(out, o++, bit);
        if (bit == 0)
            run++;
        else
            run = 0;
    }
    *out_bits = o;
    return 0;
}
