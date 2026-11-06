"""Actions-only Release build, unmodified official fixtures, and lexer edges."""
import difflib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "reports/lab2-results"

def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], cwd=ROOT, timeout=180,
                          check=True, **kwargs)

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    run(["cmake", "-S", ROOT, "-B", ROOT / "build", "-DCMAKE_BUILD_TYPE=Release"])
    run(["cmake", "--build", ROOT / "build", "--target", "test_lex", "lab2_scan", "-j2"])
    results = []
    for source in sorted((ROOT / "testdata/lab2/testcases").glob("*.tig")):
        # Match the official grader's CRLF normalization, without changing its
        # tracked input or expected output files.
        case = OUT / source.name
        case.write_bytes(source.read_bytes().replace(b"\r\n", b"\n"))
        expected = (ROOT / "testdata/lab2/refs" / (source.stem + ".out")).read_bytes().replace(b"\r\n", b"\n")
        proc = subprocess.run([str(ROOT / "build/test_lex"), str(case)],
                              cwd=ROOT, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=10)
        (OUT / (source.stem + ".actual")).write_bytes(proc.stdout)
        ok = proc.returncode == 0 and proc.stdout == expected
        results.append({"kind": "official", "case": source.name, "ok": ok})
        if not ok:
            print("".join(difflib.unified_diff(expected.decode(errors="replace").splitlines(True),
                  proc.stdout.decode(errors="replace").splitlines(True), fromfile="expected", tofile=source.name)))
    # Expected offsets are explicit 1-based byte positions, not scanner output.
    # Each tuple: name, raw source, [(token, offset, decoded bytes)], errors.
    cases = [
        ("nested", b"/* a /* b */ c */abc", [("ID",18,b"abc")], []),
        ("boundaries", b"arrayx a_2 Z9", [("ID",1,b"arrayx"),("ID",8,b"a_2"),("ID",12,b"Z9")], []),
        ("empty", b'"" x', [("STRING",1,b""),("ID",4,b"x")], []),
        ("escapes", br'"\n\t\"\\\000\255\^@\^_"', [("STRING",1,b'\n\t"\\\x00\xff\x00\x1f')], []),
        ("comment-in-string", b'"/* not comment */"', [("STRING",1,b"/* not comment */")], []),
        ("continuation", b'"a\\\n \t\\b" z', [("STRING",1,b"ab"),("ID",11,b"z")], []),
        ("bad-escape", br'"\q"', [("STRING",1,b"")], ["1.2: invalid string escape"]),
        ("range", br'"\256"', [("STRING",1,b"")], ["1.2: decimal escape outside byte range"]),
        ("comment-eof", b"\n/* outer /* inner */", [], ["2.1: unterminated comment"]),
        ("string-eof", b'\n"abc', [], ["2.1: unterminated string"]),
        ("continuation-eof", b'"a\\\n ', [], ["1.1: unterminated string"]),
        ("line-after-continuation", b'"a\\\n \\b"\n@', [("STRING",1,b"ab")], ["3.1: illegal token"]),
        ("raw-newline", b'"a\nb"', [("STRING",1,b"ab")], ["1.3: unescaped newline in string"]),
        ("bad-continuation", b'"a\\ x"', [("STRING",1,b"a")], ["1.5: expected backslash after string continuation"]),
    ]
    for name, source, tokens, errors in cases:
        case = OUT / (name + ".tig")
        case.write_bytes(source)
        proc = subprocess.run([str(ROOT / "build/lab2_scan"), str(case)],
                              cwd=ROOT, capture_output=True, timeout=10)
        expected = "".join(f"{kind} {pos} {value.hex()}\n" for kind,pos,value in tokens).encode()
        lines = proc.stderr.decode().splitlines()
        ok = (proc.stdout == expected and proc.returncode == bool(errors)
              and len(lines) == len(errors)
              and all(line.endswith(error) for line,error in zip(lines,errors)))
        (OUT / (name + ".stdout")).write_bytes(proc.stdout)
        (OUT / (name + ".stderr")).write_bytes(proc.stderr)
        results.append({"kind":"edge", "case":name, "ok":ok,
                        "expected_stdout":expected.decode(), "expected_errors":errors,
                        "exit_code":proc.returncode})
    summary = {"build_type":"Release", "results":results,
               "passed":sum(x["ok"] for x in results), "total":len(results)}
    (OUT / "results.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary,indent=2))
    if not results or not all(x["ok"] for x in results):
        raise SystemExit(1)

if __name__ == "__main__":
    main()
