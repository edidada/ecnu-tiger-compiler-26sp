#ifndef TIGER_COMPILER_COLOR_H
#define TIGER_COMPILER_COLOR_H

#include "tiger/codegen/assem.h"
#include "tiger/frame/temp.h"
#include "tiger/liveness/liveness.h"
#include "tiger/util/graph.h"

namespace col {

struct Result {
  Result() : coloring(nullptr), spills(nullptr) {}
  Result(temp::Map *coloring, live::INodeListPtr spills)
      : coloring(coloring), spills(spills) {}
  temp::Map *coloring;
  live::INodeListPtr spills;
};

class Color {
public:
  explicit Color(live::LiveGraph live);
  ~Color();

  Color(const Color &) = delete;
  Color(Color &&) = delete;
  Color &operator=(const Color &) = delete;
  Color &operator=(Color &&) = delete;

  // Briggs-style simplify/select over the interference graph. Precolored
  // machine registers keep their colors; anything unmatched is returned as
  // a spill candidate instead of failing allocation.
  Result AssignColors();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace col

#endif // TIGER_COMPILER_COLOR_H
