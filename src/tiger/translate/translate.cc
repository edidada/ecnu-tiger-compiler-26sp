#include "tiger/translate/translate.h"
#include "tiger/frame/x64frame.h"
#include <stdexcept>
#include <vector>
extern frame::Frags *frags;
extern frame::RegManager *reg_manager;

namespace {
tree::Stm *Seq(tree::Stm *a,tree::Stm *b) {return a?new tree::SeqStm(a,b):b;}
tree::Stm *Jump(temp::Label *l) {return new tree::JumpStm(new tree::NameExp(l),new std::vector<temp::Label *>{l});}
tree::Exp *Fp() {return new tree::TempExp(reg_manager->FramePointer());}
tree::Exp *StaticLink(tr::Level *from,tr::Level *to) {
  tree::Exp *fp=Fp();
  while(from!=to) {
    if(!from || !from->parent_ || from->frame_->Formals()->empty())
      throw std::logic_error("Invalid lexical static-link traversal");
    fp=from->frame_->Formals()->back()->ToExp(fp);from=from->parent_;
  }
  return fp;
}
}
namespace tr {
Access *Access::AllocLocal(Level *level,bool escape) {
  return new Access(level,level->frame_->AllocLocal(escape));
}
class Cx {
public:
  PatchList trues_,falses_;tree::Stm *stm_;
  Cx(PatchList t,PatchList f,tree::Stm *s):trues_(t),falses_(f),stm_(s) {}
};
class Exp {
public:
  virtual ~Exp()=default;
  virtual tree::Exp *UnEx() const=0;
  virtual tree::Stm *UnNx() const=0;
  virtual Cx UnCx(err::ErrorMsg *) const=0;
};
class ExpAndTy {
public:
  Exp *exp_;type::Ty *ty_;
  ExpAndTy(Exp *e,type::Ty *t):exp_(e),ty_(t) {}
};
class ExExp:public Exp {
public:
  tree::Exp *exp_;
  explicit ExExp(tree::Exp *e):exp_(e) {}
  tree::Exp *UnEx() const override {return exp_;}
  tree::Stm *UnNx() const override {return new tree::ExpStm(exp_);}
  Cx UnCx(err::ErrorMsg *) const override {
    auto s=new tree::CjumpStm(tree::NE_OP,exp_,new tree::ConstExp(0),nullptr,nullptr);
    return Cx(PatchList({&s->true_label_}),PatchList({&s->false_label_}),s);
  }
};
class NxExp:public Exp {
public:
  tree::Stm *stm_;
  explicit NxExp(tree::Stm *s):stm_(s) {}
  tree::Exp *UnEx() const override {return new tree::EseqExp(stm_,new tree::ConstExp(0));}
  tree::Stm *UnNx() const override {return stm_;}
  Cx UnCx(err::ErrorMsg *) const override {throw std::logic_error("Void expression used as condition");}
};
class CxExp:public Exp {
public:
  Cx cx_;
  CxExp(PatchList t,PatchList f,tree::Stm *s):cx_(t,f,s) {}
  explicit CxExp(Cx c):cx_(c) {}
  tree::Exp *UnEx() const override {
    auto t=temp::LabelFactory::NewLabel(),f=temp::LabelFactory::NewLabel();
    auto r=temp::TempFactory::NewTemp();cx_.trues_.DoPatch(t);cx_.falses_.DoPatch(f);
    tree::Stm *s=new tree::MoveStm(new tree::TempExp(r),new tree::ConstExp(1));
    s=Seq(s,cx_.stm_);s=Seq(s,new tree::LabelStm(f));
    s=Seq(s,new tree::MoveStm(new tree::TempExp(r),new tree::ConstExp(0)));
    s=Seq(s,new tree::LabelStm(t));return new tree::EseqExp(s,new tree::TempExp(r));
  }
  tree::Stm *UnNx() const override {
    auto done=temp::LabelFactory::NewLabel();cx_.trues_.DoPatch(done);cx_.falses_.DoPatch(done);
    return Seq(cx_.stm_,new tree::LabelStm(done));
  }
  Cx UnCx(err::ErrorMsg *) const override {return cx_;}
};
static frame::ProcFrag *Procedure(Level *l,Exp *body) {
  auto move=new tree::MoveStm(new tree::TempExp(reg_manager->ReturnValue()),body->UnEx());
  return new frame::ProcFrag(frame::ProcEntryExit1(l->frame_,move),l->frame_);
}
void ProgTr::Translate() {
  FillBaseVEnv();FillBaseTEnv();
  // Reject invalid programs before constructing unsafe typed accesses.
  absyn_tree_->SemAnalyze(venv_.get(),tenv_.get(),errormsg_.get());
  if(errormsg_->AnyErrors())return;
  auto body=absyn_tree_->Translate(venv_.get(),tenv_.get(),main_level_.get(),nullptr,errormsg_.get());
  if(!errormsg_->AnyErrors())frags->PushBack(Procedure(main_level_.get(),body->exp_));
}
}
namespace {
tr::ExpAndTy *Value(tree::Exp *e,type::Ty *t) {return new tr::ExpAndTy(new tr::ExExp(e),t);}
tr::ExpAndTy *Statement(tree::Stm *s) {return new tr::ExpAndTy(new tr::NxExp(s),type::VoidTy::Instance());}
tr::Exp *Noop() {return new tr::NxExp(new tree::ExpStm(new tree::ConstExp(0)));}
}
namespace absyn {
tr::ExpAndTy *AbsynTree::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  return root_->Translate(v,t,l,done,e);
}
tr::ExpAndTy *SimpleVar::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto entry=dynamic_cast<env::VarEntry *>(v->Look(sym_));
  if(!entry || !entry->access_)throw std::logic_error("Missing translated variable access");
  return Value(entry->access_->access_->ToExp(StaticLink(l,entry->access_->level_)),entry->ty_);
}
tr::ExpAndTy *FieldVar::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto base=var_->Translate(v,t,l,done,e);
  auto record=dynamic_cast<type::RecordTy *>(base->ty_->ActualTy());int offset=0;
  for(auto field:record->fields_->GetList()) {
    if(field->name_==sym_)return Value(new tree::MemExp(new tree::BinopExp(tree::PLUS_OP,base->exp_->UnEx(),new tree::ConstExp(offset))),field->ty_);
    offset+=reg_manager->WordSize();
  }
  throw std::logic_error("Missing checked record field");
}
tr::ExpAndTy *SubscriptVar::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto base=var_->Translate(v,t,l,done,e),index=subscript_->Translate(v,t,l,done,e);
  auto array=dynamic_cast<type::ArrayTy *>(base->ty_->ActualTy());
  return Value(new tree::MemExp(new tree::BinopExp(tree::PLUS_OP,base->exp_->UnEx(),new tree::BinopExp(tree::MUL_OP,index->exp_->UnEx(),new tree::ConstExp(reg_manager->WordSize())))),array->ty_);
}
tr::ExpAndTy *VarExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  return var_->Translate(v,t,l,done,e);
}
tr::ExpAndTy *NilExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  return Value(new tree::ConstExp(0),type::NilTy::Instance());
}
tr::ExpAndTy *IntExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  return Value(new tree::ConstExp(val_),type::IntTy::Instance());
}
tr::ExpAndTy *StringExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto name=temp::LabelFactory::NewLabel();frags->PushBack(new frame::StringFrag(name,str_));
  return Value(new tree::NameExp(name),type::StringTy::Instance());
}
tr::ExpAndTy *CallExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto function=dynamic_cast<env::FunEntry *>(v->Look(func_));auto args=new tree::ExpList();
  for(auto arg:args_->GetList())args->Append(arg->Translate(v,t,l,done,e)->exp_->UnEx());
  if(function->label_)args->Append(StaticLink(l,function->level_->parent_));
  int n=static_cast<int>(args->GetList().size());l->frame_->AllocOutgoSpace(n>6?(n-6)*8:0);
  tree::Exp *call=function->label_?new tree::CallExp(new tree::NameExp(function->label_),args):frame::ExternalCall(func_->Name(),args);
  return Value(call,function->result_);
}
tr::ExpAndTy *OpExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto left=left_->Translate(v,t,l,done,e),right=right_->Translate(v,t,l,done,e);
  if(oper_==AND_OP || oper_==OR_OP) {
    auto a=left->exp_->UnCx(e),b=right->exp_->UnCx(e);auto next=temp::LabelFactory::NewLabel();
    if(oper_==AND_OP) {
      a.trues_.DoPatch(next);
      return new tr::ExpAndTy(new tr::CxExp(b.trues_,tr::PatchList::JoinPatch(a.falses_,b.falses_),Seq(a.stm_,Seq(new tree::LabelStm(next),b.stm_))),type::IntTy::Instance());
    }
    a.falses_.DoPatch(next);
    return new tr::ExpAndTy(new tr::CxExp(tr::PatchList::JoinPatch(a.trues_,b.trues_),b.falses_,Seq(a.stm_,Seq(new tree::LabelStm(next),b.stm_))),type::IntTy::Instance());
  }
  tree::Exp *a=left->exp_->UnEx(),*b=right->exp_->UnEx();
  if(oper_==PLUS_OP || oper_==MINUS_OP || oper_==TIMES_OP || oper_==DIVIDE_OP) {
    tree::BinOp op=oper_==PLUS_OP?tree::PLUS_OP:oper_==MINUS_OP?tree::MINUS_OP:oper_==TIMES_OP?tree::MUL_OP:tree::DIV_OP;
    return Value(new tree::BinopExp(op,a,b),type::IntTy::Instance());
  }
  tree::RelOp op=oper_==EQ_OP?tree::EQ_OP:oper_==NEQ_OP?tree::NE_OP:oper_==LT_OP?tree::LT_OP:oper_==LE_OP?tree::LE_OP:oper_==GT_OP?tree::GT_OP:tree::GE_OP;
  if(left->ty_->ActualTy()==type::StringTy::Instance()) {
    bool equality=oper_==EQ_OP || oper_==NEQ_OP;
    auto args=new tree::ExpList({a,b});
    a=frame::ExternalCall(equality?"stringEqual":"stringCompare",args);
    b=new tree::ConstExp(equality?1:0);
  }
  auto jump=new tree::CjumpStm(op,a,b,nullptr,nullptr);
  return new tr::ExpAndTy(new tr::CxExp(tr::PatchList({&jump->true_label_}),tr::PatchList({&jump->false_label_}),jump),type::IntTy::Instance());
}
tr::ExpAndTy *RecordExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto ty=t->Look(typ_);auto reg=temp::TempFactory::NewTemp();int offset=0;
  auto args=new tree::ExpList({new tree::ConstExp(static_cast<int>(fields_->GetList().size())*8)});
  tree::Stm *s=new tree::MoveStm(new tree::TempExp(reg),frame::ExternalCall("allocRecord",args));
  for(auto field:fields_->GetList()) {
    auto value=field->exp_->Translate(v,t,l,done,e);
    s=Seq(s,new tree::MoveStm(new tree::MemExp(new tree::BinopExp(tree::PLUS_OP,new tree::TempExp(reg),new tree::ConstExp(offset))),value->exp_->UnEx()));offset+=8;
  }
  return Value(new tree::EseqExp(s,new tree::TempExp(reg)),ty);
}
tr::ExpAndTy *SeqExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  tree::Stm *prefix=nullptr;tr::ExpAndTy *last=nullptr;
  for(auto exp:seq_->GetList()) {
    if(last)prefix=Seq(prefix,last->exp_->UnNx());
    last=exp->Translate(v,t,l,done,e);
  }
  if(!last)return Value(new tree::ConstExp(0),type::VoidTy::Instance());
  return prefix?Value(new tree::EseqExp(prefix,last->exp_->UnEx()),last->ty_):last;
}
tr::ExpAndTy *AssignExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto dst=var_->Translate(v,t,l,done,e),src=exp_->Translate(v,t,l,done,e);
  return Statement(new tree::MoveStm(dst->exp_->UnEx(),src->exp_->UnEx()));
}
tr::ExpAndTy *IfExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto test=test_->Translate(v,t,l,done,e)->exp_->UnCx(e);
  auto yes=temp::LabelFactory::NewLabel(),no=temp::LabelFactory::NewLabel(),join=temp::LabelFactory::NewLabel();
  test.trues_.DoPatch(yes);test.falses_.DoPatch(no);
  auto a=then_->Translate(v,t,l,done,e);tree::Stm *s=Seq(test.stm_,new tree::LabelStm(yes));
  if(!elsee_) {
    s=Seq(s,a->exp_->UnNx());s=Seq(s,new tree::LabelStm(no));return Statement(s);
  }
  auto b=elsee_->Translate(v,t,l,done,e);auto result=temp::TempFactory::NewTemp();
  s=Seq(s,new tree::MoveStm(new tree::TempExp(result),a->exp_->UnEx()));s=Seq(s,Jump(join));
  s=Seq(s,new tree::LabelStm(no));s=Seq(s,new tree::MoveStm(new tree::TempExp(result),b->exp_->UnEx()));s=Seq(s,new tree::LabelStm(join));
  auto ty=a->ty_->ActualTy()==type::NilTy::Instance()?b->ty_:a->ty_;
  return Value(new tree::EseqExp(s,new tree::TempExp(result)),ty);
}
tr::ExpAndTy *WhileExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto test_label=temp::LabelFactory::NewLabel(),body_label=temp::LabelFactory::NewLabel(),exit=temp::LabelFactory::NewLabel();
  auto condition=test_->Translate(v,t,l,done,e)->exp_->UnCx(e);condition.trues_.DoPatch(body_label);condition.falses_.DoPatch(exit);
  tree::Stm *s=new tree::LabelStm(test_label);s=Seq(s,condition.stm_);s=Seq(s,new tree::LabelStm(body_label));
  s=Seq(s,body_->Translate(v,t,l,exit,e)->exp_->UnNx());s=Seq(s,Jump(test_label));s=Seq(s,new tree::LabelStm(exit));return Statement(s);
}
tr::ExpAndTy *ForExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto lo=lo_->Translate(v,t,l,done,e),hi=hi_->Translate(v,t,l,done,e);
  auto index=tr::Access::AllocLocal(l,escape_);auto limit=temp::TempFactory::NewTemp();
  auto body_label=temp::LabelFactory::NewLabel(),increment=temp::LabelFactory::NewLabel(),exit=temp::LabelFactory::NewLabel();
  v->BeginScope();v->Enter(var_,new env::VarEntry(index,type::IntTy::Instance(),true));
  auto body=body_->Translate(v,t,l,exit,e);v->EndScope();
  auto access=[&](){return index->access_->ToExp(Fp());};
  tree::Stm *s=new tree::MoveStm(access(),lo->exp_->UnEx());
  s=Seq(s,new tree::MoveStm(new tree::TempExp(limit),hi->exp_->UnEx()));
  s=Seq(s,new tree::CjumpStm(tree::LE_OP,access(),new tree::TempExp(limit),body_label,exit));
  s=Seq(s,new tree::LabelStm(body_label));s=Seq(s,body->exp_->UnNx());
  // Check before increment so a loop ending at INT_MAX cannot overflow.
  s=Seq(s,new tree::CjumpStm(tree::LT_OP,access(),new tree::TempExp(limit),increment,exit));
  s=Seq(s,new tree::LabelStm(increment));
  s=Seq(s,new tree::MoveStm(access(),new tree::BinopExp(tree::PLUS_OP,access(),new tree::ConstExp(1))));
  s=Seq(s,Jump(body_label));s=Seq(s,new tree::LabelStm(exit));return Statement(s);
}
tr::ExpAndTy *BreakExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  if(!done)throw std::logic_error("Break without loop label");return Statement(Jump(done));
}
tr::ExpAndTy *LetExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  v->BeginScope();t->BeginScope();tree::Stm *s=nullptr;
  for(auto dec:decs_->GetList())s=Seq(s,dec->Translate(v,t,l,done,e)->UnNx());
  auto body=body_->Translate(v,t,l,done,e);t->EndScope();v->EndScope();
  return s?Value(new tree::EseqExp(s,body->exp_->UnEx()),body->ty_):body;
}
tr::ExpAndTy *ArrayExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto size=size_->Translate(v,t,l,done,e),init=init_->Translate(v,t,l,done,e);
  return Value(frame::ExternalCall("initArray",new tree::ExpList({size->exp_->UnEx(),init->exp_->UnEx()})),t->Look(typ_));
}
tr::ExpAndTy *VoidExp::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  return Value(new tree::ConstExp(0),type::VoidTy::Instance());
}
tr::Exp *VarDec::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  auto init=init_->Translate(v,t,l,done,e);auto access=tr::Access::AllocLocal(l,escape_);
  auto ty=typ_?t->Look(typ_):init->ty_;v->Enter(var_,new env::VarEntry(access,ty));
  return new tr::NxExp(new tree::MoveStm(access->access_->ToExp(Fp()),init->exp_->UnEx()));
}
tr::Exp *TypeDec::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const { SemAnalyze(v,t,0,e);return Noop(); }
tr::Exp *FunctionDec::Translate(env::VEnvPtr v, env::TEnvPtr t, tr::Level *l, temp::Label *done, err::ErrorMsg *e) const {
  struct Header {FunDec *declaration;env::FunEntry *entry;};std::vector<Header> headers;
  for(auto function:functions_->GetList()) {
    auto types=new type::TyList();std::list<bool> escapes;
    for(auto param:function->params_->GetList()) {types->Append(t->Look(param->typ_));escapes.push_back(param->escape_);}
    auto label=temp::LabelFactory::NewLabel();auto level=tr::Level::NewLevel(l,label,escapes);
    auto result=function->result_?t->Look(function->result_):type::VoidTy::Instance();
    auto entry=new env::FunEntry(level,label,types,result);v->Enter(function->name_,entry);headers.push_back({function,entry});
  }
  for(auto header:headers) {
    auto function=header.declaration;auto entry=header.entry;v->BeginScope();
    auto accesses=entry->level_->Formals();auto access=accesses->begin();auto ty=entry->formals_->GetList().begin();
    for(auto param:function->params_->GetList())v->Enter(param->name_,new env::VarEntry(*access++,*ty++));
    auto body=function->body_->Translate(v,t,entry->level_,nullptr,e);v->EndScope();
    frags->PushBack(tr::Procedure(entry->level_,body->exp_));
  }
  return Noop();
}
type::Ty *NameTy::Translate(env::TEnvPtr t,err::ErrorMsg *e) const {return SemAnalyze(t,e);}
type::Ty *RecordTy::Translate(env::TEnvPtr t,err::ErrorMsg *e) const {return SemAnalyze(t,e);}
type::Ty *ArrayTy::Translate(env::TEnvPtr t,err::ErrorMsg *e) const {return SemAnalyze(t,e);}
}
