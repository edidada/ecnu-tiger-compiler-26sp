"""Remote-only Release build and complete original AST comparison."""
import difflib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "reports/lab3-results"

def execute(target, files):
    return subprocess.run([str(ROOT / "build" / target), *map(str, files)],
                          cwd=ROOT, capture_output=True, timeout=10)

def compact(data):
    # Edge snapshots below contain no string literals: removing formatting
    # whitespace cannot erase a meaningful value. Original fixtures stay exact.
    return re.sub(rb"\s+", b"", data).decode()

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for command in (["cmake", "-S", ROOT, "-B", ROOT / "build", "-DCMAKE_BUILD_TYPE=Release"],
                    ["cmake", "--build", ROOT / "build", "--target", "test_parse", "lab3_ast", "lab3_positions", "-j2"]):
        subprocess.run(list(map(str, command)), cwd=ROOT, check=True, timeout=240)
    results = []
    for source in sorted((ROOT / "testdata/lab3/testcases").glob("*.tig")):
        case = OUT / source.name
        case.write_bytes(source.read_bytes().replace(b"\r\n", b"\n"))
        proc = execute("test_parse", [case])
        output = proc.stdout + proc.stderr
        (OUT / (source.stem + ".actual")).write_bytes(output)
        if source.stem == "test49":
            ok = proc.returncode != 0 and b"test49.tig:5.18: syntax error" in output
        else:
            expected = (ROOT / "testdata/lab3/refs" / (source.stem + ".out")).read_bytes().replace(b"\r\n", b"\n")
            ok = proc.returncode == 0 and output == expected
            if not ok:
                print("".join(difflib.unified_diff(expected.decode().splitlines(True),
                       output.decode(errors="replace").splitlines(True), fromfile="expected", tofile=source.name)))
        results.append({"kind":"official", "case":source.name, "ok":ok, "exit_code":proc.returncode})
    snapshots = [
        ("single-parens", "(1)", "intExp(1)"),
        ("precedence", "1+2*3", "opExp(PLUS,intExp(1),opExp(TIMES,intExp(2),intExp(3)))"),
        ("left-associative", "8-3-1", "opExp(MINUS,opExp(MINUS,intExp(8),intExp(3)),intExp(1))"),
        ("unary", "--2", "opExp(MINUS,intExp(0),opExp(MINUS,intExp(0),intExp(2)))"),
        ("dangling-else", "if 1 then if 0 then 2 else 3", "iffExp(intExp(1),iffExp(intExp(0),intExp(2),intExp(3)))"),
        ("control-body", "while 1 do 2+3*4", "whileExp(intExp(1),opExp(PLUS,intExp(2),opExp(TIMES,intExp(3),intExp(4))))"),
        ("array", "a[2] of 3+4", "arrayExp(a,intExp(2),opExp(PLUS,intExp(3),intExp(4)))"),
        ("subscript", "a[2].x[3]", "varExp(subscriptVar(fieldVar(subscriptVar(simpleVar(a),intExp(2)),x),intExp(3)))"),
        ("call-order", "f(1,2,3)", "callExp(f,expList(intExp(1),expList(intExp(2),expList(intExp(3),expList()))))"),
        ("empty-call", "f()", "callExp(f,expList())"),
        ("empty-parens", "()", "voidExp()"),
        ("sequence", "(1;2;3)", "seqExp(expList(intExp(1),expList(intExp(2),expList(intExp(3),expList()))))"),
        ("type-group", "let type a=int type b=a in 0 end", "letExp(decList(typeDec(nameAndTyList(nameAndTy(a,nameTy(int)),nameAndTyList(nameAndTy(b,nameTy(a)),nameAndTyList()))),decList()),seqExp(expList(intExp(0),expList())))"),
        ("symbol-reuse", "x+x", "opExp(PLUS,varExp(simpleVar(x)),varExp(simpleVar(x)))"),
    ]
    for name,source,expected in snapshots:
        case = OUT / (name + ".tig")
        case.write_text(source)
        proc = execute("lab3_ast", [case])
        ok = proc.returncode == 0 and not proc.stderr and compact(proc.stdout) == expected
        (OUT / (name + ".stdout")).write_bytes(proc.stdout)
        (OUT / (name + ".stderr")).write_bytes(proc.stderr)
        results.append({"kind":"snapshot", "case":name, "ok":ok, "expected":expected, "exit_code":proc.returncode})
    case = OUT / "symbol-reuse.tig"
    proc = execute("lab3_ast", [case, case, case])
    expected = snapshots[-1][2] * 3
    results.append({"kind":"lifetime", "case":"three-parsers", "ok":proc.returncode == 0 and not proc.stderr and compact(proc.stdout) == expected})
    (OUT / "three-parsers.stdout").write_bytes(proc.stdout)
    (OUT / "three-parsers.stderr").write_bytes(proc.stderr)
    # Full preorder offsets, independently specified from the input bytes.
    positions = [
        ("offset-arithmetic", "  1 + 2*3", "OpExp 3\nIntExp 3\nOpExp 7\nIntExp 7\nIntExp 9\n"),
        ("offset-comment", "/*c*/\nx := 4", "AssignExp 7\nSimpleVar 7\nIntExp 12\n"),
        ("offset-call", "while x do\n  f(2)", "WhileExp 1\nVarExp 7\nSimpleVar 7\nCallExp 14\nIntExp 16\n"),
        ("offset-string", '"a\\n" +\n 2', "OpExp 1\nStringExp 1\nIntExp 10\n"),
        ("offset-continuation", '"a\\\n \\b" + 1', "OpExp 1\nStringExp 1\nIntExp 12\n"),
        ("offset-let", "let var x:=1 in x end", "LetExp 1\nVarDec 5\nIntExp 12\nSeqExp 17\nVarExp 17\nSimpleVar 17\n"),
    ]
    for name,source,expected in positions:
        case = OUT / (name + ".tig")
        case.write_text(source)
        proc = execute("lab3_positions", [case])
        ok = proc.returncode == 0 and not proc.stderr and proc.stdout == expected.encode()
        results.append({"kind":"positions", "case":name, "ok":ok, "expected":expected, "exit_code":proc.returncode})
        (OUT / (name + ".stdout")).write_bytes(proc.stdout)
        (OUT / (name + ".stderr")).write_bytes(proc.stderr)
    negatives = [
        ("comparison-chain", "1<2<3"),
        ("computed-assignment", "(1+2):=3"),
        ("call-trailing-comma", "f(1,)"),
        ("record-trailing-comma", "r{x=1,}"),
        ("missing-else-body", "if 1 then 2 else"),
        ("bad-type-prefix", "let var a:=r nil in a end"),
        ("lexical-error", "1 @"),
        ("eof-error-position", "f(1,"),
    ]
    for name,source in negatives:
        case = OUT / (name + ".tig")
        case.write_text(source)
        proc = execute("lab3_ast", [case])
        diagnostic = b"illegal token" if name == "lexical-error" else b"syntax error"
        ok = proc.returncode != 0 and diagnostic in proc.stderr
        if name == "eof-error-position":
            ok = ok and b":1.5: syntax error" in proc.stderr
        results.append({"kind":"negative", "case":name, "ok":ok, "exit_code":proc.returncode})
        (OUT / (name + ".stderr")).write_bytes(proc.stderr)
    summary = {"build_type":"Release", "passed":sum(x["ok"] for x in results), "total":len(results), "results":results}
    (OUT / "results.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary,indent=2))
    if not results or not all(x["ok"] for x in results):
        raise SystemExit(1)

if __name__ == "__main__":
    main()
