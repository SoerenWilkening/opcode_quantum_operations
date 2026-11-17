; NORTH_STAR "Done when" 1 (kmgy.23): a program that calls only ABI symbols (and printf to report),
; written as LLVM IR the way CQ_lang's lowered code calls the backend. It uses every family once:
; alloc, a template with a literal and its _unc, an i1 compare, a rotation, the core i1 logic, a bus
; store and load at a classical address, a tape write and a performed effect entry, an invert region,
; push/pop, measure and free. Linked as main.o libcq_templates.a libcq_runtime.a <the seven
; operations> (ABI:102-104), once with libcq_gates_replay.a and once with tests/abi_link_gates.c.
; Exit status 0 iff every measured value is the expected one.

@cell = global [16 x i8] zeroinitializer          ; host bytes the bus stores to and loads from
@.abs = private constant [4 x i8] c"abs\00"
@.bad = private constant [22 x i8] c"FAIL %s: got %lld\0A\00\00\00\00"
@.ok = private constant [13 x i8] c"abi_link ok\0A\00"
@.n_tape = private constant [5 x i8] c"tape\00"
@.n_add = private constant [4 x i8] c"add\00"
@.n_unc = private constant [4 x i8] c"unc\00"
@.n_cmp = private constant [4 x i8] c"cmp\00"
@.n_and = private constant [4 x i8] c"and\00"
@.n_bus = private constant [4 x i8] c"bus\00"
@.n_inv = private constant [4 x i8] c"inv\00"
@.n_pop = private constant [4 x i8] c"pop\00"
@.n_x = private constant [2 x i8] c"x\00"

declare i32 @printf(ptr, ...)
declare i32 @cqrt_alloc_i1(i1)
declare i32 @cqrt_alloc_i8(i8)
declare i32 @cqrt_alloc_i32(i32)
declare void @cqrt_free(i32)
declare zeroext i1 @cqrt_measure_i1(i32)
declare signext i8 @cqrt_measure_i8(i32)
declare i32 @cqrt_measure_i32(i32)
declare void @cqrt_ry_i8(i32, double)
declare void @cqrt_rz_i1(i32, double)
declare void @cqrt_and_i1(i32, i32, i32)
declare void @cqrt_copy_i8(i32, i32)
declare void @cqrt_push(i32)
declare i32 @cqrt_pop()
declare void @cqrt_invert_begin()
declare void @cqrt_invert_end()
declare i32 @cq_template_add_i8_hl(i32, i8)
declare void @cq_template_add_i8_hl_unc(i32, i32, i8)
declare i32 @cq_template_icmp_ult_i8_hl(i32, i8)
declare void @cq_template_icmp_ult_i8_hl_unc(i32, i32, i8)
declare i32 @cqrt_qram_alloc_i8(i32)
declare void @cqrt_qram_store_i8_hl(i32, i64, i32)
declare i32 @cqrt_qram_load_i8_hl(i32, i64)
declare i32 @cqrt_tape_alloc()
declare i32 @cqrt_tape_write_i32(i32, i32)
declare i32 @cqrt_tape_effect_i32_controlled(i32, i32, ptr, i32)

define private void @check(ptr %what, i64 %got, i64 %want, ptr %fail) {
  %ok = icmp eq i64 %got, %want
  br i1 %ok, label %done, label %bad
bad:
  call i32 (ptr, ...) @printf(ptr @.bad, ptr %what, i64 %got)
  store i32 1, ptr %fail
  br label %done
done:
  ret void
}

