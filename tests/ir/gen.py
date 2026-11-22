# Regenerate caller.ll (optional; the .ll is the test source): python3 gen.py > caller.ll
# Emit an LLVM IR caller of every core entry point with a literal or a typed return, and of the
# template and tape entry points whose literals cross the calling convention (kmgy.33).
import random
random.seed(7)
ints=[1,6,7,8,14,15,16,17,24,31,32,48,50,63,64,80,100,112,128,200]
fps=[('f16','half',16),('bf16','bfloat',16),('f32','float',32),('f64','double',64),('f80','x86_fp80',80)]
ext={1:'zeroext ',8:'signext ',16:'signext '}
out=[]; decl=set(); strs=[]
def D(s): decl.add(s)
D('declare i32 @printf(ptr, ...)')
D('declare i32 @cqrt_alloc_i1(i1)'); D('declare void @cqrt_free(i32)')
body=[]; n=[0]
def t(): n[0]+=1; return f'%t{n[0]}'
def check(name, ok):
    i=len(strs); strs.append(name)
    nb=t(); body.append(f'  br i1 {ok}, label %ok{i}, label %bad{i}')
    body.append(f'bad{i}:\n  call i32 (ptr, ...) @printf(ptr @.s{i})\n  store i32 1, ptr %fail\n  br label %ok{i}')
    body.append(f'ok{i}:')
def run(name, W, ty, retattr, frombits, tobits, wbits):
    # values: v (alloc), x (xorc), y (xorc_controlled); garbage above W in the source i256
    mask=(1<<wbits)-1
    v,x,y=[random.getrandbits(wbits) for _ in range(3)]
    if not frombits:   # fp: v a negative signalling NaN, x == y, so every value crossing is an sNaN or random
        v={16:0xfcf7 if name=='f16' else 0xff81,32:0xff800001,64:0xfff0000000000001,80:0xffffa000000000000001}[wbits]; y=x
    want=v^x^y
    D(f'declare i32 @cqrt_alloc_{name}({ty})')
    D(f'declare void @cqrt_xorc_{name}(i32, {ty})')
    D(f'declare void @cqrt_xorc_{name}_controlled(i32, i32, {ty})')
    D(f'declare {retattr}{ty} @cqrt_measure_{name}(i32)')
    D(f'declare void @cqrt_copy_{name}(i32, i32)')
    D(f'declare void @cqrt_copy_{name}_controlled(i32, i32, i32)')
    vals=[]
    for val in (v,x,y):
        g=random.getrandbits(256) & ~mask
        a=t(); body.append(f'  {a} = load volatile i256, ptr @g{len(vals)}_{name}')
        D(f'@g{len(vals)}_{name} = global i256 {val|g if frombits else val}')
        if frombits: b=t(); body.append(f'  {b} = trunc i256 {a} to {ty}'); vals.append(b)
        else:
            b=t(); body.append(f'  {b} = trunc i256 {a} to i{wbits}'); c=t(); body.append(f'  {c} = bitcast i{wbits} {b} to {ty}'); vals.append(c)
    def meas(hh, exp, what):
        m=t(); body.append(f'  {m} = call {retattr}{ty} @cqrt_measure_{name}(i32 {hh})')
        if frombits: ri=m
        else: ri=t(); body.append(f'  {ri} = bitcast {ty} {m} to i{wbits}')
        ok=t(); body.append(f'  {ok} = icmp eq i{wbits} {ri}, {exp}'); check(f'{name}: {what}', ok)
        return m
    h=t(); body.append(f'  {h} = call i32 @cqrt_alloc_{name}({ty} {vals[0]})')
    meas(h, v, 'cqrt_alloc / cqrt_measure value')
    body.append(f'  call void @cqrt_xorc_{name}(i32 {h}, {ty} {vals[1]})')
    meas(h, v^x, 'cqrt_xorc value')
    c=t(); body.append(f'  {c} = call i32 @cqrt_alloc_i1(i1 1)')
    body.append(f'  call void @cqrt_xorc_{name}_controlled(i32 {c}, i32 {h}, {ty} {vals[2]})')
    m=meas(h, want, 'cqrt_xorc_controlled value')
    d=t(); body.append(f'  {d} = call i32 @cqrt_alloc_{name}({ty} {vals[1]})')   # d = x
    body.append(f'  call void @cqrt_copy_{name}(i32 {h}, i32 {d})')            # d = x ^ want
    meas(d, x^want, 'cqrt_copy value')
    body.append(f'  call void @cqrt_copy_{name}_controlled(i32 {c}, i32 {h}, i32 {d})')  # d = x
    meas(d, x, 'cqrt_copy_controlled value')
    if W in ext:   # the extension the ABI promises: call with an i32 return type (allowed with opaque pointers) to read all of EAX
        z=t(); body.append(f'  {z} = call i32 @cqrt_measure_{name}(i32 {h})')
        ok=t(); body.append(f'  {ok} = icmp eq i32 {z}, {(want - (1<<W) if W>1 and want>>(W-1) else want) & 0xffffffff}'); check(f'{name}: cqrt_measure return not extended to 32 bits', ok)
    body.append(f'  call void @cqrt_free(i32 {c})')
