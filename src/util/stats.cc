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

#include "util/stats.h"

#include <cmath>
#include "base/stringprintf.h"
#include "base/stl_util.h"

namespace operations_research {

Stat::Stat(const string& name, StatsGroup *group) : name_(name) {
  group->Register(this);
}

string Stat::StatString() const {
  return string(name_ + ": " + ValueAsString());
}

StatsGroup::~StatsGroup() {
  STLDeleteValues(&time_distributions_);
}

void StatsGroup::Register(Stat* stat) {
  stats_.push_back(stat);
}

void StatsGroup::Reset() {
  for (int i = 0; i < stats_.size(); ++i) {
    stats_[i]->Reset();
  }
}

string StatsGroup::StatString() const {
  string result(name_ + " {\n");

  // Computes the longest name of all the stats we want to display.
  int longest_name_size = 0;
  for (int i = 0; i < stats_.size(); ++i) {
    if (!stats_[i]->WorthPrinting()) continue;
    const int size = static_cast<int>(stats_[i]->Name().size());
    longest_name_size = std::max(longest_name_size, size);
  }

  // Pretty-print all the stats.
  for (int i = 0; i < stats_.size(); ++i) {
    if (!stats_[i]->WorthPrinting()) continue;
    result += "  ";
    result += stats_[i]->Name();
    result.append(longest_name_size - stats_[i]->Name().size() , ' ');
    result += " : " + stats_[i]->ValueAsString();
  }
  result += "}\n";
  return result;
}

TimeDistribution *StatsGroup::LookupOrCreateTimeDistribution(string name) {
  TimeDistribution* &ref = time_distributions_[name];
  if (ref == NULL) {
    ref = new TimeDistribution(name);
    Register(ref);
  }
  return ref;
}

DistributionStat::DistributionStat(const string& name)
    : Stat(name),
      sum_(0.0), average_(0.0), sum_squares_from_average_(0.0),
      min_(0.0), max_(0.0), num_(0) {}

DistributionStat::DistributionStat(const string& name, StatsGroup* group)
    : Stat(name, group),
      sum_(0.0), average_(0.0), sum_squares_from_average_(0.0),
      min_(0.0), max_(0.0), num_(0) {}

void DistributionStat::Reset() {
  sum_ = 0.0;
  average_ = 0.0;
  sum_squares_from_average_ = 0.0;
  min_ = 0.0;
  max_ = 0.0;
  num_ = 0;
}

void DistributionStat::AddToDistribution(double value) {
  if (num_ == 0) {
    min_ = value;
    max_ = value;
    sum_ = value;
    average_ = value;
    num_ = 1;
    return;
  }
  min_ = std::min(min_, value);
  max_ = std::max(max_, value);
  sum_ += value;
  ++num_;
  const double delta = value - average_;
  average_ = sum_ / num_;
  sum_squares_from_average_ += delta * (value - average_);
}

double DistributionStat::Average() const {
  return average_;
}

double DistributionStat::StdDeviation() const {
  if (num_ == 0) return 0.0;
  return sqrt(sum_squares_from_average_ / num_);
}

double TimeDistribution::CyclesToSeconds(double cycles) {
  const double seconds_per_cycles = CycleTimerBase::CyclesToSeconds(1);
  return cycles * seconds_per_cycles;
}

string TimeDistribution::PrintCyclesAsTime(double cycles) {
  DCHECK_GE(cycles, 0.0);
  // This epsilon is just to avoid displaying 1000.00ms instead of 1.00s.
  double eps1 = 1 + 1e-3;
  double sec = CyclesToSeconds(cycles);
  if (sec * eps1 >= 3600.0) return StringPrintf("%.2fh", sec / 3600.0);
  if (sec * eps1 >= 60.0) return StringPrintf("%.2fm", sec / 60.0);
  if (sec * eps1 >= 1.0) return StringPrintf("%.2fs", sec);
  if (sec * eps1 >= 1e-3) return StringPrintf("%.2fms", sec * 1e3);
  if (sec * eps1 >= 1e-6) return StringPrintf("%.2fus", sec * 1e6);
  return StringPrintf("%.2fns", sec * 1e9);
}

void TimeDistribution::AddTimeInSec(double seconds) {
  DCHECK_GE(seconds, 0.0);
  const double cycles_per_seconds = 1.0 / CycleTimerBase::CyclesToSeconds(1);
  AddToDistribution(seconds * cycles_per_seconds);
}

void TimeDistribution::AddTimeInCycles(double cycles) {
  DCHECK_GE(cycles, 0.0);
  AddToDistribution(cycles);
}

string TimeDistribution::ValueAsString() const {
  return StringPrintf("%8llu [%8s, %8s] %8s %8s %8s\n",
      num_,
      PrintCyclesAsTime(min_).c_str(),
      PrintCyclesAsTime(max_).c_str(),
      PrintCyclesAsTime(Average()).c_str(),
      PrintCyclesAsTime(StdDeviation()).c_str(),
      PrintCyclesAsTime(sum_).c_str());
}

void RatioDistribution::Add(double value) {
  DCHECK_GE(value, 0.0);
  AddToDistribution(value);
}

string RatioDistribution::ValueAsString() const {
  return StringPrintf("%8llu [%7.2lf%%, %7.2lf%%] %7.2lf%% %7.2lf%%\n",
      num_,
      100.0 * min_,
      100.0 * max_,
      100.0 * Average(),
      100.0 * StdDeviation());
}

void DoubleDistribution::Add(double value) {
  AddToDistribution(value);
}

string DoubleDistribution::ValueAsString() const {
  return StringPrintf("%8llu [%8.1e, %8.1e] %8.1e %8.1e\n",
      num_,
      min_,
      max_ ,
      Average(),
      StdDeviation());
}

void IntegerDistribution::Add(int64 value) {
  AddToDistribution(static_cast<double>(value));
}

string IntegerDistribution::ValueAsString() const {
  return StringPrintf("%8llu [%8.lf, %8.lf] %8.2lf %8.2lf %8.lf\n",
      num_,
      min_,
      max_ ,
      Average(),
      StdDeviation(),
      sum_);
}

}  // namespace operations_research
