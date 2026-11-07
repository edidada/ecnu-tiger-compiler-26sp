"""Actions-only Release build, unchanged official AST refs, and IR execution."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'reports/lab5-1-results'
POSITIVE=[
 ('literal','42','',42),
 ('arithmetic','(7+5)*3-8/2','',32),
 ('factorial','let function f(n:int):int=if n=0 then 1 else n*f(n-1) in f(5) end','',120),
 ('capture','let var x:=1 function bump(d:int):int=(x:=x+d;x) in printi(bump(2));printi(bump(3));x end','36',6),
 ('three_levels','let var x:=5 function outer(a:int):int=let var b:=7 function middle(c:int):int=let function inner(d:int):int=x+a+b+c+d in inner(11) end in middle(3) end in outer(2) end','',28),
 ('mutual','let function even(n:int):int=if n=0 then 1 else odd(n-1) function odd(n:int):int=if n=0 then 0 else even(n-1) in even(10)+odd(9) end','',2),
 ('stack_formals','let var z:=10 function f(a:int,b:int,c:int,d:int,e:int,f:int,g:int,h:int):int=a+b+c+d+e+f+g+h+z in f(1,2,3,4,5,6,7,8) end','',46),
 ('arg_order','let var x:=0 function bump():int=(x:=x+1;x) function combine(a:int,b:int,c:int):int=100*a+10*b+c in combine(bump(),bump(),bump()) end','',123),
 ('record_alias','let type r={a:int,b:int} var x:=r{a=3,b=4} var y:=x in y.a:=9;x.a+x.b end','',13),
 ('recursive_record','let type node={value:int,next:node} var x:node:=nil in x:=node{value=7,next=nil};if x.next=nil then x.value else 0 end','',7),
 ('array_alias','let type a=array of int var x:=a[4] of 2 var y:=x in y[2]:=9;x[0]+x[2] end','',11),
 ('array_init_once','let type a=array of int var n:=0 function bump():int=(n:=n+1;n) var x:=a[3] of bump() in x[0]+x[2]+n end','',3),
 ('record_init_order','let type r={a:int,b:int} var n:=0 function bump():int=(n:=n+1;n) var x:=r{a=bump(),b=bump()} in n*100+x.a*10+x.b end','',212),
 ('shadow_initializer','let var x:=9 var x:=let function get():int=x in get() end in x end','',9),
 ('param_shadow','let var x:=99 function f(x:int):int=let function g():int=x in g() end in f(7)+x end','',106),
 ('loop_bound_once','let var calls:=0 var sum:=0 function limit():int=(calls:=calls+1;3) in for i:=0 to limit() do sum:=sum+i;sum+calls*10 end','',16),
 ('loop_max','let var n:=0 in for i:=2147483647 to 2147483647 do n:=n+1;n end','',1),
 ('empty_loop','let var n:=0 in for i:=3 to 2 do n:=n+1;n end','',0),
 ('loop_capture','let var sum:=0 in for i:=1 to 3 do let function get():int=i in sum:=sum+get() end;sum end','',6),
 ('while_break','let var n:=0 in while n<10 do (n:=n+1;if n=3 then break);n end','',3),
 ('break_inside_eseq','let var n:=0 in while 1 do n:=n+(if 1 then (break;1) else 2);n end','',0),
 ('short_skip','let var n:=0 function bump():int=(n:=n+1;n) in 0&bump();1|bump();n end','',0),
 ('short_run','let var n:=0 function bump():int=(n:=n+1;n) in 1&bump();0|bump();n end','',2),
 ('discard_condition','let var n:=0 in 1<2;n:=7;if 2<3 then n else 0 end','',7),
 ('nested_if','if 2<3 then (if 4>1 then 5 else 6) else 7','',5),
 ('nil_join','let type r={a:int} function f():r=if 1 then nil else r{a=1} in if f()=nil then 1 else 0 end','',1),
 ('string_functions','(print(concat("ab",substring("cdef",0,2)));ord("A")+size("hi"))','abcd',67),
 ('string_compare','(if "abc"<"abd" then 10 else 0)+(if concat("a","b")="ab" then 1 else 0)','',11),
 ('reference_equality','let type r={a:int} var x:=r{a=1} var y:=r{a=1} var z:=x in (if x=y then 10 else 0)+(if x=z then 1 else 0) end','',1),
 ('type_alias','let type a=array of int type b=a var x:=a[2] of 4 var y:b:=x in y[1]:=8;x[1] end','',8),
]
NEGATIVE=[
 ('unbound','missing'),('outside_break','break'),
 ('function_break','while 1 do let function f()=break in f() end'),
 ('readonly_index','for i:=0 to 2 do i:=1'),
 ('bad_type','let var x:int:="bad" in x end'),
 ('nil_inference','let var x:=nil in x end'),
 ('type_cycle','let type a=b type b=a in 0 end'),
 ('call_arity','let function f(x:int):int=x in f() end'),
]

def call(args,**kw):
    return subprocess.run(args,cwd=ROOT,text=True,capture_output=True,**kw)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    records=[]
    try:
        for args in [['cmake','-S','.','-B','build','-DCMAKE_BUILD_TYPE=Release'],['cmake','--build','build','--parallel','2','--target','test_translate','lab5_ir','lab5_frame','test_lex','test_parse','test_semant']]:
            p=call(args,timeout=300)
            with (OUT/'build.log').open('a',encoding='utf-8') as f:f.write(p.stdout+p.stderr)
            if p.returncode:raise RuntimeError('Remote Release build failed; inspect build.log')
        for path in sorted((ROOT/'testdata/lab5or6/testcases').glob('*.tig')):
            ref=ROOT/'testdata/lab5or6/refs-part1'/f'{path.stem}.out'
            p=call([str(ROOT/'build/test_translate'),str(path)],timeout=30)
            actual=p.stdout+p.stderr;expected=ref.read_text()
            (OUT/f'official-{path.stem}.txt').write_text(actual)
            records.append({'kind':'official_escape_ast','name':path.stem,'passed':p.returncode==0 and actual==expected,'reference_sha256':hashlib.sha256(ref.read_bytes()).hexdigest()})
        p=call([str(ROOT/'build/lab5_frame')],timeout=15)
        (OUT/'frames.txt').write_text(p.stdout+p.stderr)
        records.append({'kind':'frame_layout','name':'SysV_formals_locals_outgoing','passed':p.returncode==0 and p.stdout=='frame checks passed\n'})
        # Same whitespace/blank-line tolerance as the official diff -w -B.
        def normalized(text):
            return [re.sub(r'[ \t\r\v\f]','',line) for line in text.splitlines() if line.strip()]
        for source in sorted((ROOT/'testdata/lab5or6/testcases').glob('*.tig')):
            inputs=sorted((source.parent/'merge').glob('*.in')) if source.stem=='merge' else [None]
            for stdin in inputs:
                name=source.stem+('/'+stdin.stem if stdin else '')
                ref=ROOT/'testdata/lab5or6/refs'/('merge/'+stdin.stem+'.out' if stdin else source.stem+'.out')
                p=call([str(ROOT/'build/lab5_ir'),str(source),'--official-memory'],input=stdin.read_text() if stdin else '',timeout=30)
                (OUT/('official-ir-'+name.replace('/','-')+'.txt')).write_text(p.stdout+'\nSTDERR\n'+p.stderr)
                reads=re.search(r'UNMAPPED_READS (\d+)',p.stderr)
                records.append({'kind':'official_ir_output','name':name,'passed':p.returncode==0 and 'IR_RESULT' in p.stderr and normalized(p.stdout)==normalized(ref.read_text()) and bool(reads) and int(reads[1])==(1 if source.stem=='qsort' else 0),'unmapped_reads':int(reads[1]) if reads else None,'memory_policy':'provided StateTable.load_mem returns 0xbaadbeef for missing addresses; qsort has one unused out-of-bounds base-case read','reference_sha256':hashlib.sha256(ref.read_bytes()).hexdigest()})
        strict=call([str(ROOT/'build/lab5_ir'),str(ROOT/'testdata/lab5or6/testcases/qsort.tig')],timeout=30)
        (OUT/'qsort-strict.txt').write_text(strict.stdout+strict.stderr)
        records.append({'kind':'fixture_boundary','name':'qsort_strict_rejects_unmapped_read','passed':strict.returncode==3 and 'Unmapped IR memory read' in strict.stderr})
        cases=OUT/'inputs';cases.mkdir(exist_ok=True)
        for name,source,stdout,result in POSITIVE:
            path=cases/f'{name}.tig';path.write_text(source,encoding='utf-8')
            p=call([str(ROOT/'build/lab5_ir'),str(path)],timeout=15)
            (OUT/f'ir-{name}.txt').write_text(p.stdout+'\nSTDERR\n'+p.stderr)
            match=re.search(r'IR_RESULT (-?\d+) STEPS (\d+)',p.stderr)
            records.append({'kind':'ir_execution','name':name,'passed':p.returncode==0 and p.stdout==stdout and bool(match) and int(match[1])==result and 'UNMAPPED_READS 0' in p.stderr,'expected_stdout':stdout,'actual_stdout':p.stdout,'expected_result':result,'actual_result':int(match[1]) if match else None})
        for name,source in NEGATIVE:
            path=cases/f'bad-{name}.tig';path.write_text(source,encoding='utf-8')
            p=call([str(ROOT/'build/lab5_ir'),str(path)],timeout=15)
            (OUT/f'bad-{name}.txt').write_text(p.stdout+p.stderr)
            records.append({'kind':'semantic_rejection','name':name,'passed':p.returncode==1 and 'IR_ERROR' not in p.stderr and 'IR_RESULT' not in p.stderr})
    finally:
        sha=os.getenv('GITHUB_SHA') or call(['git','rev-parse','HEAD']).stdout.strip()
        (OUT/'results.json').write_text(json.dumps({'commit':sha,'scope':'Lab5-1 official escape AST + independent generated IR execution; no native backend acceptance','tests':records,'passed':sum(x['passed'] for x in records),'total':len(records)},indent=2)+'\n')
    failed=[f"{x['kind']}:{x['name']}" for x in records if not x['passed']]
    print(f'{len(records)-len(failed)}/{len(records)} checks passed')
    if failed:raise SystemExit('Failed: '+', '.join(failed))

if __name__=='__main__':main()
