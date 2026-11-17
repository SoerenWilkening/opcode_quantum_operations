/* The sampled operation test harness (NORTH_STAR "Testing"). Rail-level: an
 * operation is a callback on rail handles (cq_rt.h), its reference is a C
 * function on values. Every sample runs forced-quantum and all-classical.
 *
 *   static void copy(int32_t o, const int32_t *a, uint32_t w) {
 *       cq_bit *ob = cq_rail_bits(o), *ab = cq_rail_bits(a[0]);
 *       for (uint32_t i = 0; i < w; i++) cq_cx(&ab[i], &ob[i]);
 *   }
 *   static hx_val copy_ref(const hx_val *a, uint32_t w) { (void)w; return a[0]; }
 *   hx_check_all(&(hx_op){ "copy", 1, 0, copy, copy, copy_ref });
 */
#ifndef CQ_HARNESS_H
#define CQ_HARNESS_H
#include <stdint.h>
#include "cq_rt.h"

/* A value of up to CQ_RAIL_MAX_WIDTH bits: little-endian 64-bit limbs, the layout cq_rail_alloc reads. */
typedef struct { uint64_t l[CQ_LIMBS]; } hx_val;

hx_val  hx_mask(hx_val v, uint32_t w);           /* v mod 2^w */
hx_val  hx_ones(uint32_t w);                     /* 2^w - 1 */
int     hx_eq(hx_val a, hx_val b);
hx_val  hx_xor(hx_val a, hx_val b);
hx_val  hx_and(hx_val a, hx_val b);
/* For values that fit in 128 bits: hx_u128 drops limbs 2 and up. */
static inline unsigned __int128 hx_u128(hx_val v) { return (unsigned __int128)v.l[1] << 64 | v.l[0]; }
static inline hx_val hx_of_u128(unsigned __int128 x) { return (hx_val){ { (uint64_t)x, (uint64_t)(x >> 64) } }; }
int32_t hx_to_rail(hx_val v, uint32_t w);        /* classical rail holding v mod 2^w */
hx_val  hx_from_rail(int32_t h);                 /* measures every bit (qubits go back to the pool) */

/* o ^= f(args): the result rail o starts at a chosen value, args unchanged.
 * Args have the sample width w (see hx_run_w); the result has out_width (0: also w). */
typedef void   (*hx_op_fn)(int32_t o, const int32_t *args, uint32_t w);
typedef hx_val (*hx_ref_fn)(const hx_val *args, uint32_t w);   /* masked by the harness */
#define HX_MAX_ARGS 4
typedef struct {
    const char *name;
    int         nargs;       /* 0..HX_MAX_ARGS */
    uint32_t    out_width;   /* 0: the sample width */
    hx_op_fn    op;
    hx_op_fn    unc;         /* the _unc, or NULL: after op then unc, o is back to its start */
    hx_ref_fn   ref;
} hx_op;

/* The current sample's arg values while op/unc runs, for an op that passes an
 * operand to the code under test as a literal instead of as its rail. */
extern hx_val hx_sample[HX_MAX_ARGS];

/* Forced-quantum cost of the op alone (input preparation and measurement excluded),
 * max over samples. For the depth tables (kmgy.9). */
typedef struct { uint64_t gates; uint32_t depth, qubits, samples; } hx_stats;

typedef enum {
    HX_OK = 0,
    HX_WRONG_RESULT,      /* o != o0 ^ ref(args) */
    HX_OPERAND_CHANGED,   /* an operand measured differently after the op (or op+unc) */
    HX_LEAK,              /* pool has more live qubits after everything was freed */
    HX_CLASSICAL_GATE,    /* all-classical run emitted a gate or took a qubit */
    HX_UNC_RESULT,        /* after op then unc, o != o0 (o0 = 0: "measures 0") */
} hx_fail;

enum { HX_NOABORT = 1 };  /* return the failure instead of abort(); it is still printed */

/* hx_run's cap on samples, 0: the default 1000. A test with very many (op, width) cases lowers it. */
extern uint32_t hx_max_samples;

/* At most 1000 samples (args, o0) for one width; stops at the first failure.
 * stats may be NULL. */
hx_fail hx_run(const hx_op *op, uint32_t w, int flags, hx_stats *stats);
/* hx_run with arg j of width arg_width[j] (0: w), e.g. 1 for a control. NULL: all w. */
hx_fail hx_run_w(const hx_op *op, uint32_t w, const uint32_t *arg_width, int flags, hx_stats *stats);
/* hx_run at every width of CQ_WIDTHS, aborting on failure. */
void    hx_check_all(const hx_op *op);

#endif
