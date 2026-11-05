#include "straightline/slp.h"
#include <stdexcept>
using namespace A;
static void check(bool condition) { if (!condition) throw std::runtime_error("lab1 regression failed"); }
int main() {
  Table *initial = new Table("x", 1, nullptr);
  // The right operand must observe assignment performed by the left ESEQ.
  OpExp operation(new EseqExp(new AssignStm("x", new NumExp(9)), new IdExp("x")),
                  PLUS, new IdExp("x"));
  IntAndTable *result = operation.InterpExp(initial);
  check(result->i == 18 && result->t->Lookup("x") == 9);
  check(initial->Lookup("x") == 1);
  AssignStm assignment("y", new EseqExp(new AssignStm("x", new NumExp(7)), new NumExp(3)));
  Table *assigned = assignment.Interp(initial);
  check(assigned->Lookup("x") == 7 && assigned->Lookup("y") == 3);
  auto *nested = new PrintStm(new PairExpList(new NumExp(1),
      new PairExpList(new NumExp(2), new LastExpList(new NumExp(3)))));
  PrintStm print(new LastExpList(new EseqExp(nested, new NumExp(4))));
  check(print.MaxArgs() == 3);
  print.Interp(initial);
  PrintStm stateful(new PairExpList(new EseqExp(new AssignStm("x", new NumExp(5)), new IdExp("x")),
                                  new LastExpList(new IdExp("x"))));
  check(stateful.Interp(initial)->Lookup("x") == 5);
  check(initial->Lookup("x") == 1);
  bool missing=false;
  try { IdExp("absent").InterpExp(nullptr); } catch (const std::runtime_error &) { missing=true; }
  check(missing);
  return 0;
}
