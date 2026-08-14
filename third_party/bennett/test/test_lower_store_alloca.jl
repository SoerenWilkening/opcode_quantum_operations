using Test
using Bennett
using LLVM

# Helper: compile hand-crafted LLVM IR through the pipeline.
function _compile_ir(ir_string::String)
    c = nothing
    LLVM.Context() do _ctx
        mod = parse(LLVM.Module, ir_string)
        parsed = Bennett._module_to_parsed_ir(mod)
        lr = Bennett.lower(parsed)
        c = Bennett.bennett(lr)
        dispose(mod)
    end
    return c
end

@testset "T1b.3 lower_alloca! / lower_store! via soft_mux callees" begin

    @testset "alloca + store slot 0 + load slot 0 round-trips" begin
        ir = """
        define i8 @julia_f_1(i8 %x) {
        top:
          %p = alloca i8, i32 4
          store i8 %x, ptr %p
          %v = load i8, ptr %p
          ret i8 %v
        }
        """
        c = _compile_ir(ir)
        @test gate_count(c).total > 0
        @test c.n_wires > 0
        @test verify_reversibility(c)
        # Storing x to slot 0 then reading slot 0 should give x back
        for x in Int8(-8):Int8(8)
            @test simulate(c, x) == x
        end
    end

    @testset "alloca + store slot 2 via PtrOffset + load slot 2" begin
        # GEP with constant offset 2 → IRPtrOffset
        ir = """
        define i8 @julia_f_1(i8 %x) {
        top:
          %p = alloca i8, i32 4
          %g = getelementptr i8, ptr %p, i32 2
          store i8 %x, ptr %g
          %v = load i8, ptr %g
          ret i8 %v
        }
        """
        c = _compile_ir(ir)
        @test verify_reversibility(c)
        for x in Int8(-8):Int8(8)
            @test simulate(c, x) == x
        end
    end

    # T3b.3 universal dispatcher relaxed shape constraints: non-(8,4) allocas
    # now compile successfully when accessed with static idx (shadow path) or
    # when shape matches another registered mux-exch variant. Below we verify
    # the new wider coverage rather than the old "error" behavior.

    @testset "alloca i8 × 8 is now supported (T3b.3 MUX EXCH 8x8 path)" begin
        ir = """
        define i8 @julia_f_1(i8 %x) {
        top:
          %p = alloca i8, i32 8
          ret i8 %x
        }
        """
        c = _compile_ir(ir)
        @test verify_reversibility(c)
    end

    @testset "alloca i16 × 4 unused compiles — size extraction succeeds" begin
        ir = """
        define i8 @julia_f_1(i8 %x) {
        top:
          %p = alloca i16, i32 4
          ret i8 %x
        }
        """
        c = _compile_ir(ir)
        @test verify_reversibility(c)
    end

    @testset "store without prior alloca provenance fails loudly" begin
        # Storing to a pointer parameter (no alloca in the IR).
        # Acceptable failure mode: either the extractor rejects, or lower_store! errors.
        ir = """
        define void @julia_f_1(i8 %x, ptr %buf) {
        top:
          store i8 %x, ptr %buf
          ret void
        }
        """
        @test_throws Exception _compile_ir(ir)
    end
end
