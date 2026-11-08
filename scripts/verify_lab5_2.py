"""Actions-only Lab5-2 verification. Never compiled or executed on the local host.

Sections and their expected counts are asserted, so an empty sweep can never
report 0/0 as success:
  1. official 20 virtual-assembly cases on the unmodified provided interpreter;
  2. 35 extended programs through scripts/run_lab5_2_extended.py, which only
     extends the interpreter for extra programs and never replaces the
     official path above;
  3. five adapter boundary fixtures for signed division truncation toward
     zero, wide numbered virtual registers, string-library escapes and
     unmapped-memory rejection;
  4. text metadata checks over every generated assembly file (prologue /
     framesize / epilogue, cqto-idivq pairing, frame-pointer addressing
     shape, jump targets and branch pairs, outgoing-argument timing with six
     ABI registers plus outgoing stack slots);
  5. the copied Lab5-1 77-item regression, executed unchanged.

Known blind spots are recorded in scope: the ReturnSink Oper prints nothing,
so caller-save/return-sink use/def metadata cannot be checked from printed
text; private grading or native register allocation is not implied.
"""
import hashlib,json,os,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'reports/lab5-2-results'
OFFICIAL_FILES=17
OFFICIAL_CASES=20
EXTENDED_CASES=35
BOUNDARY_FIXTURES=5
REGRESSION_CHECKS=77
REG_RECORDS={'print','printi','init_array','alloc_record','ord','getchar','chr','string_equal','size','concat','substring','string_compare','flush','exit','not'}
ABI=('%rdi','%rsi','%rdx','%rcx','%r8','%r9')
CONDITIONALS={'je','jne','jg','jge','jl','jle','ja','jae','jb','jbe'}
JUMPS=CONDITIONALS|{'jmp'}

def run(args,**kwargs):return subprocess.run(args,cwd=ROOT,text=True,capture_output=True,**kwargs)

def normalized(text):
    # Same tolerance as the official `diff -w -B` comparison: only real blanks
    # collapse. Line content is preserved and letters are never deleted.
    return [re.sub(r'[ \t\r\v\f]','',line) for line in text.splitlines() if line.strip()]

def code_lines(path):
    return [line.strip() for line in Path(path).read_text(errors='replace').splitlines() if line.strip()]

def abistore(line):
    """Return (source, dst) of `movq x, y`, else None."""
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

def memory_offset(line,token):
    m=re.fullmatch(r'movq (\S+), (\d+|0x[0-9a-fA-F]+)\(%rsp\)',line)
    if not m:return None
    raw=m.group(2)
    return int(raw,16) if raw.startswith('0x') else int(raw)

def metadata_checks(gen_path):
    problems=[]
    lines=code_lines(gen_path)
    if not lines:problems.append('empty generated assembly')
    labels={line[:-1] for line in lines if line.endswith(':') and not line.startswith('.')}
    functions=parse_functions(lines)
    for function in functions:
        fn=function['name'];body=function['body']
        if not body:
            problems.append(f'proc {fn} has no lines');continue
        if not body[0]=='.type '+fn+', @function':
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
            head=line.split(' ',1)[0]
            if head=='idivq' and prev!='cqto':
                problems.append(f'in {fn} idivq not preceded by cqto: {prev!r}')
            if head in JUMPS:
                target=line.split(' ',1)[1].strip()
                if target not in labels:problems.append(f'in {fn} jump target not defined: {line!r}')
                if head in CONDITIONALS:
                    if not prev or not prev.startswith('cmpq'):
                        problems.append(f'in {fn} conditional {head} not right after cmpq: {prev!r}')
                    nxt=None
                    for later in instr[idx+1:]:
                        if not later.startswith('.'):
                            nxt=later;break
                    if nxt is None or not nxt.startswith('jmp '):
                        problems.append(f'in {fn} conditional {head} not followed by jmp: {nxt!r}')
            for token in line.split():
                token=token.rstrip(',')
                if 'framesize' in token and '(%rsp)' in token:
                    if not re.fullmatch(r'[A-Za-z_]\w*_framesize(?:[+-](?:\d+|0x[0-9a-fA-F]+))?\(%rsp\)',token):
                        problems.append(f'in {fn} unexpected framesize operand {token!r}')
            prev=line
        call_window_checks(instr,fn,problems)
    if not any(f['name']=='tigermain' for f in functions):
        problems.append('no tigermain proc found in generated assembly')
    return problems

