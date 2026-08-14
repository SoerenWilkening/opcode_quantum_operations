/* tests/support/mock_sink.h — the recording sink. Plan §2.2's workhorse.
 *
 * Captures the (op, operands) stream so a test can compare it against an
 * expected sequence and dump the actual one on failure. Step 6's 159-case
 * fold suite and every Phase-B L4 golden run through this file.
 *
 * THIS IS NOT A RULE 13 VIOLATION. "The library holds no circuit object, no
 * gate list and no statevector" is a constraint on src/, not on the far side
 * of the vtable: a gate is emitted through a function pointer and is gone from
 * our side, and what a SINK does with it is the sink's business. Recording it
 * here is precisely the freedom that design buys — it is also why the
 * sandwich palindrome check at Step 8 is possible at all. Nothing in src/ may
 * hold one of these.
 *
 * What it deliberately does NOT do: interpret. There is no notion of a
 * register, a bit-kind or a shadow in here, and no assertion about what a
 * correct stream looks like. It records indices and angles; the suite that
 * owns the semantics decides whether they are right.
 */
#ifndef CQOPS_TEST_MOCK_SINK_H
#define CQOPS_TEST_MOCK_SINK_H

#include "cqops/cqops.h"

#include <stddef.h>

typedef enum {
    CQ_OP_X = 0, CQ_OP_CX, CQ_OP_CCX, CQ_OP_RY, CQ_OP_RZ, CQ_OP_MZ,
    CQ_N_OPS
} cq_op;

/* One emitted gate. `t` is always the target, so a single field carries it
 * for all six ops; `a`/`b` are the controls, unused entries left 0. `angle`
 * is meaningful only for RY and RZ and is compared bitwise, never with ==,
 * so a -0.0 that arrived as 0.0 is a failure rather than a match. */
typedef struct {
    cq_op    op;
    uint32_t a, b, t;
    double   angle;
} cq_rec;

#define CQ_REC_X(q)          { CQ_OP_X,   0u,  0u, (q), 0.0 }
#define CQ_REC_CX(c, t)      { CQ_OP_CX,  (c), 0u, (t), 0.0 }
#define CQ_REC_CCX(a, b, t)  { CQ_OP_CCX, (a), (b), (t), 0.0 }
#define CQ_REC_RY(q, th)     { CQ_OP_RY,  0u,  0u, (q), (th) }
#define CQ_REC_RZ(q, ph)     { CQ_OP_RZ,  0u,  0u, (q), (ph) }
#define CQ_REC_MZ(q)         { CQ_OP_MZ,  0u,  0u, (q), 0.0 }

typedef struct {
    cq_rec *v;
    size_t  n, cap;
    size_t  by_op[CQ_N_OPS];
} cq_mock;

void cq_mock_init(cq_mock *m);
void cq_mock_dispose(cq_mock *m);
void cq_mock_reset(cq_mock *m);      /* empties, keeps the buffer */

/* A vtable bound to `m`. Returned by value: the caller owns the cq_sink and
 * may hold several at once, which is what proves nothing is global. */
cq_sink cq_mock_sink(cq_mock *m);

size_t        cq_mock_count(const cq_mock *m);
size_t        cq_mock_count_op(const cq_mock *m, cq_op op);
const cq_rec *cq_mock_at(const cq_mock *m, size_t i);   /* aborts if i >= n */

/* Exact sequence comparison, length included. Returns 1 on match. */
int  cq_mock_matches(const cq_mock *m, const cq_rec *want, size_t n);

/* Prints the recorded stream as TAP comments, so a failing golden shows what
 * was actually emitted instead of only that it differed. */
void cq_mock_dump(const cq_mock *m, const char *label);

#endif /* CQOPS_TEST_MOCK_SINK_H */
