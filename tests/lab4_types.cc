#include <stdexcept>
#include "tiger/semant/types.h"
#include "tiger/env/env.h"
void Check(bool condition) { if (!condition) throw std::runtime_error("type or scope regression"); }
int main() {
  auto *integer = type::IntTy::Instance();
  auto *nil = type::NilTy::Instance();
  type::NameTy forward(sym::Symbol::UniqueSymbol("forward"), nullptr);
  type::NameTy alias(sym::Symbol::UniqueSymbol("alias"), &forward);
  forward.ty_ = integer;
  Check(alias.ActualTy() == integer);
  type::RecordTy record1(new type::FieldList()), record2(new type::FieldList());
  type::ArrayTy array1(integer), array2(integer);
  Check(!record1.IsSameType(&record2));
  Check(!array1.IsSameType(&array2));
  Check(nil->IsSameType(&record1) && record1.IsSameType(nil));
  Check(!nil->IsSameType(integer));
  alias.ty_ = &forward; forward.ty_ = &alias;
  Check(alias.ActualTy() == type::VoidTy::Instance());
  type::NameTy unresolved(sym::Symbol::UniqueSymbol("unresolved"), nullptr);
  Check(unresolved.ActualTy() == type::VoidTy::Instance());
  env::VEnv vars;
  auto *name = sym::Symbol::UniqueSymbol("x");
  env::VarEntry outer(integer), inner(type::StringTy::Instance(), true);
  vars.Enter(name, &outer);
  vars.BeginScope(); vars.Enter(name, &inner);
  Check(vars.Look(name) == &inner);
  vars.BeginScope(); vars.Enter(name, &outer); vars.EndScope();
  Check(vars.Look(name) == &inner);
  vars.EndScope(); Check(vars.Look(name) == &outer);
}
