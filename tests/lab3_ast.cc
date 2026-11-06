#include <cstdio>
#include <iostream>
#include "tiger/parse/parser.h"

// Repeated files are intentional: AST destruction must not free interned names
// needed by the next parser in the same process.
int main(int argc, char **argv) {
  if (argc < 2) return 2;
  for (int i = 1; i < argc; ++i) {
    Parser parser(argv[i], std::cerr);
    if (parser.parse() != 0) return 1;
    auto errors = parser.TransferErrormsg();
    auto tree = parser.TransferAbsynTree();
    if (errors->AnyErrors() || !tree) return 1;
    tree->Print(stdout);
    std::printf("\n");
  }
}