for W in ints: run(f'i{W}',W,f'i{W}',ext.get(W,''),True,True,W)
for nm,ty,b in fps: run(nm,0,ty,'',False,False,b)

# Templates (kmgy.33): every wide width, i128, i100, and i17 (junk in the high register bits).
# fshl/fshr in all seven shapes: the shapes where a wide literal starts with one integer register
# left (_qll, _lql, _llq_unc) are the ones LLVM 19.1.7 passes on the stack, docs/ABI_SYMBOLS.md.
def lit(name, W, val):   # a value of type iW from a volatile global, junk above bit W
    k=len(decl); D(f'@k{k}_{name} = global i256 {val | (random.getrandbits(256) & ~((1<<W)-1))}')
    a=t(); body.append(f'  {a} = load volatile i256, ptr @k{k}_{name}')
    b=t(); body.append(f'  {b} = trunc i256 {a} to i{W}'); return b
def tpl(op, W, sfx, shape, ref, rw=None):   # shape letter c: an i1 handle (select's condition); rw: result width
    name=f'cq_template_{op}_i{W}{sfx}'; ty=f'i{W}'; rw=rw or W; rty=f'i{rw}'
    ws=[1 if s=='c' else W for s in shape]
    vals=[random.getrandbits(w) for w in ws]
    args=[]
    for s,v,w in zip(shape,vals,ws):
        x=lit(name, w, v)
        if s in 'qc': h=t(); body.append(f'  {h} = call i32 @cqrt_alloc_i{w}(i{w} {x})'); args.append(f'i32 {h}')
        else: args.append(f'{ty} {x}')
    ps=', '.join('i32' if s in 'qc' else ty for s in shape)
    D(f'declare i32 @{name}({ps})'); D(f'declare void @{name}_unc(i32, {ps})')
    want=ref(W, *vals) & ((1<<rw)-1)
    r=t(); body.append(f'  {r} = call i32 @{name}({", ".join(args)})')
    o=t(); body.append(f'  {o} = call i32 @cqrt_alloc_{rty}({rty} 0)')
    body.append(f'  call void @{name}_unc(i32 {o}, {", ".join(args)})')
    for h,what in ((r,name),(o,name+'_unc')):
        m=t(); body.append(f'  {m} = call {ext.get(rw,"")}{rty} @cqrt_measure_{rty}(i32 {h})')
        ok=t(); body.append(f'  {ok} = icmp eq {rty} {m}, {want}'); check(f'i{W}: {what} value', ok)
