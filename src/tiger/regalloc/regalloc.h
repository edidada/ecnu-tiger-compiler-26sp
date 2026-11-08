#ifndef TIGER_REGALLOC_REGALLOC_H_
#define TIGER_REGALLOC_REGALLOC_H_

#include <list>
#include <map>
#include <memory>
#include <set>

#include "tiger/codegen/assem.h"
#include "tiger/codegen/codegen.h"
#include "tiger/frame/frame.h"
#include "tiger/frame/temp.h"
#include "tiger/liveness/liveness.h"
#include "tiger/regalloc/color.h"
#include "tiger/util/graph.h"

namespace ra {

class Result {
public:
  temp::Map *coloring_;
  assem::InstrList *il_;

  Result() : coloring_(nullptr), il_(nullptr) {}
  Result(temp::Map *coloring, assem::InstrList *il)
      : coloring_(coloring), il_(il) {}
  Result(const Result &result) = delete;
  Result(Result &&result) = delete;
  Result &operator=(const Result &result) = delete;
  Result &operator=(Result &&result) = delete;
  ~Result(){delete coloring_; }
};

class RegAllocator {
public:
  RegAllocator(frame::Frame *frame,
               std::unique_ptr<cg::AssemInstr> assem_instr)
      : frame_(frame), assem_instr_(std::move(assem_instr)) {}

  ~RegAllocator() = default;

  RegAllocator(const RegAllocator &) = delete;
  RegAllocator(RegAllocator &&) = delete;
  RegAllocator &operator=(const RegAllocator &) = delete;
  RegAllocator &operator=(RegAllocator &&) = delete;

  // Iterative graph coloring with spill rewriting. The source instruction
  // list is never mutated; every round rewrites from the original snapshot,
  // so spill slots stay stable across rounds (spilled temps keep their
  // pre-assigned frame offsets).
  void RegAlloc();
  Result *BuildAllocationResult();

  [[nodiscard]] temp::Map *GetColoring() const { return coloring_; }
  [[nodiscard]] assem::InstrList *GetInstrList() const { return il_; }

private:
  assem::InstrList *Rewrite(const std::list<assem::Instr *> &list_slices,
                            const std::set<temp::Temp *> &spilled);
  assem::InstrList *Clone(const std::list<assem::Instr *> &list_slices);
  std::string SpillOperand(int offset) const;

  frame::Frame *frame_;
  std::unique_ptr<cg::AssemInstr> assem_instr_;
  temp::Map *coloring_ = nullptr;
  assem::InstrList *il_ = nullptr;
  std::map<temp::Temp *, int> spill_offsets_;
};

} // namespace ra

#endif
