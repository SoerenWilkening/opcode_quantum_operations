using Test
using Bennett: IRStore, IRAlloca, IROperand, ssa, iconst, _narrow_inst, _ssa_operands

@testset "T1a.1 IRStore and IRAlloca types" begin

    @testset "IRStore — struct shape" begin
        s = IRStore(ssa(:gep), ssa(:v), 8)
        @test s.ptr == ssa(:gep)
        @test s.val == ssa(:v)
        @test s.width == 8
        # No dest field — matches IRBranch/IRRet void-instruction pattern
        @test !hasproperty(s, :dest)
    end

    @testset "IRAlloca — struct shape" begin
        a = IRAlloca(:p_arr, 8, iconst(4))
        @test a.dest == :p_arr
        @test a.elem_width == 8
        @test a.n_elems == iconst(4)
    end

    @testset "IRStore _narrow_inst preserves i1 width" begin
        # i8 store → narrowed to W
        s8 = IRStore(ssa(:p), ssa(:v), 8)
        s8w = _narrow_inst(s8, 3)
        @test s8w.width == 3
        @test s8w.ptr == s8.ptr
        @test s8w.val == s8.val

        # i1 store stays i1 (boolean predicate stored to flag slot)
        s1 = IRStore(ssa(:p), ssa(:b), 1)
        s1w = _narrow_inst(s1, 3)
        @test s1w.width == 1
    end

    @testset "IRAlloca _narrow_inst preserves i1 elem_width" begin
        # i8 alloca → narrowed
        a8 = IRAlloca(:p, 8, iconst(4))
        a8w = _narrow_inst(a8, 3)
        @test a8w.elem_width == 3
        @test a8w.n_elems == iconst(4)   # count, NOT a width, pass through

        # i1 alloca stays i1 (flag buffer)
        a1 = IRAlloca(:flags, 1, iconst(8))
        a1w = _narrow_inst(a1, 3)
        @test a1w.elem_width == 1
        @test a1w.n_elems == iconst(8)   # still 8 elements
    end

    @testset "IRStore _ssa_operands reports ptr and val" begin
        # Both SSA
        s = IRStore(ssa(:p), ssa(:v), 8)
        @test Set(_ssa_operands(s)) == Set([:p, :v])

        # Constant val (common after mem2reg leaves residue)
        sc = IRStore(ssa(:p), iconst(0), 8)
        @test _ssa_operands(sc) == [:p]

        # Fully-constant store (degenerate)
        scc = IRStore(iconst(0), iconst(0), 8)
        @test _ssa_operands(scc) == Symbol[]
    end

    @testset "IRAlloca _ssa_operands is empty for static, names for dynamic" begin
        # Static: n_elems is a const
        a_static = IRAlloca(:p, 8, iconst(4))
        @test _ssa_operands(a_static) == Symbol[]

        # Dynamic (not yet supported at lower time, but type accepts)
        a_dyn = IRAlloca(:p, 8, ssa(:n))
        @test _ssa_operands(a_dyn) == [:n]
    end

    @testset "Existing IR types still work (backward compat)" begin
        # sanity: existing instruction types still construct correctly
        using Bennett: IRBinOp, IRICmp, IRCast
        b = IRBinOp(:dst, :add, ssa(:a), ssa(:b), 8)
        @test b.width == 8
        c = IRCast(:dst, :zext, ssa(:x), 1, 8)
        @test c.from_width == 1
    end
end
