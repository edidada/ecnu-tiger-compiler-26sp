#include "tiger/liveness/liveness.h"

#include <algorithm>
#include <functional>
#include <set>

extern frame::RegManager *reg_manager;

namespace live {

bool MoveList::Contain(INodePtr src, INodePtr dst) {
  return std::any_of(move_list_.cbegin(), move_list_.cend(),
                     [src, dst](std::pair<INodePtr, INodePtr> move) {
                       return move.first == src && move.second == dst;
                     });
}

void MoveList::Delete(INodePtr src, INodePtr dst) {
  assert(src && dst);
  auto move_it = move_list_.begin();
  for (; move_it != move_list_.end(); move_it++) {
    if (move_it->first == src && move_it->second == dst) {
      break;
    }
  }
  move_list_.erase(move_it);
}

MoveList *MoveList::Union(MoveList *list) {
  auto *res = new MoveList();
  for (auto move : move_list_) {
    res->move_list_.push_back(move);
  }
  for (auto move : list->GetList()) {
    if (!res->Contain(move.first, move.second))
      res->move_list_.push_back(move);
  }
  return res;
}

MoveList *MoveList::Intersect(MoveList *list) {
  auto *res = new MoveList();
  for (auto move : list->GetList()) {
    if (Contain(move.first, move.second))
      res->move_list_.push_back(move);
  }
  return res;
}

namespace {
using TempSet = std::set<temp::Temp *>;

TempSet ToSet(temp::TempList *list) {
  TempSet set;
  if (list)
    for (auto temp : list->GetList())
      set.insert(temp);
  return set;
}

temp::TempList *ToTempList(const TempSet &set) {
  auto list = new temp::TempList();
  for (auto temp : set)
    list->Append(temp);
  return list;
}

TempSet SetMinus(TempSet left, const TempSet &right) {
  for (auto item : right)
    left.erase(item);
  return left;
}

} // namespace

void LiveGraphFactory::LiveMap() {
  auto nodes = flowgraph_->Nodes()->GetList();
  int iteration = 0;
  bool changed = true;
  fprintf(stderr, "[cc] liveness nodes %d iterations begin\n", (int)nodes.size());
  while (changed) {
    changed = false;
    if (++iteration > 2048) {
      fprintf(stderr, "[cc] liveness iteration %d exceed cap\n", iteration);
      break;
    }
    if (iteration % 64 == 0)
      fprintf(stderr, "[cc] liveness iteration %d\n", iteration);
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
      auto node = *it;
      TempSet out;
      for (auto succ : node->Succ()->GetList()) {
        auto succ_in = in_->Look(succ);
        auto succ_set = succ_in ? ToSet(succ_in) : TempSet();
        out.insert(succ_set.begin(), succ_set.end());
      }
      auto instr_use = ToSet(node->NodeInfo()->Use());
      auto instr_def = ToSet(node->NodeInfo()->Def());
      auto in = instr_use;
      auto rest = SetMinus(out, instr_def);
      in.insert(rest.begin(), rest.end());
      auto out_ptr = out_->Look(node);
      auto out_old = out_ptr ? ToSet(out_ptr) : TempSet();
      auto in_ptr = in_->Look(node);
      auto in_old = in_ptr ? ToSet(in_ptr) : TempSet();
      if (out_old != out || in_old != in) {
        changed = true;
        if (in_ptr)
          in_->Set(node, ToTempList(in));
        else
          in_->Enter(node, ToTempList(in));
        if (out_ptr)
          out_->Set(node, ToTempList(out));
        else
          out_->Enter(node, ToTempList(out));
      }
    }
  }
}

void LiveGraphFactory::InterfGraph() {
  auto node_of = [this](temp::Temp *temp) {
    auto existing = temp_node_map_->Look(temp);
    if (existing)
      return existing;
    auto node = live_graph_.interf_graph->NewNode(temp);
    temp_node_map_->Enter(temp, node);
    return node;
  };
  // Precolored machine registers never move and always exclude the same
  // color from every neighbor.
  auto machine_regs = reg_manager->Registers()->GetList();
  for (auto reg : machine_regs) {
    auto reg_node = node_of(reg);
    for (auto other : machine_regs) {
      if (other == reg)
        continue;
      auto other_node = node_of(other);
      live_graph_.interf_graph->AddEdge(reg_node, other_node);
      live_graph_.interf_graph->AddEdge(other_node, reg_node);
    }
  }
  for (auto node : flowgraph_->Nodes()->GetList()) {
    auto out_ptr = out_->Look(node);
    if (!out_ptr)
      continue;
    TempSet out = ToSet(out_ptr);
    auto instr = node->NodeInfo();
    if (auto move = dynamic_cast<assem::MoveInstr *>(instr)) {
      auto uses = ToSet(move->Use());
      for (auto defined : move->Def()->GetList()) {
        auto defined_node = node_of(defined);
        for (auto live : SetMinus(out, uses)) {
          if (live == defined)
            continue;
          auto live_node = node_of(live);
          live_graph_.interf_graph->AddEdge(defined_node, live_node);
          live_graph_.interf_graph->AddEdge(live_node, defined_node);
        }
      }
      if (move->Def() && move->Use() && !move->Def()->GetList().empty() &&
          !move->Use()->GetList().empty())
        live_graph_.moves->Append(node_of(move->Use()->GetList().front()),
                                  node_of(move->Def()->GetList().front()));
    } else {
      for (auto defined : instr->Def()->GetList()) {
        auto defined_node = node_of(defined);
        for (auto live : out) {
          if (live == defined)
            continue;
          auto live_node = node_of(live);
          live_graph_.interf_graph->AddEdge(defined_node, live_node);
          live_graph_.interf_graph->AddEdge(live_node, defined_node);
        }
      }
    }
  }
}

void LiveGraphFactory::Liveness() {
  LiveMap();
  InterfGraph();
}

} // namespace live
