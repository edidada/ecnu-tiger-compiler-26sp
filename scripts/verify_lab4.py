"""Actions-only Release checks: official diagnostic obligations and extra rules."""
from collections import Counter
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "reports/lab4-results"

def diagnostics(data):
    return [match.group(1).strip() for line in data.decode(errors="replace").splitlines()
            if (match := re.search(r"\.tig:\d+\.\d+:\s*(.+)$", line))]

def execute(target, case):
    return subprocess.run([str(ROOT / "build" / target), str(case)], cwd=ROOT,
                          capture_output=True, timeout=10)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    commands = [
        ["cmake","-S",ROOT,"-B",ROOT/"build","-DCMAKE_BUILD_TYPE=Release"],
        ["cmake","--build",ROOT/"build","--target","test_semant","lab4_semant","lab4_types","-j2"],
    ]
    for command in commands:
        subprocess.run(list(map(str,command)),cwd=ROOT,check=True,timeout=240)
    subprocess.run([str(ROOT/"build/lab4_types")],cwd=ROOT,check=True,timeout=10)
    results = [{"kind":"types-and-scopes","case":"direct-cpp-checks","ok":True}]
    for source in sorted((ROOT/"testdata/lab4/testcases").glob("*.tig")):
        case = OUT/source.name
        case.write_bytes(source.read_bytes().replace(b"\r\n",b"\n"))
        reference = ROOT/"testdata/lab4/refs"/(source.stem+".out")
        policy = "official-reference-required-diagnostics"
        if reference.exists():
            required = diagnostics(reference.read_bytes())
        elif source.stem == "test50":
            # The source contains a break after the loop, but its reference is
            # absent. Preserve the file and check the language rule explicitly.
            required = ["break is not inside a loop"]
            policy = "language-rule-missing-upstream-reference"
        else:
            raise RuntimeError(f"unreviewed missing reference: {source.name}")
        proc = execute("test_semant",case)
        output = proc.stdout+proc.stderr
        (OUT/(source.stem+".actual")).write_bytes(output)
        actual = diagnostics(output)
        if required:
            ok = (not (Counter(required)-Counter(actual))
                  and proc.returncode == (1 if source.stem == "test49" else 0))
        else:
            ok = proc.returncode == 0 and output == b""
        results.append({"kind":"official","case":source.name,"policy":policy,"ok":ok,
                        "required":required,"actual":actual,"extra":list((Counter(actual)-Counter(required)).elements()),
                        "exit_code":proc.returncode})
    # Supplemental cases use exact message multisets and meaningful exit codes.
    cases = [
        ("mutual-functions", "let function f(x:int):int=if x=0 then 0 else g(x-1) function g(x:int):int=f(x) in f(2) end", []),
        ("recursive-record", "let type node={next:node} var n:node:=nil in n:=node{next=n} end", []),
        ("record-alias", "let type r={x:int} type s=r var n:s:=nil in n:=r{x=1} end", []),
        ("nil-branch-join", "let type r={x:int} var n:=if 0 then nil else r{x=1} in n.x end", []),
        ("shadow-initializer", "let var x:=1 in let var x:=x+1 in x end end", []),
        ("shadow-loop-index", "for i:=0 to 1 do let var i:=2 in i:=3 end", []),
        ("builtin-not", "not(0)", []),
        ("cycle", "let type a=b type b=a in 0 end", ["illegal type cycle"]),
        ("undefined-alias", "let type a=missing in 0 end", ["undefined type missing"]),
        ("nominal-records", "let type a={x:int} type b={x:int} var x:a:=b{x=1} in 0 end", ["type mismatch"]),
        ("nominal-arrays", "let type a=array of int type b=array of int var x:a:=b[1] of 0 in 0 end", ["type mismatch"]),
        ("bare-break", "break", ["break is not inside a loop"]),
        ("function-break", "while 1 do let function f()=break in f() end", ["break is not inside a loop"]),
        ("readonly-index", "for i:=0 to 1 do i:=2", ["loop variable can't be assigned"]),
        ("index-scope", "(for i:=0 to 1 do (); i)", ["undefined variable i"]),
        ("forward-variable", "let var a:=b var b:=1 in a end", ["undefined variable b"]),
        ("nil-inference", "let var x:=nil in 0 end", ["init should not be nil without type specified"]),
        ("duplicate-params", "let function f(x:int,x:int):int=x in 0 end", ["two parameters have the same name"]),
        ("duplicate-fields", "let type r={x:int,x:int} in 0 end", ["two fields have the same name"]),
        ("missing-args", "substring(\"x\",0)", ["too few params in function substring"]),
        ("wrong-args", "print(1)", ["para type mismatch"]),
        ("procedure-result", "let function f()=1 in f() end", ["procedure returns value"]),
    ]
    for name,source,required in cases:
        case=OUT/(name+".tig")
        case.write_text(source)
        proc=execute("lab4_semant",case)
        output=proc.stdout+proc.stderr
        actual=diagnostics(output)
        ok=(proc.returncode==bool(required) and Counter(actual)==Counter(required)
            and (bool(required) or output==b""))
        (OUT/(name+".actual")).write_bytes(output)
        results.append({"kind":"extra","case":name,"ok":ok,"required":required,
                        "actual":actual,"exit_code":proc.returncode})
    summary={"build_type":"Release","passed":sum(x["ok"] for x in results),"total":len(results),"results":results}
    (OUT/"results.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary,indent=2))
    if not all(x["ok"] for x in results):raise SystemExit(1)

if __name__=="__main__":main()
