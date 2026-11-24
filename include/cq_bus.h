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
/* n consecutive cells from a (wrapping mod 2^64) as cq_cell_read gives them, into b[8n]; returns 1
 * if any is QUANTUM. Never faults. */
int  cq_cells_read(uint64_t a, cq_bit *b, uint32_t n);
/* A bus store (ABI §1.10) of the w-bit v at the classical address a under c (NULL: none; a
 * classical c folds). t: the tape slot the displaced content goes to (8 ceil(w/8) bits), shared
 * by the stores of one quantum-address access; NULL: a fresh slot rail, as the ABI's store takes. */
void cq_bus_store_at(const cq_bit *c, uint64_t a, cq_bit *v, uint32_t w, cq_bit *t);

/* One access whose address rail addr (64 bits) holds a qubit: the QRAM of docs/QRAM_CONSTRUCTION.md
 * (src/qram.c). c: a qubit control, or NULL for none. h: the loaded or swapped rail, or the value
 * v of a store, w bits (handles: the derivation note, cq_rt.h). A store takes its tape slot here.
 * A note naming objects bounds the leaves by them where that gives fewer than the bit pattern (ABI §1.10
 * "Objects"); else the leaves are every registered cell the bit pattern can reach ("The memory size"). */
enum { CQ_BUS_LOAD, CQ_BUS_STORE, CQ_BUS_SWAP };
void cq_qram_access(int op, const cq_bit *c, uint32_t addr, uint32_t h, uint32_t w);
/* Test only: nonzero makes every access take the bit-pattern bound; the leaves the last access built. */
extern int cq_qram_force_bits;
extern uint32_t cq_qram_leaves;
/* Q, the cap on quantum address bits and the memory size: the registered objects hold at most 2^Q cells
 * (cqrt_qram_object aborts above). Environment CQ_MAX_QADDR_BITS, default the build's CQ_MAX_QADDR_BITS. */
uint32_t cq_qaddr_bits(void);
/* What cell a can hold ("Data"), in *t as cq_note_content gives it. QUANTUM: what the last store left;
 * returns 1 if that note is weak: it came with a rail that had no address note (a narrow or integer-typed
 * copy), so it counts for the pointer rule only. HOST: by its aligned 8-byte word, read now: the objects of
 * its value, or data if it is in none (zero included); returns 0. m: the caller's memo of the last word,
 * { 0 } at first. */
typedef struct { uint64_t a; int have; cq_note t; } cq_word;
int cq_cell_kind(uint64_t a, cq_note *t, cq_word *m);
/* The address note of the 8 cells read at a, rb their bits: null/objects by the value rule; if quantum (a
 * QUANTUM cell among them) the union of the cells' notes, none where one is not noted or weak. */
cq_note cq_note_cells(uint64_t a, const cq_bit *rb, int quantum);
/* The cell rule: what QUANTUM cell a can hold (cq_cell_write leaves unknown); nothing on a HOST cell. */
void cq_cell_note_set(uint64_t a, const cq_note *t, int weak);

/* Objects registered by cqrt_qram_object (src/qram.c), disjoint after merging. The object containing
 * a, and the object whose end (start + size) is a (at most one): 1 and its start and size, or 0. */
int cq_object_at(uint64_t a, uint64_t *start, uint64_t *size);
int cq_object_ending_at(uint64_t a, uint64_t *start, uint64_t *size);

#endif
