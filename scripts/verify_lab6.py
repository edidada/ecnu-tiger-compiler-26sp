"""Actions-only Lab6 verification: register allocation on generated assembly.

Never compiled or executed on the local host; everything runs on GitHub
Actions. Sections and counts are asserted so an empty sweep can never be
reported as success:
  1. official 20 cases compiled with the Lab6 `tiger-compiler` target
     (allocation enabled) and executed on the UNMODIFIED provided
     interpreter: 20/20;
  2. 35 extended programs through the same RA path plus the explicit
     scripts/run_lab5_2_extended.py adapter: 35/35;
  3. 5 adapter boundary fixtures (signed idiv truncation, wide register
     numbering, string escapes, unmapped-memory rejection): 5/5;
  4. flow metadata over every generated assembly (52 files): prologue /
     framesize / epilogue, cqto-idivq pairing (spill reloads tolerated and
     recorded), frame-pointer addressing shape, jump targets and branch
     pairs, outgoing-argument timing, and — Lab6-specific — zero `t<num>`
     virtual temporaries anywhere in a procedure body;
  5. the copied Lab5-1 regression, executed unchanged: 77/77 internal.

Recorded blind spots: the ReturnSink Oper prints nothing, so caller-save /
return-sink use/def metadata stays IR-level; spill traffic is allowed inside
the call window and around idivq without any proof that a spill was required;
iron interpreter semantics are unchanged and no native machine execution or
private grading is implied.
"""
import hashlib,json,os,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'reports/lab6-results'
OFFICIAL_FILES=17
OFFICIAL_CASES=20
EXTENDED_CASES=35
BOUNDARY_FIXTURES=5
REGRESSION_CHECKS=77
REG_RECORDS={'print','printi','init_array','alloc_record','ord','getchar','chr','string_equal','size','concat','substring','string_compare','flush','exit','not'}
ABI=('%rdi','%rsi','%rdx','%rcx','%r8','%r9')
CONDITIONALS={'je','jne','jg','jge','jl','jle','ja','jae','jb','jbe'}
JUMPS=CONDITIONALS|{'jmp'}
FRAMESIZE=r'[A-Za-z_]\w*_framesize(?:[+-]\d+)?'
SPILL_LOAD=re.compile(r'movq '+FRAMESIZE+r'\(%rsp\), %r[a-z0-9]+$')
SPILL_STORE=re.compile(r'movq %r[a-z0-9]+, '+FRAMESIZE+r'\(%rsp\)$')
STACK_STORE=re.compile(r'movq \S+, (?:\d+|0x[0-9a-fA-F]+)\(%rsp\)$')

def run(args,**kwargs):return subprocess.run(args,cwd=ROOT,text=True,capture_output=True,**kwargs)

def announce(msg):print(f'[lab6] {msg}',flush=True)

def normalized(text):
    return [re.sub(r'[ \t\r\v\f]','',line) for line in text.splitlines() if line.strip()]

def code_lines(path):
    return [line.strip() for line in Path(path).read_text(errors='replace').splitlines() if line.strip()]

def abistore(line):
    if line.startswith('movq ') and ', ' in line:
        left,right=line[5:].split(', ',1)
        return left,right.strip()
    return None

def parse_functions(lines):
    functions=[];current=None
    for line in lines:
        if line.startswith('.globl '):
            current={'name':line.split(' ',1)[1],'body':[]};functions.append(current)
        elif line.startswith('.size '):
            current=None
        elif current is not None:
            current['body'].append(line)
    return functions

def is_spill(line):
    return bool(SPILL_LOAD.fullmatch(line) or SPILL_STORE.fullmatch(line))

