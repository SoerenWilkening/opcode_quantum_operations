using Test
using Bennett

@testset "Cuccaro in-place adder safety" begin
    @testset "Cuccaro used only when operand is dead" begin
        # f(x) = x + x: op2 (x) is the SAME as op1 and MUST NOT be overwritten
        # since x is used twice. The liveness analysis should mark x as live past
        # the first use, preventing Cuccaro.
        f_double(x::Int8) = x + x
        c = reversible_compile(f_double, Int8)
        for x in typemin(Int8):typemax(Int8)
            @test simulate(c, x) == f_double(x)
        end
        @test verify_reversibility(c)
    end

    @testset "Cuccaro preserves correctness with dead operand" begin
        # g(x) = (x + 1) + (x + 2): the intermediate (x+1) is dead after the
        # outer add, so Cuccaro could be used. Verify correct output.
        g(x::Int8) = (x + Int8(1)) + (x + Int8(2))
        c = reversible_compile(g, Int8)
        for x in typemin(Int8):typemax(Int8)
            @test simulate(c, x) == g(x)
        end
        @test verify_reversibility(c)
    end

    @testset "Multi-use operand prevents Cuccaro" begin
        # h(x) = x + x + x: x is used 3 times, never dead
        h(x::Int8) = x + x + x
        c = reversible_compile(h, Int8)
        for x in typemin(Int8):typemax(Int8)
            @test simulate(c, x) == h(x)
        end
        @test verify_reversibility(c)
    end

    @testset "Two-arg function: neither arg overwritten" begin
        # m(x,y) = x + y + x: both x and y are used multiple times
        m(x::Int8, y::Int8) = x + y + x
        c = reversible_compile(m, Int8, Int8)
        for x in Int8(-5):Int8(5), y in Int8(-5):Int8(5)
            @test simulate(c, (x, y)) == m(x, y)
        end
        @test verify_reversibility(c)
    end
end
