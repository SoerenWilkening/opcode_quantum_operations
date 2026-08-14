using Test
using Bennett
using Random

@testset "soft_fround library (trunc, floor, ceil)" begin

    @testset "Bennett.soft_trunc bit-exact" begin
        for (x, expected) in [
            (2.7, 2.0), (-2.7, -2.0), (0.5, 0.0), (-0.5, -0.0),
            (3.0, 3.0), (-3.0, -3.0), (0.0, 0.0), (-0.0, -0.0),
            (1.0e15, 1.0e15), (-1.0e15, -1.0e15),
            (Inf, Inf), (-Inf, -Inf), (NaN, NaN),
            (4.503599627370496e15, 4.503599627370496e15),  # 2^52 (already integer)
            (0.1, 0.0), (-0.1, -0.0),
            (nextfloat(0.0), 0.0), (prevfloat(0.0), -0.0),
        ]
            result = reinterpret(Float64, Bennett.soft_trunc(reinterpret(UInt64, x)))
            if isnan(expected)
                @test isnan(result)
            else
                @test result === expected
            end
        end
    end

    @testset "Bennett.soft_floor bit-exact" begin
        for (x, expected) in [
            (2.7, 2.0), (-2.7, -3.0), (0.5, 0.0), (-0.5, -1.0),
            (3.0, 3.0), (-3.0, -3.0), (0.0, 0.0), (-0.0, -0.0),
            (1.0e15, 1.0e15), (-1.0e15, -1.0e15),
            (Inf, Inf), (-Inf, -Inf), (NaN, NaN),
            (0.1, 0.0), (-0.1, -1.0),
            (1.999999999999999, 1.0), (-1.999999999999999, -2.0),
        ]
            result = reinterpret(Float64, Bennett.soft_floor(reinterpret(UInt64, x)))
            if isnan(expected)
                @test isnan(result)
            else
                @test result === expected
            end
        end
    end

    @testset "Bennett.soft_ceil bit-exact" begin
        for (x, expected) in [
            (2.3, 3.0), (-2.3, -2.0), (0.5, 1.0), (-0.5, -0.0),
            (3.0, 3.0), (-3.0, -3.0), (0.0, 0.0), (-0.0, -0.0),
            (1.0e15, 1.0e15), (-1.0e15, -1.0e15),
            (Inf, Inf), (-Inf, -Inf), (NaN, NaN),
            (0.1, 1.0), (-0.1, -0.0),
            (1.000000000000001, 2.0), (-1.000000000000001, -1.0),
        ]
            result = reinterpret(Float64, Bennett.soft_ceil(reinterpret(UInt64, x)))
            if isnan(expected)
                @test isnan(result)
            else
                @test result === expected
            end
        end
    end

    @testset "random trunc (500)" begin
        rng = Xoshiro(42)
        for _ in 1:500
            x = randn(rng) * 1000
            expected = trunc(x)
            result = reinterpret(Float64, Bennett.soft_trunc(reinterpret(UInt64, x)))
            @test result === expected
        end
    end

    @testset "random floor (500)" begin
        rng = Xoshiro(43)
        for _ in 1:500
            x = randn(rng) * 1000
            expected = floor(x)
            result = reinterpret(Float64, Bennett.soft_floor(reinterpret(UInt64, x)))
            @test result === expected
        end
    end

    @testset "random ceil (500)" begin
        rng = Xoshiro(44)
        for _ in 1:500
            x = randn(rng) * 1000
            expected = ceil(x)
            result = reinterpret(Float64, Bennett.soft_ceil(reinterpret(UInt64, x)))
            @test result === expected
        end
    end
end