def metadata_checks(gen_path):
    problems=[]
    virtual=[]
    lines=code_lines(gen_path)
    if not lines:problems.append('empty generated assembly')
    labels={line[:-1] for line in lines if line.endswith(':') and not line.startswith('.')}
    functions=parse_functions(lines)
    for function in functions:
        fn=function['name'];body=function['body']
        if not body:
            problems.append(f'proc {fn} has no lines');continue
        if body[0]!='.type '+fn+', @function':
            problems.append(f'proc {fn} missing .type header: {body[0]}')
        m=re.fullmatch(r'\.set '+re.escape(fn)+r'_framesize, (\d+)',body[1]) if len(body)>1 else None
        size=None
        if not m:
            problems.append(f'proc {fn} missing .set {fn}_framesize line')
        else:
            size=int(m.group(1))
            if body[2]!=fn+':':problems.append(f'proc {fn} label not after .set: {body[2]}')
            if body[3]!='subq $'+str(size)+', %rsp':problems.append(f'proc {fn} prologue not subq ${size}, %rsp: {body[3]}')
        instr=[line for line in body if not (line.endswith(':') and not line.startswith('.')) and not line.startswith('.')]
        if not instr:
            problems.append(f'proc {fn} has no instructions');continue
        if instr[-1]!='retq':
            problems.append(f'proc {fn} does not end with retq: {instr[-1]}')
        elif len(instr)<2 or instr[-2]!='addq $'+str(size)+', %rsp':
            problems.append(f'proc {fn} epilogue not addq ${size}, %rsp: {instr[-2] if len(instr)>1 else None}')
        prev=None
        for idx,line in enumerate(instr):
            token=line.split(' ',1)[0]
            if re.search(r'\bt\d+\b',line):
                virtual.append(f'in {fn}: {line}')
            if token=='idivq':
                back=idx-1
                while back>=0 and is_spill(instr[back]):
                    back-=1
                if back<0 or instr[back]!='cqto':
                    problems.append(f'in {fn} idivq preceded by spill traffic without cqto: {instr[back] if back>=0 else None!r}')
            if token in JUMPS:
                target=line.split(' ',1)[1].strip()
                if target not in labels:problems.append(f'in {fn} jump target not defined: {line!r}')
                if token in CONDITIONALS:
                    if not prev or not prev.startswith('cmpq'):
                        problems.append(f'in {fn} conditional {token} not right after cmpq: {prev!r}')
                    nxt=None
                    for later in instr[idx+1:]:
                        if not later.startswith('.'):
                            nxt=later;break
                    if nxt is None or not nxt.startswith('jmp '):
                        problems.append(f'in {fn} conditional {token} not followed by jmp: {nxt!r}')
            for piece in line.split():
                piece=piece.rstrip(',')
                if 'framesize' in piece and '(%rsp)' in piece:
                    if not re.fullmatch(FRAMESIZE+r'\(%rsp\)',piece):
                        problems.append(f'in {fn} unexpected framesize operand {piece!r}')
            prev=line
        call_window_checks(instr,fn,problems)
    if virtual:problems[:0]=virtual[:3]
    if not any(f['name']=='tigermain' for f in functions):
        problems.append('no tigermain proc found in generated assembly')
    return sorted(set(problems))[:8]+(['...'] if len(set(problems))>8 else [])

def call_window_checks(instr,fn,problems):
    bound=-1
    for pos,line in enumerate(instr):
        if not line.startswith('call '):continue
        target=line.split(' ',1)[1].strip()
        window=instr[max(bound+1,0):pos]
        bound=pos
        abi=[i for i,l in enumerate(window) if abistore(l) and abistore(l)[1] in ABI]
        if not abi:continue
        order=[abistore(window[i])[1] for i in abi]
        segment=window[abi[0]:]
        for l in segment:
            moved=abistore(l)
            if moved and moved[1] in ABI:continue
            if STACK_STORE.fullmatch(l) or is_spill(l):continue
            problems.append(f'call to {fn}->{target}: work after first ABI fill: {l!r}')
            break
        if order!=list(ABI[:len(order)]):
            problems.append(f'call to {fn}->{target}: ABI fills not an ordered prefix: {order!r}')
        offs=[]
        for l in window:
            if STACK_STORE.fullmatch(l):
                m=re.fullmatch(r'movq (\S+), (\d+|0x[0-9a-fA-F]+)\(%rsp\)',l)
                raw=m.group(2)
                offs.append(int(raw,16) if raw.startswith('0x') else int(raw))
        if offs:
            if len(order)!=6:
                problems.append(f'call to {fn}->{target}: stack args with {len(order)} ABI writes, not six')
            if offs!=list(range(0,8*len(offs),8)):
                problems.append(f'call to {fn}->{target}: outgoing stack offsets not consecutive words: {offs!r}')

