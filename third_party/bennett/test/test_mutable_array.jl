using Test
using Bennett
using LLVM

# End-to-end tests for reversible mutable memory. Uses hand-crafted LLVM IR
# because Julia's codegen aggressively eliminates allocas before we see them
# (even at optimize=false most small local arrays get promoted). The hand-
# crafted IR exercises the same T1b.3 lowering path (alloca → lower_alloca! →
# IRStore → soft_mux_store_4x8 callee → vw[alloca_dest] rebind → IRLoad via
# soft_mux_load_4x8).

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

@testset "T1b.4 end-to-end mutable array patterns" begin

    @testset "store then load same slot (identity via memory)" begin
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
        @test verify_reversibility(c)
        for x in Int8(-128):Int8(3):Int8(127)
            @test simulate(c, x) == x
        end
    end

    @testset "store + store + load last slot" begin
        # Store x to slot 0, store y to slot 2, load slot 2 → y
        ir = """
        define i8 @julia_f_1(i8 %x, i8 %y) {
        top:
          %p  = alloca i8, i32 4
          %g2 = getelementptr i8, ptr %p, i32 2
          store i8 %x, ptr %p
          store i8 %y, ptr %g2
          %v = load i8, ptr %g2
          ret i8 %v
        }
        """
        c = _compile_ir(ir)
        @test verify_reversibility(c)
        for x in Int8(-8):Int8(8), y in Int8(-8):Int8(8)
            @test simulate(c, (x, y)) == y
        end
    end

    @testset "store + store + load first slot preserves earlier write" begin
        # Store x to slot 0, store y to slot 2, load slot 0 → x
        # Verifies rebinding doesn't clobber slots we didn't write.
        ir = """
        define i8 @julia_f_1(i8 %x, i8 %y) {
        top:
          %p  = alloca i8, i32 4
          %g2 = getelementptr i8, ptr %p, i32 2
          store i8 %x, ptr %p
          store i8 %y, ptr %g2
          %v = load i8, ptr %p
          ret i8 %v
        }
        """
        c = _compile_ir(ir)
        @test verify_reversibility(c)
        for x in Int8(-4):Int8(4), y in Int8(-4):Int8(4)
            @test simulate(c, (x, y)) == x
        end
    end

    @testset "overwrite: store x then store y to same slot, load → y" begin
        ir = """
        define i8 @julia_f_1(i8 %x, i8 %y) {
        top:
          %p = alloca i8, i32 4
          store i8 %x, ptr %p
          store i8 %y, ptr %p
          %v = load i8, ptr %p
          ret i8 %v
        }
        """
        c = _compile_ir(ir)
        @test verify_reversibility(c)
        for x in Int8(-4):Int8(4), y in Int8(-4):Int8(4)
            @test simulate(c, (x, y)) == y
        end
    end

    @testset "write all 4 slots + sum" begin
        # Fill slots 0..3 with arg values, then add slot 0 + slot 3
        ir = """
        define i8 @julia_f_1(i8 %a, i8 %b, i8 %c, i8 %d) {
        top:
          %p  = alloca i8, i32 4
          %g1 = getelementptr i8, ptr %p, i32 1
          %g2 = getelementptr i8, ptr %p, i32 2
          %g3 = getelementptr i8, ptr %p, i32 3
          store i8 %a, ptr %p
          store i8 %b, ptr %g1
          store i8 %c, ptr %g2
          store i8 %d, ptr %g3
          %va = load i8, ptr %p
          %vd = load i8, ptr %g3
          %s = add i8 %va, %vd
          ret i8 %s
        }
        """
        c = _compile_ir(ir)
        @test verify_reversibility(c)
        for a in Int8(-4):Int8(2):Int8(4), d in Int8(-4):Int8(2):Int8(4)
            b = Int8(0); cc = Int8(0)
            @test simulate(c, (a, b, cc, d)) == Int8(a + d)
        end
    end

    @testset "read uninitialized slot returns zero" begin
        # Alloca is zero-initialized per our invariant; unwritten slots read 0.
        ir = """
        define i8 @julia_f_1(i8 %x) {
        top:
          %p  = alloca i8, i32 4
          %g3 = getelementptr i8, ptr %p, i32 3
          store i8 %x, ptr %p
          %v = load i8, ptr %g3
          ret i8 %v
        }
        """
        c = _compile_ir(ir)
        @test verify_reversibility(c)
        for x in Int8(-4):Int8(4)
            @test simulate(c, x) == Int8(0)
        end
    end
end
