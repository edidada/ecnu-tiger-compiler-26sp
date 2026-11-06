#include "tiger/semant/types.h"
#include <unordered_set>
#include <typeinfo>

namespace type {

NilTy NilTy::nilty_;
IntTy IntTy::intty_;
StringTy StringTy::stringty_;
VoidTy VoidTy::voidty_;

Ty *Ty::ActualTy() { return this; }

Ty *NameTy::ActualTy() {
  // TypeDec diagnoses illegal aliases. Recovery must not recurse forever or
  // dereference an unresolved forward declaration after another diagnostic.
  Ty *current = this;
  std::unordered_set<Ty *> visited;
  while (auto *alias = dynamic_cast<NameTy *>(current)) {
    if (!visited.insert(current).second || alias->ty_ == nullptr)
      return VoidTy::Instance();
    current = alias->ty_;
  }
  return current;
}

bool Ty::IsSameType(Ty *expected) {
  Ty *a = ActualTy();
  Ty *b = expected->ActualTy();

  if ((typeid(*a) == typeid(NilTy) && typeid(*b) == typeid(RecordTy)) ||
      (typeid(*a) == typeid(RecordTy) && typeid(*b) == typeid(NilTy)))
    return true;

  return a == b;
}

} // namespace type
