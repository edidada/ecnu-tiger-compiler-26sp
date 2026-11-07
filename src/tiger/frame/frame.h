#ifndef TIGER_FRAME_FRAME_H_
#define TIGER_FRAME_FRAME_H_

#include <list>
#include <memory>
#include <string>

#include "tiger/frame/temp.h"
#include "tiger/translate/tree.h"

namespace frame {

class RegManager {
public:
  RegManager() : temp_map_(temp::Map::Empty()) {}

  temp::Temp *GetRegister(int regno) { return regs_[regno]; }

  /**
   * Get general-purpose registers except RSI
   * NOTE: returned temp list should be in the order of calling convention
   * @return general-purpose registers
   */
  [[nodiscard]] virtual temp::TempList *Registers() = 0;

  /**
   * Get registers which can be used to hold arguments
   * NOTE: returned temp list must be in the order of calling convention
   * @return argument registers
   */
  [[nodiscard]] virtual temp::TempList *ArgRegs() = 0;

  /**
   * Get caller-saved registers
   * NOTE: returned registers must be in the order of calling convention
   * @return caller-saved registers
   */
  [[nodiscard]] virtual temp::TempList *CallerSaves() = 0;

  /**
   * Get callee-saved registers
   * NOTE: returned registers must be in the order of calling convention
   * @return callee-saved registers
   */
  [[nodiscard]] virtual temp::TempList *CalleeSaves() = 0;

  /**
   * Get return-sink registers
   * @return return-sink registers
   */
  [[nodiscard]] virtual temp::TempList *ReturnSink() = 0;

  /**
   * Get word size
   */
  [[nodiscard]] virtual int WordSize() = 0;

  [[nodiscard]] virtual temp::Temp *FramePointer() = 0;

  [[nodiscard]] virtual temp::Temp *StackPointer() = 0;

  [[nodiscard]] virtual temp::Temp *ReturnValue() = 0;

  temp::Map *temp_map_;
protected:
  std::vector<temp::Temp *> regs_;
};

class Access {
public:
  virtual tree::Exp *ToExp(tree::Exp *frame_ptr) const = 0;
  virtual ~Access() = default;
  
};

class Frame {
public:
  int word_size_, offset_, outgo_size_=0;
  temp::Label *name_;
  std::list<Access *> *formals_;
  Frame(int word_size,int offset,temp::Label *name,std::list<Access *> *formals)
      :word_size_(word_size),offset_(offset),name_(name),formals_(formals) {}
  virtual ~Frame()=default;
  virtual std::string GetLabel() const=0;
  virtual temp::Label *Name() const=0;
  virtual std::list<Access *> *Formals() const=0;
  virtual Access *AllocLocal(bool escape)=0;
  virtual void AllocOutgoSpace(int size)=0;
  int FrameSize() const {return ((-offset_+outgo_size_+15)/16)*16;}
};

/**
 * Fragments
 */

class Frag {
public:
  virtual ~Frag() = default;
};

class StringFrag : public Frag {
public:
  temp::Label *label_;
  std::string str_;

  StringFrag(temp::Label *label, std::string str)
      : label_(label), str_(std::move(str)) {}
};

class ProcFrag : public Frag {
public:
  tree::Stm *body_;
  Frame *frame_;

  ProcFrag(tree::Stm *body, Frame *frame) : body_(body), frame_(frame) {}
};

class Frags {
public:
  Frags() = default;
  void PushBack(Frag *frag) { frags_.push_back(frag); }
  const std::list<Frag*> &GetList() { return frags_; }

private:
  std::list<Frag*> frags_;
};

Frame *NewFrame(temp::Label *name,std::list<bool> formals);
tree::Exp *ExternalCall(std::string_view name,tree::ExpList *args);
tree::Stm *ProcEntryExit1(Frame *frame,tree::Stm *body);

} // namespace frame

#endif