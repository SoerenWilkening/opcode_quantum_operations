#!/usr/bin/env python3
"""free_pairing_check.py — structural Rule-6 free-pairing checker over LOWERED IR.

WHY THIS EXISTS (bd test_C_libtooling-8txa, the CHEAP half)
==========================================================
Rule 6 says a `cqrt_free(%r)` is sound only on a **provably-|0> rail**. The
project's Rule-2 gate is the e2e golden byte-diff — the only layer that links
and runs — but that layer **cannot witness this property**, because a generated
adjoint template prints

    cq_template_<op>_<W>_hl_inv(<forward args>) -> h<fresh>

and **never names the rail it is supposed to restore**. When bd `oudq` moved
atanhl's `cqrt_free` count 300 -> 217, the golden alone could not distinguish
"shared predicate, fewer rails" from "dropped uncompute" — closing that took two
throwaway out-of-band tools. This file promotes the structural one to a committed
gate at the IR layer, where the rail identity IS present (`%r` is an SSA name)
even though the trace has lost it.

WHAT bd `8txa` CHANGED, AND WHAT IT DID NOT
-------------------------------------------
The COMPARE FLAG-STRIP no longer has this defect. It used to be the flag-shaped
instance of it — this docstring's own motivating example was four textually
IDENTICAL `cq_template_fcmp_olt_f80_hl_inv(h67, …)` lines in
`slice_libm_real_log1pl.expected.log` that paired with four DIFFERENT
`cqrt_free`s. 8txa switched that strip to the void, out-first `_unc` ABI, so the
same four lines now read

    cq_template_fcmp_olt_f80_hl_unc(h78,  h60, 0xb.504f333f9de6484p-4)
    cq_template_fcmp_olt_f80_hl_unc(h80,  h60, 0xb.504f333f9de6484p-4)
    cq_template_fcmp_olt_f80_hl_unc(h106, h60, 0xb.504f333f9de6484p-4)
    cq_template_fcmp_olt_f80_hl_unc(h110, h60, 0xb.504f333f9de6484p-4)

— each naming the rail it zeroes, and the golden byte-diff DOES now witness that
pairing for compare flags.

This checker is NOT thereby redundant. It still owns every rail the trace label
cannot speak for: the whole-DAG uncompute spine's data `_unc` chains, the loop
reverse-play write history, and the "was this rail actually |0> at the free"
question, which no label can answer at any ABI. Read `classify()` and the
obligation below for exactly what it does and does not witness.

THE OBLIGATION IT WITNESSES
===========================
For every `cqrt_free(%r)`: **at the free, %r holds a KNOWN CLASSICAL BASIS
STATE and is unentangled** — the property that makes the free (a hardware reset
= projective measurement, Rule 6) collapse nothing. It establishes that by one
of two structural proofs, one per rail class.

MINTED rails — `%r = M(a...)` for a `cq_template_*`, a `cqrt_qram_load_<W>`, a
`cqrt_tape_write_<W>`, or a Phase-6 clone `@__cq_q_*`. The mint creates a fresh
|0> rail and writes `f(a...)` into it, so the free is sound iff that write was
undone:

  (T1) a REVERSAL exists and is claimed ONE-TO-ONE: either the void
       `M_unc(%r, a...)` (which names the rail — no inference needed) or an
       `M_inv(a...)` whose ARGUMENT LIST matches the forward's operand for
       operand. Every `_inv` token is consumed by at most one free, so N frees
       of same-shaped rails need N reversals. A deleted uncompute, or a
       reversal whose operands do not match the forward's, fails here.
  (T2) the forward's own handle operands are UNCHANGED between the forward and
       the reversal — either never written, or written only in a self-cancelling
       way (below). An `_inv` recomputes `f` from the live sources; if a source
       moved in between, it recomputes a DIFFERENT value and leaves the rail
       dirty.
  (T3) the rail itself is unchanged between mint and reversal (the `¬flag`
       `cqrt_x` bracket is such a self-cancelling pair) and is NOT written at
       all between the reversal and the free.

ALLOCATED rails — `%r = cqrt_alloc_<W>(v)`: the conjunction ancillas
(`cqrt_alloc_i1` + the toffoli/cnot flag network), the swap-route work rails,
and the `cq_theta` source rails. `v` is a CLASSICAL value, so the rail starts in
a known basis state and the free is sound iff it is still in one:

  (A1) every basis-changing write is a classical-immediate in-place update
       (`cqrt_addc_<W>` / `cqrt_xorc_<W>`, whose second operand is by ABI a
       classical literal) AND that operand is one this file can SHOW is not a
       rail. A known classical state plus a classical offset is still a known
       classical state — no superposition, no entanglement. This is the dtzb
       `break` index rail (`alloc(k)`, `addc(-k)`, free).
       The second half is not pedantry (bd az1h): the ABI's declaration that
       the slot is classical is an ASSERTION, and until 2026-07-31 this rule
       converted it straight into a Rule-6 clearance without ever looking. A
       planted `cqrt_xorc_i32(%r, %h)` entangles `%r` with the rail `%h`, and
       the next line's `cqrt_free(%r)` is then the hidden measurement Rules 6/8
       forbid — yet it reported `OK … via A1-classical-immediate`, exit 0. At
       W=i32 BOTH parameters are `int32_t`, so the verifier cannot see it, and
       `cq_runtime.c` prints the immediate slot as a bare decimal, so rail h13
       renders as `13` and the GOLDEN TRACE CANNOT SEE IT EITHER. Both gates
       blind on the same axis is precisely the complementarity bd 8txa claims,
       so the assertion is now CHECKED (`provably_not_a_rail`); an immediate
       that cannot be shown classical falls through to (A2), which still
       discharges a genuine `xorc(%r,%h) … xorc(%r,%h)` bracket, and otherwise
       reports `A1-imm-not-classical`.
  (A2) otherwise the write history REDUCES TO IDENTITY, returning the rail to
       its alloc-time classical `v`. Reduction repeatedly cancels a write
       against an earlier ADJOINT write when every write between them commutes
       with it (see COMMUTATION). `x`/`cnot`/`toffoli`/`cswap`/`xorc`/`copy`
       are self-adjoint; `ry`/`rz` pair with the negated angle; `addc` with the
       negated immediate; a qram store with its `_unc` twin. The plain
       even-length adjoint palindrome the ticket asks for is the common case of
       this reduction; a deleted uncompute toffoli leaves an odd, unpairable
       history and fails here.
       Those stability questions recurse (proving an operand held still means
       reducing ITS write history, whose pairs have operands of their own), and
       the recursion has NO STEP BUDGET: it terminates because every recursive
       edge strictly narrows the position interval. There used to be a
       `depth > 6` cut-off and it was a defect, not a safety net — see
       `unchanged_over` for the argument and for what the budget did to the
       memo (bd xzdx).
       A pair cancels only if everything ITS action depends on held still in
       between: the declared reads, the `classical_imm` slots (bd az1h), and —
       for the one symbol in the model that writes two rails — the CO-WRITTEN
       slot (bd 0gwl). `cqrt_cswap` is modelled `reads=(0,) writes=(1,2)`
       though a Fredkin physically reads all three, so until 2026-08-07 a
       route-pair cancelled with only the FLAG ever checked:
       `cswap(%f,%q,%rail); ry(%q,0.75); cswap(%f,%q,%rail); free(%rail)`
       reported `A2-reduces-to-identity`, exit 0, while the route-back parked
       `Ry(0.75)|0>` on the rail it then freed. See
       `pair_operands_unchanged` / `co_written_stable`.

Plus two whole-stream invariants:

  (W)  every `_inv` is claimed by EXACTLY ONE free. An `_inv` is the only
       construct in the lowered IR that writes a rail its signature does not
       name, so an unclaimed one is an adjoint whose target cannot be
       identified — the "re-paired to the wrong rail" defect in its purest
       form. The "exact bijection: 25174 `_inv` tokens, 25174 claims, 0
       orphans" this paragraph used to quote was measured BEFORE bd 8txa moved
       the compare flag-strip onto the void `_unc` ABI. RE-MEASURED 2026-07-31
       over the 237 registered slices: the ship floor now emits **ZERO** `_inv`
       calls. So (W) and the (T1b) `_inv` matching arm have NO corpus coverage
       left at all — their only live exercise is the POSITIVE fixtures in
       tools/free_pairing_check_test.py, which are therefore HISTORICAL rather
       than current lowerings and must not be "refreshed" from today's output
       without replacing that coverage. Do not quote the old bijection as
       evidence the rule is exercised; it is evidence about a corpus that no
       longer exists.
  (F)  the lifetime hygiene that has no other gate anywhere in the project:
       double free, use after free, freeing a measured handle, and freeing a
       non-rail resource token (a `cqrt_qram_alloc_*` array or a
       `cqrt_tape_alloc` tape).

THE SECOND OBLIGATION: THE CASE-A / CASE-B PARTITION  (bd 6f3o)
===============================================================
Independent of any free site, EVERY emitted gate must satisfy:

    *** NO INPUT OPERAND MAY ALIAS A RAIL THE GATE WRITES. ***

  CASE A — aliased INPUTS, output a FRESH |0> rail. REALIZABLE, ALLOWED, and
    deliberately NOT flagged. The generated ABI's out-of-place shape is
    `out(fresh |0>) ^= f(in0, in1)`, so aliasing the inputs gives `out ^= g(a)`
    with `g(a) := f(a, a)`, and `(a, 0) -> (a, g(a))` is the standard
    reversible embedding of a classical function — unitary and self-inverse for
    ANY `g`. Realizability is UNCONDITIONAL; aliasing changes only COST, and
    the argument needs no per-op identity (which is exactly why it is the one
    to use — do NOT justify Case A by enumerating degeneracies; `fsub(a,a)==0`
    and `fdiv(a,a)==1` are both WRONG under IEEE-754 and both ship in
    `slice_libm_real_acos`). MEASURED over this corpus: 638 Case-A calls across
    27 template symbols, plus 15 `_unc` twins of the same shape. A rule that
    flags those is a WRONG RULE, not a discovery.
  CASE B — an input aliases a rail the gate WRITES. NOT realizable:
    `cswap(c,c,b)` maps both |1,0> and |0,1> to |0,1>, which is not injective,
    so no unitary implements it. Reported `B1-input-aliases-written-rail`.
    `B2-two-written-rails-alias` covers the adjacent spelling — a symbol whose
    TWO write slots are the same rail — and its ground is DIFFERENT, so do not
    quote B1's non-unitarity argument for it: `cswap(f,x,x)` is the IDENTITY,
    hence perfectly unitary. It is flagged as DEGENERATE (no router can have
    meant to swap a register with itself), which is a decline of intent rather
    than of physics, and is what the bd 6f3o write-set table specifies.

The write-sets are `classify()`'s, not a second table: `cnot(c,t)` writes t,
`toffoli(c1,c2,t)` writes t (c1==c2 is Case A), `cswap(f,x,y)` writes BOTH x
and y, `copy_W(s,d)` writes d, `copy_W_controlled(f,s,d)` writes d, a
`cq_template_OP_W(ins)` writes NO operand, `cq_template_OP_W_unc(out, ins...)`
writes out. A `classical_imm` slot counts as an input, so `xorc(%r,%r)` is
Case B like any other; a slot that is both read and written at the SAME index
is an in-place update, not aliasing (`cqrt_measure_<W>(%h)`), and is skipped.

A 14-agent audit (bd big5, 2026-07-31) swept every rail-writing mint in the
pass and found ZERO Case B emitted; this rule is what keeps that true. It is a
STATIC check over SSA-value identity and deliberately does not depend on the
runtime's monotonic never-recycling handle counter — that counter is a property
of the TRACE-ONLY STUB, not of the ABI contract a Phase-9 backend must honour.

TWO MODELLING DECISIONS THAT ARE PHYSICS, NOT CONVENIENCE
=========================================================
* `cqrt_rz_<W>` IS TRANSPARENT. Rz = diag(1, e^{i.theta}) is DIAGONAL in the
  computational basis: it changes no basis label, so it can neither dirty a
  |0> rail nor invalidate a recorded value. This is exactly Rule 6's phase-arm
  clause ("the rail is |0> for free, Rz|0> = |0>") and the same
  diagonal/non-diagonal split the pass's own `declineStaleRecordOps` /
  `WireWalk` guard uses. It is what lets the canonical swap-routed phase arm
  (`cswap; rz; rz; cswap; free`) verify. `cqrt_ry_<W>` is NOT diagonal and
  stays a full write — dropping an `ry` adjoint before a free IS caught.
* COMMUTATION IS LIMITED TO COMPLEMENTARY FLAG BRANCHES. Two controlled writes
  to the same rail are treated as commuting only when they share a control
  register whose `cqrt_x` parity differs between them AND that register is
  touched by nothing but `cqrt_x` in between — i.e. they provably act on
  disjoint branches of the same flag. That is the `¬flag cqrt_x` bracket the
  two-arm emitter emits (§5b), and it is what makes the if/else `A B A B`
  routing of a shared base reduce to identity. No other reordering is allowed.
  The same admission — and no wider one — governs the co-written slot check
  (bd 0gwl): an intervening write on the complementary branch of a flag the
  pair also carries is dropped before that slot's history is reduced. Two
  operators supported on complementary blocks of one flag commute exactly, so
  on the branch where the pair acts such a write is the identity. Without it
  the `A B A B` two-arm route stops reducing and a correct, reviewed lowering
  is reported UNPROVEN — for a GATE, that is failing a good build.

WHAT IT DOES **NOT** WITNESS  (read this before quoting the tool)
================================================================
  * PHASE. Because Rz is transparent (above), a dropped or wrong-angle `rz`
    adjoint is INVISIBLE here. That is a real miscompile — it is the golden
    trace's job, and the two gates are complementary by construction: the trace
    sees the angles and not the rail identities, this file sees the rail
    identities and not the angles.
  * The UNITARY. This is a structural check, not a simulator. It proves the
    emitted gate sequence has the SHAPE of a reversal; it does not evaluate it.
    Reduction-to-identity is SUFFICIENT for "unchanged", never necessary:
    `cqrt_x(h); cqrt_cnot(c,h); cqrt_x(h); cqrt_cnot(c,h)` is identity and
    is reported UNPROVEN, not verified. Likewise a Bennett-style uncompute that zeroes an ancilla by
    RECOMPUTING its predicate from updated state — the dtzb `break` fire rail,
    `toffoli(c,eqz,fire) … cnot(eqz',fire)` — is genuinely correct and
    genuinely UNPROVEN here; witnessing it needs the basis-state evaluator that
    is the other, expensive half of bd 8txa.
  * INTERCHANGEABLE reversals. When k frees pair with k character-identical
    `_inv` calls (the log1pl fcmp_olt family), any permutation of the pairing
    satisfies (T1) — correctly so, since identical ops on identically-valued
    rails ARE interchangeable. What it catches is a count or argument mismatch,
    which is what "re-paired to the wrong rail" degenerates into whenever the
    rails are not interchangeable.
  * Rails that are NEVER freed. Rule 6's "a rail not provably |0> is left
    allocated, never freed" is a rule about what the pass must NOT emit; a
    leaked dirty rail is invisible here (and is accepted in the
    qubits-not-scarce regime).
  * The ENTANGLEMENT half of Rule 8. A copy+uncompute that should have been a
    `cqrt_cswap` presents as a well-formed reversal.
  * ESCAPE analysis (`ret` / `cq_invert_*` handles) beyond "was it measured in
    this function", and anything cross-function or cross-stream (below).
  * The FORWARD program. It checks reversal, not computation.
  * ALIASING THAT IS NOT SSA-IDENTITY. The Case-B rule compares operand SSA
    names, so it sees `cqrt_cnot(%c, %c)` and not two DISTINCT names that carry
    the same rail at run time. Three ways that can happen, none witnessed here:
    a `phi` that merges a handle with itself; a callee whose two parameters
    receive the same handle at the call site (the check is intra-procedural —
    MEASURED: no `@__cq_q_*` call in the corpus passes a repeated argument, so
    nothing is being hidden today); and a handle laundered through classical
    arithmetic. What makes SSA identity the right layer anyway is that it is
    the one an emitter bug actually produces — a `CreateCall` with the same
    `Value*` twice — and it is exactly the layer a later RAUW can retarget.
    The live example, cited precisely because the two halves are often
    conflated: SwapRouteEmit.cpp:589 only RECORDS
    `taint.SharedCondFlags[Cond] = Flag` and deliberately does NOT RAUW there
    (read its comment); CompareLowering.cpp:415 later runs
    `cmp->replaceAllUsesWith(Shared)`, and THAT is what can rewrite an
    already-emitted gate operand. See bd
    `case-b-cswap-603-near-miss-2026-07-31` for why it cannot reach the routing
    `cqrt_cswap` today, and for the ArmClassify gate that is load-bearing here.
  * SYMBOLS `classify()` DOES NOT MODEL. Write-sets exist only for the symbols
    it knows, so the Case-B rule's coverage is exactly that set. MEASURED
    2026-07-31: of the 173 `cqrt_*` functions declared in `runtime/cq_runtime.h`
    it models 145; the 28 it does not are the entire `_controlled` /
    `_controlled_inv` reserve family (`cqrt_x_controlled`, `cqrt_cnot_controlled`,
    `cqrt_h_controlled`, `cqrt_rz_<W>_controlled[_inv]`,
    `cqrt_ry_<W>_controlled_inv`), retained per CLAUDE.md only as a reserve for
    the optional §6a control-in-place peephole and emitted by nothing today.
    This is NOT a silent gap: any of them being CALLED in lowered IR raises
    `Unclassified` and fails the whole run, so the failure mode is a loud stop,
    never an unchecked call. (A bare `declare` does not — only a call is
    parsed — which is correct: an unused declaration acts on no rail.) It IS a
    standing obligation: whoever un-shelves that family must add its read/write
    sets here first, or the run will refuse to render a verdict.
  * NON-CALL instructions. Only calls are modelled; a rail handle reaching a
    gate through `select`/`phi`/integer arithmetic is invisible to the alias
    rule, to `provably_not_a_rail` (which answers "not a rail" for any non-call
    SSA value that is not a formal parameter), AND — the sharper consequence,
    named here after the bd xzdx round found this bullet understated it — to the
    WRITE HISTORY itself. `Rail.writes` is keyed on the literal SSA operand
    string, so `%v = select …, %h, …` followed by `cqrt_x(%v)` records the flip
    against a fresh EXTERN rail called `%v` and leaves `%h`'s history empty;
    (T2), (T3) and (A2) then all report `%h` unchanged and the free VERIFIES,
    where the same flip written directly on `%h` correctly declines. Nothing
    inside this checker stands behind that — what stands in front of it is the
    PASS: `declineClassicalMuxes` (ir-pass/src/TaintPropagation.cpp) loudly
    rejects a live select/phi over tainted data, so stage 2b never sees one, and
    MEASURED over the 237 slices no phi or select result reaches a runtime-call
    argument. It is a standing obligation on whoever widens that (the 8h7d loop
    work, Phase-8 `cq_invert`), and it is filed rather than left implicit.

STREAMS / CFG SCOPE. Execution order is only knowable without a simulator on
straight-line code, so the analysis runs per STREAM: a maximal chain of basic
blocks B0 -> B1 -> ... where each Bi ends in an unconditional `br label %B(i+1)`
and B(i+1) has Bi as its only predecessor. That is the shape the lowering emits
(`RegionFlatten` linearizes accepted control regions; the uncompute spine and
the flag network emit adjacent runs). A rail minted in one stream and freed in
another — the dtzb whole-loop reverse-play is the live example — is reported
UNPROVEN, never silently passed (`U1-cross-stream`, or `U3-extern-rail` when it
is also written here). Writes to a handle that arrives from outside ARE
recorded, so "this operand has no writes in this interval" is a fact about the
stream rather than an absence of information.

EXIT STATUS. 0 iff every free site VERIFIED. Non-zero on any VIOLATION, on any
UNPROVEN site (unless `--allow-unproven`), and on any UNCLASSIFIED runtime
symbol. The last is deliberate and mirrors the Prime Directive: a new `cqrt_*`
symbol whose read/write effects this file does not know makes the model
unsound, so it fails loudly rather than checking the rest and reporting success.

MEASURED STANDING (2026-07-31, the 237 registered e2e slices at their stage-2
`opt` output — 218 single-file + 19 multi-file): 50504 `cqrt_free` sites, 50484
VERIFIED, 0 VIOLATIONS, 20 UNPROVEN, and 0 Case-B findings. All 20 UNPROVEN are
in the five `slice_loop_break*` slices and are the dtzb whole-loop reverse-play
(10 `A-not-reducible` on the `cq.brk.fire` ancilla, which a correct Bennett
RECOMPUTE zeroes rather than a palindrome, and 10 `U1-cross-stream` on
`cq.brk.R`/`cq.brk.idx`, whose write history spans the loop back-edge). Those
five e2e entries therefore pass `--allow-unproven` with that reason written at
the call site; every other slice runs strict. Adding the (A1) immediate check
and the Case-B rule moved ZERO of the 237 verdicts; RE-MEASURED 2026-08-07,
adding the (A2) co-written-slot check (bd 0gwl) moved ZERO of them as well —
same 50504/50484/20/0, no slice changed a single finding. RE-MEASURED AGAIN
2026-08-08 on a freshly regenerated corpus after removing the recursion's depth
budget (bd xzdx): same 50504/50484/20/0, again no slice changed a finding —
expected, because the recursion nests at most 3 frames deep on any of the 237
slices (a `depth` of 2 against a budget that refused at 7), so the ship floor
never touched it. That is a statement about the corpus, NOT about the defect:
see VALIDATED BEFORE USE for where it bites.

VALIDATED BEFORE USE. Proof-targeted mutation — blank or corrupt an instruction
some VERIFIED site's own proof consumes, then re-check — killed 6569 of 6579
mutants (99.8%) when it was run, ON THE 2026-07-26 CORPUS OF 232 SLICES. That
figure is dated, not current: the same experiment over today's 237-slice corpus
generates ~160k mutants and has not been re-run at that scale. All 10 survivors
were the same by-design case: perturbing a classical `cqrt_addc` on an (A1)
rail changes WHICH known classical basis state the rail holds, never whether it
holds one, so the free stays Rule-6 sound.
tools/free_pairing_check_test.py is the committed, ctest-wired subset of that
experiment (positive fixtures + hand-planted named defects + a proof-targeted
sweep), and it IS current.

The Case-B rule was validated the same way before being trusted, because a
rule that has never fired is a rule nobody should believe. Over the SAME 237
lowered slices — the ship floor, not hand-written .ll — write-bearing calls had
one input operand retargeted onto a slot the call writes (and, for the
two-write `cswap`, its two written slots aliased): 392 planted aliases, 392
CAUGHT, 0 SURVIVED, with all 237 slices Case-B-clean before mutation.

THE CAP, STATED RATHER THAN IMPLIED (Rule 12, no silent caps): those 392 are
not every candidate. The corpus offers 109759 (call, alias-plan) candidates
across 165 (symbol, kind) families, and the experiment plants at most 3 per
FAMILY — so coverage is complete across families and a 0.36% sample within
them. That is the right axis to be exhaustive on, because the rule branches on
the symbol's Effect and not on the call site; but it is a sample, and 392/392
must not be read as "every aliasable call in the corpus was tried".

THE (R1) RECORDED-RAIL rule (bd 261j(b)) was validated the same way, 2026-08-11,
and it is the only rule here that does not go THROUGH a proof — so the evidence
matters more, not less.

 (1) VERDICT DIFF over every one of the 240 REGISTERED e2e slices (rebuilt from
     build/tests/e2e/CTestTestfile.cmake so the list cannot drift from ctest),
     comparing the FULL finding list and not just exit codes, against a pinned
     pre-change copy of this file: 0 moved. False positives are the expensive
     direction for a GATE and this is the only measurement that bears on them.
 (2) FIRE TEST on synthetic IR: the pre-261j lowering, its controlled-twin
     ctrl-slot form, a chained theta, and an ANCESTOR-only correlation all
     VIOLATE; the three adjacent shapes it must stay silent on — a diagonal
     `cqrt_rz_` rail, the DERIVED rail whose free bd 261j(a) deliberately KEPT,
     and a rotated rail with no record — all still VERIFY. All six are committed
     in free_pairing_check_test.py, which is what finally gives
     `cqrt_tape_write_<W>` the mutant that file's own docstring admits it lacked.
 (3) FIRE TEST ON REAL LOWERED IR, not hand-written .ll: plant a tape record on a
     rail that the corpus module itself both rotates and frees. 19 planted, 19
     caught, 0 survived. THE CAP, STATED: one plant per module, and only in the
     19 of 240 modules that HAVE such a rail — the other 221 have none, because
     the 261j fix already stopped the compiler emitting that shape. This is not
     "every rotated rail in the corpus".
 (4) ARM-KILL MATRIX, and it found two gaps in the first draft of the mutants
     rather than confirming them. Deleting the ROTATION conjunct, the RECORDED
     conjunct, or the BACKWARD CLOSURE each turns the self-test red (2, 5 and 1
     failures) — but only after adding the ancestor mutant, WITHOUT which
     deleting the closure entirely left the file green. The fourth arm, the
     `cqrt_ry_` name test, could NOT be killed: an `rz` never enters a rail's
     write history at all (`if not e.diagonal`), so the name test is defence in
     depth over an exclusion made one layer earlier. Recorded rather than
     claimed the other way round.

THE LEAK CENSUS ships with it, for the reason bd 261j(b) names: this file counts
free SITES, so a compiler that frees nothing scores "0 cqrt_free site(s): 0
verified" and passes STRICT. The summary now always states how many rail mints
were left allocated. Leaks are legal (Rule 6's fallback) so it never fails on
them — but the number can no longer be silently zero. On the corpus it is what
makes bd 261j(a)'s own effect visible: `slice_io` reads "1 rail mint(s) of which
1 left allocated, 0 cqrt_free site(s)".

The (A2) CO-WRITTEN-SLOT rule (bd 0gwl) was validated the same way, 2026-08-07,
and here the population is small enough to be EXHAUSTIVE rather than sampled —
so unlike the Case-B figure above, this one has no cap to state. Instrumenting
`pair_operands_unchanged` over the same 237 lowered slices finds 60 co-written
cancellations, all `cqrt_cswap`, across 10 slices (the 11th cswap-bearing
slice, `slice_control_abort`, routes to a grave rail that is never routed back
and never freed, so no pair forms). Planting a dirtying `cqrt_ry` on the
co-written register INSIDE each: 60 planted, 60 CAUGHT, 0 survived.
Then the ARM-KILL MATRIX, because a predicate with N accept-arms and one probe
lets N-1 be deleted with a green suite (the az1h lesson): each arm was deleted
individually and the committed selftest re-run, and each of the FOUR turns it
RED via a DIFFERENT fixture — the co-written loop itself (the three 0gwl
mutants; with it gone the ticket's own reproducer is silently VERIFIED again),
the `v == target` skip (`a2-co-written-target-skip`), the complementary-branch
commutation filter (`IFELSE_DERIVED`), and the nested reduction rather than a
demand for an empty history (`VALUE_DIAMOND`).

REMOVING THE DEPTH BUDGET (bd xzdx, 2026-08-08) is the one change here that
WIDENS what verifies, so it was measured on the axis that matters — is anything
it newly accepts actually dirty? The corpus cannot answer that (it never nests
past 3 frames, so all 237 verdicts are unmoved), and the flat random
streams the Case-B work used cannot either (4-30 gates, 8000 modules, ZERO
sites changed verdict — a probe with no power on this change, which is worth
saying rather than quoting as a pass). The population that does have power is
LADDERS: nested adjoint pairs 5 to 14 levels deep, built from
`cqrt_x`/`cnot`/`toffoli`/`cswap` only — all classical permutations, so the same
exact oracle the Case-B validation used decides the truth by evaluating the
network over every assignment of the unknown rails — with HALF the modules
deliberately perturbed (a gate dropped, duplicated, or inserted) so the
population is not clean by construction. Over 12000 such modules — SEEDS 41 AND
77, 6000 each, named because a count nobody can re-derive is not a measurement —
4201 sites that the old tool declined now verify, and the oracle says every one
of them is genuinely |0> at the free; 0 sites moved the other way; and across
all 23278 OK verdicts the new tool issues there, 0 over-accepts (the old tool:
19077 OK verdicts, also 0). Other seeds give the same picture with different
counts: the invariant being claimed is the two ZEROS, not the 4201.

Read that 23278 as a denominator with care: most of those OK verdicts are on
rails the old tool ALSO cleared, so the figure the widening actually rests on is
the 4201 — every one of which was adjudicated, none of which was dirty.

AND READ THE POPULATION'S SCOPE, which is a real limit and not a formality: it
is permutation-only BECAUSE that is the class the oracle can decide, and that
deliberately excludes the superposition class — `cqrt_ry` / `cqrt_h`. This file
does not witness that class at all (see WHAT IT DOES **NOT** WITNESS: "The
ENTANGLEMENT half of Rule 8"), at any depth, before or after this change: a
`cqrt_ry(R,t) … <a CNOT-class READ of R> … cqrt_ry(R,-t)` pair cancels here
while R is left entangled with the reader, because the reduction reasons over
WRITES to R and never asks what read it. The bd xzdx Rule-13 round confirmed
both halves of the attribution — the hole is HEAD's (patch HEAD's `depth > 6` to
`> 100` and HEAD accepts the same input at any depth), and removing the budget
widens its REACH from depth <= 6 to any depth. So the two zeros above are zeros
over the permutation class only; the superposition class is the pre-existing
8txa "expensive half" and needs the basis-state evaluator, not a deeper fuzz.
It is filed, with its one live corpus instance, rather than left implicit.

The other three measurements: verdict diff over the 237 slices, 0 moved (above);
proof-targeted mutants planted in the SHIPPED lowered IR, 735 planted (at most 4
per slice, out of 160229 witness lines — a cap, stated), 735 caught by BOTH
tools, 0 catches lost; and the order property itself — 4000 random modules per
seed whose trailing `cqrt_free`s are permuted, where the old tool moved a
verdict on 27 (seed 7) and 19 (seed 99) of them and the new tool on 0 of either.
On real IR that last probe is nearly powerless and says so: only 25 of the 237
slices have two `cqrt_free`s on adjacent lines at all, and swapping them moved
nothing under EITHER tool.

WHAT IT COSTS, since a budget also caps work. With both edges cached the
recursion is polynomial, and the corpus does not notice: the 237-slice sweep is
6-8 s before and after. A deep SYNTHETIC nest does pay, because it now runs to
completion instead of being cut — MEASURED on `order_ladder`, 2026-08-08:
D=2000 / 6005 calls 0.09 s -> 0.31 s, D=8000 / 24005 calls 0.38 s -> 3.73 s
(~10x, growing about quadratically in the nesting depth, mostly in the
`witness |= sub` frozenset unions that `reduces_to_identity` accumulates one per
level). Every one of those PRE timings is the tool declining a stream it should
verify, so the extra time buys the right answer; it is recorded because a gate
with no timeout should have its cost curve written down somewhere.

Its ARM-KILL MATRIX has five rows, and rows (3b) and (4b) exist because the
first pass of it was wrong twice. All five re-measured 2026-08-08 against the
committed selftest as it stands today; the counts move when fixtures are added,
so re-measure rather than quote.
  (1) THE BUDGET REMOVAL ITSELF — run the PRE-change tool against today's
      selftest: 7 FAIL rows over 6 distinct fixtures (both order pairs, the two
      D=7 ladder positives, the deep D=500 positive, the braid, the ticket
      repro).
  (2) `ensure_recursion_headroom` — delete the call and `xzdx-deep-ladder-d500`
      dies on `RecursionError` mid-run, with no summary line at all.
  (3a) THE `co_written_stable` RE-ENTRY ASSERTION — break narrowing by weakening
      `writes_in` to `lo <= p <= hi`. With both assertions in place the tool
      stops loudly and NAMES the re-entered query; delete this one and the same
      input dies on a `RecursionError` that names nothing. The runaway goes
      through this edge on every fixture carrying a `cqrt_cswap` route pair,
      7 of the 19.
  (3b) THE `unchanged_over` RE-ENTRY ASSERTION — which SURVIVES the probe in
      (3a), because that runaway never reaches this edge. Surviving means the
      probe was too weak for this arm, not that the arm is spare. The probe that
      does kill it is the committed mutant
      `case-b-self-aliased-cnot-aka-the-narrowing-probe`: no `cqrt_cswap` at
      all, so `co_written_stable` is never entered, and a self-aliased
      `cqrt_cnot` makes the READS edge re-enter `('%a', 3, 4)`. Guard present:
      named `NarrowingViolation`. Guard deleted: unnamed `RecursionError`.
  (4a) `_stable_memo` IS A PURE CACHE, which is checkable rather than
      assertable: delete its writes and both the selftest and all 237 corpus
      verdicts are byte-identical (slower, and that is the only difference).
  (4b) `_co_written_memo` IS NOT — do not read row (4a) as covering both. Delete
      ITS writes and the selftest turns RED on the `xzdx-cswap-braid-d20` cost
      bound at 1048575 entries against a bound of 5000. That cache is load
      bearing for termination-in-practice, not a speed-up.

THE ARM COUNT IS FOUR BECAUSE THE FIRST ANSWER WAS WRONG, AND THAT IS THE
LESSON WORTH KEEPING. The `v == target` skip survived its first kill test, and
this file then recorded it as "redundant by construction … no input can
distinguish the two". The bd 0gwl Rule-13 round refuted that with a runnable
stream: the caller's `commutes` filter and the one this path would use are
evaluated over DISJOINT intervals ((w, p2) versus (p1, w)), so they disagree
whenever a non-`cqrt_x` write touches the flag on one side of `w`. Surviving a
kill test means the PROBE was too weak, and "redundant by construction" is a
claim about all inputs that a corpus sweep cannot establish — measuring zero
moved verdicts shows only that the corpus lacks the distinguishing shape. Build
the probe; do not promote its absence into a proof.

USAGE
    tools/free_pairing_check.py [options] lowered1.ll [lowered2.ll ...]
      -q/--quiet          only print the summary and any findings
      --allow-unproven    downgrade UNPROVEN sites from failure to a report
      --explain           per verified site, the rule and the witness lines
      --stats             print per-category counts
      --json              machine-readable report on stdout
Self-test / mutant validation: tools/free_pairing_check_test.py.
"""

