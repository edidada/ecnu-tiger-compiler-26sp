// Independent executor of the generated IR; never evaluates the source AST.
#include "tiger/parse/parser.h"
#include "tiger/escape/escape.h"
#include "tiger/translate/translate.h"
#include "tiger/frame/x64frame.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
extern frame::RegManager *reg_manager;
extern frame::Frags *frags;
namespace {
using Word=std::int64_t;
struct Transfer {temp::Label *target;};
struct Exit {int code;};
struct Activation {std::unordered_map<temp::Temp *,Word> regs;};
class Machine {
  std::unordered_map<Word,Word> memory;
  std::unordered_map<Word,std::string> strings;
  std::unordered_map<temp::Label *,Word> literals;
  std::unordered_map<temp::Label *,frame::ProcFrag *> procedures;
  Word next=65536;
  std::uint64_t steps=0,unmapped_reads=0;
  bool official_memory;
  void tick() {if(++steps>50000000)throw std::runtime_error("IR step budget exceeded");}
  Word alloc(Word words) {
    if(words<0 || words>1000000)throw std::runtime_error("Invalid allocation");
    Word address=next;next+=(words+1)*8;
    for(Word i=0;i<words;i++)memory[address+i*8]=0;
    return address;
  }
  Word string(std::string value) {Word address=alloc(0);strings[address]=std::move(value);return address;}
  const std::string &text(Word address) {return strings.at(address);}
  void flatten(tree::Stm *s,std::vector<tree::Stm *> &out) {
    if(auto seq=dynamic_cast<tree::SeqStm *>(s)) {flatten(seq->left_,out);if(seq->right_)flatten(seq->right_,out);}
    else if(s)out.push_back(s);
  }
  bool compare(tree::RelOp op,Word a,Word b) {
    switch(op) {
      case tree::EQ_OP:return a==b;case tree::NE_OP:return a!=b;
      case tree::LT_OP:return a<b;case tree::LE_OP:return a<=b;
      case tree::GT_OP:return a>b;case tree::GE_OP:return a>=b;
      default:throw std::runtime_error("Unsupported unsigned comparison");
    }
  }
  Word external(const std::string &name,const std::vector<Word> &args) {
    if(name=="print") {std::cout<<text(args.at(0));return 0;}
    if(name=="printi") {std::cout<<args.at(0);return 0;}
    if(name=="flush") {std::cout.flush();return 0;}
    if(name=="not")return args.at(0)==0;
    if(name=="size")return text(args.at(0)).size();
    if(name=="ord")return text(args.at(0)).empty()?-1:static_cast<unsigned char>(text(args.at(0))[0]);
    if(name=="chr") {if(args.at(0)<0 || args[0]>255)throw std::runtime_error("chr range");return string(std::string(1,static_cast<char>(args[0])));}
    if(name=="getchar") {int c=std::cin.get();return string(c==EOF?"":std::string(1,static_cast<char>(c)));}
    if(name=="concat")return string(text(args.at(0))+text(args.at(1)));
    if(name=="substring") {
      auto value=text(args.at(0));Word start=args.at(1),length=args.at(2);
      if(start<0 || length<0 || start+length>static_cast<Word>(value.size()))throw std::runtime_error("substring range");
      return string(value.substr(start,length));
    }
    if(name=="string_equal")return text(args.at(0))==text(args.at(1));
    if(name=="string_compare") {int result=text(args.at(0)).compare(text(args.at(1)));return (result>0)-(result<0);}
    if(name=="alloc_record")return alloc((args.at(0)+7)/8);
    if(name=="init_array") {Word pointer=alloc(args.at(0));for(Word i=0;i<args[0];i++)memory[pointer+i*8]=args.at(1);return pointer;}
    if(name=="exit")throw Exit{static_cast<int>(args.at(0))};
    throw std::runtime_error("Unknown external call: "+name);
  }
  Word eval(tree::Exp *e,Activation &a) {
    tick();
    if(auto c=dynamic_cast<tree::ConstExp *>(e))return c->consti_;
    if(auto t=dynamic_cast<tree::TempExp *>(e))return a.regs.at(t->temp_);
    if(auto n=dynamic_cast<tree::NameExp *>(e))return literals.at(n->name_);
    if(auto m=dynamic_cast<tree::MemExp *>(e)) {
      Word address=eval(m->exp_,a);auto found=memory.find(address);
      if(found!=memory.end())return found->second;
      if(official_memory) {++unmapped_reads;return 0xbaadbeef;}
      throw std::runtime_error("Unmapped IR memory read at "+std::to_string(address));
    }
    if(auto s=dynamic_cast<tree::EseqExp *>(e)) {execute(s->stm_,a);return eval(s->exp_,a);}
    if(auto b=dynamic_cast<tree::BinopExp *>(e)) {
      Word left=eval(b->left_,a),right=eval(b->right_,a);
      switch(b->op_) {
        case tree::PLUS_OP:return left+right;case tree::MINUS_OP:return left-right;
        case tree::MUL_OP:return left*right;case tree::DIV_OP:if(!right)throw std::runtime_error("division by zero");return left/right;
        default:throw std::runtime_error("Unsupported binary IR operation");
      }
    }
    if(auto c=dynamic_cast<tree::CallExp *>(e)) {
      auto name=dynamic_cast<tree::NameExp *>(c->fun_);if(!name)throw std::runtime_error("Indirect call");
      bool internal=procedures.count(name->name_)!=0;std::vector<Word> args;
      for(auto arg:c->args_->GetList()) {
        auto marker=dynamic_cast<tree::NameExp *>(arg);
        if(!internal && args.empty() && marker && marker->name_->Name()=="staticLink")continue;
        args.push_back(eval(arg,a));
      }
      return internal?invoke(procedures.at(name->name_),args):external(name->name_->Name(),args);
    }
    throw std::runtime_error("Unknown IR expression");
  }
  void execute(tree::Stm *root,Activation &a) {
    std::vector<tree::Stm *> code;flatten(root,code);
    std::unordered_map<temp::Label *,std::size_t> labels;
    for(std::size_t i=0;i<code.size();i++)if(auto l=dynamic_cast<tree::LabelStm *>(code[i]))labels[l->label_]=i;
    for(std::size_t pc=0;pc<code.size();pc++) {
      tick();auto s=code[pc];
      try {
        if(dynamic_cast<tree::LabelStm *>(s))continue;
        if(auto j=dynamic_cast<tree::JumpStm *>(s))throw Transfer{j->exp_->name_};
        if(auto j=dynamic_cast<tree::CjumpStm *>(s)) {
          Word left=eval(j->left_,a),right=eval(j->right_,a);
          throw Transfer{compare(j->op_,left,right)?j->true_label_:j->false_label_};
        }
        if(auto m=dynamic_cast<tree::MoveStm *>(s)) {
          // An lvalue address is evaluated before the rhs (possibly a call).
          if(auto dst=dynamic_cast<tree::MemExp *>(m->dst_)) {
            Word address=eval(dst->exp_,a);Word value=eval(m->src_,a);
            if(!memory.count(address))throw std::runtime_error("Invalid IR store");memory[address]=value;
          } else if(auto dst=dynamic_cast<tree::TempExp *>(m->dst_))a.regs[dst->temp_]=eval(m->src_,a);
          else throw std::runtime_error("Invalid MOVE destination");
        } else if(auto exp=dynamic_cast<tree::ExpStm *>(s))eval(exp->exp_,a);
        else throw std::runtime_error("Unknown IR statement");
      } catch(const Transfer &jump) {
        auto label=labels.find(jump.target);if(label==labels.end())throw;
        pc=label->second; // The label itself has no effect.
      }
    }
  }
  Word invoke(frame::ProcFrag *proc,const std::vector<Word> &args) {
    Activation a;
    for(auto reg:reg_manager->Registers()->GetList())a.regs[reg]=0;
    Word fp=next+1000000;next=fp+1000000;
    a.regs[reg_manager->FramePointer()]=fp;a.regs[reg_manager->StackPointer()]=fp;a.regs[reg_manager->ReturnValue()]=0;
    for(int offset=-8;offset>=proc->frame_->offset_;offset-=8)memory[fp+offset]=0;
    auto regs=reg_manager->ArgRegs()->GetList();auto reg=regs.begin();std::size_t index=0;
    for(Word value:args) {
      if(reg!=regs.end())a.regs[*reg++]=value;
      else memory[fp+8+static_cast<Word>(index-6)*8]=value;
      index++;
    }
    execute(proc->body_,a);return a.regs.at(reg_manager->ReturnValue());
  }
public:
  explicit Machine(bool compatibility):official_memory(compatibility) {}
  Word run() {
    frame::ProcFrag *main=nullptr;
    for(auto frag:frags->GetList()) {
      if(auto s=dynamic_cast<frame::StringFrag *>(frag))literals[s->label_]=string(s->str_);
      if(auto p=dynamic_cast<frame::ProcFrag *>(frag)) {
        procedures[p->frame_->Name()]=p;if(p->frame_->GetLabel()=="tigermain")main=p;
      }
    }
    if(!main)throw std::runtime_error("Missing main fragment");
    Word value=invoke(main,{});std::cerr<<"IR_RESULT "<<value<<" STEPS "<<steps<<" UNMAPPED_READS "<<unmapped_reads<<"\n";return value;
  }
};
}
int main(int argc,char **argv) {
  if(argc<2 || argc>3 || (argc==3 && std::string(argv[2])!="--official-memory"))return 2;
  reg_manager=new frame::X64RegManager();frags=new frame::Frags();
  try {
    Parser parser(argv[1],std::cerr);parser.parse();
    auto tree=parser.TransferAbsynTree();auto errors=parser.TransferErrormsg();
    if(!tree || errors->AnyErrors())return 1;
    esc::EscFinder finder(std::move(tree));finder.FindEscape();
    tr::ProgTr translator(finder.TransferAbsynTree(),std::move(errors));translator.Translate();
    errors=translator.TransferErrormsg();if(errors->AnyErrors())return 1;
    Machine machine(argc==3);machine.run();return 0;
  } catch(const Exit &exit) {return exit.code;}
  catch(const std::exception &error) {std::cerr<<"IR_ERROR "<<error.what()<<"\n";return 3;}
  catch(const Transfer &) {std::cerr<<"IR_ERROR unresolved control label\n";return 3;}
}
