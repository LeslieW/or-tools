// Copyright 2010-2013 Google
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <string>
#include <vector>

#include "base/integral_types.h"
#include "base/concise_iterator.h"
#include "base/int-type-indexed-vector.h"
#include "base/int-type.h"
#include "base/logging.h"
#include "base/hash.h"
#include "base/scoped_ptr.h"
#include "base/stringprintf.h"
#include "constraint_solver/constraint_solver.h"
#include "constraint_solver/constraint_solveri.h"
#include "util/string_array.h"

namespace operations_research {
// Diffn constraint, Non overlapping rectangles.
namespace {
DEFINE_INT_TYPE(Box, int);
class Diffn : public Constraint {
 public:
  Diffn(Solver* const solver,
        const std::vector<IntVar*>& x_vars,
        const std::vector<IntVar*>& y_vars,
        const std::vector<IntVar*>& x_size,
        const std::vector<IntVar*>& y_size)
      : Constraint(solver),
        x_(x_vars),
        y_(y_vars),
        dx_(x_size),
        dy_(y_size),
        size_(x_vars.size()) {
    CHECK_EQ(x_vars.size(), y_vars.size());
    CHECK_EQ(x_vars.size(), x_size.size());
    CHECK_EQ(x_vars.size(), y_size.size());
  }

  virtual ~Diffn() {}

  virtual void Post() {
    Solver* const s = solver();
    for (int i = 0; i < size_; ++i) {
      Demon* const demon =
          MakeConstraintDemon1(solver(),
                               this,
                               &Diffn::RangeBox,
                               "RangeBox",
                               i);
      x_[i]->WhenRange(demon);
      y_[i]->WhenRange(demon);
      dx_[i]->WhenRange(demon);
      dy_[i]->WhenRange(demon);
    }
    delayed_demon_ = MakeDelayedConstraintDemon0(
        solver(),
        this,
        &Diffn::PropagateAll,
        "PropagateAll");
    if (AreAllBound(dx_) && AreAllBound(dy_) &&
        IsArrayInRange(x_, 0LL, kint64max) &&
        IsArrayInRange(y_, 0LL, kint64max)) {
      // We can add redundant cumulative constraints.

      // Cumulative on x variables.e
      const int64 min_x = MinVarArray(x_);
      const int64 max_x = MaxVarArray(x_);
      const int64 max_size_x = MaxVarArray(dx_);
      const int64 min_y = MinVarArray(y_);
      const int64 max_y = MaxVarArray(y_);
      const int64 max_size_y = MaxVarArray(dy_);
      vector<int64> size_x;
      FillValues(dx_, &size_x);
      vector<int64> size_y;
      FillValues(dy_, &size_y);

      AddCumulativeConstraint(x_, size_x, size_y, max_size_y + max_y - min_y);
      AddCumulativeConstraint(y_, size_y, size_x, max_size_x + max_x - min_x);
    }
  }

  virtual void InitialPropagate() {
    // All sizes should be > 0.
    for (int i = 0; i < size_; ++i) {
      dx_[i]->SetMin(1);
      dy_[i]->SetMin(1);
    }

    // Force propagation on all boxes.
    to_propagate_.clear();
    for (int i = 0; i < size_; i++) {
      to_propagate_.insert(i);
    }
    PropagateAll();
  }

  void RangeBox(int box) {
    to_propagate_.insert(box);
    EnqueueDelayedDemon(delayed_demon_);
  }

  void PropagateAll() {
    for (ConstIter<hash_set<int>> it(to_propagate_); !it.at_end(); ++it) {
      const int box = *it;
      FillNeighbors(box);
      CheckEnergy(box);
      PushOverlappingBoxes(box);
    }
    to_propagate_.clear();
  }

  virtual string DebugString() const {
    return StringPrintf("Diffn(x = [%s], y = [%s], dx = [%s], dy = [%s]))",
                        DebugStringVector(x_, ", ").c_str(),
                        DebugStringVector(y_, ", ").c_str(),
                        DebugStringVector(dx_, ", ").c_str(),
                        DebugStringVector(dy_, ", ").c_str());
  }

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->BeginVisitConstraint(ModelVisitor::kDisjunctive, this);
    visitor->VisitIntegerVariableArrayArgument(
        ModelVisitor::kPositionXArgument, x_);
    visitor->VisitIntegerVariableArrayArgument(
        ModelVisitor::kPositionYArgument, y_);
    visitor->VisitIntegerVariableArrayArgument(
        ModelVisitor::kSizeXArgument, dx_);
    visitor->VisitIntegerVariableArrayArgument(
        ModelVisitor::kSizeYArgument, dy_);
    visitor->EndVisitConstraint(ModelVisitor::kDisjunctive, this);
  }

 private:
  bool Overlap(int i, int j) {
    if (DisjointHorizontal(i, j) || DisjointVertical(i, j)) {
      return false;
    }
    return true;
  }

  bool DisjointHorizontal(int i, int j) {
    return (x_[i]->Min() >= x_[j]->Max() + dx_[j]->Max())
        || (x_[j]->Min() >= x_[i]->Max() + dx_[i]->Max());
  }

  bool DisjointVertical(int i, int j) {
    return (y_[i]->Min() >= y_[j]->Max() + dy_[j]->Max())
        || (y_[j]->Min() >= y_[i]->Max() + dy_[i]->Max());
  }

  // Fill neighbors_ with all boxes overlapping box.
  void FillNeighbors(int box) {
    neighbors_.clear();
    for (int other = 0; other < size_; ++other) {
      if (other != box && Overlap(other, box)) {
        neighbors_.push_back(other);
      }
    }
  }