import argparse
import json
import re
import sys
from collections import Counter

# ---------------------------------------------------------------------------
# Effect model — the read/write/mint behaviour of every symbol the pass emits.
#
# Handle-ness of an operand is decided DYNAMICALLY (an operand is a rail iff
# this analysis already saw it minted), so the `_hl`/`_lh`/`_qq` operand-shape
# grammar never has to be re-parsed here; a classical literal or a classical
# SSA value simply is not in the rail table. Rule 15 (width-only ABI) means the
# width suffix is irrelevant to the effect model, so every entry below is keyed
# by the width-stripped stem.
# ---------------------------------------------------------------------------

_W = r"(?:i1|i8|i16|i32|i64|i80|i128|f16|f32|f64|f80|f128)"

ALL = "ALL"            # every operand is read
ALL_BUT_0 = "ALL_BUT_0"  # `_unc(out, srcs...)` — out is written, srcs read


class Unclassified(Exception):
    def __init__(self, sym):
        super().__init__(f"unclassified runtime symbol: @{sym}")
        self.sym = sym


class NarrowingViolation(Exception):
    """The stability recursion re-entered a query it was already computing.

    This is an INTERNAL INVARIANT FAILURE, not a property of the input. The
    reduction terminates because every recursive edge strictly narrows the
    position interval (see `unchanged_over`), so a re-entry means an edge that
    does not narrow was added and the recursion is no longer known to
    terminate. It is raised rather than answered `False` on purpose: a silent
    `False` here is exactly the depth-budget artefact bd
    test_C_libtooling-xzdx removed — an over-decline whose value depends on the
    order queries happen to run in. Fix the edge; do not re-add a budget.
    """

    def __init__(self, key):
        super().__init__(
            f"free_pairing_check INTERNAL: stability query {key} re-entered "
            f"itself, so some recursive edge no longer narrows the position "
            f"interval; the termination argument in `unchanged_over` is void")
        self.key = key


