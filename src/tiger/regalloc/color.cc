#include "tiger/regalloc/color.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

extern frame::RegManager *reg_manager;

namespace col {

namespace {
using NodeAdj = std::set<live::INodePtr>;
} // namespace

struct Color::Impl {
  explicit Impl(live::LiveGraph live) : live_(std::move(live)) {}

  Result AssignColors() {
    BuildPalette();
    BuildAdjacency();
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
    auto machine = PrecoloredTemps();
    for (auto node : live_.interf_graph->Nodes()->GetList()) {
      adjacency_[node];
      if (machine.count(node->NodeInfo())) {
        if (auto name = reg_manager->temp_map_->Look(node->NodeInfo()))
          precolored_color_[node] = *name;
      } else {
        colorable_.insert(node);
      }
    }
    for (auto node : live_.interf_graph->Nodes()->GetList()) {
      for (auto succ : node->Succ()->GetList())
        adjacency_[node].insert(succ);
      for (auto pred : node->Pred()->GetList())
        adjacency_[node].insert(pred);
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

  void SimplifySelect() {
    while (colorable_.size() + stack_.size() < adjacency_.size()) {
      live::INodePtr pick = nullptr;
      for (auto node : colorable_) {
        if (adjacency_.at(node).size() < palette_.size() &&
            (!pick || adjacency_.at(node).size() > adjacency_.at(pick).size()))
          pick = node;
      }
      if (pick) {
        colorable_.erase(pick);
        stack_.push_back(pick);
        for (auto other : adjacency_.at(pick))
          adjacency_.at(other).erase(pick);
        adjacency_.at(pick).clear();
        continue;
      }
      // Optimistic potentialization: still push the highest-degree node and
      // hope a color frees up during selection.
      for (auto node : colorable_) {
        if (!pick || adjacency_.at(node).size() > adjacency_.at(pick).size())
          pick = node;
      }
      if (!pick)
        break;
      colorable_.erase(pick);
      stack_.push_back(pick);
      for (auto other : adjacency_.at(pick))
        adjacency_.at(other).erase(pick);
      adjacency_.at(pick).clear();
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

  Result Emit() {
    auto *coloring = temp::Map::Empty();
    auto *spills = new live::INodeList();
    auto machine = PrecoloredTemps();
    for (auto [node, color] : colored_) {
      auto info = node->NodeInfo();
      if (machine.count(info))
        continue;
      if (color.empty())
        spills->Append(node);
      else
        coloring->Enter(info, new std::string(color));
    }
    return Result(coloring, spills);
  }

  live::LiveGraph live_;
  std::vector<std::string> palette_;
  std::map<live::INodePtr, NodeAdj> adjacency_;
  std::set<live::INodePtr> colorable_;
  std::list<live::INodePtr> stack_;
  std::map<live::INodePtr, std::string> colored_;
  std::map<live::INodePtr, std::string> precolored_color_;
};

Color::Color(live::LiveGraph live) : impl_(std::make_unique<Impl>(std::move(live))) {}
Color::~Color() = default;

Result Color::AssignColors() { return impl_->AssignColors(); }

} // namespace col
