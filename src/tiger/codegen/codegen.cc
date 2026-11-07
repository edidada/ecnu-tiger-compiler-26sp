#include "tiger/codegen/codegen.h"
#include <stdexcept>
#include <vector>
extern frame::RegManager *reg_manager;
namespace {
temp::TempList *Temps(std::initializer_list<temp::Temp *> list) {return new temp::TempList(list);}
void Move(assem::InstrList &out,temp::Temp *dst,temp::Temp *src) {
  out.Append(new assem::MoveInstr("movq `s0, `d0\n",Temps({dst}),Temps({src})));
}
void Op(assem::InstrList &out,std::string text,temp::TempList *dst=nullptr,temp::TempList *src=nullptr,assem::Targets *targets=nullptr) {
  out.Append(new assem::OperInstr(std::move(text),dst,src,targets));
}
struct Address {std::string text;temp::Temp *base;};
Address AddressOf(tree::Exp *expression,assem::InstrList &out,std::string_view fs) {
  tree::Exp *base=expression;int displacement=0;
  if(auto add=dynamic_cast<tree::BinopExp *>(expression))if(add->op_==tree::PLUS_OP) {
    if(auto c=dynamic_cast<tree::ConstExp *>(add->right_)) {base=add->left_;displacement=c->consti_;}
    else if(auto c=dynamic_cast<tree::ConstExp *>(add->left_)) {base=add->right_;displacement=c->consti_;}
  }
  if(auto fp=dynamic_cast<tree::TempExp *>(base))if(fp->temp_==reg_manager->FramePointer())
    return {std::string(fs)+(displacement<0?"":"+")+std::to_string(displacement)+"(`s0)",reg_manager->StackPointer()};
  return {std::to_string(displacement)+"(`s0)",base->Munch(out,fs)};
}
std::string Branch(tree::RelOp op) {
  switch(op) {
    case tree::EQ_OP:return "je";case tree::NE_OP:return "jne";
    case tree::LT_OP:return "jl";case tree::LE_OP:return "jle";
    case tree::GT_OP:return "jg";case tree::GE_OP:return "jge";
    case tree::ULT_OP:return "jb";case tree::ULE_OP:return "jbe";
    case tree::UGT_OP:return "ja";case tree::UGE_OP:return "jae";
    default:throw std::logic_error("Unknown relation");
  }
}
}
namespace cg {
void CodeGen::Codegen() {
  fs_=frame_->GetLabel()+"_framesize";
  auto instructions=new assem::InstrList();
  for(auto statement:traces_->GetStmList()->GetList())statement->Munch(*instructions,fs_);
  assem_instr_=std::make_unique<AssemInstr>(frame::ProcEntryExit2(instructions));
}
void AssemInstr::Print(FILE *out,temp::Map *map) const {
  instr_list_->Print(out,map);fprintf(out,"\n");
}
}
namespace tree {
temp::TempList *ExpList::MunchArgs(assem::InstrList &out,std::string_view fs) {
  // Evaluate/snapshot every argument before writing ABI registers; a later
  // argument may call another function and clobber all caller-saved registers.
  std::vector<temp::Temp *> values;bool first=true;
  for(auto expression:GetList()) {
    auto marker=dynamic_cast<NameExp *>(expression);
    if(first && marker && marker->name_->Name()=="staticLink") {first=false;continue;}
    first=false;auto snapshot=temp::TempFactory::NewTemp();
    Move(out,snapshot,expression->Munch(out,fs));values.push_back(snapshot);
  }
  auto argregs=reg_manager->ArgRegs()->GetList();auto reg=argregs.begin();
  auto used=new temp::TempList();std::size_t index=0;
  for(auto value:values) {
    if(reg!=argregs.end()) {Move(out,*reg,value);used->Append(*reg++);}
    else Op(out,"movq `s0, "+std::to_string((index-6)*8)+"(`s1)\n",nullptr,Temps({value,reg_manager->StackPointer()}));
    ++index;
  }
  used->Append(reg_manager->StackPointer());return used;
}
void SeqStm::Munch(assem::InstrList &,std::string_view) {throw std::logic_error("SEQ survived canonicalization");}
void LabelStm::Munch(assem::InstrList &out,std::string_view) {out.Append(new assem::LabelInstr(label_->Name()+":\n",label_));}
void JumpStm::Munch(assem::InstrList &out,std::string_view) {Op(out,"jmp `j0\n",nullptr,nullptr,new assem::Targets(jumps_));}
void CjumpStm::Munch(assem::InstrList &out,std::string_view fs) {
  auto left=left_->Munch(out,fs);auto snapshot=temp::TempFactory::NewTemp();Move(out,snapshot,left);
  if(auto right=dynamic_cast<ConstExp *>(right_))Op(out,"cmpq $"+std::to_string(right->consti_)+", `s0\n",nullptr,Temps({snapshot}));
  else {auto right=right_->Munch(out,fs);Op(out,"cmpq `s0, `s1\n",nullptr,Temps({right,snapshot}));}
  if(!true_label_ || !false_label_)throw std::logic_error("Unpatched conditional label");
  Op(out,Branch(op_)+" `j0\n",nullptr,nullptr,new assem::Targets(new std::vector<temp::Label *>{true_label_}));
  Op(out,"jmp `j0\n",nullptr,nullptr,new assem::Targets(new std::vector<temp::Label *>{false_label_}));
}
void MoveStm::Munch(assem::InstrList &out,std::string_view fs) {
  if(auto dst=dynamic_cast<TempExp *>(dst_)) {
    if(dst->temp_==reg_manager->FramePointer())throw std::logic_error("Assignment to virtual frame pointer");
    Move(out,dst->temp_,src_->Munch(out,fs));
  } else if(auto dst=dynamic_cast<MemExp *>(dst_)) {
    auto address=AddressOf(dst->exp_,out,fs);auto value=src_->Munch(out,fs);
    // Memory string uses source slot zero for its base; keep value in slot one.
    Op(out,"movq `s1, "+address.text+"\n",nullptr,Temps({address.base,value}));
  } else throw std::logic_error("Noncanonical MOVE destination");
}
void ExpStm::Munch(assem::InstrList &out,std::string_view fs) {if(!dynamic_cast<ConstExp *>(exp_))exp_->Munch(out,fs);}
temp::Temp *BinopExp::Munch(assem::InstrList &out,std::string_view fs) {
  auto result=temp::TempFactory::NewTemp();Move(out,result,left_->Munch(out,fs));
  const char *mnemonic=op_==PLUS_OP?"addq":op_==MINUS_OP?"subq":op_==AND_OP?"andq":op_==OR_OP?"orq":op_==XOR_OP?"xorq":nullptr;
  if(mnemonic) {
    if(auto immediate=dynamic_cast<ConstExp *>(right_))Op(out,std::string(mnemonic)+" $"+std::to_string(immediate->consti_)+", `d0\n",Temps({result}),Temps({result}));
    else {auto right=right_->Munch(out,fs);Op(out,std::string(mnemonic)+" `s0, `d0\n",Temps({result}),Temps({right,result}));}
    return result;
  }
  auto right=temp::TempFactory::NewTemp();Move(out,right,right_->Munch(out,fs));
  auto rax=reg_manager->ReturnValue(),rdx=reg_manager->GetRegister(frame::X64RegManager::RDX);
  if(op_==MUL_OP || op_==DIV_OP) {
    Move(out,rax,result);
    if(op_==MUL_OP)Op(out,"imulq `s0\n",Temps({rax,rdx}),Temps({right,rax}));
    else {Op(out,"cqto\n",Temps({rdx}),Temps({rax}));Op(out,"idivq `s0\n",Temps({rax,rdx}),Temps({right,rax,rdx}));}
    Move(out,result,rax);return result;
  }
  if(op_==LSHIFT_OP || op_==RSHIFT_OP || op_==ARSHIFT_OP) {
    auto rcx=reg_manager->GetRegister(frame::X64RegManager::RCX);Move(out,rcx,right);
    Op(out,std::string(op_==LSHIFT_OP?"salq":op_==RSHIFT_OP?"shrq":"sarq")+" %cl, `d0\n",Temps({result}),Temps({rcx,result}));return result;
  }
  throw std::logic_error("Unknown IR binary operation");
}
temp::Temp *MemExp::Munch(assem::InstrList &out,std::string_view fs) {
  auto address=AddressOf(exp_,out,fs);auto result=temp::TempFactory::NewTemp();
  Op(out,"movq "+address.text+", `d0\n",Temps({result}),Temps({address.base}));return result;
}
temp::Temp *TempExp::Munch(assem::InstrList &out,std::string_view fs) {
  if(temp_!=reg_manager->FramePointer())return temp_;
  auto result=temp::TempFactory::NewTemp();
  Op(out,"leaq "+std::string(fs)+"(`s0), `d0\n",Temps({result}),Temps({reg_manager->StackPointer()}));return result;
}
temp::Temp *EseqExp::Munch(assem::InstrList &,std::string_view) {throw std::logic_error("ESEQ survived canonicalization");}
temp::Temp *NameExp::Munch(assem::InstrList &out,std::string_view) {
  auto result=temp::TempFactory::NewTemp();Op(out,"leaq "+name_->Name()+"(%rip), `d0\n",Temps({result}));return result;
}
temp::Temp *ConstExp::Munch(assem::InstrList &out,std::string_view) {
  auto result=temp::TempFactory::NewTemp();Op(out,"movq $"+std::to_string(consti_)+", `d0\n",Temps({result}));return result;
}
temp::Temp *CallExp::Munch(assem::InstrList &out,std::string_view fs) {
  auto target=dynamic_cast<NameExp *>(fun_);if(!target)throw std::logic_error("Tiger indirect call not supported");
  auto used=args_->MunchArgs(out,fs);
  Op(out,"call "+target->name_->Name()+"\n",reg_manager->CallerSaves(),used);
  auto result=temp::TempFactory::NewTemp();Move(out,result,reg_manager->ReturnValue());return result;
}
}