class Effect:
    """Per-call effect record.

    reads/writes are operand INDICES (or the ALL / ALL_BUT_0 sentinels).
    `mints` is None (void or classical return), 'rail', 'array', 'tape', or
    'implicit-rail' (an `_inv`: it does write a rail, but the ABI does not say
    which — the whole reason this file exists).
    `diagonal` marks a write that changes no computational-basis label.
    `controls` are the operand indices whose value selects the branch the write
    acts on (used only by the complementary-branch commutation rule).
    `classical_imm` is the tuple of operand INDICES the ABI declares to be
    classical literals rather than rail handles (`addc`/`xorc` slot 1). It is
    an ASSERTION ABOUT THE ABI, never a fact about the call — rule (A1) must
    CHECK the slot before relying on it (bd az1h), and the Case-B scan treats
    these slots as inputs like any other.
    """

    __slots__ = ("role", "reads", "writes", "mints", "adjoint_of", "base",
                 "diagonal", "controls", "classical_imm")

    def __init__(self, role, reads=(), writes=(), mints=None, adjoint_of=None,
                 base=None, diagonal=False, controls=(), classical_imm=()):
        self.role = role
        self.reads = reads
        self.writes = tuple(writes)
        self.mints = mints
        # 'self' | 'negate-arg<N>' | 'twin:<symbol>' | None
        self.adjoint_of = adjoint_of
        self.base = base            # forward stem for an _inv/_unc twin
        self.diagonal = diagonal    # phase-only: invisible to the basis ledger
        self.controls = tuple(controls)
        # addc/xorc: the slot the ABI says is a classical literal. CHECKED, not
        # trusted — see `provably_not_a_rail` / rule (A1).
        self.classical_imm = tuple(classical_imm)


