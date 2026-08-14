using Test
using Bennett
using Bennett: run_memssa, parse_memssa_annotations, MemSSAInfo

# T2a.2 — Parse MemorySSA printer-pass output, expose Def/Use/Phi graph.

@testset "T2a.2 MemorySSA ingest" begin

    @testset "parse basic Def/Use annotations from known text" begin
        # A hand-crafted fragment exercising all three annotation forms.
        txt = """
MemorySSA for function: f
define i64 @f(i64 %x) {
top:
  %a = alloca i64
; 1 = MemoryDef(liveOnEntry)
  store i64 %x, ptr %a
; MemoryUse(1)
  %v = load i64, ptr %a
  ret i64 %v
}
"""
        info = parse_memssa_annotations(txt)
        @test info isa MemSSAInfo
        @test length(info.def_at_line) >= 1
        @test length(info.use_at_line) >= 1
        # Def 1 clobbers liveOnEntry
        @test info.def_clobber[1] === :live_on_entry
        # The Use references Def 1
        @test 1 in values(info.use_at_line)
    end

    @testset "parse conditional-store MemoryPhi" begin
        # Diamond CFG with both branches storing → phi at merge.
        txt = """
MemorySSA for function: g
define i64 @g(i1 %c, i64 %x) {
entry:
; 1 = MemoryDef(liveOnEntry)
  %a = alloca i64
  br i1 %c, label %L, label %R
L:
; 2 = MemoryDef(1)
  store i64 %x, ptr %a
  br label %M
R:
; 3 = MemoryDef(1)
  store i64 0, ptr %a
  br label %M
M:
; 4 = MemoryPhi({L,2},{R,3})
; MemoryUse(4)
  %v = load i64, ptr %a
  ret i64 %v
}
"""
        info = parse_memssa_annotations(txt)
        @test haskey(info.phis, 4)
        @test info.phis[4] == [(:L, 2), (:R, 3)]
        # Use references Phi #4
        @test 4 in values(info.use_at_line)
    end

    @testset "run_memssa end-to-end on a Julia function" begin
        f(x::Int) = let arr = [x, x+1, 0, 0]; arr[1] + arr[2]; end
        info = run_memssa(f, Tuple{Int}; preprocess=false)
        # Expect at least one MemoryDef (for the stores to arr) and at least
        # one MemoryUse (for the reads)
        @test length(info.def_clobber) >= 1
        @test !isempty(info.use_at_line)
    end

    @testset "run_memssa is a no-op for functions with no memory ops" begin
        # f(x) = x+1 has no store/load/call — memssa should report empty or
        # only pgcstack-related Defs.
        f(x::Int) = x + 1
        info = run_memssa(f, Tuple{Int}; preprocess=false)
        @test info isa MemSSAInfo  # just runs without erroring
    end

    @testset "extract_parsed_ir with use_memory_ssa=true carries memssa" begin
        f(x::Int) = let a = [x, x+1]; a[1] + a[2]; end

        parsed_off = Bennett.extract_parsed_ir(f, Tuple{Int})
        @test parsed_off.memssa === nothing

        parsed_on = Bennett.extract_parsed_ir(f, Tuple{Int}; use_memory_ssa=true)
        @test parsed_on.memssa !== nothing
        @test parsed_on.memssa isa MemSSAInfo
        # At minimum, we expect to see some Def/Use annotations
        @test !isempty(parsed_on.memssa.def_clobber) ||
              !isempty(parsed_on.memssa.use_at_line) ||
              !isempty(parsed_on.memssa.phis)
    end
end
