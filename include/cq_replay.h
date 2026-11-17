/* Test-facing API of the replay gate backend (libcq_gates_replay.a). */
#ifndef CQ_REPLAY_H
#define CQ_REPLAY_H
#include <stdint.h>

typedef enum { CQ_G_X, CQ_G_CNOT, CQ_G_CCX, CQ_G_RY, CQ_G_RY_CTL, CQ_G_RZ, CQ_G_M, CQ_G_KINDS } cq_gate_kind;

void     cq_replay_reset(void);                       /* all bits 0, all counters 0 */
int      cq_replay_get_bit(uint32_t q);               /* untouched qubits read 0 */
void     cq_replay_set_bit(uint32_t q, int v);        /* input preparation: no gate, not "touched" */
uint64_t cq_replay_gates(void);                       /* total, all kinds incl. rotations and m */
uint64_t cq_replay_gates_of(cq_gate_kind k);
uint32_t cq_replay_depth(void);                       /* max layer; 0 if no gate */
uint32_t cq_replay_qubits(void);                      /* distinct qubits touched by a gate */
uint32_t cq_replay_max_index(void);                   /* highest qubit index touched (0 if none) */
/* The angle of the i-th rotation (ry, ry__ctl, rz) since the last reset; aborts unless
 * i < number of rotations and i < CQ_REPLAY_ANGLES (only the first ones are kept). */
#define CQ_REPLAY_ANGLES 4096
double   cq_replay_angle(uint64_t i);

#endif