def classify(sym):
    """Map a callee name to an Effect, or None if the symbol is not ours.

    Raises Unclassified for a `cqrt_*` symbol this model does not know: a new
    runtime primitive changes what "written" means, so it must be added here
    before any verdict from this file means anything.
    """
    # ---- allocation / lifetime -------------------------------------------
    if re.fullmatch(rf"cqrt_alloc_{_W}", sym):
        return Effect("alloc", mints="rail")
    if re.fullmatch(rf"cqrt_qram_alloc_{_W}", sym):
        return Effect("alloc-array", mints="array")
    if sym == "cqrt_tape_alloc":
        return Effect("alloc-tape", mints="tape")
    if sym == "cqrt_alloc_handle":
        return Effect("alloc", mints="rail")
    if sym == "cqrt_free":
        return Effect("free")
    if re.fullmatch(rf"cqrt_measure_{_W}", sym):
        # Recorded as a WRITE with no adjoint: a measurement is irreversible, so
        # a rail it touched can never afterwards be shown "unchanged" for some
        # later reversal, and can never be freed (F8).
        return Effect("measure", reads=(0,), writes=(0,))

    # ---- in-place gates ---------------------------------------------------
    if re.fullmatch(rf"cqrt_rz_{_W}", sym):
        # DIAGONAL: Rz = diag(1, e^{i.theta}) relabels nothing (Rule 6 phase arm).
        return Effect("gate", writes=(0,), adjoint_of="negate-arg1",
                      diagonal=True)
    if re.fullmatch(rf"cqrt_ry_{_W}", sym):
        return Effect("gate", writes=(0,), adjoint_of="negate-arg1")
    if sym == "cqrt_x":
        return Effect("gate", writes=(0,), adjoint_of="self")
    if sym == "cqrt_h":
        # Self-adjoint; the pass does not emit it. Classified for safety.
        return Effect("gate", writes=(0,), adjoint_of="self")
    if sym == "cqrt_cnot":
        return Effect("gate", reads=(0,), writes=(1,), adjoint_of="self",
                      controls=(0,))
    if sym == "cqrt_toffoli":
        return Effect("gate", reads=(0, 1), writes=(2,), adjoint_of="self",
                      controls=(0, 1))
    if sym == "cqrt_cswap":
        return Effect("gate", reads=(0,), writes=(1, 2), adjoint_of="self",
                      controls=(0,))
    if re.fullmatch(rf"cqrt_copy_{_W}", sym):
        # dst ^= src — an involution (cq_runtime.h: "no cqrt_copy_<W>_inv twin")
        return Effect("gate", reads=(0,), writes=(1,), adjoint_of="self")
    if re.fullmatch(rf"cqrt_copy_{_W}_controlled", sym):
        return Effect("gate", reads=(0, 1), writes=(2,), adjoint_of="self",
                      controls=(0,))
    if re.fullmatch(rf"cqrt_addc_{_W}", sym):
        return Effect("gate", writes=(0,), adjoint_of="negate-arg1",
                      classical_imm=(1,))
    if re.fullmatch(rf"cqrt_xorc_{_W}", sym):
        return Effect("gate", writes=(0,), adjoint_of="self",
                      classical_imm=(1,))

    # ---- output tape (7.6) ------------------------------------------------
    if re.fullmatch(rf"cqrt_tape_write_{_W}", sym):
        return Effect("mint", reads=(1,), writes=(0,), mints="rail")
    if re.fullmatch(rf"cqrt_tape_write_{_W}_controlled", sym):
        return Effect("mint", reads=(0, 2), writes=(1,), mints="rail",
                      controls=(0,))

    # ---- QRAM (7.5) -------------------------------------------------------
    if re.fullmatch(rf"cqrt_qram_load_{_W}", sym):
        return Effect("mint", reads=(0, 1), mints="rail")
    m = re.fullmatch(rf"cqrt_qram_load_({_W})_unc", sym)
    if m:
        return Effect("unc", reads=(1, 2), writes=(0,),
                      base=f"cqrt_qram_load_{m.group(1)}")
    if re.fullmatch(rf"cqrt_qram_store_{_W}", sym):
        return Effect("gate", reads=(1, 2), writes=(0,),
                      adjoint_of="twin:" + sym + "_unc")
    m = re.fullmatch(rf"cqrt_qram_store_({_W})_unc", sym)
    if m:
        return Effect("gate", reads=(1, 2), writes=(0,),
                      adjoint_of="twin:cqrt_qram_store_" + m.group(1))
    if re.fullmatch(rf"cqrt_qram_store_{_W}_controlled", sym):
        return Effect("gate", reads=(0, 2, 3), writes=(1,), controls=(0,),
                      adjoint_of="twin:" + sym + "_unc")
    m = re.fullmatch(rf"cqrt_qram_store_({_W})_controlled_unc", sym)
    if m:
        return Effect("gate", reads=(0, 2, 3), writes=(1,), controls=(0,),
                      adjoint_of="twin:cqrt_qram_store_" + m.group(1) +
                                 "_controlled")

    # ---- generated templates + specialization clones ----------------------
    # Grammar (TemplateName.h):
    #   cq_template_<op>[_<pred>]_<W>[_<from>_to_<to>][_hl|_lh][_controlled][_inv]
    #   cq_template_..._unc(out, forward operands...)      (DD-3 slot order)
    #   @__cq_q_<callee>_<mask>[_unc]                      (Phase 6.3)
    if sym.startswith("cq_template_") or sym.startswith("__cq_q_"):
        ctl = (0,) if "_controlled" in sym else ()
        if sym.endswith("_unc"):
            # `_unc` puts `out` in slot 0 (DD-3), so a control — if a
            # `..._controlled_unc` twin is ever generated; none exists today,
            # EmitInverse declines controlled callees — shifts to slot 1.
            return Effect("unc", reads=ALL_BUT_0, writes=(0,),
                          base=sym[: -len("_unc")],
                          controls=(1,) if ctl else ())
        if sym.endswith("_inv"):
            # The 8txa gap: writes a rail the signature does not name.
            return Effect("inv", reads=ALL, mints="implicit-rail",
                          base=sym[: -len("_inv")], controls=ctl)
        return Effect("mint", reads=ALL, mints="rail", controls=ctl)

    if sym.startswith("cqrt_"):
        raise Unclassified(sym)
    return None  # @llvm.*, @printf, user classical calls — not our surface.


# ---------------------------------------------------------------------------
# Parsing. LLVM prints one instruction per line, so a line-oriented parser is
# exact; nothing below depends on formatting the verifier does not guarantee
# (Rule 9 — no assumptions about naming or ordering, only about the textual
# grammar `llvm-as` accepts).
# ---------------------------------------------------------------------------

DEFINE_RE = re.compile(r"^define\s.*?@([\w.$\"]+)\s*\(")
LABEL_RE = re.compile(r"^([-\w.$]+):")
CALL_RE = re.compile(
    r"^\s*(?:(%[-\w.$]+|%\"[^\"]+\")\s*=\s*)?"
    r"(?:tail\s+|musttail\s+|notail\s+)?call\s+"
    r"(?:[^@()]|\([^()]*\))*?@([\w.$]+|\"[^\"]+\")\s*\(")
TERM_BR_UNCOND_RE = re.compile(r"^\s*br\s+label\s+%([-\w.$]+)\s*$")
TERM_TARGETS_RE = re.compile(r"label\s+%([-\w.$]+)")
TERM_START_RE = re.compile(r"^\s*(br|switch|ret|unreachable|indirectbr|resume)\b")


