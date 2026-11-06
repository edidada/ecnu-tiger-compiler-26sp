#include <iostream>
#include "tiger/parse/parser.h"
#include "tiger/semant/semant.h"
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  Parser parser(argv[1], std::cerr);
  if (parser.parse() != 0) return 1;
  auto errors = parser.TransferErrormsg();
  auto tree = parser.TransferAbsynTree();
  if (errors->AnyErrors() || !tree) return 1;
  sem::ProgSem analyzer(std::move(tree), std::move(errors));
  analyzer.SemAnalyze();
  return analyzer.TransferErrormsg()->AnyErrors() ? 1 : 0;
}
