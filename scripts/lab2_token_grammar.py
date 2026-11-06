"""Generate only Lab2's token interface from the untouched Lab3 starter.

The upstream Lab2 CMake invokes the intentionally unfinished Lab3 grammar.
Keep the real token declarations, union and preinclude, but provide an empty
start rule. The generated parser is never linked or used as an acceptance test.
"""
import argparse
from pathlib import Path

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.source.read_text()
    marker = " /* token priority */"
    if source.count(marker) != 1:
        raise SystemExit("unexpected upstream grammar layout")
    declarations = source.split(marker, 1)[0]
    if "%token <sym> ID" not in declarations or "%token <ival> INT" not in declarations:
        raise SystemExit("unexpected upstream token declarations")
    args.output.write_text(declarations + "\n%start lab2_tokens\n%%\nlab2_tokens: ;\n", newline="\n")

if __name__ == "__main__":
    main()