def split_args(s):
    """Split a call's argument text on top-level commas."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([{<":
            depth += 1
        elif ch in ")]}>":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def norm_arg(a):
    """Canonical `(type, value)` for one argument, parameter attributes dropped.

    `i32 noundef %0` -> `i32 %0`; `x86_fp80 0xK3FFE…` -> unchanged. Every symbol
    in the runtime ABI takes scalars only (i32 handles plus classical literals),
    so first-token/last-token is an exact normalisation here — and it is the
    reason a `noundef` on a Phase-6 clone argument does not desynchronise a
    forward from its `_unc` twin.
    """
    toks = a.split()
    if not toks:
        return a
    return toks[0] + " " + toks[-1]


def arg_value(a):
    toks = a.split()
    return toks[-1] if toks else a


class Call:
    __slots__ = ("line", "res", "sym", "args", "vals", "eff", "text",
                 "ctl_parity")

    def __init__(self, line, res, sym, args, text):
        self.line = line
        self.res = res
        self.sym = sym
        self.args = [norm_arg(a) for a in args]
        self.vals = [arg_value(a) for a in args]
        self.eff = None
        self.text = text
        self.ctl_parity = ()   # per-control `cqrt_x` parity at this position

    def key(self):
        return (self.sym, tuple(self.args))

    def read_idx(self):
        r = self.eff.reads
        if r == ALL:
            return range(len(self.vals))
        if r == ALL_BUT_0:
            return range(1, len(self.vals))
        return r


class Function:
    def __init__(self, name, params=()):
        self.name = name
        # The formal parameters. A parameter's value ARRIVES FROM OUTSIDE this
        # function, so nothing here can show it is (or is not) a rail handle —
        # a Phase-6 clone's quantum arguments are rails, a loop trip count is
        # classical, and both spell `%0`. `provably_not_a_rail` therefore
        # refuses to vouch for one, the same way `U3-extern-rail` refuses to
        # vouch for a handle minted outside the stream.
        self.params = frozenset(params)
        self.blocks = []          # [(label, [Call], terminator_text)]
        self.succ = {}
        self.preds = {}


def balanced(s, open_at):
    """Index of the `)` matching the `(` at `open_at` (or len(s) if unbalanced)."""
    depth = 0
    for j in range(open_at, len(s)):
        if s[j] == "(":
            depth += 1
        elif s[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    return len(s)


SSA_NAME_RE = re.compile(r"%(?:\"[^\"]+\"|[-\w.$]+)")


def parse_module(text):
    """Split a .ll into functions -> basic blocks -> calls, with a CFG."""
    funcs = []
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        m = DEFINE_RE.match(lines[i])
        if not m:
            i += 1
            continue
        # Formal parameters: the balanced `(...)` the define's `@name` opens.
        popen = lines[i].index("(", m.end() - 1)
        params = SSA_NAME_RE.findall(lines[i][popen + 1:balanced(lines[i],
                                                                 popen)])
        fn = Function(m.group(1), params)
        i += 1
        cur_label, cur_calls, term = "%__entry", [], ""
        while i < len(lines) and lines[i] != "}":
            ln = lines[i]
            lm = LABEL_RE.match(ln)
            if lm:
                fn.blocks.append((cur_label, cur_calls, term))
                cur_label, cur_calls, term = "%" + lm.group(1), [], ""
                i += 1
                continue
            cm = CALL_RE.match(ln)
            if cm:
                open_at = ln.index("(", cm.end() - 1)
                j = balanced(ln, open_at)
                cur_calls.append(Call(i + 1, cm.group(1),
                                      cm.group(2).strip('"'),
                                      split_args(ln[open_at + 1:j]), ln.strip()))
            elif TERM_START_RE.match(ln):
                term = ln.strip()
            i += 1
        fn.blocks.append((cur_label, cur_calls, term))
        for label, _, t in fn.blocks:
            tgt = ["%" + x for x in TERM_TARGETS_RE.findall(t)]
            fn.succ[label] = tgt
            fn.preds.setdefault(label, [])
            for x in tgt:
                fn.preds.setdefault(x, []).append(label)
        funcs.append(fn)
        i += 1
    return funcs


def build_streams(fn):
    """Maximal single-pred / unconditional-successor block chains.

    A stream is a run of blocks whose concatenation is a valid linear execution
    trace: Bi ends in `br label %B(i+1)` and B(i+1)'s only predecessor is Bi.
    """
    by_label = {lbl: (lbl, calls, t) for lbl, calls, t in fn.blocks}
    consumed, streams = set(), []
    for lbl, calls, t in fn.blocks:
        if lbl in consumed:
            continue
        chain, cur_t = [lbl], t
        consumed.add(lbl)
        while True:
            m = TERM_BR_UNCOND_RE.match("  " + cur_t)
            if not m:
                break
            nxt = "%" + m.group(1)
            if nxt in consumed or nxt not in by_label:
                break
            if fn.preds.get(nxt, []) != [chain[-1]]:
                break
            chain.append(nxt)
            consumed.add(nxt)
            cur_t = by_label[nxt][2]
        merged = []
        for c in chain:
            merged.extend(by_label[c][1])
        streams.append((chain, merged))
    return streams


# ---------------------------------------------------------------------------
# Numeric helpers for adjoint matching (negated angle / negated immediate).
# ---------------------------------------------------------------------------

def _hex_neg(a, b):
    """LLVM hex float payloads: negation flips the SIGN (top) bit.

    Covers `0x…` (double, 16 nibbles), `0xK…` (x86_fp80, 20), `0xL…` (fp128,
    32) and `0xH…` (half, 4) — for each the sign bit is bit 4*len-1. `0xM…`
    (ppc_fp128) is deliberately NOT handled: its negation touches both member
    doubles, so falling through to "not a negation" over-declines, which is the
    safe direction. The pass emits `cqrt_rz`/`cqrt_ry` angles as `double`, so
    this path only fires on hand-written or future wider-angle IR.
    """
    for pre in ("0xK", "0xL", "0xH", "0x"):
        if a.startswith(pre) and b.startswith(pre):
            ha, hb = a[len(pre):], b[len(pre):]
            if len(ha) != len(hb) or not ha or not hb:
                return False
            try:
                va, vb = int(ha, 16), int(hb, 16)
            except ValueError:
                return False
            return va ^ vb == 1 << (4 * len(ha) - 1)
    return False


def is_negation(a, b):
    """True iff argument strings `a` and `b` are `x` and `-x`."""
    ta, tb = a.split(), b.split()
    if len(ta) < 2 or len(tb) < 2 or ta[:-1] != tb[:-1]:
        return False
    va, vb = ta[-1], tb[-1]
    if va.lower().startswith("0x") or vb.lower().startswith("0x"):
        return _hex_neg(va, vb)
    try:
        na = float(va) if ("." in va or "e" in va.lower()) else int(va)
        nb = float(vb) if ("." in vb or "e" in vb.lower()) else int(vb)
    except ValueError:
        return False
    return na == -nb


# ---------------------------------------------------------------------------
# The analysis.
# ---------------------------------------------------------------------------

VIOLATION = "VIOLATION"    # demonstrably unsound
UNPROVEN = "UNPROVEN"      # outside the structural proofs this file can give


class Finding:
    def __init__(self, kind, code, fn, line, handle, msg):
        self.kind, self.code, self.fn = kind, code, fn
        self.line, self.handle, self.msg = line, handle, msg
        self.path = None

    def __str__(self):
        return (f"{self.kind} {self.code} @{self.fn} line {self.line} "
                f"[{self.handle}]: {self.msg}")


class Rail:
    __slots__ = ("name", "kind", "origin", "pos", "writes", "measured_at")

    def __init__(self, name, kind, origin, pos):
        self.name, self.kind = name, kind
        self.origin, self.pos = origin, pos
        self.writes = []          # [(pos, Call)] basis-changing, stream order
        self.measured_at = None


class StreamAnalysis:
    def __init__(self, fnname, calls, module_defs, params=frozenset(),
                 rail_defs=frozenset(), recorded=frozenset()):
        self.fnname, self.calls = fnname, calls
        self.module_defs = module_defs
        self.params = params
        self.rail_defs = rail_defs
        # bd 261j(b): names correlated with a kept output carrier. Computed per
        # FUNCTION, not per stream — a tape write in another block still records.
        self.recorded = recorded
        self.rails = {}
        self.findings = []
        self.verified = self.sites = 0
        self.inv_pool = {}          # (base, args) -> [positions]
        self.inv_claimed = set()
        self.freed_names = {}
        self.x_parity = {}          # register -> `cqrt_x` count so far
        self._stable_memo = {}      # (name, lo, hi) -> (ok, witness lines)
        self._stable_active = set()  # queries currently being computed
        self._co_written_memo = {}   # (v, p1, p2, id(c1)) -> (ok, lines)
        self._co_written_active = set()   # ditto, the co-written edge
        self.proofs = []            # [(fn, free_line, handle, rule, lines)]

    # -- main scan ---------------------------------------------------------
    def run(self):
        for pos, c in enumerate(self.calls):
            c.eff = classify(c.sym)
            if c.eff is None:
                continue
            e = c.eff
            self.check_case_b(c)
            for v in c.vals:
                if v in self.freed_names and e.role != "free":
                    self.add(VIOLATION, "F2-use-after-free", c, v,
                             f"@{c.sym} uses {v} after its cqrt_free at line "
                             f"{self.freed_names[v]}")
            # Parity MOD 2 — `cqrt_x` is an involution, so only the polarity of
            # the flag at this point matters, not how many brackets deep it is.
            c.ctl_parity = tuple(self.x_parity.get(c.vals[i], 0) & 1
                                 for i in e.controls if i < len(c.vals))
            if e.role == "free":
                self.sites += 1
                self.check_free(pos, c)
                continue
            if e.role == "measure":
                r = self.rail_for_write(c.vals[0])
                r.measured_at = c.line
                r.writes.append((pos, c))
                continue
            if e.role == "inv":
                self.inv_pool.setdefault((e.base, tuple(c.args)),
                                         []).append(pos)
            if not e.diagonal:
                for wi in e.writes:
                    if wi < len(c.vals):
                        self.rail_for_write(c.vals[wi]).writes.append((pos, c))
            if c.sym == "cqrt_x":
                self.x_parity[c.vals[0]] = self.x_parity.get(c.vals[0], 0) + 1
            if e.mints in ("rail", "array", "tape") and c.res:
                kind = {"rail": "alloc" if e.role == "alloc" else "mint",
                        "array": "array", "tape": "tape"}[e.mints]
                self.rails[c.res] = Rail(c.res, kind, c, pos)
        self.check_orphan_reversals()
        return self.findings

    def check_orphan_reversals(self):
        """Every `_inv` must be claimed by exactly one free.

        An `_inv` is the ONLY construct in the lowered IR that writes a rail the
        signature does not name (bd 8txa). If no free claims it, this file
        cannot say which rail it acted on — and one extra adjoint on a live rail
        is precisely the "re-paired to the wrong rail" defect the golden trace
        is blind to. The old "exact bijection (25174 tokens, 25174 claims)"
        justification is STALE — since bd 8txa moved the compare flag-strip to
        the void `_unc` ABI the ship floor emits ZERO `_inv` (re-measured over
        all 237 slices, 2026-07-31), so this rule's only live exercise is the
        selftest's historical fixtures. It stays because an `_inv` is still the
        one construct whose target the ABI does not name, so if one ever comes
        back an orphan is still a real signal — but do not cite corpus counts
        for it.
        """
        for positions in self.inv_pool.values():
            for p in positions:
                if p in self.inv_claimed:
                    continue
                c = self.calls[p]
                self.add(UNPROVEN, "W-orphan-reversal", c, c.res or "<void>",
                         f"`{c.text}` is an adjoint no cqrt_free claims; the "
                         f"ABI does not name the rail it acts on, so the rail "
                         f"it left (or dirtied) cannot be identified here")

    def rail_for_write(self, name):
        """The record for `name`, materialising an EXTERN one if it was not
        minted in this stream.

        Without this, a write to a handle that arrives from outside (a Phase-6
        clone parameter, a rail defined in another block) would be INVISIBLE:
        `unchanged_over` would report the operand stable because it had no
        recorded writes, and `commutes` would take a flag's `cqrt_x` parity at
        face value while a `cnot` retargeted it. Recording externs makes the
        in-stream write history complete for every handle, so "no writes here"
        is a fact rather than an absence of information. An extern rail can
        never be VERIFIED at a free (its incoming state is unknown) — see
        `U3-extern-rail`.
        """
        r = self.rails.get(name)
        if r is None:
            r = Rail(name, "extern", None, -1)
            self.rails[name] = r
        return r

    # -- CASE B: an input aliasing a rail the gate WRITES (bd 6f3o) ---------
    def check_case_b(self, c):
        """No emitted gate may take an INPUT that aliases a rail it WRITES.

        THE PARTITION (bd big5, 2026-07-31). For every gate, ask: can an input
        operand alias a rail the gate writes?

          CASE A — aliased INPUTS, output a FRESH |0> rail. REALIZABLE, ALLOWED,
            and NOT flagged here. The generated ABI's out-of-place shape is
            `out(fresh |0>) ^= f(in0, in1)`; aliasing the inputs gives
            `out ^= g(a)` with `g(a) := f(a, a)`, and `(a, 0) -> (a, g(a))` is
            the standard reversible embedding of a classical function — unitary
            for ANY `g`. Realizability is UNCONDITIONAL; aliasing only changes
            COST. 638 calls across 27 template symbols in the committed corpus
            are Case A (`cq_template_fmul_f64(%h, %h)` — `x*x`), plus 15 `_unc`
            twins of the same shape (`..._unc(%out, %h, %h)`). All must stay
            silent, so this rule compares inputs against WRITE slots only and
            never against each other.

          CASE B — an input aliases a rail the gate WRITES. NOT realizable:
            `cswap(c, c, b)` sends both |1,0> and |0,1> to |0,1>, which is not
            injective, so no unitary implements it. A backend handed one cannot
            honour the ABI contract.

        The write-sets come straight from `classify()`, so this rule needs no
        per-symbol table of its own: `cnot(c,t)` writes t, `toffoli(c1,c2,t)`
        writes t (c1==c2 is Case A), `cswap(f,x,y)` writes BOTH x and y,
        `copy_W(s,d)` writes d, `copy_W_controlled(f,s,d)` writes d, a
        `cq_template_OP_W(ins)` writes NO operand (it returns a fresh rail), and
        `cq_template_OP_W_unc(out, ins...)` writes out.

        A `classical_imm` slot counts as an input here: `xorc(%r, %r)` is Case B
        exactly as `cnot(%r, %r)` is. A slot that is BOTH read and written by
        the same index is an in-place update, not aliasing (`measure(%h)`), and
        is skipped.
        """
        e = c.eff
        ws = [i for i in e.writes if i < len(c.vals)]
        if not ws:
            return
        ins = sorted(set(c.read_idx()) | set(e.classical_imm))
        for wi in ws:
            for ri in ins:
                if ri == wi or ri >= len(c.vals) or c.vals[ri] != c.vals[wi]:
                    continue
                self.add(VIOLATION, "B1-input-aliases-written-rail", c,
                         c.vals[wi],
                         f"`{c.text}`: operand {ri} aliases operand {wi}, a "
                         f"rail @{c.sym} WRITES. An input that aliases a "
                         f"written rail is CASE B — not a unitary, so no "
                         f"backend can realize it (aliased inputs with a fresh "
                         f"output, CASE A, are allowed and not flagged)")
                return
        for a in range(len(ws)):
            for b in range(a + 1, len(ws)):
                if c.vals[ws[a]] != c.vals[ws[b]]:
                    continue
                self.add(VIOLATION, "B2-two-written-rails-alias", c,
                         c.vals[ws[a]],
                         f"`{c.text}`: operands {ws[a]} and {ws[b]} are the "
                         f"same rail and @{c.sym} WRITES BOTH through two "
                         f"distinct write ports, which is outside the ABI "
                         f"contract. Note this one is DEGENERATE rather than "
                         f"non-unitary — `cqrt_cswap(f,x,x)` is the identity, "
                         f"so it is flagged because no router can have meant "
                         f"it, not because no backend could run it")
                return

    # -- is an operand PROVABLY not a rail handle? (bd az1h) ---------------
    def provably_not_a_rail(self, v):
        """Can this file SHOW that operand value `v` is not a rail handle?

        `classify()` records which slots the ABI DECLARES classical, but a
        declaration is not an observation: at W=i32 both `cqrt_xorc_i32`
        parameters are `int32_t`, so a rail handle in the immediate slot is
        verify-clean, and the runtime prints that slot as a bare decimal, so
        the golden trace renders rail h13 as `13`. Both gates are blind on the
        SAME axis — which is why the assertion has to be CHECKED here.

        Answers True only on positive evidence:
          * a literal constant (no `%`), or
          * an SSA value that is neither a known rail nor a formal parameter
            nor the result of a rail-minting call anywhere in THIS FUNCTION —
            i.e. a plain classical instruction result (`sub i32 0, %k`, a
            counter `phi`) or a classical call result such as `cqrt_measure_W`.

        MEASURED over the 237-slice corpus: there are 41 `addc`/`xorc` CALL
        sites (11 with a literal immediate, 30 with an SSA one), and every one
        of the 30 is non-call integer arithmetic — `sub i32 0, %6`,
        `add i32 %cq.brk.ip, 1`, `add nuw nsw i32 %5, 1`. So this answers True
        for all 41 and rule (A1) still discharges every site it used to. (41,
        not 52: a `grep -c` over the corpus also counts the 11 `declare`
        prototype lines. And no immediate is a `phi` DIRECTLY — the loop
        counter reaches the slot through an `add` — so do not cite one.)
        """
        if not v.startswith("%"):
            return True                              # a literal constant
        if v in self.rails:
            return False       # minted in, or written in, this stream
        if v in self.params:
            return False       # arrives from outside; nothing here can vouch
        if v in self.rail_defs:
            return False       # rail minted elsewhere in this function
        return True

    def add(self, kind, code, c, handle, msg):
        self.findings.append(Finding(kind, code, self.fnname, c.line, handle,
                                     msg))

    def ok(self, c, r, rule, witness):
        """Record a discharged free site plus the lines its proof consumed."""
        self.verified += 1
        self.proofs.append((self.fnname, c.line, r.name, rule,
                            tuple(sorted(set(witness) | {r.origin.line}))))

    # -- reduction to identity --------------------------------------------
    def writes_in(self, name, lo, hi):
        r = self.rails.get(name)
        return [] if r is None else [(p, c) for p, c in r.writes if lo < p < hi]

    def unchanged_over(self, name, lo, hi):
        """(ok, witness lines) — `name`'s writes inside (lo, hi) net to identity.

        WHY THERE IS NO STEP BUDGET HERE (bd test_C_libtooling-xzdx, 2026-08-08)
        ----------------------------------------------------------------------
        There used to be a `depth > 6` cut-off, and it was UNSOUND IN THE MEMO:
        the cache is keyed `(name, lo, hi)` with no depth, so a `False` produced
        by exhausting the budget deep in one recursion was served to a later,
        SHALLOWER query for the same interval that had budget to succeed. The
        cache outlives the free site (it is per STREAM), so the artefact leaked
        ACROSS free sites and the verdict came to depend on the ORDER of the
        `cqrt_free` calls. The committed demonstration is the two PAIRS in
        `ORDER_PAIRS`, tools/free_pairing_check_test.py — `xzdx-ladder-d7-*`
        (16 gates, hand-explainable) and `xzdx-ticket-repro-*` (the ticket's own
        18-gate find). Each pair is one stream written twice with the frees in a
        different order, and the OLD tool split both: 0 of 2 sites verified
        versus 2 of 2 for the ladder, 1 of 3 versus 3 of 3 for the repro.
        The direction was always safe (an artefact `False` over-declines, never
        over-accepts), but a GATE whose answer moves when you move a free is a
        gate people route around with `--allow-unproven`.

        The budget is gone rather than depth-keyed because IT WAS NEVER WHAT
        MADE THIS TERMINATE. Every recursive edge STRICTLY NARROWS the position
        interval, and the interval is a pair of integer stream positions:

          * `unchanged_over(name, lo, hi)` reduces exactly `writes_in(name, lo,
            hi)`, whose members satisfy `lo < p < hi` STRICTLY (the comparison
            is `lo < p < hi`, not `<=`), and it does not widen `(lo, hi)`.
          * `reduces_to_identity` only ever pairs two of those members, an
            earlier `pk` against a later `p`, so any interval it hands down
            satisfies `lo < pk < p < hi`.
          * `pair_operands_unchanged(c1, p1, p2, …)` passes exactly that pair
            down, to `unchanged_over(v, p1, p2)` and to `co_written_stable(v,
            p1, p2, …)`, and the latter reduces a SUBSET of `writes_in(v, p1,
            p2)` — the complementary-branch filter only removes members.
          * the three entry points (`check_alloc_rail`, the two in
            `check_minted_rail`) start from a write list drawn the same strict
            way, from `(r.pos, pos)` or `(r.pos, rev_pos)`.

        So `hi - lo` strictly decreases by at least 2 down every edge and is
        bounded below by 0: the recursion is at most `(hi - lo) / 2` deep and
        cannot cycle, for ANY input, including adversarial ones. MEASURED on the
        237-slice corpus (2026-08-08): the recursion nests at most 3 frames deep
        — a `depth` of 2 in the units the deleted budget used, which refused at 7
        — so the budget was hit ZERO times and removing it moves no corpus
        verdict; it only removes the artefact.

        The re-entry check below is therefore an ASSERTION on that argument, not
        a policy: it can only fire if a future edit adds an edge that does not
        narrow, and it fires LOUDLY (see `NarrowingViolation`) instead of
        answering `False`, because a silent `False` is the very bug this
        replaced. It has never fired. `co_written_stable` carries the SAME
        assertion, and it has to: they are the two edges that re-enter the
        reduction, and a tripwire on only one of them is not a tripwire. That
        is not a hypothetical — it is what the arm-kill probe found. Weakening
        `writes_in` to `lo <= p <= hi` (the one-character way to break
        narrowing) sends every committed fixture carrying a `cqrt_cswap` route
        pair — 7 of the 19 as of 2026-08-08, a count that moves whenever
        fixtures are added — into an unbounded `co_written_stable` recursion
        that this check never sees, so with the guard here alone they died on
        `RecursionError` instead of naming the broken invariant. The converse
        also holds and is why BOTH assertions ship: that same probe leaves the
        guard here untouched, and killing it needs the committed mutant
        `case-b-self-aliased-cnot-aka-the-narrowing-probe` (no `cqrt_cswap`, so
        the co-written edge is never entered).

        THE MEMO IS SOUND WITH NO DEPTH IN THE KEY, which is the other half of
        the fix, and the cache is shared by every free site in the stream — so
        "pure function of `(name, lo, hi)`" has to hold ACROSS sites, while
        `run()` is still mutating the analysis between them. Three reads matter:

          * `Rail.writes`, via `writes_in`. Every query a free site at position
            `pos` issues has `hi <= pos`, and `run()` appends a write when it
            scans it, so every write inside `(lo, hi)` was already appended
            before that site was checked. The list can grow later; the WINDOW
            cannot gain a member.
          * `self.rails` MEMBERSHIP, which the scan grows — `rail_for_write`
            materialises an extern record the first time a handle is written.
            `pair_operands_unchanged` skips an operand that is `not in
            self.rails`, but an operand with a write inside `(p1, p2)` was
            written before `p2 < hi <= pos`, so it is already present whenever
            skipping it could change anything; a later-materialised record can
            only carry writes at positions past the window.
          * the same membership test inside `commutes`, which is the one that
            looks dangerous — it declines the complementary-branch claim for a
            register this stream has never seen, and a later site might have
            seen it. It CANNOT differ, because the test is only reached after
            `pu != pw`, and a `ctl_parity` that differs means `run()` scanned a
            `cqrt_x` on that register between the two calls; `cqrt_x` is
            non-diagonal with `writes=(0,)` and is the only thing that bumps
            `x_parity`, so that same scan step already put the register in
            `self.rails`. MEASURED over the 237 slices: 30 `commutes` calls, 6
            of them with differing parity, the membership guard fired 0 times.

        Nothing else the scan mutates (`inv_claimed`, `freed_names`, `x_parity`
        itself) is read on this path — `ctl_parity` is frozen onto each Call at
        scan time. And the whole argument has a cheap standing check: the memo
        is a PURE CACHE, so deleting its writes must not move a verdict, which
        is measured (all 237 slices byte-identical) rather than asserted.
        """
        memo = (name, lo, hi)
        hit = self._stable_memo.get(memo)
        if hit is not None:
            return hit
        if memo in self._stable_active:
            raise NarrowingViolation(memo)
        self._stable_active.add(memo)
        try:
            ok, _why, lines = self.reduces_to_identity(
                self.writes_in(name, lo, hi), name)
        finally:
            self._stable_active.discard(memo)
        self._stable_memo[memo] = (ok, lines)
        return ok, lines

    def adjoint_matches(self, a, b):
        """Is call `b` the adjoint of call `a` on their shared target rail?"""
        ea = a.eff
        if ea is None or ea.adjoint_of is None:
            return False
        if a.ctl_parity != b.ctl_parity:
            # Same gate on the complementary branch of its flag is NOT the
            # adjoint — composing the two is a full, unconditional operation.
            return False
        if ea.adjoint_of == "self":
            return a.key() == b.key()
        if ea.adjoint_of.startswith("negate-arg"):
            n = int(ea.adjoint_of[len("negate-arg"):])
            if a.sym != b.sym or len(a.args) != len(b.args):
                return False
            for i, (x, y) in enumerate(zip(a.args, b.args)):
                if i == n:
                    if not is_negation(x, y):
                        return False
                elif x != y:
                    return False
            return True
        if ea.adjoint_of.startswith("twin:"):
            want = ea.adjoint_of[len("twin:"):]
            return b.sym == want and a.vals == b.vals
        return False

    def commutes(self, u_pos, u, w_pos, w):
        """Do writes `u` and `w` to the same rail provably commute?

        The ONLY admitted reason is complementary flag branches: they share a
        control register whose `cqrt_x` parity differs, and that register is
        touched by nothing but `cqrt_x` between them — so the two writes act on
        disjoint branches of the same flag. This is exactly the `¬flag`
        `cqrt_x` bracket the two-arm phase emitter emits (§5b).
        """
        ucs = [(u.vals[i], p) for i, p in zip(u.eff.controls, u.ctl_parity)
               if i < len(u.vals)]
        wcs = [(w.vals[i], p) for i, p in zip(w.eff.controls, w.ctl_parity)
               if i < len(w.vals)]
        for reg, pu in ucs:
            for reg2, pw in wcs:
                if reg != reg2 or pu == pw:
                    continue
                if reg not in self.rails:
                    # Never minted and never written here: its write history is
                    # not this stream's to know, so the parity claim is not
                    # backed by anything. Decline rather than assume.
                    continue
                lo, hi = min(u_pos, w_pos), max(u_pos, w_pos)
                if all(c.sym == "cqrt_x"
                       for _, c in self.writes_in(reg, lo, hi)):
                    return True
        return False

    def reduces_to_identity(self, ws, target):
        """Cancel adjoint pairs in `target`'s write history (commutation-aware).

        `ws` is the write history of the rail `target` over some interval, and
        the question is whether those writes net to identity ON THAT RAIL.
        Threading `target` through is not cosmetic (bd 0gwl):
        `pair_operands_unchanged` needs it to tell a gate's CO-WRITTEN slots —
        which must hold still for the pair to cancel — from the slot carrying
        `target` itself, whose stability is the very thing this reduction is
        establishing and so cannot be an input to it.

        Returns (ok, reason, witness) where `witness` is the set of SOURCE LINES
        the proof consumed — every cancelled pair, plus the pairs any nested
        operand-stability check consumed. Deleting or corrupting any witness
        line must break this proof; that is what the mutant self-test targets.
        """
        stack, witness, rejects = [], set(), []
        for p, c in ws:
            placed = False
            for k in range(len(stack) - 1, -1, -1):
                pk, ck = stack[k]
                if not self.adjoint_matches(ck, c):
                    continue
                if not all(self.commutes(stack[m][0], stack[m][1], p, c)
                           for m in range(k + 1, len(stack))):
                    continue
                ok, sub, blocker = self.pair_operands_unchanged(
                    ck, pk, p, target)
                if not ok:
                    rejects.append((ck, c, blocker))
                    continue
                witness |= sub
                witness.add(ck.line)
                witness.add(c.line)
                del stack[k]
                placed = True
                break
            if not placed:
                stack.append((p, c))
        if not stack:
            return True, None, frozenset(witness)
        p, c = stack[0]
        why = (f"write `{c.text}` (line {c.line}) is never cancelled "
               f"by an adjoint ({len(stack)} of {len(ws)} write(s) "
               f"left after reduction)")
        if rejects:
            # A matched adjoint that was REJECTED is a much sharper signal than
            # "nothing cancelled it" — it says the shape was right and an
            # operand moved. Name it, or the ticket's own repro reports only
            # that two identical `cqrt_cswap`s failed to pair, which reads as a
            # checker bug rather than as the dirty rail it is.
            ck, cc, blocker = rejects[-1]
            why += (f"; the nearest adjoint candidate (lines {ck.line} and "
                    f"{cc.line}) matched but was REJECTED because operand "
                    f"{blocker} is not provably unchanged between them")
        return False, why, frozenset(witness)

    def pair_operands_unchanged(self, c1, p1, p2, target):
        """(ok, witness, blocker) — everything `c1`'s action depends on holds.

        THREE operand classes, each here for a reason that was once a hole:

        * the DECLARED READS. A gate acts on its live sources; if one moved in
          between, the second call is not the adjoint of the first.
        * the `classical_imm` slots (bd az1h). They are not in `reads` because
          the ABI calls them literals, and for a genuine literal the check
          costs nothing (a constant is not in `self.rails`, so it is skipped).
          It matters when the slot holds a RAIL: `xorc(%r,%h); ry(%h,0.75);
          xorc(%r,%h)` cancels on `key()` equality alone otherwise, because
          xorc declares reads=() and this loop would inspect ZERO operands — so
          (A2) would VERIFY the very entangle-then-free that (A1) had just
          declined, and the az1h hole would simply move one rule down. Found by
          the bd az1h/6f3o Rule-13 round, 2026-07-31, in the fall-through that
          change itself introduced.
        * the CO-WRITTEN slots (bd 0gwl, 2026-08-07) — every write slot except
          the one carrying `target`. `cqrt_cswap` is the ONLY symbol in the
          model that writes two rails (`reads=(0,) writes=(1,2)`; a Fredkin
          physically reads all three), so this loop used to iterate `read_idx()`
          alone and never look at the other routed register:

              %rail = cqrt_alloc_i32(0)
              cqrt_cswap(%f, %q, %rail)   ; f=1 routes psi onto %rail, |0> onto %q
              cqrt_ry_i32(%q, 0.75)       ; dirties %q
              cqrt_cswap(%f, %q, %rail)   ; route-back parks Ry(0.75)|0> on %rail
              cqrt_free(%rail)            ; hidden measurement — NOT |0>

          reported `OK … via A2-reduces-to-identity`, exit 0, because the pair
          matched on `key()` equality and only `%f`'s stability was ever
          checked. The slot carrying `target` is skipped because proving it is
          this reduction's job, not its premise.

        `blocker` is the operand that failed, for the diagnostic; None on ok.
        """
        acc = set()
        for ri in list(c1.read_idx()) + list(c1.eff.classical_imm):
            if ri >= len(c1.vals):
                continue
            v = c1.vals[ri]
            if v not in self.rails:
                continue
            ok, lines = self.unchanged_over(v, p1, p2)
            if not ok:
                return False, frozenset(), v
            acc |= lines
        for wi in c1.eff.writes:
            if wi >= len(c1.vals):
                continue
            v = c1.vals[wi]
            # `v == target` is the rail under reduction, and skipping it is
            # what keeps this loop from taking its own conclusion as a premise.
            # It is also why single-write gates add NO work here: their one
            # write slot IS the target, so only `cqrt_cswap` ever reaches
            # `co_written_stable` at all.
            #
            # THIS CLAUSE IS AN ACCEPT-ARM. It is kill-tested by the POSITIVE
            # fixture `a2-co-written-target-skip`, which the checker VERIFIES
            # today and reports `A-not-reducible` on with only this clause
            # removed — a false decline of a stream whose net unitary is the
            # identity on the freed rail in BOTH branches of its flag.
            #
            # An earlier draft of this comment claimed the clause was
            # "redundant by construction … no input can distinguish the two",
            # on the argument that a target write inside (p1, p2) either already
            # cancelled or survived onto the stack, where `reduces_to_identity`
            # made it COMMUTE with the pair using "the same `commutes` call"
            # this path would use. THAT ARGUMENT IS FALSE, on both horns, and
            # the bd 0gwl Rule-13 round refuted it with a runnable input:
            #   * IT IS NOT THE SAME CALL. The caller filters with
            #     `commutes(stack[m], …, p, c)` — interval (w, p2), against the
            #     LATER pair member. This path would filter with
            #     `commutes(p, w, p1, c1)` — interval (p1, w), against the
            #     EARLIER one. `adjoint_matches` does force the two calls to
            #     agree on control register and `cqrt_x` parity, but NOT on the
            #     interval, and `commutes` demands the flag see nothing but
            #     `cqrt_x` inside its own interval. One non-`cqrt_x` write on
            #     the flag placed on a single side of `w` makes the two answers
            #     differ.
            #   * THE OTHER HORN LEAKS TOO. A target write inside (p1, p2) whose
            #     adjoint partner lies OUTSIDE that window cannot cancel in a
            #     reduction restricted to `writes_in(target, p1, p2)`, however
            #     the caller disposed of it.
            # The lesson is the file's own (bd az1h): a model's assertion about
            # itself is not an observation. Both statements were checkable and
            # neither had been checked — the corpus being unmoved (still true:
            # dropping the clause moves 0 of 237 slices) says only that the
            # corpus does not contain the distinguishing shape.
            #
            # DIRECTION, so the risk is not overstated: re-deriving the target's
            # stability here can only ADD declines, never accept more. So the
            # clause guards against a FALSE POSITIVE — which for a gate wired
            # into every e2e slice means failing a CORRECT build.
            if v == target or v not in self.rails:
                continue
            ok, lines = self.co_written_stable(v, p1, p2, c1)
            if not ok:
                return False, frozenset(), v
            acc |= lines
        return True, frozenset(acc), None

    def co_written_stable(self, v, p1, p2, c1):
        """(ok, witness) — co-written register `v` holds still across the pair.

        The same question `unchanged_over` answers, with ONE extra admission:
        an intervening write that provably acts on the COMPLEMENTARY BRANCH of
        a flag `c1` also acts on is dropped BEFORE the reduction runs. Two
        operators supported on complementary blocks of the same flag commute
        exactly, so on the branch where the pair acts such a write is the
        identity. That is not a new physics claim — it is the argument
        `commutes` already makes for the target rail's own intervening writes,
        applied to a co-written one.

        WITHOUT this admission the rule fails a CORRECT, REVIEWED build, which
        for a gate is the expensive direction. In
        `slice_control_phase_ifelse_derived` the shared base `%h` is routed
        `A B A B`: the `%rail` route-pair has the `%rail1` route sitting inside
        it, on the far side of the `¬flag cqrt_x` bracket. `%h` is genuinely
        written in between, and only complementary-branch commutation explains
        why the `%rail` pair still cancels. Pinned BOTH ways, by fixtures that
        EXIST — name them, never invent one (a docstring that cites a fixture
        nobody can grep sends the next maintainer to duplicate coverage that is
        already there): deleting this admission turns the POSITIVE
        `ifelse-derived-two-rail` red, and widening it into "any intervening
        write is fine" is caught by the MUTANTS
        `a2-cswap-route-pair-whose-co-written-rail-was-dirtied` and
        `t2-cswap-route-pair-whose-work-rail-was-dirtied`. All three live in
        tools/free_pairing_check_test.py.

        WHY `c1` ALONE IS CONSULTED, AND NOT THE PAIR. `adjoint_matches` forces
        both calls to carry the same control REGISTERS at the same `cqrt_x`
        PARITY (`self` compares whole keys; `negate-arg<N>` never negates a
        control slot, since no symbol with that adjoint has controls; `twin:`
        compares whole value lists). It does NOT make `commutes(…, c1)` and
        `commutes(…, c2)` agree, and an earlier draft of this docstring wrongly
        said it did: `commutes` also demands the flag see nothing but `cqrt_x`
        inside ITS OWN interval, and those intervals are (p1, w) and (w, p2) —
        disjoint. The bd 0gwl Rule-13 round built a stream where the two answers
        differ, so do not repeat that claim.

        The argument that actually holds splits the window in two, and the two
        halves are established by two DIFFERENT checks:
          * (p1, w) — this `commutes` call. Only `cqrt_x` touched the flag, so
            the flag at `w` really is `ctl_parity`'s complement of the flag at
            `p1`, and `w` therefore acts on the other branch from `c1`.
          * (w, p2) — NOT re-checked here, because `pair_operands_unchanged`
            has ALREADY run its READS loop before reaching this one, and that
            loop required `unchanged_over(flag, p1, p2)` over the whole window.
            So the flag at `p2` carries the same basis label as at `p1`, and
            `c2` acts on the same branch as `c1`.
        Together: the pair acts on one branch throughout and `w` on the other.
        That rests on the shared control register being one of `c1`'s READS,
        which holds for every symbol `classify()` models — controls ⊆ reads for
        cnot, toffoli, cswap, copy_controlled, qram_store_controlled and the
        `_controlled` template/`_unc` arms alike. A future symbol with a control
        outside its read-set would break this argument, so put it in `reads`.

        NO STEP BUDGET, for the reason `unchanged_over` gives at length: this
        edge narrows too (`writes_in(v, p1, p2)` is strict and the
        complementary-branch filter only DROPS members), so the recursion is
        bounded by the interval.

        IT IS MEMOISED, AND THAT IS NOT OPTIONAL — the bd xzdx Rule-13 round
        found this out the expensive way. The first draft of the budget removal
        left this edge uncached, on the argument that its population is tiny:
        `cqrt_cswap` is the only multi-write symbol in `classify()`, so nothing
        else reaches here at all, and the whole 237-slice corpus makes just 66
        calls here against 90671 to `unchanged_over`. That argument is the
        corpus-has-no-power error in its purest form. The deleted budget was the
        only thing bounding the WORK on this edge, and without a cache the cost
        is EXPONENTIAL: on a braid of cswap route pairs over one register pair
        with a distinct flag per level (`cswap(f0,A,B) … cswap(f19,A,B)
        cswap(f19,A,B) … cswap(f0,A,B)`), reducing A recurses into
        `co_written_stable(B, …)`, which recurses back into A over every
        strictly-inner window, and the same window is recomputed along
        4^(D/2) paths. MEASURED 2026-08-08 on a 63-CALL module: 0.017 s
        pre-change (which declined it on the budget), 44.6 s uncached, 0.008 s
        with this cache — and D=30 never finished. A build gate that a 66-line
        input can hang is a denial of service on every e2e slice.

        The key is `(v, p1, p2, id(c1))` and both the cache and the re-entry
        assertion use it. `c1` is in it by IDENTITY rather than by position
        because this file never needs `c1` to be the call AT `p1` and does not
        check that it is — keying on `(v, p1, p2)` alone would bake in an
        unstated assumption, and `commutes` reads `c1`. `id()` is safe here
        precisely because it is scoped to one `StreamAnalysis`: every `c1` that
        reaches this method is an element of `self.calls`, which outlives the
        analysis, so no id can be recycled underneath the table.
        """
        key = (v, p1, p2, id(c1))
        hit = self._co_written_memo.get(key)
        if hit is not None:
            return hit
        if key in self._co_written_active:
            raise NarrowingViolation(key[:3])
        self._co_written_active.add(key)
        try:
            ws = [(p, w) for p, w in self.writes_in(v, p1, p2)
                  if not self.commutes(p, w, p1, c1)]
            ok, _why, lines = self.reduces_to_identity(ws, v)
        finally:
            self._co_written_active.discard(key)
        self._co_written_memo[key] = (ok, lines)
        return ok, lines

    # -- the free site -----------------------------------------------------
    def check_free(self, pos, c):
        name = c.vals[0]
        if name in self.freed_names:
            self.add(VIOLATION, "F1-double-free", c, name,
                     f"already freed at line {self.freed_names[name]}")
            return
        self.freed_names[name] = c.line
        r = self.rails.get(name)
        if r is None:
            where = self.module_defs.get(name)
            if where is None:
                self.add(UNPROVEN, "U2-unknown-def", c, name,
                         "no definition for the freed handle in this function")
            else:
                self.add(UNPROVEN, "U1-cross-stream", c, name,
                         f"handle is defined by @{where[1]} outside this "
                         f"straight-line stream, so its write history is not "
                         f"linearly ordered here")
            return
        if r.kind == "extern":
            where = self.module_defs.get(name)
            self.add(UNPROVEN, "U3-extern-rail", c, name,
                     f"handle is written in this stream but minted outside it"
                     + (f" (by @{where[1]})" if where else "")
                     + "; its state on entry to this stream is unknown")
            return
        if r.kind in ("array", "tape"):
            self.add(VIOLATION, "F9-free-of-nonrail", c, name,
                     f"{r.kind} resource token minted by @{r.origin.sym} is "
                     f"not a rail and must never be freed")
            return
        if r.measured_at is not None:
            self.add(VIOLATION, "F8-free-after-measure", c, name,
                     f"handle was measured at line {r.measured_at}; the "
                     f"measurement owns it (Rule 6)")
            return
        # (R1) bd 261j(b) — RECORDED onto a kept carrier AND carrying a
        # non-diagonal rotation. Checked BEFORE the proof rules and independently
        # of them: all four rules that certified this defect ask only whether the
        # write history returns the rail to its alloc value, and `Ry(-t) Ry(t)`
        # does. What they cannot see is that the copy's DESTINATION is never
        # restored, so the rail is entangled with it and the reduction is no
        # longer a statement about the rail alone (Rules 5/6/8).
        rot = [w for p, w in r.writes
               if p < pos and w.sym.startswith("cqrt_ry_")]
        if rot and r.name in self.recorded:
            self.add(VIOLATION, "R1-recorded-rotated-rail", c, r.name,
                     f"rail carries `{rot[0].text if rot else '<rotation>'}` "
                     f"(line {rot[0].line if rot else 0}) and is "
                     f"transitively RECORDED onto a kept carrier (an output tape, or "
                     f"since bd 5pio a `cqrt_copy_<W>` destination), which the "
                     f"reversal never restores; it is therefore ENTANGLED with "
                     f"that carrier at the free, so `cqrt_free` is a hardware "
                     f"reset of an entangled rail = a hidden measurement (Rules "
                     f"5/6/8). `cqrt_ry` has no live condition to be a "
                     f"permutation OF, so no reverse-play returns it to |0>. "
                     f"Leave it allocated instead (the Rule-6 fallback). NOTE "
                     f"this fires REGARDLESS of the proof rules: each of them "
                     f"certifies this exact shape.")
            return
        if r.kind == "alloc":
            self.check_alloc_rail(pos, c, r)
        else:
            self.check_minted_rail(pos, c, r)

    def check_alloc_rail(self, pos, c, r):
        ws = [(p, w) for p, w in r.writes if r.pos < p < pos]
        # (A1) classical-immediate updates keep a known classical basis state —
        # but ONLY if the immediate really is classical. The ABI's word for it
        # is not evidence (bd az1h): a rail in that slot ENTANGLES the two, and
        # the free then collapses both. Check every declared immediate slot, and
        # fall through to (A2) when one cannot be shown classical — a genuine
        # `xorc(%r,%h); xorc(%r,%h)` bracket still reduces to identity there, so
        # this tightening declines nothing that (A2) can prove.
        dirty = [(w, i) for _, w in ws for i in w.eff.classical_imm
                 if i < len(w.vals) and not self.provably_not_a_rail(w.vals[i])]
        if ws and not dirty and all(w.eff.classical_imm for _, w in ws):
            self.ok(c, r, "A1-classical-immediate", {w.line for _, w in ws})
            return
        # (A2) otherwise the history must return the rail to its alloc value.
        ok, why, wit = self.reduces_to_identity(ws, r.name)
        if ok:
            self.ok(c, r, "A2-reduces-to-identity", wit)
        elif dirty:
            w, i = dirty[0]
            self.add(UNPROVEN, "A1-imm-not-classical", c, r.name,
                     f"`{w.text}` (line {w.line}) puts {w.vals[i]} in the "
                     f"classical-immediate slot {i} of @{w.sym}, and this file "
                     f"cannot show {w.vals[i]} is not a rail handle; if it is "
                     f"one, the update ENTANGLES the two and the free at line "
                     f"{c.line} collapses both (Rules 6/8). The ABI declares "
                     f"that slot classical, but at i32 both parameters are "
                     f"int32_t and the trace prints the slot as a bare decimal, "
                     f"so neither the verifier nor the golden can see it. "
                     f"(A2) did not rescue it either: {why}")
        else:
            self.add(UNPROVEN, "A-not-reducible", c, r.name,
                     f"rail allocated at line {r.origin.line} by "
                     f"@{r.origin.sym}: {why}; cannot prove it holds a known "
                     f"classical basis state at the free")

    def check_minted_rail(self, pos, c, r):
        fwd, base = r.origin, r.origin.sym
        fwd_args = tuple(fwd.args)

        # (T1a) an explicit `_unc(%r, forward args...)` naming the rail.
        rev_pos = rev_kind = None
        for p, w in r.writes:
            if r.pos < p < pos and w.eff.role == "unc" and \
                    w.eff.base == base and tuple(w.args[1:]) == fwd_args:
                rev_pos, rev_kind = p, "_unc"
        # (T1b) otherwise an unclaimed `_inv(forward args...)` token.
        if rev_pos is None:
            # EARLIEST unclaimed token in the window, not the latest. Free
            # sites are visited in stream order (increasing right endpoint), so
            # "smallest feasible available token" is the optimal matching here;
            # taking the latest can strand a later free whose window is
            # narrower and report a reversal that exists as missing.
            cands = [p for p in self.inv_pool.get((base, fwd_args), [])
                     if r.pos < p < pos and p not in self.inv_claimed]
            if cands:
                rev_pos, rev_kind = min(cands), "_inv"
                self.inv_claimed.add(rev_pos)
        if rev_pos is None:
            pre = [(p, w) for p, w in r.writes if r.pos < p < pos]
            # If the rail's own writes net to identity it DEMONSTRABLY still
            # holds f(args) at the free; only an unrecognised write history
            # leaves room for some other zeroing mechanism.
            inert, _why, _wit = self.reduces_to_identity(pre, r.name)
            self.add(VIOLATION if inert else UNPROVEN, "T1-no-reversal", c,
                     r.name,
                     f"rail minted at line {fwd.line} by `{fwd.text}` is freed "
                     f"with no matching @{base}_inv / @{base}_unc reversal in "
                     f"between" + ("" if not pre else
                     f" ({len(pre)} intervening write(s), first "
                     f"`{pre[0][1].text}` at line {pre[0][1].line})"))
            return

        rev = self.calls[rev_pos]
        witness = {fwd.line, rev.line}
        # (T3) rail unchanged mint->reversal, untouched reversal->free.
        ok, why, wit = self.reduces_to_identity(
            [(p, w) for p, w in r.writes if r.pos < p < rev_pos], r.name)
        if not ok:
            self.add(UNPROVEN, "T3-rail-written-before-reversal", c, r.name,
                     f"rail minted at line {fwd.line}: {why}, so the "
                     f"{rev_kind} at line {rev.line} does not provably zero it")
            return
        witness |= wit
        post = [(p, w) for p, w in r.writes if rev_pos < p < pos]
        if post:
            self.add(VIOLATION, "T3-rail-written-after-reversal", c, r.name,
                     f"rail is written by `{post[0][1].text}` (line "
                     f"{post[0][1].line}) after its {rev_kind} reversal at "
                     f"line {rev.line} and before the free")
            return
        # (T2) forward operands must be unchanged across [mint, reversal].
        for v in fwd.vals:
            if v not in self.rails:
                continue
            ok, wit = self.unchanged_over(v, r.pos, rev_pos)
            if not ok:
                bad = self.writes_in(v, r.pos, rev_pos)
                self.add(UNPROVEN, "T2-operand-mutated", c, r.name,
                         f"forward operand {v} of `{fwd.text}` (line "
                         f"{fwd.line}) is written by `{bad[0][1].text}` (line "
                         f"{bad[0][1].line}) with no proof it is restored "
                         f"before the {rev_kind} reversal at line {rev.line}, "
                         f"so the reversal may recompute a different value")
                return
            witness |= wit
        self.ok(c, r, "T1-" + rev_kind.strip("_") + "-reversal", witness)


# Python frames the stability recursion burns per nesting level: the chain is
# `unchanged_over -> reduces_to_identity -> pair_operands_unchanged ->
# {unchanged_over | co_written_stable -> reduces_to_identity -> ...}`, so three
# to four, and 4 is the generous count.
_FRAMES_PER_LEVEL = 4


def ensure_recursion_headroom(stream_len):
    """Raise the interpreter's recursion limit to cover this stream's WORST CASE.

    The stability recursion is bounded by INTERVAL NARROWING, not by a step
    budget (bd test_C_libtooling-xzdx — see `unchanged_over` for why the budget
    had to go). Narrowing bounds the DEPTH at `stream_len / 2`, since every
    level consumes at least two stream positions, so the interpreter stack is
    the only thing left that can stop the proof — and it stops it by CRASHING,
    which for a gate wired into every e2e slice is a correct build failing on a
    traceback. MEASURED 2026-08-08 against the SHIPPED code path, with this call
    removed and the default limit of 1000: `order_ladder(D)` from
    tools/free_pairing_check_test.py — a perfectly nested chain of self-inverse
    `cqrt_cnot`s, an exact palindrome, so the free IS sound — verifies at D=300
    and raises `RecursionError` from D=350. Measure that BARE: a tracer that
    adds one frame per level moves the wall a long way and will flatter you.

    So size the limit from the input rather than capping the proof. The figures
    this rests on are measured, not assumed:
      * the deepest nesting any of the 237 registered slices actually reaches is
        3 frames — the headroom is for adversarial input, not the ship floor;
      * the longest single stream in that corpus is 14135 calls
        (`slice_libm_real_lgammaf`), which makes this function set the limit to
        57540. That is the CEILING it grants, not a frame count anything
        reaches: narrowing caps that stream at 7067 levels and the measured cost
        is 3 frames per level, so its true worst case is ~21k frames;
      * either number is far inside what the interpreter takes — a plain
        300000-deep pure-Python recursion completes on CPython 3.11 here without
        a segfault (3.11 does not consume C stack for Python-to-Python calls,
        and nothing on this recursion path goes through C).
    The limit is only ever RAISED, never lowered, so importing this module
    cannot shrink a caller's headroom.
    """
    need = _FRAMES_PER_LEVEL * stream_len + 1000
    if sys.getrecursionlimit() < need:
        sys.setrecursionlimit(need)


# ---- bd 261j(b): rails CORRELATED with a kept output carrier ---------------
#
# THE HOLE THIS CLOSES. bd 261j(a) ruled that freeing a rail RECORDED onto a carrier
# the reversal never touches is a hidden measurement unless the rail's own forward
# chain is a basis permutation of live conditions (bd memory
# [record-freeze-invariant-2026-08-10]). This checker CERTIFIED that defect — four
# separate proof paths did (`A2-reduces-to-identity`, `T1-unc-reversal`, and two more
# found on `p_i8`/`p_qram_qidx` shapes in the Rule-13 round) — because every one of
# them asks only "does the write history return the rail to its alloc value", and
# `Ry(-t) Ry(t)` does. What none of them can see is that a CNOT-class copy onto a
# carrier that is never restored has already correlated the rail with it, so the
# reduction is no longer a statement about the rail alone.
#
# So this is NOT a tightening of a proof rule: no rule change can close it, because
# the correlation is absent from the EFFECT MODEL — `classify` records that
# `cqrt_tape_write_<W>` reads its src and writes the tape token, which is true and
# says nothing about the carrier being kept. The rule is layered ON TOP, and it
# rejects regardless of which proof would otherwise have discharged the site.
#
# BOTH CONJUNCTS ARE REQUIRED and dropping either is a false-positive machine:
#   (i)  the rail's own write history contains a NON-DIAGONAL rotation. `cqrt_rz_` is
#        excluded — diagonal, trivial on |0>, and its label-phase cancels against a
#        basis-label record (the ruling's own whitelist). SAY WHERE THAT EXCLUSION
#        ACTUALLY HAPPENS, because the obvious answer is wrong: it is NOT the
#        `cqrt_ry_` name test below. The stream scan appends to a rail's write
#        history only `if not e.diagonal`, so an `rz` never enters it at all, and
#        MEASURED by arm-kill, widening the name test to `cqrt_r` does not make the
#        rz positive control fire. The name test is defence in depth over an
#        exclusion the effect model already makes one layer earlier.
#   (ii) the rail is transitively CORRELATED with a kept carrier.
# (i) alone fires on all 36 legitimate Bennett straddles; (ii) alone fires on every
# sound `cq_template_*` rail a tape ever reads — including the DERIVED rail whose
# free bd 261j(a) deliberately KEPT (lowering_io_tape_derived_uncompute.ll).
#
# CORRELATION PROPAGATES BACKWARD ONLY, over ancestor edges, mirroring the pass: the
# record reads a descendant's label, which is a function of its ancestors'. A
# descendant of a recorded rail reverses cleanly (the record is diagonal on the
# ancestor and commutes with the descendant's chain).
#
# SLOT-BLIND at the seed, deliberately, for the same reason the pass is: the
# controlled twin `cqrt_tape_write_<W>_controlled(ctrl, tape, src)` records BOTH ctrl
# and src, and a slot-indexed test would miss the controlled form's src. The tape
# TOKEN swept in with them is a `tape`-kind resource that `F9-free-of-nonrail`
# already refuses to free, so blindness costs nothing here.
#
# WHAT THE ARM-KILL MATRIX ACTUALLY MEASURED about that, because the tidy claim is
# not the true one: replacing the blind seed with a slot-indexed one does NOT turn
# the controlled-twin mutant red. The backward closure reaches the ctrl anyway — the
# tape token is a WRITTEN slot of the same call, so closing over that call's operands
# picks the ctrl up one hop later. The two mechanisms OVERLAP here. Slot-blindness is
# kept because it is the direct statement of the physics and does not depend on the
# token happening to be modelled as written; it is not, on this shape, independently
# load-bearing, and saying otherwise would be exactly the unverified claim this
# ticket's history is made of.
#
# The closure walks only names this function MINTS as rails — the same restriction
# the pass's `taint.Tainted` test applies. Widening it to every operand would drag
# classical immediates in and turn a GATE into a false-positive generator, which is
# the expensive direction (bd memory [gate-validation-not-corpus-silence-2026-07-31]).
_TAPE_SINK = re.compile(rf"cqrt_tape_write_{_W}(?:_controlled)?$")

# THE COPY-OUT SURFACE (bd 5pio, owner ruling 2026-08-11). A method-B
# `cqrt_copy_<W>(src, dst)` records `src`'s basis label onto `dst`, and `dst` is a
# kept carrier this reversal never restores — the SAME relation the tape has, so it
# seeds R1 the same way. The pass made exactly this change to
# `ChainWalk::recordsIntoKeptCarrier`; keeping the two gates' seeds in step is the
# whole point, because they are INDEPENDENT and complementary (the trace sees the
# angles and not the rail identities; this checker sees the identities and not the
# angles). Before this, the pass froze the copy surface while stage 2b would still
# have CERTIFIED the very shape it froze — a gate disagreement of exactly the class
# bd uow0/1fim are about, found by the bd 5pio Rule-13 round.
#
# SLOT-AWARE, unlike the tape seed above, and it mirrors `isCopyOutSink` argument for
# argument: only arg 0 (the SOURCE) is a recorded rail; arg 1 is the carrier itself.
# The uncontrolled 2-arg form only — the `_controlled` data-mux copy and the
# `_inv`/`_unc` twins are excluded — and the two slots must be DISTINCT (an aliased
# `cqrt_copy_<W>(%x, %x)` is not a source-preserving read at all; the checker already
# rejects it as `B1-input-aliases-written-rail`).
_COPY_SINK = re.compile(rf"cqrt_copy_{_W}$")


def recorded_rails(fn, mints):
    """Names in `fn` transitively correlated with a kept output carrier."""
    writers = {}
    for _lbl, calls, _term in fn.blocks:
        for c in calls:
            eff = classify(c.sym)
            if eff is None:
                continue
            targets = set()
            if c.res:
                targets.add(c.res)
            for wi in eff.writes:
                if wi < len(c.vals):
                    targets.add(c.vals[wi])
            for tgt in targets:
                writers.setdefault(tgt, []).append(c)

    seed, work = set(), []
    for _lbl, calls, _term in fn.blocks:
        for c in calls:
            if _TAPE_SINK.fullmatch(c.sym):
                for v in c.vals:        # SLOT-BLIND — see the note above
                    if v not in seed:
                        seed.add(v)
                        work.append(v)
                continue
            # bd 5pio: the copy-out SOURCE only (arg 0), distinct from its carrier.
            if _COPY_SINK.fullmatch(c.sym) and len(c.vals) == 2 \
                    and c.vals[0] != c.vals[1]:
                v = c.vals[0]
                if v not in seed:
                    seed.add(v)
                    work.append(v)
    while work:
        x = work.pop()
        for w in writers.get(x, ()):
            for v in w.vals:
                if v in mints and v not in seed:
                    seed.add(v)
                    work.append(v)
    return seed


def check_module(text):
    """Returns (findings, verified_count, free_site_count, proofs).

    `proofs` is one record per VERIFIED free site: the rule that discharged it
    and the set of source lines the proof consumed. It is what
    tools/free_pairing_check_test.py mutates, so the self-test can plant a
    defect in exactly the instructions a proof rests on rather than shotgunning
    the module and drowning in semantically-null mutants.
    """
    funcs = parse_module(text)
    module_defs = {}
    # Rails minted anywhere in a FUNCTION, keyed per function. `self.rails`
    # knows only the current stream, so without this a rail minted in another
    # block of the same function is invisible to `provably_not_a_rail`. It is
    # deliberately NOT module-wide: LLVM reuses `%0` / `%h` / `%h_op` across
    # functions, so a module-wide table both over- and UNDER-reports — a `%h`
    # that names a rail here and a `cqrt_measure` result elsewhere would
    # resolve to whichever definition was parsed last. Per function it is
    # exact. (`classify` may raise Unclassified here rather than in the stream
    # scan; that is the same loud failure, one step earlier.)
    rail_defs = {}
    for fn in funcs:
        mints = set()
        for _, calls, _ in fn.blocks:
            for c in calls:
                if not c.res:
                    continue
                module_defs[c.res] = (fn.name, c.sym)
                eff = classify(c.sym)
                if eff is not None and eff.mints is not None:
                    mints.add(c.res)
        rail_defs[fn.name] = mints
    recorded = {fn.name: recorded_rails(fn, rail_defs.get(fn.name, frozenset()))
                for fn in funcs}
    # bd 261j(b) — THE LEAK CENSUS. This file counts free SITES, so a compiler that
    # stops freeing everything reports "0 cqrt_free site(s): 0 verified" and passes
    # STRICT. That is not a hypothetical failure mode: bd 261j(a) is a change whose
    # whole content is "emit fewer frees", and its blunt alternative would have
    # emitted none at all. A count of verified proofs is not a coverage measure
    # unless you also know what it did NOT have to prove. Rails left allocated are
    # LEGAL (Rule 6's fallback) so this is reported, never failed on — but it is
    # reported, so the number can never be silently zero.
    minted = leaked = 0
    for fn in funcs:
        freed = set()
        for _lbl, calls, _term in fn.blocks:
            for c in calls:
                if c.sym == "cqrt_free" and c.vals:
                    freed.add(c.vals[0])
        for _lbl, calls, _term in fn.blocks:
            for c in calls:
                if not c.res:
                    continue
                eff = classify(c.sym)
                if eff is not None and eff.mints == "rail" and eff.role == "alloc":
                    minted += 1
                    if c.res not in freed:
                        leaked += 1
    findings, verified, sites, proofs = [], 0, 0, []
    for fn in funcs:
        for _chain, calls in build_streams(fn):
            if not calls:
                continue
            ensure_recursion_headroom(len(calls))
            sa = StreamAnalysis(fn.name, calls, module_defs, fn.params,
                                rail_defs.get(fn.name, frozenset()),
                                recorded.get(fn.name, frozenset()))
            findings.extend(sa.run())
            verified += sa.verified
            sites += sa.sites
            proofs.extend(sa.proofs)
    return findings, verified, sites, proofs, (minted, leaked)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Rule-6 free-pairing checker over lowered CQ IR.")
    ap.add_argument("files", nargs="+")
    ap.add_argument("-q", "--quiet", action="store_true")
    ap.add_argument("--allow-unproven", action="store_true",
                    help="report UNPROVEN free sites without failing")
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--explain", action="store_true",
                    help="print, per verified free site, the rule that "
                         "discharged it and the source lines its proof rests on")
    args = ap.parse_args(argv)

    all_findings, verified, sites, unclassified = [], 0, 0, []
    minted_total = leaked_total = 0
    for path in args.files:
        try:
            with open(path) as fh:
                text = fh.read()
        except OSError as e:
            print(f"free_pairing_check: cannot read {path}: {e}",
                  file=sys.stderr)
            return 2
        try:
            f, v, s, proofs, (mi, lk) = check_module(text)
        except Unclassified as e:
            unclassified.append((path, e.sym))
            continue
        for x in f:
            x.path = path
        all_findings.extend(f)
        verified += v
        sites += s
        minted_total += mi
        leaked_total += lk
        if args.explain:
            for fn, line, handle, rule, lines in proofs:
                print(f"{path}: OK @{fn} line {line} [{handle}] via {rule} "
                      f"(witness lines {','.join(str(x) for x in lines)})")

    viol = [f for f in all_findings if f.kind == VIOLATION]
    unpr = [f for f in all_findings if f.kind == UNPROVEN]

    if args.json:
        def dump(fs):
            return [{"code": f.code, "file": f.path, "fn": f.fn,
                     "line": f.line, "handle": f.handle, "msg": f.msg}
                    for f in fs]
        print(json.dumps({"files": len(args.files), "free_sites": sites,
                          "verified": verified, "violations": dump(viol),
                          "unproven": dump(unpr),
                          "unclassified": [{"file": p, "symbol": s}
                                           for p, s in unclassified]},
                         indent=2))
    else:
        for path, sym in unclassified:
            print(f"free_pairing_check: {path}: UNCLASSIFIED SYMBOL @{sym} — "
                  f"add its read/write effects to classify() before trusting "
                  f"any verdict from this file", file=sys.stderr)
        for f in viol:
            print(f"{f.path}: {f}", file=sys.stderr)
        for f in unpr:
            print(f"{f.path}: {f}",
                  file=sys.stdout if args.allow_unproven else sys.stderr)
        if not args.quiet or viol or unpr or unclassified:
            print(f"free_pairing_check: {len(args.files)} file(s), "
                  f"{minted_total} rail mint(s) of which {leaked_total} left "
                  f"allocated, {sites} "
                  f"cqrt_free site(s): {verified} verified, {len(viol)} "
                  f"violation(s), {len(unpr)} unproven"
                  + (f", {len(unclassified)} unclassified symbol(s)"
                     if unclassified else ""))
    if args.stats:
        for k, n in Counter(f.code for f in all_findings).most_common():
            print(f"   {n:6d}  {k}")

    if unclassified or viol:
        return 1
    if unpr and not args.allow_unproven:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