def call_window_checks(instr,fn,problems):
    bound=-1
    for pos,line in enumerate(instr):
        if not line.startswith('call '):continue
        target=line.split(' ',1)[1].strip()
        window=instr[max(bound+1,0):pos]
        bound=pos
        abi=[(i,abistore(l)) for i,l in enumerate(window) if abistore(l) and abistore(l)[1] in ABI]
        if not abi:continue
        order=[moved[1] for i,moved in abi]
        segment=window[abi[0][0]:]
        for l in segment:
            moved=abistore(l)
            if moved and moved[1] in ABI:continue
            if re.fullmatch(r'movq \S+, (?:\d+|0x[0-9a-fA-F]+)\(%rsp\)',l):continue
            problems.append(f'call to {fn}->{target}: work after first ABI fill: {l!r}')
            break
        if order!=list(ABI[:len(order)]):
            problems.append(f'call to {fn}->{target}: ABI fills not an ordered prefix: {order!r}')
        offs=[memory_offset(l,0) for l in window if re.fullmatch(r'movq \S+, (?:\d+|0x[0-9a-fA-F]+)\(%rsp\)',l)]
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
        (OUT/'results.json').write_text(json.dumps({'commit':sha,'scope':'Lab5-2 instruction selection on generated virtual-register assembly: official interpreter path untouched for the official 20 cases; extended 35 run on the explicit adapter; ReturnSink use/def is IR-level only and invisible in printed text; no native allocation or private grading claim','tests':records,'passed':sum(1 for x in records if x['passed']),'total':len(records)},indent=2)+'\n')
    try:
        for args,label in [(['cmake','-S','.','-B','build','-DCMAKE_BUILD_TYPE=Release'],'configure'),(['cmake','--build','build','--parallel','2','--target','test_codegen'],'build')]:
            p=run(args,timeout=300)
            with (OUT/'build.log').open('a') as f:f.write(p.stdout+p.stderr)
            if p.returncode:
                records.append({'kind':'build','name':label,'passed':False})
                raise RuntimeError(f'{label} failed; inspect build.log')
        official=sorted((ROOT/'testdata/lab5or6/testcases').glob('*.tig'))
        if len(official)!=OFFICIAL_FILES:
            records.append({'kind':'fixture_inventory','name':'official tig files','passed':False,'found':len(official),'expected':OFFICIAL_FILES})
            raise SystemExit('official testcase inventory mismatch; do not accept')
        extended=json.loads((ROOT/'scripts/lab5_2_cases.json').read_text(encoding='utf-8'))
        if len(extended)!=EXTENDED_CASES:
            records.append({'kind':'fixture_inventory','name':'extended cases json','passed':False,'found':len(extended),'expected':EXTENDED_CASES})
            raise SystemExit('extended case inventory mismatch; do not accept')
        cases_dir=OUT/'cases';cases_dir.mkdir(parents=True,exist_ok=True)
        # Section 1: official 20 cases, always on the unmodified interpreter.
        for source in official:
            target=cases_dir/source.name
            target.write_bytes(source.read_bytes())
            compiled=run([str(ROOT/'build/test_codegen'),str(target)],timeout=30)
            (OUT/f'compile-{source.stem}.txt').write_text(compiled.stdout+compiled.stderr)
            inputs=sorted((source.parent/'merge').glob('*.in')) if source.stem=='merge' else [None]
            for stdin in inputs:
                name=source.stem+('/'+stdin.stem if stdin else '')
                ref=ROOT/'testdata/lab5or6/refs'/('merge/'+stdin.stem+'.out' if stdin else source.stem+'.out')
                if compiled.returncode:
                    records.append({'kind':'official_virtual_assembly','name':name,'passed':False,'stage':'compile','exit':compiled.returncode})
                    continue
                p=run([sys.executable,str(ROOT/'scripts/lab5_test/main.py'),str(target)+'.s'],input=stdin.read_text() if stdin else '',timeout=60)
                (OUT/(name.replace('/','-')+'.txt')).write_text(p.stdout+'\nSTDERR\n'+p.stderr)
                records.append({'kind':'official_virtual_assembly','name':name,'passed':p.returncode==0 and normalized(p.stdout)==normalized(ref.read_text()),'exit':p.returncode,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'reference_sha256':hashlib.sha256(ref.read_bytes()).hexdigest()})
        official_records=[x for x in records if x['kind']=='official_virtual_assembly']
        if len(official_records)!=OFFICIAL_CASES:
            raise SystemExit(f'official case count {len(official_records)} != {OFFICIAL_CASES}; inventory changed, do not accept')
        # Section 2: extended 35 programs through the explicit adapter.
        ext_dir=OUT/'extended';ext_dir.mkdir(parents=True,exist_ok=True)
        for name,program,expected_stdout,expected_result in extended:
            path=ext_dir/f'{name}.tig'
            path.write_text(program,encoding='utf-8')
            compiled=run([str(ROOT/'build/test_codegen'),str(path)],timeout=30)
            (OUT/f'ext-compile-{name}.txt').write_text(compiled.stdout+compiled.stderr)
            if compiled.returncode:
                records.append({'kind':'extended_adapter','name':name,'passed':False,'stage':'codegen','exit':compiled.returncode})
                continue
            p=run([sys.executable,str(ROOT/'scripts/run_lab5_2_extended.py'),str(path)+'.s'],timeout=60)
            (OUT/f'ext-{name}.txt').write_text(p.stdout+'\nSTDERR\n'+p.stderr)
            match=re.search(r'ASM_RESULT (-?\d+)',p.stderr)
            records.append({'kind':'extended_adapter','name':name,'passed':p.returncode==0 and bool(match) and int(match.group(1))==expected_result and normalized(p.stdout)==normalized(expected_stdout),'exit':p.returncode,'expected_stdout':expected_stdout,'actual_stdout':p.stdout,'expected_result':expected_result,'actual_result':int(match.group(1)) if match else None})
        extended_records=[x for x in records if x['kind']=='extended_adapter']
        if len(extended_records)!=EXTENDED_CASES:
            raise SystemExit(f'extended records {len(extended_records)} != {EXTENDED_CASES}')
        # Section 3: adapter boundary fixtures.
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
        # Section 4: metadata checks over every generated assembly file.
        metadata_targets=sorted(cases_dir.glob('*.tig.s'))+sorted(ext_dir.glob('*.tig.s'))
        if len(metadata_targets)!=OFFICIAL_FILES+EXTENDED_CASES:
            raise SystemExit(f'metadata target count {len(metadata_targets)} != {OFFICIAL_FILES+EXTENDED_CASES}')
        for path in metadata_targets:
            problems=metadata_checks(path)
            records.append({'kind':'asm_flow_metadata','name':path.stem,'passed':not problems,'problems':problems,'checks':['prologue_and_epilogue','idivq_after_cqto','frame_pointer_shapes','jump_targets_and_branch_pairs','outgoing_arguments_and_stack_slots']})
        # Section 5: copied Lab5-1 regression, executed unchanged.
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
