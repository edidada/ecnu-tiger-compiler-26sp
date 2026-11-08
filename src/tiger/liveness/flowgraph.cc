#include "tiger/liveness/flowgraph.h"

#include <list>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace fg {

void FlowGraphFactory::AssemFlowGraph() {
  std::list<FNodePtr> nodes;
  std::list<std::pair<FNodePtr, temp::Label *>> jumps;
  std::unordered_map<assem::Instr *, FNodePtr> node_of;
  FNodePtr last = nullptr;
  for (auto instr : instr_list_->GetList()) {
    auto node = flowgraph_->NewNode(instr);
    node_of[instr] = node;
    nodes.push_back(node);
    if (last && !last->GoesTo(node) && !node->Adj(last))
      flowgraph_->AddEdge(last, node);
    bool fallthrough = true;
    if (auto oper = dynamic_cast<assem::OperInstr *>(instr)) {
      if (oper->jumps_ && oper->jumps_->labels_ && !oper->jumps_->labels_->empty())
        for (auto label : *oper->jumps_->labels_)
          jumps.emplace_back(node, label);
      if (oper->assem_.rfind("jmp ", 0) == 0)
        fallthrough = false;
    }
    last = fallthrough ? node : nullptr;
    if (auto label = dynamic_cast<assem::LabelInstr *>(instr))
      label_map_->Enter(label->label_, node);
  }
  for (auto [node, label] : jumps) {
    auto target = label_map_->Look(label);
    if (!target || target == node)
      throw std::logic_error("Jump target outside this proc flow graph");
    if (!node->GoesTo(target))flowgraph_->AddEdge(node, target);
  }
}

} // namespace fg

namespace assem {

temp::TempList *LabelInstr::Def() const { return nullptr; }

temp::TempList *MoveInstr::Def() const { return dst_; }

temp::TempList *OperInstr::Def() const { return dst_; }

temp::TempList *LabelInstr::Use() const { return nullptr; }

temp::TempList *MoveInstr::Use() const { return src_; }

temp::TempList *OperInstr::Use() const { return src_; }

} // namespace assem
