#ifndef PORTWALK_RTM_H
#define PORTWALK_RTM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Single-nanowire skyrmion racetrack model.
 *
 * Domains sit on a line. Access ports are fixed in the lab frame and spaced
 * by port_off[] (port 0 is the origin). `head` is the domain currently under
 * port 0, so port i is aligned with domain head + port_off[i].
 *
 * Aligning a domain chooses the port that minimizes travel and then walks
 * `head` there. One domain of travel costs one cycle. This is the sequentiality
 * constraint a compiler has to schedule for. It is not a device-physics model:
 * a domain here is one machine word, not one skyrmion.
 *
 * A long run of zero bits stands in for the published SK-RTM failure mode
 * (a long stretch with no skyrmion). max_zero_run is the longest run a write
 * may contain before the controller counts a reliability fault.
 */

#define RTM_MAX_PORTS 4

typedef struct rtm_geom {
    int domains; /* addressable words; 0 means the tape length is not checked */
    int nports;
    int port_off[RTM_MAX_PORTS];
    int word_bits;
    int max_zero_run;
} rtm_geom_t;

typedef struct rtm_port {
    rtm_geom_t geom;
    int head;
    int last_port;
    uint32_t *mem; /* NULL if this port is only a cost model */
    uint64_t shifts;     /* domains of travel */
    uint64_t shift_ops;  /* align calls that actually moved */
    uint64_t accesses;
    uint64_t reads;
    uint64_t writes;
    uint64_t range_faults;
    uint64_t zero_run_faults;
    uint64_t cycle;
    FILE *trace;
} rtm_port_t;

typedef struct rtm_op {
    int domain;
    int is_write;
    uint32_t data;
} rtm_op_t;

/* Returns 0, or -1 if nports is out of range. port_off[i] = i * spacing. */
int rtm_geom_set(rtm_geom_t *g, int domains, int nports, int spacing,
                 int max_zero_run);

/* with_mem allocates a zeroed word array of length geom.domains. */
int rtm_port_init(rtm_port_t *p, const rtm_geom_t *g, int with_mem);
void rtm_port_free(rtm_port_t *p);

int rtm_trace_open(rtm_port_t *p, const char *path);
void rtm_trace_close(rtm_port_t *p);

/* Distance the head must travel to put `domain` under some port.
 * *port_out and *new_head are optional. */
int rtm_align_distance(const rtm_port_t *p, int domain, int *port_out,
                       int *new_head);

/* Move the head. Returns the distance travelled. */
int rtm_align(rtm_port_t *p, int domain);

/* Align, then read or write. Returns the shift distance, or -1 on a range fault. */
int rtm_access(rtm_port_t *p, int domain, int is_write, uint32_t data);

int rtm_zero_run_word(uint32_t data, int bits);

/* Bit-stuff a 1 after every run of `max_zero` zeros (USB/HDLC style).
 * Bit 0 of byte 0 is the first bit on the wire, matching rtm_zero_run_word.
 * Returns 0, or -1 if the output buffer is too small or max_zero < 1.
 */
int rtm_rll_encode(const uint8_t *in, size_t in_bits, uint8_t *out,
                   size_t out_cap_bits, int max_zero, size_t *out_bits);
int rtm_rll_decode(const uint8_t *in, size_t in_bits, uint8_t *out,
                   size_t out_cap_bits, int max_zero, size_t *out_bits);
int rtm_max_zero_run(const uint8_t *in, size_t nbits);

/* Replay `order[0..n)` (or program order when order is NULL). */
uint64_t rtm_replay(const rtm_geom_t *geom, int head, const rtm_op_t *ops,
                    const int *order, int n);

/* Dependence-aware nearest-port scheduling.
 * A later op waits for the most recent earlier op on the same domain when
 * either side is a write. Independent ops (the gather case) are free to
 * reorder. Fills order[0..n) and returns the shift count.
 * Returns UINT64_MAX if n is outside 0..4096 or a pointer is NULL.
 */
uint64_t rtm_schedule(const rtm_geom_t *geom, int head, const rtm_op_t *ops,
                      int n, int *order);

#ifdef __cplusplus
}
#endif

#endif
