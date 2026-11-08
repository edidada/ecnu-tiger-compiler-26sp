#include "tiger/regalloc/color.h"

#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

extern frame::RegManager *reg_manager;

namespace col {

struct Color::Impl {
  using NodeSet = std::set<live::INodePtr>;

  explicit Impl(live::LiveGraph live) : live_(std::move(live)) {}

  Result AssignColors() {
    BuildPalette();
    BuildAdjacency();
    CoalesceMoves();
    SimplifySelect();
    return Emit();
  }

private:
  std::set<temp::Temp *> PrecoloredTemps() const {
    std::set<temp::Temp *> set;
    for (auto reg : reg_manager->Registers()->GetList())
      set.insert(reg);
    set.insert(reg_manager->StackPointer());
    set.insert(reg_manager->FramePointer());
    return set;
  }

  void BuildPalette() {
    for (auto reg : reg_manager->Registers()->GetList()) {
      auto name = reg_manager->temp_map_->Look(reg);
      if (name)
        palette_.push_back(*name);
    }
  }

  void BuildAdjacency() {
    for (auto node : live_.interf_graph->Nodes()->GetList()) {
      adjacency_[node].clear();
      auto &members = members_[node];
      members.clear();
      members.insert(node);
      if (auto name = reg_manager->temp_map_->Look(node->NodeInfo()))
        precolored_color_[node] = *name;
    }
    for (auto node : live_.interf_graph->Nodes()->GetList()) {
      for (auto succ : node->Succ()->GetList())
        adjacency_[node].insert(succ);
      for (auto pred : node->Pred()->GetList())
        adjacency_[node].insert(pred);
    }
  }

  // Group-find over coalesced groups: roots hold fixed colors until select.
  live::INodePtr Find(live::INodePtr node) const {
    auto found = group_.find(node);
    while (found != group_.end() && found->second != node) {
      node = found->second;
      found = group_.find(node);
    }
    return node;
  }

  bool Interferes(live::INodePtr a, live::INodePtr b) const {
    return adjacency_.at(a).count(b) || adjacency_.at(b).count(a);
  }

  // Always-safe coalescing: two temps of the same move share one color when
  // they do not interfere. Coalescing into a precolored register inherits the
  // fixed color, which keeps callee-save shadow temporaries from forcing
  // spills for their whole procedure.
  void CoalesceMoves() {
    for (auto [move_src, move_dst] : live_.moves->GetList()) {
      auto src = Find(move_src);
      auto dst = Find(move_dst);
      if (src == dst)
        continue;
      // Non-interference of the two move operands is THE safety criterion;
      // precolored machine registers get no exception.
      if (Interferes(src, dst))
        continue;
      bool fixed_pair = precolored_color_.count(src) || precolored_color_.count(dst);
      live::INodePtr keep = precolored_color_.count(src) ? src
                            : precolored_color_.count(dst)
                                ? dst
                                : (src < dst ? src : dst);
      auto fold = keep == src ? dst : src;
      group_[fold] = keep;
      auto &keep_members = members_[keep];
      auto &fold_members = members_[fold];
      for (auto member : fold_members)
        keep_members.insert(member);
      fold_members.clear();
      for (auto other : adjacency_[fold]) {
        if (other == keep)
          continue;
        adjacency_[other].erase(fold);
        adjacency_[other].insert(keep);
        adjacency_[keep].insert(other);
      }
      adjacency_[keep].erase(fold);
      adjacency_[keep].erase(keep);
      adjacency_[fold].clear();
      if (precolored_color_.count(fold)) {
        precolored_color_[keep] = precolored_color_[fold];
        precolored_color_.erase(fold);
      }
    }
  }

  void keep_adjacent(live::INodePtr node, live::INodePtr neighbor) {
    adjacency_[node].insert(neighbor);
  }

  std::set<std::string> FixedColors(live::INodePtr node) const {
    std::set<std::string> colors;
    for (auto other : adjacency_.at(node))
      if (precolored_color_.count(other))
        colors.insert(precolored_color_.at(other));
    return colors;
  }

  int Degree(live::INodePtr node) const {
    return adjacency_.at(node).size() + FixedColors(node).size();
  }

  void SimplifySelect() {
    std::set<live::INodePtr> colorable;
    for (auto [node, unused] : adjacency_) {
      (void)unused;
      if (!precolored_color_.count(node))
        colorable.insert(node);
    }
    while (!colorable.empty()) {
      live::INodePtr pick = nullptr;
      for (auto node : colorable)
        if (!pick || Degree(node) > Degree(pick))
          pick = node;
      if (!pick)
        break;
      colorable.erase(pick);
      stack_.push_back(pick);
    }
    while (!stack_.empty()) {
      auto node = stack_.back();
      stack_.pop_back();
      auto used = AdjacentColors(node);
      const std::string *chosen = nullptr;
      for (auto &color : palette_) {
        if (std::find(used.begin(), used.end(), color) == used.end()) {
          chosen = &color;
          break;
        }
      }
      colored_[node] = chosen ? *chosen : std::string();
    }
  }

  std::vector<std::string> AdjacentColors(live::INodePtr node) const {
    std::vector<std::string> used;
    for (auto other : adjacency_.at(node)) {
      auto colored = colored_.find(other);
      if (colored != colored_.end()) {
        if (std::find(used.begin(), used.end(), colored->second) == used.end())
          used.push_back(colored->second);
        continue;
      }
      auto fixed = precolored_color_.find(other);
      if (fixed != precolored_color_.end() &&
          std::find(used.begin(), used.end(), fixed->second) == used.end())
        used.push_back(fixed->second);
    }
    return used;
  }

  Result Emit() {
    auto *coloring = temp::Map::Empty();
    auto *spills = new live::INodeList();
    for (auto [rep, color] : colored_) {
      for_each_member(rep, [&](live::INodePtr member) {
        if (color.empty())
          spills->Append(member);
        else
          coloring->Enter(member->NodeInfo(), new std::string(color));
      });
    }
    for (auto [rep, color] : precolored_color_) {
      for_each_member(rep, [&](live::INodePtr member) {
        coloring->Enter(member->NodeInfo(), new std::string(color));
      });
    }
    return Result(coloring, spills);
  }

  template <typename F> void for_each_member(live::INodePtr rep, F emit) const {
    auto found = members_.find(rep);
    if (found != members_.end())
      for (auto member : found->second)
        emit(member);
    else
      emit(rep);
  }

  live::LiveGraph live_;
  std::vector<std::string> palette_;
  std::map<live::INodePtr, NodeSet> adjacency_;
  std::map<live::INodePtr, live::INodePtr> group_;
  std::map<live::INodePtr, NodeSet> members_;
  std::map<live::INodePtr, std::string> precolored_color_;
  std::list<live::INodePtr> stack_;
  std::map<live::INodePtr, std::string> colored_;
};

Color::Color(live::LiveGraph live) : impl_(std::make_unique<Impl>(std::move(live))) {}
Color::~Color() = default;

Result Color::AssignColors() { return impl_->AssignColors(); }

} // namespace col
