#include "tiger/absyn/absyn.h"
#include "tiger/semant/semant.h"
#include <unordered_set>
#include <vector>

namespace {
using Ty = type::Ty;
Ty *Int() { return type::IntTy::Instance(); }
Ty *Void() { return type::VoidTy::Instance(); }
Ty *Actual(Ty *ty) { return ty ? ty->ActualTy() : Void(); }
bool IsInt(Ty *ty) { return Actual(ty) == Int(); }
bool IsVoid(Ty *ty) { return Actual(ty) == Void(); }
bool Same(Ty *a, Ty *b) { return a && b && a->IsSameType(b); }
Ty *LookupType(env::TEnvPtr tenv, sym::Symbol *name, int pos, err::ErrorMsg *errors) {
  auto *ty = tenv->Look(name);
  if (!ty) { errors->Error(pos, "undefined type %s", name->Name().c_str()); return Void(); }
  return ty;
}
// Both namespaces enter/leave together even when a child reports errors.
class Scope {
  env::VEnvPtr vars_;
  env::TEnvPtr types_;
public:
  Scope(env::VEnvPtr vars, env::TEnvPtr types) : vars_(vars), types_(types) {
    vars_->BeginScope(); types_->BeginScope();
  }
  ~Scope() { types_->EndScope(); vars_->EndScope(); }
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;
};
}