define i32 @main() {
  %fail = alloca i32
  store i32 0, ptr %fail

  ; Tape: a classical field and a resolved effect entry, performed at the call: abs(-7) = 7.
  %t = call i32 @cqrt_tape_alloc()
  %s = call i32 @cqrt_alloc_i32(i32 -7)
  %f = call i32 @cqrt_tape_write_i32(i32 %t, i32 %s)
  %one = call i32 @cqrt_alloc_i1(i1 true)
  %r = call i32 @cqrt_tape_effect_i32_controlled(i32 %one, i32 %t, ptr @.abs, i32 1)
  %rv = call i32 @cqrt_measure_i32(i32 %r)
  %rv64 = sext i32 %rv to i64
  call void @check(ptr @.n_tape, i64 %rv64, i64 7, ptr %fail)

  ; A quantum i8 rail x = 90: alloc, then a rotation by 0 forces its qubits.
  %x = call i32 @cqrt_alloc_i8(i8 90)
  call void @cqrt_ry_i8(i32 %x, double 0.0)

  ; A template with a literal, its _unc, then free: o = x + 37, o ^= x + 37 is 0.
  %o = call i32 @cq_template_add_i8_hl(i32 %x, i8 37)
  call void @cq_template_add_i8_hl_unc(i32 %o, i32 %x, i8 37)
  %ov = call signext i8 @cqrt_measure_i8(i32 %o)
  %ov64 = sext i8 %ov to i64
  call void @check(ptr @.n_unc, i64 %ov64, i64 0, ptr %fail)
  call void @cqrt_free(i32 %o)
  %o2 = call i32 @cq_template_add_i8_hl(i32 %x, i8 37)

  ; A compare flag [x <u 100] = 1, ANDed with itself into a fresh flag g, a phase on g.
  %k = call i32 @cq_template_icmp_ult_i8_hl(i32 %x, i8 100)
  %g = call i32 @cqrt_alloc_i1(i1 false)
  call void @cqrt_and_i1(i32 %k, i32 %one, i32 %g)
  call void @cqrt_rz_i1(i32 %g, double 0.5)

  ; Bus at a classical address: store the quantum x, load it back into a fresh rail.
  %m = call i32 @cqrt_qram_alloc_i8(i32 0)
  %a = ptrtoint ptr @cell to i64
  call void @cqrt_qram_store_i8_hl(i32 %m, i64 %a, i32 %x)
  %h = call i32 @cqrt_qram_load_i8_hl(i32 %m, i64 %a)

  ; An invert region: d ^= x, then invert{d ^= x}, leaves d as it was (0).
  %d = call i32 @cqrt_alloc_i8(i8 0)
  call void @cqrt_copy_i8(i32 %x, i32 %d)
  call void @cqrt_invert_begin()
  call void @cqrt_copy_i8(i32 %x, i32 %d)
  call void @cqrt_invert_end()

  call void @cqrt_push(i32 41)
  %p = call i32 @cqrt_pop()
  %p64 = sext i32 %p to i64
  call void @check(ptr @.n_pop, i64 %p64, i64 41, ptr %fail)

  ; Measure everything.
  %o2v = call signext i8 @cqrt_measure_i8(i32 %o2)
  %o2v64 = sext i8 %o2v to i64
  call void @check(ptr @.n_add, i64 %o2v64, i64 127, ptr %fail)
  %kv = call zeroext i1 @cqrt_measure_i1(i32 %k)
  %kv64 = zext i1 %kv to i64
  call void @check(ptr @.n_cmp, i64 %kv64, i64 1, ptr %fail)
  %gv = call zeroext i1 @cqrt_measure_i1(i32 %g)
  %gv64 = zext i1 %gv to i64
  call void @check(ptr @.n_and, i64 %gv64, i64 1, ptr %fail)
  %hv = call signext i8 @cqrt_measure_i8(i32 %h)
  %hv64 = sext i8 %hv to i64
  call void @check(ptr @.n_bus, i64 %hv64, i64 90, ptr %fail)
  %dv = call signext i8 @cqrt_measure_i8(i32 %d)
  %dv64 = sext i8 %dv to i64
  call void @check(ptr @.n_inv, i64 %dv64, i64 0, ptr %fail)
  call void @cqrt_free(i32 %d)
  %xv = call signext i8 @cqrt_measure_i8(i32 %x)
  %xv64 = sext i8 %xv to i64
  call void @check(ptr @.n_x, i64 %xv64, i64 90, ptr %fail)

  %rc = load i32, ptr %fail
  %good = icmp eq i32 %rc, 0
  br i1 %good, label %ok, label %out
ok:
  call i32 (ptr, ...) @printf(ptr @.ok)
  br label %out
out:
  ret i32 %rc
}