BOUNDARY_FIXTURES_LIST=[
 ('adapter_accepts_wide_temp_registers','tigermain:\nmovq $7, t1234\nmovq t1234, %rax\nretq\n','','ASM_RESULT 7'),
 ('idiv_truncates_toward_zero','tigermain:\nmovq $-17, %rax\nidivq $5\nretq\n','','ASM_RESULT -3'),
 ('idiv_remainder_toward_zero','tigermain:\nmovq $-17, %rax\nidivq $5\nmovq %rdx, %rax\nretq\n','','ASM_RESULT -2'),
 ('adapter_rejects_unmapped_memory','tigermain:\nmovq 1234, %rax\nretq\n','','Unmapped assembly memory read'),
 ('adapter_preserves_string_escapes','.text\ntigermain:\nmovq $0x400000, %rdi\ncall size\nretq\n.section .rodata\nL0:\n.long 7\n.string "a\\"b\\\\c\\000d"\n','','ASM_RESULT 7'),
]

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    records=[]
    def lock():
        sha=os.getenv('GITHUB_SHA') or run(['git','rev-parse','HEAD']).stdout.strip()
        (OUT/'results.json').write_text(json.dumps({'commit':sha,'scope':'Lab6 graph-coloring register allocation over the Lab5-2 instruction selector: official 20 stay on the unmodified interpreter, extended 35 use the explicit adapter, spill rewriting runs until no spills remain; ReturnSink use/def stays IR-level; no native machine execution or private grading claim','tests':records,'passed':sum(1 for x in records if x['passed']),'total':len(records)},indent=2)+'\n')
    try:
        for args,label in [(['cmake','-S','.','-B','build','-DCMAKE_BUILD_TYPE=Release'],'configure'),(['cmake','--build','build','--parallel','2','--target','tiger-compiler'],'build')]:
            announce(f'{label} start')
            p=run(args,timeout=1200)
            announce(f'{label} rc={p.returncode}')
            with (OUT/'build.log').open('a') as f:f.write(p.stdout+p.stderr)
            if p.returncode:
                records.append({'kind':'build','name':label,'passed':False})
                print(p.stdout[-4000:]+p.stderr[-4000:])
                raise RuntimeError(f'{label} failed; inspect build.log')
        official=sorted((ROOT/'testdata/lab5or6/testcases').glob('*.tig'))
        announce('inventory loaded')
        if len(official)!=OFFICIAL_FILES:
            records.append({'kind':'fixture_inventory','name':'official tig files','passed':False,'found':len(official),'expected':OFFICIAL_FILES})
            raise SystemExit('official testcase inventory mismatch; do not accept')
        extended=json.loads((ROOT/'scripts/lab5_2_cases.json').read_text(encoding='utf-8'))
        announce(f'extended cases {len(extended)}')
        if len(extended)!=EXTENDED_CASES:
            records.append({'kind':'fixture_inventory','name':'extended cases json','passed':False,'found':len(extended),'expected':EXTENDED_CASES})
            raise SystemExit('extended case inventory mismatch; do not accept')
        cases_dir=OUT/'cases';cases_dir.mkdir(parents=True,exist_ok=True)
        log_harness=OUT/'compiler-logs';log_harness.mkdir(parents=True,exist_ok=True)
        for i,source in enumerate(sorted(official)):
            announce(f'official {i+1}/{OFFICIAL_FILES}: {source.stem}')
            target=cases_dir/source.name
            target.write_bytes(source.read_bytes())
            logging=run([str(ROOT/'build/tiger-compiler'),str(target)],timeout=60)
            (log_harness/f'{source.stem}.txt').write_text(logging.stdout+'\nSTDERR\n'+logging.stderr)
            if logging.returncode:
                records.append({'kind':'official_ra_execution','name':source.stem,'passed':False,'stage':'compiler','exit':logging.returncode});
                continue
            asm=cases_dir/(source.name+'.s')
            inputs=sorted((source.parent/'merge').glob('*.in')) if source.stem=='merge' else [None]
            for stdin in inputs:
                name=source.stem+('/'+stdin.stem if stdin else '')
                ref=ROOT/'testdata/lab5or6/refs'/('merge/'+stdin.stem+'.out' if stdin else source.stem+'.out')
                p=run([sys.executable,str(ROOT/'scripts/lab5_test/main.py'),str(asm)],input=stdin.read_text() if stdin else '',timeout=60)
                (OUT/(name.replace('/','-')+'.txt')).write_text(p.stdout+'\nSTDERR\n'+p.stderr)
                records.append({'kind':'official_ra_execution','name':name,'passed':p.returncode==0 and normalized(p.stdout)==normalized(ref.read_text()),'exit':p.returncode,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'reference_sha256':hashlib.sha256(ref.read_bytes()).hexdigest()})
        official_records=[x for x in records if x['kind']=='official_ra_execution']
        if len(official_records)!=OFFICIAL_CASES:
            raise SystemExit(f'official records {len(official_records)} != {OFFICIAL_CASES}; inventory changed, do not accept')
        ext_dir=OUT/'extended';ext_dir.mkdir(parents=True,exist_ok=True)
        for name,program,expected_stdout,expected_result in extended:
            path=ext_dir/f'{name}.tig'
            path.write_text(program,encoding='utf-8')
            compiled=run([str(ROOT/'build/tiger-compiler'),str(path)],timeout=60)
            (OUT/f'ext-compile-{name}.txt').write_text(compiled.stdout+compiled.stderr)
            if compiled.returncode:
                records.append({'kind':'extended_ra_adapter','name':name,'passed':False,'stage':'compiler','exit':compiled.returncode})
                continue
            p=run([sys.executable,str(ROOT/'scripts/run_lab5_2_extended.py'),str(path)+'.s'],timeout=60)
            (OUT/f'ext-{name}.txt').write_text(p.stdout+'\nSTDERR\n'+p.stderr)
            match=re.search(r'ASM_RESULT (-?\d+)',p.stderr)
            records.append({'kind':'extended_ra_adapter','name':name,'passed':p.returncode==0 and bool(match) and int(match.group(1))==expected_result and normalized(p.stdout)==normalized(expected_stdout),'exit':p.returncode,'expected_stdout':expected_stdout,'actual_stdout':p.stdout,'expected_result':expected_result,'actual_result':int(match.group(1)) if match else None})
        extended_records=[x for x in records if x['kind']=='extended_ra_adapter']
        if len(extended_records)!=EXTENDED_CASES:
            raise SystemExit(f'extended records {len(extended_records)} != {EXTENDED_CASES}')
        fix_dir=OUT/'adapter-fixtures';fix_dir.mkdir(parents=True,exist_ok=True)
        for name,assembly,expected_stdout,marker in BOUNDARY_FIXTURES_LIST:
            path=fix_dir/f'{name}.s'
            path.write_text(assembly,encoding='utf-8')
            p=run([sys.executable,str(ROOT/'scripts/run_lab5_2_extended.py'),str(path)],timeout=30)
            (OUT/f'fixture-{name}.txt').write_text(p.stdout+'\nSTDERR\n'+p.stderr)
            rejected=name=='adapter_rejects_unmapped_memory'
            records.append({'kind':'adapter_boundary','name':name,'passed':(p.returncode!=0 and marker in p.stderr) if rejected else (p.returncode==0 and marker in p.stderr and p.stdout==expected_stdout),'exit':p.returncode,'marker':marker})
        fixture_records=[x for x in records if x['kind']=='adapter_boundary']
        if len(fixture_records)!=BOUNDARY_FIXTURES:
            raise SystemExit(f'boundary fixture records {len(fixture_records)} != {BOUNDARY_FIXTURES}')
        metadata_targets=sorted(cases_dir.glob('*.tig.s'))+sorted(ext_dir.glob('*.tig.s'))
        if len(metadata_targets)!=OFFICIAL_FILES+EXTENDED_CASES:
            raise SystemExit(f'metadata target count {len(metadata_targets)} != {OFFICIAL_FILES+EXTENDED_CASES}')
        for path in metadata_targets:
            problems=metadata_checks(path)
            records.append({'kind':'asm_flow_metadata','name':path.stem,'passed':not problems,'problems':problems,'checks':['prologue_and_epilogue','idivq_after_cqtolike_spill_traffic','no_virtual_temps','frame_pointer_shapes','jump_targets_and_branch_pairs','outgoing_arguments_and_stack_slots']})
        p=run([sys.executable,str(ROOT/'scripts/verify_lab5_1.py')],timeout=1200)
        summary={}
        try:summary=json.loads((ROOT/'reports/lab5-1-results/results.json').read_text())
        except OSError:pass
        records.append({'kind':'lab5_1_regression','name':'copied verify_lab5_1.py','passed':p.returncode==0 and summary.get('passed')==summary.get('total')==REGRESSION_CHECKS,'exit':p.returncode,'passed_count':summary.get('passed'),'total':summary.get('total'),'expected':REGRESSION_CHECKS})
    finally:
        lock()
    failed=[f"{x['kind']}:{x['name']}"+(' problems: '+str(x['problems'][:3]) if x.get('problems') else '') for x in records if not x['passed']]
    print(f'{len(records)-len(failed)}/{len(records)} passed')
    if failed:
        raise SystemExit('Failed: '+', '.join(failed))
if __name__=='__main__':main()