namespace absyn {
void AbsynTree::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv, err::ErrorMsg *errors) const {
  root_->SemAnalyze(venv, tenv, 0, errors);
}
type::Ty *SimpleVar::SemAnalyze(env::VEnvPtr v, env::TEnvPtr, int, err::ErrorMsg *e) const {
  auto *entry = dynamic_cast<env::VarEntry *>(v->Look(sym_));
  if (!entry) { e->Error(pos_, "undefined variable %s", sym_->Name().c_str()); return Int(); }
  return entry->ty_;
}
type::Ty *FieldVar::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  auto *record = dynamic_cast<type::RecordTy *>(Actual(var_->SemAnalyze(v,t,depth,e)));
  if (!record) { e->Error(pos_, "not a record type"); return Void(); }
  for (auto *field : record->fields_->GetList()) if (field->name_ == sym_) return field->ty_;
  e->Error(pos_, "field %s doesn't exist", sym_->Name().c_str());
  return Void();
}
type::Ty *SubscriptVar::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  auto *array = dynamic_cast<type::ArrayTy *>(Actual(var_->SemAnalyze(v,t,depth,e)));
  auto *index = subscript_->SemAnalyze(v,t,depth,e);
  if (!IsInt(index)) e->Error(subscript_->pos_, "integer required");
  if (!array) { e->Error(pos_, "array type required"); return Void(); }
  return array->ty_;
}
type::Ty *VarExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int d, err::ErrorMsg *e) const { return var_->SemAnalyze(v,t,d,e); }
type::Ty *NilExp::SemAnalyze(env::VEnvPtr, env::TEnvPtr, int, err::ErrorMsg *) const { return type::NilTy::Instance(); }
type::Ty *IntExp::SemAnalyze(env::VEnvPtr, env::TEnvPtr, int, err::ErrorMsg *) const { return Int(); }
type::Ty *StringExp::SemAnalyze(env::VEnvPtr, env::TEnvPtr, int, err::ErrorMsg *) const { return type::StringTy::Instance(); }
type::Ty *VoidExp::SemAnalyze(env::VEnvPtr, env::TEnvPtr, int, err::ErrorMsg *) const { return Void(); }
type::Ty *CallExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  auto *function = dynamic_cast<env::FunEntry *>(v->Look(func_));
  std::vector<type::Ty *> args;
  for (auto *arg : args_->GetList()) args.push_back(arg->SemAnalyze(v,t,depth,e));
  if (!function) { e->Error(pos_, "undefined function %s", func_->Name().c_str()); return Int(); }
  const auto &formals = function->formals_->GetList();
  auto actual_node = args_->GetList().begin();
  size_t i = 0;
  for (auto *formal : formals) {
    if (i == args.size()) break;
    if (!Same(args[i], formal)) e->Error((*actual_node)->pos_, "para type mismatch");
    ++i; ++actual_node;
  }
  if (args.size() < formals.size()) e->Error(pos_, "too few params in function %s", func_->Name().c_str());
  if (args.size() > formals.size()) e->Error(pos_, "too many params in function %s", func_->Name().c_str());
  return function->result_;
}
type::Ty *OpExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  auto *left = left_->SemAnalyze(v,t,depth,e);
  auto *right = right_->SemAnalyze(v,t,depth,e);
  if (oper_ == PLUS_OP || oper_ == MINUS_OP || oper_ == TIMES_OP || oper_ == DIVIDE_OP || oper_ == AND_OP || oper_ == OR_OP) {
    if (!IsInt(left)) e->Error(left_->pos_, "integer required");
    if (!IsInt(right)) e->Error(right_->pos_, "integer required");
  } else if (oper_ == EQ_OP || oper_ == NEQ_OP) {
    auto *a = Actual(left); auto *b = Actual(right);
    if (!Same(a,b) || IsVoid(a) || (a == type::NilTy::Instance() && b == type::NilTy::Instance()))
      e->Error(pos_, "same type required");
  } else {
    auto *a = Actual(left); auto *b = Actual(right);
    if (a != b || (a != Int() && a != type::StringTy::Instance())) e->Error(pos_, "same type required");
  }
  return Int();
}
type::Ty *RecordExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  auto *declared = LookupType(t,typ_,pos_,e);
  auto *record = dynamic_cast<type::RecordTy *>(Actual(declared));
  std::vector<type::Ty *> actuals;
  for (auto *field : fields_->GetList()) actuals.push_back(field->exp_->SemAnalyze(v,t,depth,e));
  if (!record) { e->Error(pos_, "not a record type"); return Void(); }
  auto provided = fields_->GetList().begin();
  size_t i = 0;
  bool mismatch = fields_->GetList().size() != record->fields_->GetList().size();
  for (auto *field : record->fields_->GetList()) {
    if (provided == fields_->GetList().end()) break;
    if ((*provided)->name_ != field->name_ || !Same(actuals[i],field->ty_)) mismatch = true;
    ++provided; ++i;
  }
  if (mismatch) e->Error(pos_, "type mismatch");
  return declared;
}
type::Ty *SeqExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  type::Ty *result = Void();
  for (auto *exp : seq_->GetList()) result = exp->SemAnalyze(v,t,depth,e);
  return result;
}
type::Ty *AssignExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  auto *left = var_->SemAnalyze(v,t,depth,e);
  auto *right = exp_->SemAnalyze(v,t,depth,e);
  if (auto *simple = dynamic_cast<SimpleVar *>(var_)) {
    auto *entry = dynamic_cast<env::VarEntry *>(v->Look(simple->sym_));
    if (entry && entry->readonly_) e->Error(pos_, "loop variable can't be assigned");
  }
  if (!Same(left,right)) e->Error(pos_, "unmatched assign exp");
  return Void();
}
type::Ty *IfExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  if (!IsInt(test_->SemAnalyze(v,t,depth,e))) e->Error(test_->pos_, "integer required");
  auto *then_ty = then_->SemAnalyze(v,t,depth,e);
  if (!elsee_) {
    if (!IsVoid(then_ty)) e->Error(then_->pos_, "if-then exp's body must produce no value");
    return Void();
  }
  auto *else_ty = elsee_->SemAnalyze(v,t,depth,e);
  if (!Same(then_ty,else_ty)) e->Error(pos_, "then exp and else exp type mismatch");
  return Actual(then_ty) == type::NilTy::Instance() ? else_ty : then_ty;
}
type::Ty *WhileExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  if (!IsInt(test_->SemAnalyze(v,t,depth,e))) e->Error(test_->pos_, "integer required");
  if (!IsVoid(body_->SemAnalyze(v,t,depth+1,e))) e->Error(body_->pos_, "while body must produce no value");
  return Void();
}
type::Ty *ForExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  auto *lo = lo_->SemAnalyze(v,t,depth,e);
  auto *hi = hi_->SemAnalyze(v,t,depth,e);
  if (!IsInt(lo) || !IsInt(hi)) e->Error(pos_, "for exp's range type is not integer");
  Scope scope(v,t);
  v->Enter(var_, new env::VarEntry(Int(),true));
  if (!IsVoid(body_->SemAnalyze(v,t,depth+1,e))) e->Error(body_->pos_, "for body must produce no value");
  return Void();
}
type::Ty *BreakExp::SemAnalyze(env::VEnvPtr, env::TEnvPtr, int depth, err::ErrorMsg *e) const {
  if (depth == 0) e->Error(pos_, "break is not inside a loop");
  return Void();
}
type::Ty *LetExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  Scope scope(v,t);
  for (auto *dec : decs_->GetList()) dec->SemAnalyze(v,t,depth,e);
  return body_->SemAnalyze(v,t,depth,e);
}
type::Ty *ArrayExp::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  auto *declared = LookupType(t,typ_,pos_,e);
  auto *array = dynamic_cast<type::ArrayTy *>(Actual(declared));
  auto *size = size_->SemAnalyze(v,t,depth,e);
  auto *init = init_->SemAnalyze(v,t,depth,e);
  if (!IsInt(size)) e->Error(size_->pos_, "integer required");
  if (!array) { e->Error(pos_, "array type required"); return Void(); }
  if (!Same(init,array->ty_)) e->Error(init_->pos_, "type mismatch");
  return declared;
}
void VarDec::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int depth, err::ErrorMsg *e) const {
  auto *init = init_->SemAnalyze(v,t,depth,e);
  auto *declared = typ_ ? LookupType(t,typ_,pos_,e) : init;
  if (!typ_ && Actual(init) == type::NilTy::Instance()) e->Error(pos_, "init should not be nil without type specified");
  if (typ_ && !Same(init,declared)) e->Error(pos_, "type mismatch");
  v->Enter(var_,new env::VarEntry(declared));
}
void FunctionDec::SemAnalyze(env::VEnvPtr v, env::TEnvPtr t, int, err::ErrorMsg *e) const {
  struct Header { FunDec *declaration; env::FunEntry *entry; };
  std::vector<Header> headers;
  std::unordered_set<sym::Symbol *> names;
  // Install all signatures before analyzing any mutually recursive body.
  for (auto *function : functions_->GetList()) {
    if (!names.insert(function->name_).second) { e->Error(function->pos_, "two functions have the same name"); continue; }
    auto *formals = new type::TyList();
    for (auto *field : function->params_->GetList()) formals->Append(LookupType(t,field->typ_,field->pos_,e));
    auto *result = function->result_ ? LookupType(t,function->result_,function->pos_,e) : Void();
    auto *entry = new env::FunEntry(formals,result);
    v->Enter(function->name_,entry);
    headers.push_back({function,entry});
  }
  for (const auto &header : headers) {
    auto *function = header.declaration;
    Scope scope(v,t);
    auto formal = header.entry->formals_->GetList().begin();
    std::unordered_set<sym::Symbol *> params;
    for (auto *field : function->params_->GetList()) {
      if (!params.insert(field->name_).second) e->Error(field->pos_, "two parameters have the same name");
      v->Enter(field->name_,new env::VarEntry(*formal++));
    }
    // A nested function cannot break from a loop in its lexical parent.
    auto *body = function->body_->SemAnalyze(v,t,0,e);
    if (!Same(body,header.entry->result_))
      e->Error(function->pos_, function->result_ ? "function result type mismatch" : "procedure returns value");
  }
}
void TypeDec::SemAnalyze(env::VEnvPtr, env::TEnvPtr t, int, err::ErrorMsg *e) const {
  struct Header { NameAndTy *declaration; type::NameTy *placeholder; };
  std::vector<Header> headers;
  std::unordered_set<sym::Symbol *> names;
  for (auto *declaration : types_->GetList()) {
    if (!names.insert(declaration->name_).second) { e->Error(pos_, "two types have the same name"); continue; }
    auto *placeholder = new type::NameTy(declaration->name_,nullptr);
    t->Enter(declaration->name_,placeholder);
    headers.push_back({declaration,placeholder});
  }
  for (const auto &header : headers) header.placeholder->ty_ = header.declaration->ty_->SemAnalyze(t,e);
  for (const auto &header : headers) {
    std::unordered_set<type::Ty *> visited;
    type::Ty *current = header.placeholder;
    while (auto *alias = dynamic_cast<type::NameTy *>(current)) {
      if (!visited.insert(current).second) {
        e->Error(pos_, "illegal type cycle");
        alias->ty_ = Void();
        break;
      }
      if (!alias->ty_) break;
      current = alias->ty_;
    }
  }
}
type::Ty *NameTy::SemAnalyze(env::TEnvPtr t, err::ErrorMsg *e) const { return LookupType(t,name_,pos_,e); }
type::Ty *RecordTy::SemAnalyze(env::TEnvPtr t, err::ErrorMsg *e) const {
  auto *fields = new type::FieldList();
  std::unordered_set<sym::Symbol *> names;
  for (auto *field : record_->GetList()) {
    if (!names.insert(field->name_).second) e->Error(field->pos_, "two fields have the same name");
    fields->Append(new type::Field(field->name_, LookupType(t,field->typ_,field->pos_,e)));
  }
  return new type::RecordTy(fields);
}
type::Ty *ArrayTy::SemAnalyze(env::TEnvPtr t, err::ErrorMsg *e) const { return new type::ArrayTy(LookupType(t,array_,pos_,e)); }
}

namespace sem {
void ProgSem::SemAnalyze() {
  FillBaseVEnv(); FillBaseTEnv();
  absyn_tree_->SemAnalyze(venv_.get(),tenv_.get(),errormsg_.get());
}
}
