#include <cstdio>
#include <iostream>
#include <stdexcept>
#include "tiger/parse/parser.h"

using namespace absyn;
void Walk(const Exp *);
void Walk(const Var *);
void Walk(const Dec *);
void Walk(const Ty *);

#define NODE(T) auto *node = dynamic_cast<const T *>(value)
#define POS(T) std::printf(#T " %d\n", node->pos_)

void Walk(const Var *value) {
  if (NODE(SimpleVar)) { POS(SimpleVar); }
  else if (NODE(FieldVar)) { POS(FieldVar); Walk(node->var_); }
  else if (NODE(SubscriptVar)) { POS(SubscriptVar); Walk(node->var_); Walk(node->subscript_); }
  else throw std::runtime_error("unhandled Var");
}
void Walk(const Ty *value) {
  if (NODE(NameTy)) { POS(NameTy); }
  else if (NODE(ArrayTy)) { POS(ArrayTy); }
  else if (NODE(RecordTy)) {
    POS(RecordTy);
    for (auto *field : node->record_->GetList()) std::printf("Field %d\n", field->pos_);
  } else throw std::runtime_error("unhandled Ty");
}
void Walk(const Dec *value) {
  if (NODE(VarDec)) { POS(VarDec); Walk(node->init_); }
  else if (NODE(TypeDec)) {
    POS(TypeDec);
    for (auto *entry : node->types_->GetList()) Walk(entry->ty_);
  } else if (NODE(FunctionDec)) {
    POS(FunctionDec);
    for (auto *entry : node->functions_->GetList()) {
      std::printf("FunDec %d\n", entry->pos_);
      for (auto *field : entry->params_->GetList()) std::printf("Field %d\n", field->pos_);
      Walk(entry->body_);
    }
  } else throw std::runtime_error("unhandled Dec");
}
void Walk(const Exp *value) {
  if (NODE(VarExp)) { POS(VarExp); Walk(node->var_); }
  else if (NODE(NilExp)) { POS(NilExp); }
  else if (NODE(IntExp)) { POS(IntExp); }
  else if (NODE(StringExp)) { POS(StringExp); }
  else if (NODE(VoidExp)) { POS(VoidExp); }
  else if (NODE(BreakExp)) { POS(BreakExp); }
  else if (NODE(OpExp)) { POS(OpExp); Walk(node->left_); Walk(node->right_); }
  else if (NODE(AssignExp)) { POS(AssignExp); Walk(node->var_); Walk(node->exp_); }
  else if (NODE(CallExp)) {
    POS(CallExp);
    for (auto *entry : node->args_->GetList()) Walk(entry);
  } else if (NODE(RecordExp)) {
    POS(RecordExp);
    for (auto *entry : node->fields_->GetList()) Walk(entry->exp_);
  } else if (NODE(SeqExp)) {
    POS(SeqExp);
    for (auto *entry : node->seq_->GetList()) Walk(entry);
  } else if (NODE(IfExp)) {
    POS(IfExp); Walk(node->test_); Walk(node->then_);
    if (node->elsee_) Walk(node->elsee_);
  } else if (NODE(WhileExp)) { POS(WhileExp); Walk(node->test_); Walk(node->body_); }
  else if (NODE(ForExp)) { POS(ForExp); Walk(node->lo_); Walk(node->hi_); Walk(node->body_); }
  else if (NODE(ArrayExp)) { POS(ArrayExp); Walk(node->size_); Walk(node->init_); }
  else if (NODE(LetExp)) {
    POS(LetExp);
    for (auto *entry : node->decs_->GetList()) Walk(entry);
    Walk(node->body_);
  } else throw std::runtime_error("unhandled Exp");
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  Parser parser(argv[1], std::cerr);
  if (parser.parse() != 0) return 1;
  auto errors = parser.TransferErrormsg();
  auto tree = parser.TransferAbsynTree();
  if (errors->AnyErrors() || !tree) return 1;
  Walk(tree->Root());
}
