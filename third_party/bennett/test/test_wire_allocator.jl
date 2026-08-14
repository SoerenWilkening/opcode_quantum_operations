using Test
using Bennett

@testset "WireAllocator unit tests" begin
    @testset "sequential allocation" begin
        wa = Bennett.WireAllocator()
        w1 = Bennett.allocate!(wa, 3)
        @test w1 == [1, 2, 3]
        w2 = Bennett.allocate!(wa, 2)
        @test w2 == [4, 5]
        @test Bennett.wire_count(wa) == 5
    end

    @testset "free and reuse (min index first)" begin
        wa = Bennett.WireAllocator()
        Bennett.allocate!(wa, 5)  # wires 1-5
        Bennett.free!(wa, [3, 5, 1])  # free 1, 3, 5

        # Should reuse freed wires, smallest first
        w = Bennett.allocate!(wa, 3)
        @test sort(w) == [1, 3, 5]
        @test w[1] == 1  # min freed wire first
        @test w[2] == 3
        @test w[3] == 5
    end

    @testset "interleave allocate/free — no duplicates" begin
        wa = Bennett.WireAllocator()
        all_wires = Set{Int}()

        w1 = Bennett.allocate!(wa, 4)  # 1,2,3,4
        for w in w1; push!(all_wires, w); end
        @test length(all_wires) == 4

        Bennett.free!(wa, [2, 4])  # free 2 and 4
        w2 = Bennett.allocate!(wa, 3)  # should get 2, 4, then 5
        for w in w2
            @test !(w in setdiff(Set(w1), Set([2, 4])))  || w in [2, 4]  # reused or new
        end

        # All allocated wires should be unique (no wire used twice simultaneously)
        live = Set(w1)
        delete!(live, 2); delete!(live, 4)
        for w in w2; push!(live, w); end
        @test length(live) == length(w1) - 2 + length(w2)
    end

    @testset "wire_count tracks high water mark" begin
        wa = Bennett.WireAllocator()
        Bennett.allocate!(wa, 10)
        @test Bennett.wire_count(wa) == 10
        Bennett.free!(wa, [5, 6, 7])
        @test Bennett.wire_count(wa) == 10  # high water mark unchanged
        Bennett.allocate!(wa, 2)  # reuses 5, 6
        @test Bennett.wire_count(wa) == 10  # still 10
        Bennett.allocate!(wa, 5)  # reuses 7, then allocates 11, 12, 13, 14
        @test Bennett.wire_count(wa) == 14
    end

    @testset "empty free is no-op" begin
        wa = Bennett.WireAllocator()
        Bennett.allocate!(wa, 3)
        Bennett.free!(wa, Int[])
        w = Bennett.allocate!(wa, 1)
        @test w == [4]
    end
end
