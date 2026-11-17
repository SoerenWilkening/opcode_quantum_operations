/* Runtime internals of the memory bus (BACKEND_ABI §1.10): host memory access that never
 * faults, and the cells with their HOST/QUANTUM overlay. src/bus.c builds the classical-address
 * accesses on these; the quantum-address path (kmgy.21) reads and writes each cell of its
 * reachable set through the same two cell functions. */
#ifndef CQ_BUS_H
#define CQ_BUS_H
#include <stdint.h>
#include "cq_rt.h"

/* Host memory without faulting (src/hostmem.c, per platform). Read: 1 and the n bytes at a, or
 * 0 if any of them is not readable (a range past 2^64 - 1 is not). Write: 1 and one byte
 * written, or 0 if a is not writable. */
int cq_host_read(uint64_t a, uint8_t *v, uint32_t n);
int cq_host_write(uint64_t a, uint8_t v);

/* Cell a's 8 bits, bit i = bit i of the byte. QUANTUM: the overlay's bits (returns 1).
 * HOST: the host byte read now, 0 if unreadable (returns 0). The bits are a copy: a caller
 * that changes them puts them back with cq_cell_write. */
int  cq_cell_read(uint64_t a, cq_bit b[8]);
/* Puts cell a's bits back. quantum = 0 and every bit classical: the byte goes to host memory
 * and the cell is HOST, or, if a is not writable, the cell is QUANTUM holding the bits.
 * Otherwise the cell is QUANTUM holding the bits (qubits included) and no host byte is written. */
void cq_cell_write(uint64_t a, const cq_bit b[8], int quantum);

/* One access whose address rail (64 bits) holds a qubit: the QRAM of docs/QRAM_CONSTRUCTION.md
 * (src/qram.c). c: a qubit control, or NULL for none. h: the loaded or swapped rail, or the value
 * v of a store, w bits. A store takes its tape slot here. Aborts above CQ_MAX_QADDR_BITS qubits. */
enum { CQ_BUS_LOAD, CQ_BUS_STORE, CQ_BUS_SWAP };
void cq_qram_access(int op, const cq_bit *c, const cq_bit *addr, cq_bit *h, uint32_t w);

#endif
