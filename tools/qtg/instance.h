/* tools/qtg/instance.h — the KP4 instance from Box 1 of arXiv:2310.06623.
 *
 * KP_4(p, w; 7) with w = (2, 2, 1, 5) and p = (6, 2, 1, 2): the worked example
 * the paper draws its QTG circuit for, kept here verbatim so the register
 * widths below can be checked against the figure rather than recomputed.
 *
 * REGISTER WIDTHS ARE Eq. (2), |a_bin| = floor(log2 a) + 1:
 *   H1 path      n            = 4     (four items)
 *   H2 capacity  4            > 3     (see UNIFORM 4-BIT QINTS below)
 *   H3 profit    |P_bin|      = 4     (sum p = 11; Box 1 draws four |0>_3)
 *   Ha ancilla   max(n,3,4)   = 4
 * Total 15 qubits. The profit register cannot overflow — 11 < 16 — and the
 * capacity register cannot underflow, because SUB_wm runs only on a branch the
 * comparison c~ >= wm already admitted.
 *
 * THE INTERMEDIATE SOLUTION x' IS GREEDY'S, per Section IV: sort by decreasing
 * density p/w = (3, 1, 1, 0.4) and pack — item 1 (rem 5), item 2 (rem 3),
 * item 3 (rem 2), item 4 refused at w=5 > 2. So x' = 1110 with profit 9, which
 * is also OPTIMAL for this instance ({1,2,3} weighs 5; {1,4} weighs 7 for 8).
 * x' selects which of Eq. (3)'s two biased Hadamards each layer uses.
 */
#ifndef CQOPS_QTG_INSTANCE_H
#define CQOPS_QTG_INSTANCE_H

#define QTG_N   4                  /* items                                  */
#define QTG_WC  4                  /* see UNIFORM 4-BIT QINTS below          */
#define QTG_WP  4                  /* |P_bin|                                */

static const unsigned qtg_w[QTG_N] = { 2, 2, 1, 5 };
static const unsigned qtg_p[QTG_N] = { 6, 2, 1, 2 };
static const unsigned qtg_c        = 7;
static const unsigned qtg_xprime[QTG_N] = { 1, 1, 1, 0 };   /* Greedy         */

/* UNIFORM 4-BIT QINTS. Eq. (2) sizes the capacity register at |c_bin| = 3 for
 * c = 7; this build widens it to 4 so both arithmetic rails are one word.
 *
 * IT IS NOT FREE AND IT IS NOT A BUG FIX. Every kernel here is width-generic
 * over reg->width (I5), so the change is one constant — but the capacity
 * comparator and the capacity accumulator both grow with W, and K9's sandwich
 * scratch grows with them, so this costs gates AND logical qubits. It buys
 * uniformity: one word size for both registers, headroom for a capacity above
 * 7 without re-deriving a width, and a per-item cost that no longer depends on
 * which register an operation touches.
 *
 * WHAT IT CANNOT BREAK: the two's-complement SUB constant is (2^W - wm) mod 2^W
 * and is computed from QTG_WC, so widening it is automatically consistent, and
 * classical_check() re-derives the greedy walk at the new width. The profit
 * register was already 4 (sum p = 11), so QTG_WP is unchanged. */

/* The value the greedy walk must leave in each register — asserted, not assumed,
 * because widening QTG_WC changes the modular constant every SUB uses. */
#define QTG_GREEDY_CAP    2u   /* 7 - 2 - 2 - 1 */
#define QTG_GREEDY_PROFIT 9u   /* 6 + 2 + 1     */

/* Section IV: b = n/4. At b = 0 both rows of Eq. (3) collapse to the ordinary
 * Hadamard, which the paper measured as not competitive. */
#define QTG_BIAS ((double)QTG_N / 4.0)

/* THE Ry SYNTHESIS ACCURACY, AND IT LIVES HERE RATHER THAN IN qtg_qec.json.
 *
 * The biased Hadamard is the only non-Clifford rotation in the QTG, and Eq. (A1)
 * needs its amplitudes only well enough to bias the sampling — so eps = 0.125.
 * Ross-Selinger synthesis is parameterised by an INTEGER precision with
 * eps = 2^-precision, which the QEC library takes per call as qec_rz's last
 * argument and libcqops reads from the environment (CQOPS_QEC_PRECISION,
 * src/sink_qec.c:122, range [0, 30]).
 *
 * IT IS NOT A CONFIG FIELD, AND PUTTING IT THERE WOULD BE SILENTLY INERT. The
 * JSON schema does accept a `rotation` block with a `precision`, but that block
 * parameterises `qec_rz_configured`'s ONE golden angle (p/q fixed in the file) —
 * not the per-call `qec_rz` the sink actually drives. A config carrying
 * rotation.precision would be accepted, would look authoritative, and would
 * change nothing about this circuit.
 *
 * `circuit.epsilon` in the config is a DIFFERENT QUANTITY: the total logical
 * failure budget the surface-code distance is sized against. Conflating the two
 * is easy and expensive in both directions — it derives d, and d is what makes
 * one logical CX cost 112,134 physical gates. */
#define QTG_RY_EPSILON 0.125

#endif /* CQOPS_QTG_INSTANCE_H */
