#include "tiger/regalloc/regalloc.h"

#include <cstdio>
#include <stdexcept>

#include "tiger/frame/x64frame.h"
#include "tiger/liveness/flowgraph.h"

extern frame::RegManager *reg_manager;

namespace ra {

namespace {
temp::TempList *CopyTemps(temp::TempList *list) {
  if (!list)
    return nullptr;
  auto copy = new temp::TempList();
  for (auto temp : list->GetList())
    copy->Append(temp);
  return copy;
}

temp::TempList *MappedTemps(temp::TempList *list,
                            const std::set<temp::Temp *> &spilled) {
  if (!list)
    return nullptr;
  auto mapped = new temp::TempList();
  for (auto temp : list->GetList())
    mapped->Append(spilled.count(temp) ? temp::TempFactory::NewTemp() : temp);
  return mapped;
}
} // namespace

std::string RegAllocator::SpillOperand(int offset) const {
  return frame_->GetLabel() + "_framesize" + std::to_string(offset) + "(%rsp)";
}

assem::InstrList *
RegAllocator::Rewrite(const std::list<assem::Instr *> &sources,
                      const std::set<temp::Temp *> &spilled) {
  auto output = new assem::InstrList();
  for (auto instr : sources) {
    if (dynamic_cast<assem::LabelInstr *>(instr)) {
      output->Append(instr);
      continue;
    }
    temp::TempList *defs = nullptr;
    temp::TempList *uses = nullptr;
    if (auto move = dynamic_cast<assem::MoveInstr *>(instr)) {
      defs = move->dst_;
      uses = move->src_;
    } else if (auto oper = dynamic_cast<assem::OperInstr *>(instr)) {
      defs = oper->dst_;
      uses = oper->src_;
    }
    bool touched = false;
    for (auto temp : defs?defs->GetList():std::list<temp::Temp *>{})
      if (spilled.count(temp))
        touched = true;
    for (auto temp : uses?uses->GetList():std::list<temp::Temp*>{})
      if (spilled.count(temp))
        touched = true;
    if (!touched) {
      output->Append(instr);
      continue;
    }
    auto new_def = defs ? MappedTemps(defs, spilled) : nullptr;
    auto new_use = uses ? MappedTemps(uses, spilled) : nullptr;
    // Loads first: every spill read gets a fresh temporary so values can be
    // stored back right after their definitions.
    if (new_use && uses) {
      int index = 0;
      for (auto original : uses->GetList()) {
        if (spilled.count(original))
          output->Append(new assem::OperInstr(
              "movq " + SpillOperand(spill_offsets_.at(original)) + ", `d0\n",
              new temp::TempList({new_use->NthTemp(index)}), nullptr, nullptr));
        ++index;
      }
    }
    if (auto move = dynamic_cast<assem::MoveInstr *>(instr))
      output->Append(new assem::MoveInstr(move->assem_, new_def, new_use));
    else if (auto oper = dynamic_cast<assem::OperInstr *>(instr))
      output->Append(new assem::OperInstr(oper->assem_, new_def, new_use,
                                          oper->jumps_));
    if (new_def && defs) {
      int index = 0;
      for (auto original : defs->GetList()) {
        if (spilled.count(original))
          output->Append(new assem::OperInstr(
              "movq `s0, " + SpillOperand(spill_offsets_.at(original)) + "\n",
              nullptr, new temp::TempList({new_def->NthTemp(index)}), nullptr));
        ++index;
      }
    }
  }
  return output;
}

assem::InstrList *
RegAllocator::Clone(const std::list<assem::Instr *> &sources) {
  auto output = new assem::InstrList();
  for (auto instr : sources) {
    if (auto move = dynamic_cast<assem::MoveInstr *>(instr)) {
      output->Append(new assem::MoveInstr(
          move->assem_, CopyTemps(move->dst_), CopyTemps(move->src_)));
    } else if (auto oper = dynamic_cast<assem::OperInstr *>(instr)) {
      auto targets = oper->jumps_
                         ? new assem::Targets(new std::vector<temp::Label *>(
                               oper->jumps_->labels_->begin(),
                               oper->jumps_->labels_->end()))
                         : nullptr;
      output->Append(new assem::OperInstr(
          oper->assem_, CopyTemps(oper->dst_), CopyTemps(oper->src_), targets));
    } else if (auto label = dynamic_cast<assem::LabelInstr *>(instr)) {
      output->Append(new assem::LabelInstr(label->assem_, label->label_));
    }
  }
  return output;
}

void RegAllocator::RegAlloc() {
  spill_offsets_.clear();
  const std::list<assem::Instr *> original =
      assem_instr_->GetInstrList()->GetList();
  std::list<assem::Instr *> current(original);
  for (int round = 0; round < 48; ++round) {
    auto wrapper = new assem::InstrList();
    for (auto instr : current)
      wrapper->Append(instr);
    auto flow = new fg::FlowGraphFactory(wrapper);
    flow->AssemFlowGraph();
    live::LiveGraphFactory liveness(flow->GetFlowGraph());
    liveness.Liveness();
    col::Color color(liveness.GetLiveGraph());
    auto result = color.AssignColors();
    if (result.spills->GetList().empty()) {
      coloring_ = result.coloring;
      il_ = Clone(current);
      delete wrapper;
      return;
    }
    std::set<temp::Temp *> spilled;
    for (auto node : result.spills->GetList())
      spilled.insert(node->NodeInfo());
    for (auto temp : spilled)
      if (!spill_offsets_.count(temp))
        spill_offsets_[temp] = frame_->AllocSpillSlot();
    current = Rewrite(original, spilled)->GetList();
    delete wrapper;
  }
  throw std::logic_error("register allocation failed to converge");
}

Result *RegAllocator::BuildAllocationResult() {
  if (!il_)
    throw std::logic_error("RegAlloc must run before results are built");
  return new Result(coloring_, il_);
}

} // namespace ra
