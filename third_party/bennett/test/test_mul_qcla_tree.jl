using Test
using Bennett
using Bennett: lower_mul_qcla_tree!, WireAllocator, allocate!, wire_count,
    ReversibleGate, CNOTGate, ToffoliGate, NOTGate

function _simulate!(bits::Vector{Bool}, gates::Vector{<:ReversibleGate})
    for g in gates
        if g isa CNOTGate
            bits[g.target] ⊻= bits[g.control]
        elseif g isa ToffoliGate
            bits[g.target] ⊻= bits[g.control1] & bits[g.control2]
        elseif g isa NOTGate
            bits[g.target] ⊻= true
        end
    end
    return bits
end

function _load!(bits, reg, val, W)
    for i in 1:W; bits[reg[i]] = (val >> (i-1)) & 1 == 1; end
end
function _decode(bits, reg)
    v = UInt64(0)
    for i in 1:length(reg); v |= (bits[reg[i]] ? UInt64(1) : UInt64(0)) << (i-1); end
    return v
end

# Drive lower_mul_qcla_tree! directly (gate emission + simulate).
# Returns (got_xy, anc_clean, n_anc).
function _run_qcla_mul(W, xv, yv)
    wa = WireAllocator()
    a = allocate!(wa, W); b = allocate!(wa, W)
    before = wire_count(wa)
    gates = Vector{ReversibleGate}()
    result = lower_mul_qcla_tree!(gates, wa, a, b, W)
    total = wire_count(wa)

    bits = zeros(Bool, total)
    _load!(bits, a, xv, W); _load!(bits, b, yv, W)
    _simulate!(bits, gates)

    # a, b unchanged
    a_after = _decode(bits, a)
    b_after = _decode(bits, b)

    # All ancillae (everything allocated that isn't a, b, or result) zero
    ab_set = Set{Int}(vcat(a, b, result))
    anc_dirty = count(w -> !(w in ab_set) && bits[w], 1:total)

    return (xy=_decode(bits, result), a_after=a_after, b_after=b_after,
            anc_clean=(anc_dirty == 0), dirty=anc_dirty, n_anc=total - 3W,
            gates=gates, n_wires=total, result=result)
end

@testset "lower_mul_qcla_tree!: W=1 trivial" begin
    for x in 0:1, y in 0:1
        r = _run_qcla_mul(1, x, y)
        @test r.xy == UInt64(x * y)
        @test r.a_after == x
        @test r.b_after == y
        @test r.anc_clean
        @test length(r.result) == 2
    end
end

@testset "lower_mul_qcla_tree!: W=2 exhaustive" begin
    for x in 0:3, y in 0:3
        r = _run_qcla_mul(2, x, y)
        @test r.xy == UInt64(x * y)
        @test r.a_after == x
        @test r.b_after == y
        @test r.anc_clean
    end
end

@testset "lower_mul_qcla_tree!: W=4 exhaustive correctness (Sun-Borissov Algo 3)" begin
    fails = 0
    dirty_count = 0
    for x in 0:15, y in 0:15
        r = _run_qcla_mul(4, x, y)
        r.xy == UInt64(x * y) || (fails += 1)
        r.anc_clean || (dirty_count += 1)
        r.a_after == x || (fails += 1)
        r.b_after == y || (fails += 1)
    end
    @test fails == 0
    @test dirty_count == 0
end

@testset "lower_mul_qcla_tree!: W=4 result register is 2W wires" begin
    r = _run_qcla_mul(4, 5, 7)
    @test length(r.result) == 8
end

@testset "lower_mul_qcla_tree!: W=4 verify_reversibility (random sample)" begin
    wa = WireAllocator()
    a = allocate!(wa, 4); b = allocate!(wa, 4)
    gates = Vector{ReversibleGate}()
    lower_mul_qcla_tree!(gates, wa, a, b, 4)
    n = wire_count(wa)
    # Forward + reverse should return to initial bits.
    for _ in 1:20
        bits = zeros(Bool, n)
        for w in vcat(a, b); bits[w] = rand(Bool); end
        orig = copy(bits)
        _simulate!(bits, gates)
        for g in Iterators.reverse(gates); _simulate!(bits, [g]); end
        @test bits == orig
    end
end

# ---- X2: scale ----

@testset "lower_mul_qcla_tree!: W=8 exhaustive correctness" begin
    fails = 0
    dirty = 0
    for x in 0:255, y in 0:255
        r = _run_qcla_mul(8, x, y)
        r.xy == UInt64(Int(x) * Int(y)) || (fails += 1)
        r.anc_clean || (dirty += 1)
    end
    @test fails == 0
    @test dirty == 0
end

@testset "lower_mul_qcla_tree!: W=16 sampled + edges" begin
    edges = [(0,0), (65535,65535), (65535,1), (0x5555, 0xAAAA), (0x00FF, 0xFF00),
             (32768, 32768), (42, 1000), (12345, 54321)]
    for (x, y) in edges
        r = _run_qcla_mul(16, x, y)
        @test r.xy == UInt64(Int(x) * Int(y))
        @test r.anc_clean
        @test r.a_after == x
        @test r.b_after == y
    end
    for _ in 1:50
        x = rand(0:65535); y = rand(0:65535)
        r = _run_qcla_mul(16, x, y)
        @test r.xy == UInt64(Int(x) * Int(y))
        @test r.anc_clean
    end
end

@testset "lower_mul_qcla_tree!: W=32 sampled" begin
    edges = [(0, 0), (0xFFFFFFFF, 0xFFFFFFFF), (0xFFFFFFFF, 1), (0x55555555, 0xAAAAAAAA)]
    for (x, y) in edges
        r = _run_qcla_mul(32, Int(x), Int(y))
        @test r.xy == UInt64(UInt64(x) * UInt64(y))
        @test r.anc_clean
    end
    for _ in 1:20
        x = rand(0:typemax(UInt32)); y = rand(0:typemax(UInt32))
        r = _run_qcla_mul(32, Int(x), Int(y))
        @test r.xy == UInt64(UInt64(x) * UInt64(y))
        @test r.anc_clean
    end
end

@testset "lower_mul_qcla_tree!: ancilla bound (current unoptimized impl)" begin
    # Paper claims < 2n² via mid-algorithm fast_copy uncompute-then-redo
    # (paper Algorithm 3 steps 3, 5) combined with parallel_adder_tree ancilla
    # recycling. Our X1 assembly skips that optimization (parallel_adder_tree
    # is already self-cleaning as of A3, so pp + x_copies + y_bit_copies stay
    # live through the tree). Expected ancilla scales as O(n²) but with a
    # larger constant. X3 tightening via the paper's schedule is a follow-up.
    # We assert < 10n² here as a regression bound; current measurements are
    # around 7-8n².
    for W in (4, 8, 16, 32)
        wa = WireAllocator()
        a = allocate!(wa, W); b = allocate!(wa, W)
        gates = Vector{ReversibleGate}()
        result = lower_mul_qcla_tree!(gates, wa, a, b, W)
        total = wire_count(wa)
        n_anc = total - 3W  # subtract a, b, and 2W result wires
        @test n_anc < 10 * W^2
    end
end
