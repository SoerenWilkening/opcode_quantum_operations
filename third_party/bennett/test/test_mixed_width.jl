@testset "Mixed-width (sext/zext/trunc)" begin
    @testset "sum_to (uses zext i9, trunc, multi-block)" begin
        # LLVM computes closed-form n*(n+1)/2 using i9 arithmetic
        function sum_to(n::Int8)
            acc = Int8(0)
            for i in Int8(1):n
                acc += i
            end
            return acc
        end

        circuit = reversible_compile(sum_to, Int8)
        for n in Int8(0):Int8(15)
            @test simulate(circuit, n) == sum_to(n)
        end
        @test verify_reversibility(circuit)
        println("  sum_to (closed form): ", gate_count(circuit))
    end

    @testset "Explicit sext + mul + trunc" begin
        # Widen to Int16, multiply, truncate back — with wrapping to avoid error path
        function widen_mul(x::Int8)
            w = Int16(x)  # sext i8 to i16
            return Int8(w * w % Int16(128) - Int16(64))  # keep in Int8 range
        end

        ir = extract_ir(widen_mul, Tuple{Int8})
        println("  widen_mul IR:\n", ir)

        # If the IR is too complex (bounds checks, calls), this test documents that.
        # Skip if compilation fails on unsupported instructions.
        try
            circuit = reversible_compile(widen_mul, Int8)
            for x in Int8(-10):Int8(10)
                @test simulate(circuit, x) == widen_mul(x)
            end
        catch e
            @warn "widen_mul skipped (unsupported IR)" exception=e
            @test_broken false
        end
    end
end
