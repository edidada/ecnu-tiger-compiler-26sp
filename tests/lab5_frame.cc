#include "tiger/frame/x64frame.h"
#include <iostream>
#include <stdexcept>
#include <vector>
extern frame::RegManager *reg_manager;
namespace {
void check(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
int offset(tree::Exp *expression) {
  auto mem=dynamic_cast<tree::MemExp *>(expression);check(mem,"Frame access must be MEM");
  auto add=dynamic_cast<tree::BinopExp *>(mem->exp_);check(add && add->op_==tree::PLUS_OP,"Frame offset must be FP + constant");
  auto fp=dynamic_cast<tree::TempExp *>(add->left_);check(fp && fp->temp_==reg_manager->FramePointer(),"Incorrect virtual FP");
  auto value=dynamic_cast<tree::ConstExp *>(add->right_);check(value,"Nonconstant frame offset");return value->consti_;
}
bool same(tree::Exp *a,tree::Exp *b) {
  if(auto x=dynamic_cast<tree::TempExp *>(a)) {
    auto y=dynamic_cast<tree::TempExp *>(b);return y && x->temp_==y->temp_;
  }
  return dynamic_cast<tree::MemExp *>(a) && dynamic_cast<tree::MemExp *>(b) && offset(a)==offset(b);
}
void flatten(tree::Stm *statement,std::vector<tree::MoveStm *> &moves) {
  if(auto seq=dynamic_cast<tree::SeqStm *>(statement)) {flatten(seq->left_,moves);if(seq->right_)flatten(seq->right_,moves);}
  else if(auto move=dynamic_cast<tree::MoveStm *>(statement))moves.push_back(move);
}
}
int main() {
  try {
    reg_manager=new frame::X64RegManager();check(reg_manager->WordSize()==8,"Word size");
    auto label=temp::LabelFactory::NamedLabel("frame_test");
    auto activation=frame::NewFrame(label,{true,false,true,false,false,false,false,true});
    check(activation->Name()==label && activation->GetLabel()=="frame_test","Frame name");
    check(activation->Formals()->size()==8,"Formal count including static link");
    std::vector<frame::Access *> formals(activation->Formals()->begin(),activation->Formals()->end());
    check(offset(formals[0]->ToExp(new tree::TempExp(reg_manager->FramePointer())))==-8,"First escaping formal");
    check(offset(formals[2]->ToExp(new tree::TempExp(reg_manager->FramePointer())))==-16,"Second escaping formal");
    check(offset(formals[7]->ToExp(new tree::TempExp(reg_manager->FramePointer())))==-24,"Last formal static link");
    for(int i:{1,3,4,5,6})check(dynamic_cast<tree::TempExp *>(formals[i]->ToExp(nullptr)),"Nonescaping formal not in register");
    std::vector<tree::MoveStm *> moves;
    flatten(frame::ProcEntryExit1(activation,new tree::ExpStm(new tree::ConstExp(0))),moves);
    auto arguments=reg_manager->ArgRegs()->GetList();std::vector<temp::Temp *> regs(arguments.begin(),arguments.end());
    for(int i=0;i<8;i++) {
      auto dst=formals[i]->ToExp(new tree::TempExp(reg_manager->FramePointer()));tree::MoveStm *found=nullptr;
      for(auto move:moves)if(same(dst,move->dst_)) {check(!found,"Duplicate formal view shift");found=move;}
      check(found,"Missing formal view shift");
      if(i<6) {auto src=dynamic_cast<tree::TempExp *>(found->src_);check(src && src->temp_==regs[i],"Argument register order");}
      else check(offset(found->src_)==(i-6+1)*8,"Incoming stack argument offset");
    }
    auto local=activation->AllocLocal(true);check(offset(local->ToExp(new tree::TempExp(reg_manager->FramePointer())))==-32,"Escaping local overlaps formals");
    auto a=dynamic_cast<tree::TempExp *>(activation->AllocLocal(false)->ToExp(nullptr));
    auto b=dynamic_cast<tree::TempExp *>(activation->AllocLocal(false)->ToExp(nullptr));check(a && b && a->temp_!=b->temp_,"Register locals alias");
    activation->AllocOutgoSpace(24);activation->AllocOutgoSpace(8);check(activation->outgo_size_==24,"Outgoing maximum shrank");
    check(activation->FrameSize()>=-activation->offset_+activation->outgo_size_,"Frame too small");
    check(activation->FrameSize()%16==8,"SysV call-site stack alignment");
    std::cout<<"frame checks passed\n";return 0;
  } catch(const std::exception &error) {std::cerr<<error.what()<<"\n";return 1;}
}