  // Check that the minimum area of a set of boxes is always contained in
  // the bounding box of these boxes.
  void CheckEnergy(int box) {
    int64 area_min_x = x_[box]->Min();
    int64 area_max_x = x_[box]->Max() + dx_[box]->Max();
    int64 area_min_y = y_[box]->Min();
    int64 area_max_y = y_[box]->Max() + dy_[box]->Max();
    int64 sum_of_areas = dx_[box]->Min() * dy_[box]->Min();
    for (int i = 0; i < neighbors_.size(); ++i) {
      const int other = neighbors_[i];
      // Update Bounding box.
      area_min_x = std::min(area_min_x, x_[other]->Min());
      area_max_x = std::max(area_max_x, x_[other]->Max() + dx_[other]->Max());
      area_min_y = std::min(area_min_y, y_[other]->Min());
      area_max_y = std::max(area_max_y, y_[other]->Max() + dy_[other]->Max());
      // Update sum of areas.
      sum_of_areas += dx_[other]->Min() * dy_[other]->Min();
      const int64 bounding_area =
          (area_max_x - area_min_x) * (area_max_y - area_min_y);
      if (sum_of_areas > bounding_area) {
        solver()->Fail();
      }
    }
  }

  // Push all boxes apart the mandatory part of a box.
  void PushOverlappingBoxes(int box) {
    // Mandatory part of box.
    const int64 start_max_box_x = x_[box]->Max();
    const int64 end_min_box_x = x_[box]->Min() + dx_[box]->Min();
    const int64 start_max_box_y = y_[box]->Max();
    const int64 end_min_box_y = y_[box]->Min() + dy_[box]->Min();
    // Mandatory part non empty?
    if (start_max_box_x < end_min_box_x && start_max_box_y < end_min_box_y) {
      // Try to push overlapping boxes.
      for (int i = 0; i < neighbors_.size(); ++i) {
        PushOneBox(box, neighbors_[i], start_max_box_x,
                   end_min_box_x, start_max_box_y,
                   end_min_box_y);
      }
    }
  }

  void PushOneDirection(int box, int other, int64 start_max_box,
                        int64 end_min_box, int64 start_max_other,
                        int64 end_min_other,
                        const std::vector<IntVar*>& positions,
                        const std::vector<IntVar*>& sizes) {
    if (end_min_other > start_max_box) {
      // Other is forced after box.
      positions[other]->SetMin(end_min_box);
      positions[box]->SetMax(start_max_other - sizes[box]->Min());
      sizes[box]->SetMax(start_max_other - positions[box]->Min());
    } else if (end_min_box > start_max_other) {
      // Box is forced after other.
      positions[box]->SetMin(end_min_other);
      positions[other]->SetMax(start_max_box - sizes[other]->Min());
      sizes[other]->SetMax(start_max_box - positions[other]->Min());
    }
  }

  void PushOneBox(int box, int other, int64 start_max_box_x,
                  int64 end_min_box_x, int64 start_max_box_y,
                  int64 end_min_box_y) {
    // Mandatory part of the other box.
    const int64 start_max_other_x = x_[other]->Max();
    const int64 end_min_other_x = x_[other]->Min() + dx_[other]->Min();
    const int64 start_max_other_y = y_[other]->Max();
    const int64 end_min_other_y = y_[other]->Min() + dy_[other]->Min();
    // Mandatory part of the other box is non empty.
    if (start_max_other_x < end_min_other_x &&
        start_max_other_y < end_min_other_y) {
      const bool overlap_horizontal =
          start_max_other_x < end_min_box_x &&
          start_max_box_x < end_min_other_x;
      const bool overlap_vertical =
          start_max_other_y < end_min_box_y &&
          start_max_box_y < end_min_other_y;
      if (overlap_horizontal && overlap_vertical) {
        // Mandatory parts overlap. We fail early.
        solver()->Fail();
      } else if (overlap_horizontal) {
        PushOneDirection(box, other, start_max_box_y, end_min_box_y,
                         start_max_other_y, end_min_other_y,
                         y_, dy_);
      } else if (overlap_vertical) {
        PushOneDirection(box, other, start_max_box_x, end_min_box_x,
                         start_max_other_x, end_min_other_x,
                         x_, dx_);
      }
    }
  }

  void AddCumulativeConstraint(const std::vector<IntVar*>& positions,
                               const std::vector<int64>& sizes,
                               const std::vector<int64>& demands,
                               int64 capacity) {
    vector<IntervalVar*> intervals;
    solver()->MakeFixedDurationIntervalVarArray(
        positions, sizes, "interval", &intervals);
    solver()->AddConstraint(solver()->MakeCumulative(
        intervals, demands, capacity, "cumul"));
  }

  std::vector<IntVar*> x_;
  std::vector<IntVar*> y_;
  std::vector<IntVar*> dx_;
  std::vector<IntVar*> dy_;
  const int64 size_;
  Demon* delayed_demon_;
  hash_set<int> to_propagate_;
  std::vector<int> neighbors_;
};
}  // namespace

Constraint* Solver::MakeNonOverlappingRectanglesConstraint(
    const std::vector<IntVar*>& x_vars,
    const std::vector<IntVar*>& y_vars,
    const std::vector<IntVar*>& x_size,
    const std::vector<IntVar*>& y_size) {
  return RevAlloc(new Diffn(this, x_vars, y_vars, x_size, y_size));
}
}  // namespace operations_research