def fshl(W,a,b,k): k%=W; return a if not k else a<<k | b>>(W-k)
def fshr(W,a,b,k): k%=W; return b if not k else a<<(W-k) | b>>k
shapes=[('_qql','qql'),('_qlq','qlq'),('_qll','qll'),('_lqq','lqq'),('_lql','lql'),('_llq','llq')]
bins=[('sub','_hl','ql',lambda W,a,b:a-b),('sub','_lh','lq',lambda W,a,b:a-b),('and','_hl','ql',lambda W,a,b:a&b),
      ('shl','_hl','ql',lambda W,a,b:a<<(b%W)),('lshr','_lh','lq',lambda W,a,b:a>>(b%W))]
for W in [w for w in ints if w>128]+[128,100,17]:
    for op,ref in (('fshl',fshl),('fshr',fshr)):
        for sfx,sh in shapes: tpl(op, W, sfx, sh, ref)
    for op,sfx,sh,ref in bins: tpl(op, W, sfx, sh, ref)
    # kmgy.14: select in every shape (_ll at i200: arm2 after r9 is left unused), compares (i1 result), min/max, abs.
    for sfx,sh in (('','cqq'),('_hl','cql'),('_lh','clq'),('_ll','cll')): tpl('select', W, sfx, sh, lambda W,c,a,b: a if c else b)
    sg=lambda W,x: x-(1<<W) if x>>(W-1) else x
    tpl('icmp_slt', W, '_hl', 'ql', lambda W,a,b: int(sg(W,a)<sg(W,b)), 1)
    tpl('icmp_eq', W, '_hl', 'ql', lambda W,a,b: int(a==b), 1)
    tpl('smax', W, '_hl', 'ql', lambda W,a,b: max(sg(W,a),sg(W,b)))
    tpl('abs', W, '', 'q', lambda W,a: abs(sg(W,a)))
# Tape: a wide literal field echoed back by a host callee of the driver (tape family control).
for W in [w for w in ints if w>128]:
    ty=f'i{W}'; v=random.getrandbits(W); i=len(strs); nm=f'cq_ir_echo_{ty}'
    D(f'@.e{i} = private constant [{len(nm)+1} x i8] c"{nm}\\00"')
    D('declare i32 @cqrt_tape_alloc()'); D(f'declare void @cqrt_tape_write_{ty}_lit(i32, {ty})')
    D(f'declare i32 @cqrt_tape_effect_{ty}_controlled(i32, i32, ptr, i32)')
    tp=t(); body.append(f'  {tp} = call i32 @cqrt_tape_alloc()')
    body.append(f'  call void @cqrt_tape_write_{ty}_lit(i32 {tp}, {ty} {lit("tape", W, v)})')
    c=t(); body.append(f'  {c} = call i32 @cqrt_alloc_i1(i1 1)')
    h=t(); body.append(f'  {h} = call i32 @cqrt_tape_effect_{ty}_controlled(i32 {c}, i32 {tp}, ptr @.e{i}, i32 1)')
    m=t(); body.append(f'  {m} = call {ty} @cqrt_measure_{ty}(i32 {h})')
    ok=t(); body.append(f'  {ok} = icmp eq {ty} {m}, {v}'); check(f'{ty}: cqrt_tape_write_{ty}_lit value', ok)
for i,s in enumerate(strs): D(f'@.s{i} = private constant [{len(s)+7} x i8] c"FAIL {s}\\0A\\00"')
W=','.join([f'i{w}' for w in ints]+[f[0] for f in fps])+','
print('; Generated by tests/ir/gen.py (python3 gen.py > caller.ll). Do not edit.')
print(f'@cq_ir_widths = constant [{len(W)+1} x i8] c"{W}\\00"')
print('\n'.join(sorted(decl)))
print('define i32 @cq_ir_caller() {\n  %fail = alloca i32\n  store i32 0, ptr %fail')
print('\n'.join(body))
print('  %r = load i32, ptr %fail\n  ret i32 %r\n}')
