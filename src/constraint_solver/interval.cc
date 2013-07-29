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

#include <string>
#include <vector>

#include "base/integral_types.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/stringprintf.h"
#include "constraint_solver/constraint_solver.h"
#include "constraint_solver/constraint_solveri.h"

#if defined(_MSC_VER)
#pragma warning(disable : 4351 4355 4804 4805)
#endif

namespace operations_research {

// ----- Interval Var -----

// It's good to have the two extreme values being symmetrical around zero: it
// makes mirroring easier.
const int64 IntervalVar::kMaxValidValue = kint64max >> 2;
const int64 IntervalVar::kMinValidValue = -kMaxValidValue;

namespace {
enum IntervalField { START, DURATION, END};

IntervalVar* NullInterval() { return NULL; }
// ----- MirrorIntervalVar -----

class MirrorIntervalVar : public IntervalVar {
 public:
  MirrorIntervalVar(Solver* const s, IntervalVar* const t)
      : IntervalVar(s, "Mirror<" + t->name() + ">"), t_(t) {}
  virtual ~MirrorIntervalVar() {}

  // These methods query, set and watch the start position of the
  // interval var.
  virtual int64 StartMin() const { return -t_->EndMax(); }
  virtual int64 StartMax() const { return -t_->EndMin(); }
  virtual void SetStartMin(int64 m) { t_->SetEndMax(-m); }
  virtual void SetStartMax(int64 m) { t_->SetEndMin(-m); }
  virtual void SetStartRange(int64 mi, int64 ma) { t_->SetEndRange(-ma, -mi); }
  virtual int64 OldStartMin() const { return -t_->OldEndMax(); }
  virtual int64 OldStartMax() const { return -t_->OldEndMin(); }
  virtual void WhenStartRange(Demon* const d) { t_->WhenEndRange(d); }
  virtual void WhenStartBound(Demon* const d) { t_->WhenEndBound(d); }

  // These methods query, set and watch the duration of the interval var.
  virtual int64 DurationMin() const { return t_->DurationMin(); }
  virtual int64 DurationMax() const { return t_->DurationMax(); }
  virtual void SetDurationMin(int64 m) { t_->SetDurationMin(m); }
  virtual void SetDurationMax(int64 m) { t_->SetDurationMax(m); }
  virtual void SetDurationRange(int64 mi, int64 ma) {
    t_->SetDurationRange(mi, ma);
  }
  virtual int64 OldDurationMin() const { return t_->OldDurationMin(); }
  virtual int64 OldDurationMax() const { return t_->OldDurationMax(); }
  virtual void WhenDurationRange(Demon* const d) { t_->WhenDurationRange(d); }
  virtual void WhenDurationBound(Demon* const d) { t_->WhenDurationBound(d); }

  // These methods query, set and watch the end position of the interval var.
  virtual int64 EndMin() const { return -t_->StartMax(); }
  virtual int64 EndMax() const { return -t_->StartMin(); }
  virtual void SetEndMin(int64 m) { t_->SetStartMax(-m); }
  virtual void SetEndMax(int64 m) { t_->SetStartMin(-m); }
  virtual void SetEndRange(int64 mi, int64 ma) { t_->SetStartRange(-ma, -mi); }
  virtual int64 OldEndMin() const { return -t_->OldStartMax(); }
  virtual int64 OldEndMax() const { return -t_->OldStartMin(); }
  virtual void WhenEndRange(Demon* const d) { t_->WhenStartRange(d); }
  virtual void WhenEndBound(Demon* const d) { t_->WhenStartBound(d); }

  // These methods query, set and watches the performed status of the
  // interval var.
  virtual bool MustBePerformed() const { return t_->MustBePerformed(); }
  virtual bool MayBePerformed() const { return t_->MayBePerformed(); }
  virtual void SetPerformed(bool val) { t_->SetPerformed(val); }
  virtual bool WasPerformedBound() const { return t_->WasPerformedBound(); }
  virtual void WhenPerformedBound(Demon* const d) { t_->WhenPerformedBound(d); }

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(this, ModelVisitor::kMirrorOperation, 0, t_);
  }

  virtual string DebugString() const {
    return StringPrintf("MirrorInterval(%s)", t_->DebugString().c_str());
  }

 private:
  IntervalVar* const t_;
  DISALLOW_COPY_AND_ASSIGN(MirrorIntervalVar);
};

// An IntervalVar that passes all function calls to an underlying interval
// variable as long as it is not prohibited, and that interprets prohibited
// intervals as intervals of duration 0 that must be executed between
// [kMinValidValue and kMaxValidValue].
//
// Such interval variables have a very similar behavior to others.
// Invariants such as StartMin() + DurationMin() <= EndMin() that are maintained
// for traditional interval variables are maintained for instances of
// AlwaysPerformedIntervalVarWrapper. However, there is no monotonicity of the
// values returned by the start/end getters. For example, during a given
// propagation, three successive calls to StartMin could return,
// in this order, 1, 2, and kMinValidValue.
//

// This class exists so that we can easily implement the
// IntervalVarRelaxedMax and IntervalVarRelaxedMin classes below.
class AlwaysPerformedIntervalVarWrapper : public IntervalVar {
 public:
  explicit AlwaysPerformedIntervalVarWrapper(IntervalVar* const t)
      : IntervalVar(t->solver(),
                    StringPrintf("AlwaysPerformed<%s>", t->name().c_str())),
        t_(t) {}
  virtual ~AlwaysPerformedIntervalVarWrapper() {}
  virtual int64 StartMin() const {
    return MayUnderlyingBePerformed() ? t_->StartMin() : kMinValidValue;
  }
  virtual int64 StartMax() const {
    return MayUnderlyingBePerformed() ? t_->StartMax() : kMaxValidValue;
  }
  virtual void SetStartMin(int64 m) { t_->SetStartMin(m); }
  virtual void SetStartMax(int64 m) { t_->SetStartMax(m); }
  virtual void SetStartRange(int64 mi, int64 ma) { t_->SetStartRange(mi, ma); }
  virtual int64 OldStartMin() const {
    return MayUnderlyingBePerformed() ? t_->OldStartMin() : kMinValidValue;
  }
  virtual int64 OldStartMax() const {
    return MayUnderlyingBePerformed() ? t_->OldStartMax() : kMaxValidValue;
  }
  virtual void WhenStartRange(Demon* const d) { t_->WhenStartRange(d); }
  virtual void WhenStartBound(Demon* const d) { t_->WhenStartBound(d); }
  virtual int64 DurationMin() const {
    return MayUnderlyingBePerformed() ? t_->DurationMin() : 0LL;
  }
  virtual int64 DurationMax() const {
    return MayUnderlyingBePerformed() ? t_->DurationMax() : 0LL;
  }
  virtual void SetDurationMin(int64 m) { t_->SetDurationMin(m); }
  virtual void SetDurationMax(int64 m) { t_->SetDurationMax(m); }
  virtual void SetDurationRange(int64 mi, int64 ma) {
    t_->SetDurationRange(mi, ma);
  }
  virtual int64 OldDurationMin() const {
    return MayUnderlyingBePerformed() ? t_->OldDurationMin() : 0LL;
  }
  virtual int64 OldDurationMax() const {
    return MayUnderlyingBePerformed() ? t_->OldDurationMax() : 0LL;
  }
  virtual void WhenDurationRange(Demon* const d) { t_->WhenDurationRange(d); }
  virtual void WhenDurationBound(Demon* const d) { t_->WhenDurationBound(d); }
  virtual int64 EndMin() const {
    return MayUnderlyingBePerformed() ? t_->EndMin() : kMinValidValue;
  }
  virtual int64 EndMax() const {
    return MayUnderlyingBePerformed() ? t_->EndMax() : kMaxValidValue;
  }
  virtual void SetEndMin(int64 m) { t_->SetEndMin(m); }
  virtual void SetEndMax(int64 m) { t_->SetEndMax(m); }
  virtual void SetEndRange(int64 mi, int64 ma) { t_->SetEndRange(mi, ma); }
  virtual int64 OldEndMin() const {
    return MayUnderlyingBePerformed() ? t_->OldEndMin() : kMinValidValue;
  }
  virtual int64 OldEndMax() const {
    return MayUnderlyingBePerformed() ? t_->OldEndMax() : kMaxValidValue;
  }
  virtual void WhenEndRange(Demon* const d) { t_->WhenEndRange(d); }
  virtual void WhenEndBound(Demon* const d) { t_->WhenEndBound(d); }
  virtual bool MustBePerformed() const { return true; }
  virtual bool MayBePerformed() const { return true; }
  virtual void SetPerformed(bool val) {
    // An AlwaysPerformedIntervalVarWrapper interval variable is always
    // performed. So setting it to be performed does not change anything,
    // and setting it not to be performed is inconsistent and should cause
    // a failure.
    if (!val) {
      solver()->Fail();
    }
  }
  virtual bool WasPerformedBound() const { return true; }
  virtual void WhenPerformedBound(Demon* const d) { t_->WhenPerformedBound(d); }

 protected:
  const IntervalVar* const underlying() const { return t_; }
  bool MayUnderlyingBePerformed() const {
    return underlying()->MayBePerformed();
  }

 private:
  IntervalVar* const t_;
  DISALLOW_COPY_AND_ASSIGN(AlwaysPerformedIntervalVarWrapper);
};

// An interval variable that wraps around an underlying one, relaxing the max
// start and end. Relaxing means making unbounded when optional.
//
// More precisely, such an interval variable behaves as follows:
// * When the underlying must be performed, this interval variable behaves
//     exactly as the underlying;
// * When the underlying may or may not be performed, this interval variable
//     behaves like the underlying, except that it is unbounded on the max side;
// * When the underlying cannot be performed, this interval variable is of
//      duration 0 and must be performed in an interval unbounded on both sides.
//
// This class is very useful to implement propagators that may only modify
// the start min or end min.
class IntervalVarRelaxedMax : public AlwaysPerformedIntervalVarWrapper {
 public:
  explicit IntervalVarRelaxedMax(IntervalVar* const t)
      : AlwaysPerformedIntervalVarWrapper(t) {}
  virtual ~IntervalVarRelaxedMax() {}
  virtual int64 StartMax() const {
    // It matters to use DurationMin() and not underlying()->DurationMin() here.
    return underlying()->MustBePerformed() ? underlying()->StartMax()
                                           : (kMaxValidValue - DurationMin());
  }
  virtual void SetStartMax(int64 m) {
    LOG(FATAL)
        << "Calling SetStartMax on a IntervalVarRelaxedMax is not supported, "
        << "as it seems there is no legitimate use case.";
  }
  virtual int64 EndMax() const {
    return underlying()->MustBePerformed() ? underlying()->EndMax()
                                           : kMaxValidValue;
  }
  virtual void SetEndMax(int64 m) {
    LOG(FATAL)
        << "Calling SetEndMax on a IntervalVarRelaxedMax is not supported, "
        << "as it seems there is no legitimate use case.";
  }

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(this, ModelVisitor::kRelaxedMaxOperation, 0,
                                   underlying());
  }

  virtual string DebugString() const {
    return StringPrintf("IntervalVarRelaxedMax(%s)",
                        underlying()->DebugString().c_str());
  }
};

// An interval variable that wraps around an underlying one, relaxing the min
// start and end. Relaxing means making unbounded when optional.
//
// More precisely, such an interval variable behaves as follows:
// * When the underlying must be performed, this interval variable behaves
//     exactly as the underlying;
// * When the underlying may or may not be performed, this interval variable
//     behaves like the underlying, except that it is unbounded on the min side;
// * When the underlying cannot be performed, this interval variable is of
//      duration 0 and must be performed in an interval unbounded on both sides.
//

// This class is very useful to implement propagators that may only modify
// the start max or end max.
class IntervalVarRelaxedMin : public AlwaysPerformedIntervalVarWrapper {
 public:
  explicit IntervalVarRelaxedMin(IntervalVar* const t)
      : AlwaysPerformedIntervalVarWrapper(t) {}
  virtual ~IntervalVarRelaxedMin() {}
  virtual int64 StartMin() const {
    return underlying()->MustBePerformed() ? underlying()->StartMin()
                                           : kMinValidValue;
  }
  virtual void SetStartMin(int64 m) {
    LOG(FATAL)
        << "Calling SetStartMin on a IntervalVarRelaxedMin is not supported, "
        << "as it seems there is no legitimate use case.";
  }
  virtual int64 EndMin() const {
    // It matters to use DurationMin() and not underlying()->DurationMin() here.
    return underlying()->MustBePerformed() ? underlying()->EndMin()
                                           : (kMinValidValue + DurationMin());
  }
  virtual void SetEndMin(int64 m) {
    LOG(FATAL)
        << "Calling SetEndMin on a IntervalVarRelaxedMin is not supported, "
        << "as it seems there is no legitimate use case.";
  }

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(this, ModelVisitor::kRelaxedMinOperation, 0,
                                   underlying());
  }

  virtual string DebugString() const {
    return StringPrintf("IntervalVarRelaxedMin(%s)",
                        underlying()->DebugString().c_str());
  }
};

// ----- BaseIntervalVar -----

enum SetterStatus {
  NO_OP,
  INCONSISTENT,
  PUSH
};

class BaseIntervalVar : public IntervalVar {
 public:
  enum PerformedStatus {
    UNPERFORMED,
    PERFORMED,
    UNDECIDED
  };

  class Handler : public Demon {
   public:
    explicit Handler(BaseIntervalVar* const var) : var_(var) {}
    virtual ~Handler() {}
    virtual void Run(Solver* const s) { var_->Process(); }
    virtual Solver::DemonPriority priority() const {
      return Solver::VAR_PRIORITY;
    }
    virtual string DebugString() const {
      return StringPrintf("Handler(%s)", var_->DebugString().c_str());
    }

   private:
    BaseIntervalVar* const var_;
  };

  class Cleaner : public Action {
   public:
    explicit Cleaner(BaseIntervalVar* const var) : var_(var) {}
    virtual ~Cleaner() {}
    virtual void Run(Solver* const s) { var_->ClearInProcess(); }

   private:
    BaseIntervalVar* const var_;
  };

  BaseIntervalVar(Solver* const s, const string& name)
      : IntervalVar(s, name),
        in_process_(false),
        handler_(this),
        cleaner_(this) {}

  virtual ~BaseIntervalVar() {}

  virtual void Process() = 0;

  virtual void Push() = 0;

  void ClearInProcess() { in_process_ = false; }

  virtual string BaseName() const { return "IntervalVar"; }

 protected:
  // TODO(user): Remove this enum and change the protocol.
  void ProcessModification(SetterStatus status) {
    switch (status) {
      case NO_OP:
        break;
      case INCONSISTENT:
        SetPerformed(false);
        break;
      case PUSH:
        Push();
        break;
      default:
        LOG(WARNING) << "Should not be here";
    }
  }

  bool in_process_;
  Handler handler_;
  Cleaner cleaner_;
};

// ----- Propagation aware storage for booleans and intervals -----

class BooleanStorage : public PropagationBaseObject {
 public:
  static const int kFalse;
  static const int kTrue;
  static const int kUndecidedBooleanValue;

  // Ctor with true or undecided boolean value. If 'optional' is true, then
  // the performed status is UNDECIDED. If false, the performed status is
  // PERFORMED.
  BooleanStorage(Solver* const solver, bool optional)
      : PropagationBaseObject(solver),
        status_(optional ? kUndecidedBooleanValue : kTrue),
        previous_status_(status_),
        postponed_status_(status_) {}

  // Ctor with a false default value.
  explicit BooleanStorage(Solver* const solver)
      : PropagationBaseObject(solver),
        status_(kFalse),
        previous_status_(kFalse),
        postponed_status_(kFalse) {}

  virtual ~BooleanStorage() {}

  bool MayBeTrue() const { return status_ != kFalse; }

  bool MustBeTrue() const { return (status_ == kTrue); }

  bool Bound() const { return status_ != kUndecidedBooleanValue; }

  bool WasBound() const { return previous_status_ != kUndecidedBooleanValue; }

  void WhenBound(Demon* const demon) {
    if (!Bound()) {
      if (demon->priority() == Solver::DELAYED_PRIORITY) {
        delayed_demons_.PushIfNotTop(solver(), solver()->RegisterDemon(demon));
      } else {
        demons_.PushIfNotTop(solver(), solver()->RegisterDemon(demon));
      }
    }
  }

  // Returns true if we need to push the interval var onto the queue.
  bool SetValue(bool value) {
    if (status_ == kUndecidedBooleanValue) {
      // Sync previous value.
      previous_status_ = kUndecidedBooleanValue;
      // And set the current one.
      solver()->SaveAndSetValue(&status_, value ? kTrue : kFalse);
      return true;
    } else if (status_ != value) {
      solver()->Fail();
    }
    return false;
  }

  void SetValueInProcess(bool value) {
    if (status_ == kUndecidedBooleanValue) {
      if (postponed_status_ == kUndecidedBooleanValue) {
        postponed_status_ = value ? kTrue : kFalse;
      } else if (postponed_status_ != value) {
        solver()->Fail();
      }
    } else if (status_ != value) {
      solver()->Fail();
    }
  }

  void UpdatePostponedBounds() { postponed_status_ = status_; }

  void ProcessDemons() {
    if (previous_status_ != status_) {
      ExecuteAll(demons_);
      EnqueueAll(delayed_demons_);
    }
  }

  void UpdatePreviousBoundsAndApplyPostponedBounds(BaseIntervalVar* var) {
    previous_status_ = status_;
    if (postponed_status_ != status_) {
      CHECK_NE(kUndecidedBooleanValue, postponed_status_);
      var->SetPerformed(postponed_status_ != 0);
    }
  }

  string DebugString() const {
    switch (status_) {
      case 0:
        return "false";
      case 1:
        return "true";
      default:
        return "undecided";
    }
  }

 private:
  // The current status.
  int status_;
  // The status  at the end of the last time ProcessDemons() was run.
  int previous_status_;
  // If the variable this boolean belongs to is being processed, all
  // modifications are postponed. This new status is stored in this
  // field.
  int postponed_status_;
  SimpleRevFIFO<Demon*> demons_;
  SimpleRevFIFO<Demon*> delayed_demons_;
};  // class BooleanStorage

const int BooleanStorage::kFalse = 0;
const int BooleanStorage::kTrue = 1;
const int BooleanStorage::kUndecidedBooleanValue = 2;

class IntervalStorage : public PropagationBaseObject {
 public:
  IntervalStorage(Solver* const s, int64 mi, int64 ma)
      : PropagationBaseObject(s),
        min_(mi),
        max_(ma),
        postponed_min_(mi),
        postponed_max_(ma),
        previous_min_(mi),
        previous_max_(ma) {}

  virtual ~IntervalStorage() {}

  bool Bound() const { return min_.Value() == max_.Value(); }

  int64 Min() const { return min_.Value(); }

  int64 Max() const { return max_.Value(); }

  SetterStatus SetMin(int64 m) {
    if (m > max_.Value()) {
      return INCONSISTENT;
    }
    if (m > min_.Value()) {
      SyncPreviousBounds();
      min_.SetValue(solver(), m);
      return PUSH;
    }
    return NO_OP;
  }

  // Returns false if the interval is inconsistent after this modification.
  bool SetMinInProcess(int64 m) {
    if (m > postponed_max_) {
      return false;
    }
    if (m > postponed_min_) {
      postponed_min_ = m;
    }
    return true;
  }

  int64 PreviousMin() const { return previous_min_; }

  SetterStatus SetMax(int64 m) {
    if (m < min_.Value()) {
      return INCONSISTENT;
    }
    if (m < max_.Value()) {
      SyncPreviousBounds();
      max_.SetValue(solver(), m);
      return PUSH;
    }
    return NO_OP;
  }

  // Returns false if the interval is inconsistent after this modification.
  bool SetMaxInProcess(int64 m) {
    if (m < postponed_min_) {
      return false;
    }
    if (m < postponed_max_) {
      postponed_max_ = m;
    }
    return true;
  }

  int64 PreviousMax() const { return previous_min_; }

  SetterStatus SetRange(int64 mi, int64 ma) {
    if (mi > max_.Value() || ma < min_.Value()) {
      return INCONSISTENT;
    }
    if (mi > min_.Value() || ma < max_.Value()) {
      SyncPreviousBounds();
      if (mi > min_.Value()) {
        min_.SetValue(solver(), mi);
      }
      if (ma < max_.Value()) {
        max_.SetValue(solver(), ma);
      }
      return PUSH;
    }
    return NO_OP;
  }

  // Returns false if the interval is inconsistent after this modification.
  bool SetRangeInProcess(int64 mi, int64 ma) {
    if (mi > postponed_max_ || ma < postponed_min_) {
      return false;
    }
    if (mi > postponed_min_) {
      postponed_min_ = mi;
    }
    if (ma < postponed_max_) {
      postponed_max_ = ma;
    }
    return true;
  }

  virtual void WhenRange(Demon* const demon) {
    if (!Bound()) {
      if (demon->priority() == Solver::DELAYED_PRIORITY) {
        delayed_range_demons_.PushIfNotTop(solver(),
                                           solver()->RegisterDemon(demon));
      } else {
        range_demons_.PushIfNotTop(solver(), solver()->RegisterDemon(demon));
      }
    }
  }

  virtual void WhenBound(Demon* const demon) {
    if (!Bound()) {
      if (demon->priority() == Solver::DELAYED_PRIORITY) {
        delayed_bound_demons_.PushIfNotTop(solver(),
                                           solver()->RegisterDemon(demon));
      } else {
        bound_demons_.PushIfNotTop(solver(), solver()->RegisterDemon(demon));
      }
    }
  }

  void UpdatePostponedBounds() {
    postponed_min_ = min_.Value();
    postponed_max_ = max_.Value();
  }

  void ProcessDemons() {
    if (Bound()) {
      ExecuteAll(bound_demons_);
      EnqueueAll(delayed_bound_demons_);
    }
    if (min_.Value() != previous_min_ || max_.Value() != previous_max_) {
      ExecuteAll(range_demons_);
      EnqueueAll(delayed_range_demons_);
    }
  }

  void UpdatePreviousBoundsAndApplyPostponedBounds(BaseIntervalVar* const var,
                                                   IntervalField which) {
    previous_min_ = min_.Value();
    previous_max_ = max_.Value();
    if (min_.Value() < postponed_min_ || max_.Value() > postponed_max_) {
      switch (which) {
        case START:
          var->SetStartRange(std::max(postponed_min_, min_.Value()),
                             std::min(postponed_max_, max_.Value()));
          break;
        case DURATION:
          var->SetDurationRange(std::max(postponed_min_, min_.Value()),
                                std::min(postponed_max_, max_.Value()));
          break;
        case END:
          var->SetEndRange(std::max(postponed_min_, min_.Value()),
                           std::min(postponed_max_, max_.Value()));
          break;
      }
    }
  }

  string DebugString() const {
    string out = StringPrintf("%" GG_LL_FORMAT "d", min_.Value());
    if (!Bound()) {
      StringAppendF(&out, " .. %" GG_LL_FORMAT "d", max_.Value());
    }
    return out;
  }

 private:
  // The previous bounds are maintained lazily and non reversibly.
  // When going down in the search tree, the modifications are
  // monotonic, thus SyncPreviousBounds is a no-op because they are
  // correctly updated at the end of the ProcessDemons() call. After
  // a fail, if they are inconsistent, then they will be outside the
  // current interval, thus this check.
  void SyncPreviousBounds() {
    if (previous_min_ > min_.Value()) {
      previous_min_ = min_.Value();
    }
    if (previous_max_ < max_.Value()) {
      previous_max_ = max_.Value();
    }
  }

  // The current reversible bounds of the interval.
  NumericalRev<int64> min_;
  NumericalRev<int64> max_;
  // When in process, the modifications are postponed and stored in
  // these 2 fields.
  int64 postponed_min_;
  int64 postponed_max_;
  // The previous bounds stores the bounds since the last time
  // ProcessDemons() was run. These are maintained lazily.
  int64 previous_min_;
  int64 previous_max_;
  // Demons attached to the 'bound' event (min == max).
  SimpleRevFIFO<Demon*> bound_demons_;
  SimpleRevFIFO<Demon*> delayed_bound_demons_;
  // Demons attached to a modification of bounds.
  SimpleRevFIFO<Demon*> range_demons_;
  SimpleRevFIFO<Demon*> delayed_range_demons_;
};  // class IntervalStorage

// TODO(user): Move BooleanStorage and IntervalStorage into
// constraint_solveri.h

// ----- FixedDurationIntervalVar -----

class FixedDurationIntervalVar : public BaseIntervalVar {
 public:
  FixedDurationIntervalVar(Solver* const s, int64 start_min, int64 start_max,
                           int64 duration, bool optional, const string& name);
  // Unperformed interval.
  FixedDurationIntervalVar(Solver* const s, const string& name);
  virtual ~FixedDurationIntervalVar() {}

  virtual int64 StartMin() const;
  virtual int64 StartMax() const;
  virtual void SetStartMin(int64 m);
  virtual void SetStartMax(int64 m);
  virtual void SetStartRange(int64 mi, int64 ma);
  virtual int64 OldStartMin() const { return start_.PreviousMin(); }
  virtual int64 OldStartMax() const { return start_.PreviousMax(); }
  virtual void WhenStartRange(Demon* const d) {
    if (performed_.MayBeTrue()) {
      start_.WhenRange(d);
    }
  }
  virtual void WhenStartBound(Demon* const d) {
    if (performed_.MayBeTrue()) {
      start_.WhenBound(d);
    }
  }

  virtual int64 DurationMin() const;
  virtual int64 DurationMax() const;
  virtual void SetDurationMin(int64 m);
  virtual void SetDurationMax(int64 m);
  virtual void SetDurationRange(int64 mi, int64 ma);
  virtual int64 OldDurationMin() const { return duration_; }
  virtual int64 OldDurationMax() const { return duration_; }
  virtual void WhenDurationRange(Demon* const d) {}
  virtual void WhenDurationBound(Demon* const d) {}

  virtual int64 EndMin() const;
  virtual int64 EndMax() const;
  virtual void SetEndMin(int64 m);
  virtual void SetEndMax(int64 m);
  virtual void SetEndRange(int64 mi, int64 ma);
  virtual int64 OldEndMin() const { return CapAdd(OldStartMin(), duration_); }
  virtual int64 OldEndMax() const { return CapAdd(OldStartMax(), duration_); }
  virtual void WhenEndRange(Demon* const d) { WhenStartRange(d); }
  virtual void WhenEndBound(Demon* const d) { WhenStartBound(d); }

  virtual bool MustBePerformed() const;
  virtual bool MayBePerformed() const;
  virtual void SetPerformed(bool val);
  virtual bool WasPerformedBound() const { return performed_.WasBound(); }
  virtual void WhenPerformedBound(Demon* const d) { performed_.WhenBound(d); }
  virtual void Process();
  virtual string DebugString() const;

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(this, "", 0, NullInterval());
  }

 private:
  virtual void Push();

  IntervalStorage start_;
  int64 duration_;
  BooleanStorage performed_;
};

FixedDurationIntervalVar::FixedDurationIntervalVar(
    Solver* const s, int64 start_min, int64 start_max, int64 duration,
    bool optional, const string& name)
    : BaseIntervalVar(s, name),
      start_(s, start_min, start_max),
      duration_(duration),
      performed_(s, optional) {}

FixedDurationIntervalVar::FixedDurationIntervalVar(Solver* const s,
                                                   const string& name)
    : BaseIntervalVar(s, name), start_(s, 0, 0), duration_(0), performed_(s) {}

void FixedDurationIntervalVar::Process() {
  CHECK(!in_process_);
  in_process_ = true;
  start_.UpdatePostponedBounds();
  performed_.UpdatePostponedBounds();
  set_queue_action_on_fail(&cleaner_);
  if (performed_.MayBeTrue()) {
    start_.ProcessDemons();
  }
  performed_.ProcessDemons();
  clear_queue_action_on_fail();
  ClearInProcess();
  start_.UpdatePreviousBoundsAndApplyPostponedBounds(this, START);
  performed_.UpdatePreviousBoundsAndApplyPostponedBounds(this);
}

int64 FixedDurationIntervalVar::StartMin() const {
  CHECK(performed_.MayBeTrue());
  return start_.Min();
}

int64 FixedDurationIntervalVar::StartMax() const {
  CHECK(performed_.MayBeTrue());
  return start_.Max();
}

void FixedDurationIntervalVar::SetStartMin(int64 m) {
  if (performed_.MayBeTrue()) {
    if (in_process_) {
      if (!start_.SetMinInProcess(m)) {
        SetPerformed(false);
      }
    } else {
      ProcessModification(start_.SetMin(m));
    }
  }
}

void FixedDurationIntervalVar::SetStartMax(int64 m) {
  if (performed_.MayBeTrue()) {
    if (in_process_) {
      if (!start_.SetMaxInProcess(m)) {
        SetPerformed(false);
      }
    } else {
      ProcessModification(start_.SetMax(m));
    }
  }
}

void FixedDurationIntervalVar::SetStartRange(int64 mi, int64 ma) {
  if (performed_.MayBeTrue()) {
    if (in_process_) {
      if (!start_.SetRangeInProcess(mi, ma)) {
        SetPerformed(false);
      }
    } else {
      ProcessModification(start_.SetRange(mi, ma));
    }
  }
}

int64 FixedDurationIntervalVar::DurationMin() const {
  CHECK(performed_.MayBeTrue());
  return duration_;
}

int64 FixedDurationIntervalVar::DurationMax() const {
  CHECK(performed_.MayBeTrue());
  return duration_;
}

void FixedDurationIntervalVar::SetDurationMin(int64 m) {
  if (m > duration_) {
    SetPerformed(false);
  }
}

void FixedDurationIntervalVar::SetDurationMax(int64 m) {
  if (m < duration_) {
    SetPerformed(false);
  }
}

void FixedDurationIntervalVar::SetDurationRange(int64 mi, int64 ma) {
  if (mi > duration_ || ma < duration_ || mi > ma) {
    SetPerformed(false);
  }
}

int64 FixedDurationIntervalVar::EndMin() const {
  CHECK(performed_.MayBeTrue());
  return start_.Min() + duration_;
}

int64 FixedDurationIntervalVar::EndMax() const {
  CHECK(performed_.MayBeTrue());
  return CapAdd(start_.Max(), duration_);
}

void FixedDurationIntervalVar::SetEndMin(int64 m) {
  SetStartMin(CapSub(m, duration_));
}

void FixedDurationIntervalVar::SetEndMax(int64 m) {
  SetStartMax(CapSub(m, duration_));
}

void FixedDurationIntervalVar::SetEndRange(int64 mi, int64 ma) {
  SetStartRange(CapSub(mi, duration_), CapSub(ma, duration_));
}

bool FixedDurationIntervalVar::MustBePerformed() const {
  return (performed_.MustBeTrue());
}

bool FixedDurationIntervalVar::MayBePerformed() const {
  return (performed_.MayBeTrue());
}

void FixedDurationIntervalVar::SetPerformed(bool val) {
  if (in_process_) {
    performed_.SetValueInProcess(val);
  } else if (performed_.SetValue(val)) {
    Push();
  }
}

void FixedDurationIntervalVar::Push() {
  DCHECK(!in_process_);
  EnqueueVar(&handler_);
  DCHECK(!in_process_);
}

string FixedDurationIntervalVar::DebugString() const {
  const string& var_name = name();
  if (!performed_.MayBeTrue()) {
    if (!var_name.empty()) {
      return StringPrintf("%s(performed = false)", var_name.c_str());
    } else {
      return "IntervalVar(performed = false)";
    }
  } else {
    string out;
    if (!var_name.empty()) {
      out = var_name + "(start = ";
    } else {
      out = "IntervalVar(start = ";
    }
    StringAppendF(&out, "%s, duration = %" GG_LL_FORMAT "d, performed = %s)",
                  start_.DebugString().c_str(), duration_,
                  performed_.DebugString().c_str());
    return out;
  }
}

// ----- FixedDurationPerformedIntervalVar -----

class FixedDurationPerformedIntervalVar : public BaseIntervalVar {
 public:
  FixedDurationPerformedIntervalVar(Solver* const s, int64 start_min,
                                    int64 start_max, int64 duration,
                                    const string& name);
  // Unperformed interval.
  FixedDurationPerformedIntervalVar(Solver* const s, const string& name);
  virtual ~FixedDurationPerformedIntervalVar() {}

  virtual int64 StartMin() const;
  virtual int64 StartMax() const;
  virtual void SetStartMin(int64 m);
  virtual void SetStartMax(int64 m);
  virtual void SetStartRange(int64 mi, int64 ma);
  virtual int64 OldStartMin() const { return start_.PreviousMin(); }
  virtual int64 OldStartMax() const { return start_.PreviousMax(); }
  virtual void WhenStartRange(Demon* const d) { start_.WhenRange(d); }
  virtual void WhenStartBound(Demon* const d) { start_.WhenBound(d); }

  virtual int64 DurationMin() const;
  virtual int64 DurationMax() const;
  virtual void SetDurationMin(int64 m);
  virtual void SetDurationMax(int64 m);
  virtual void SetDurationRange(int64 mi, int64 ma);
  virtual int64 OldDurationMin() const { return duration_; }
  virtual int64 OldDurationMax() const { return duration_; }
  virtual void WhenDurationRange(Demon* const d) {}
  virtual void WhenDurationBound(Demon* const d) {}

  virtual int64 EndMin() const;
  virtual int64 EndMax() const;
  virtual void SetEndMin(int64 m);
  virtual void SetEndMax(int64 m);
  virtual void SetEndRange(int64 mi, int64 ma);
  virtual int64 OldEndMin() const { return CapAdd(OldStartMin(), duration_); }
  virtual int64 OldEndMax() const { return CapAdd(OldStartMax(), duration_); }
  virtual void WhenEndRange(Demon* const d) { WhenStartRange(d); }
  virtual void WhenEndBound(Demon* const d) { WhenEndRange(d); }

  virtual bool MustBePerformed() const;
  virtual bool MayBePerformed() const;
  virtual void SetPerformed(bool val);
  virtual bool WasPerformedBound() const { return true; }
  virtual void WhenPerformedBound(Demon* const d) {}
  virtual void Process();
  virtual string DebugString() const;

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(this, "", 0, NullInterval());
  }

 private:
  void CheckOldPerformed() {}
  virtual void Push();

  IntervalStorage start_;
  int64 duration_;
};

FixedDurationPerformedIntervalVar::FixedDurationPerformedIntervalVar(
    Solver* const s, int64 start_min, int64 start_max, int64 duration,
    const string& name)
    : BaseIntervalVar(s, name),
      start_(s, start_min, start_max),
      duration_(duration) {}

FixedDurationPerformedIntervalVar::FixedDurationPerformedIntervalVar(
    Solver* const s, const string& name)
    : BaseIntervalVar(s, name), start_(s, 0, 0), duration_(0) {}

void FixedDurationPerformedIntervalVar::Process() {
  CHECK(!in_process_);
  in_process_ = true;
  start_.UpdatePostponedBounds();
  set_queue_action_on_fail(&cleaner_);
  start_.ProcessDemons();
  clear_queue_action_on_fail();
  ClearInProcess();
  start_.UpdatePreviousBoundsAndApplyPostponedBounds(this, START);
}

int64 FixedDurationPerformedIntervalVar::StartMin() const {
  return start_.Min();
}

int64 FixedDurationPerformedIntervalVar::StartMax() const {
  return start_.Max();
}

void FixedDurationPerformedIntervalVar::SetStartMin(int64 m) {
  if (in_process_) {
    if (!start_.SetMinInProcess(m)) {
      SetPerformed(false);
    }
  } else {
    ProcessModification(start_.SetMin(m));
  }
}

void FixedDurationPerformedIntervalVar::SetStartMax(int64 m) {
  if (in_process_) {
    if (!start_.SetMaxInProcess(m)) {
      SetPerformed(false);
    }
  } else {
    ProcessModification(start_.SetMax(m));
  }
}

void FixedDurationPerformedIntervalVar::SetStartRange(int64 mi, int64 ma) {
  if (in_process_) {
    if (!start_.SetRangeInProcess(mi, ma)) {
      SetPerformed(false);
    }
  } else {
    ProcessModification(start_.SetRange(mi, ma));
  }
}

int64 FixedDurationPerformedIntervalVar::DurationMin() const {
  return duration_;
}

int64 FixedDurationPerformedIntervalVar::DurationMax() const {
  return duration_;
}

void FixedDurationPerformedIntervalVar::SetDurationMin(int64 m) {
  if (m > duration_) {
    SetPerformed(false);
  }
}

void FixedDurationPerformedIntervalVar::SetDurationMax(int64 m) {
  if (m < duration_) {
    SetPerformed(false);
  }
}
int64 FixedDurationPerformedIntervalVar::EndMin() const {
  return CapAdd(start_.Min(), duration_);
}

int64 FixedDurationPerformedIntervalVar::EndMax() const {
  return CapAdd(start_.Max(), duration_);
}

void FixedDurationPerformedIntervalVar::SetEndMin(int64 m) {
  SetStartMin(CapSub(m, duration_));
}

void FixedDurationPerformedIntervalVar::SetEndMax(int64 m) {
  SetStartMax(CapSub(m, duration_));
}

void FixedDurationPerformedIntervalVar::SetEndRange(int64 mi, int64 ma) {
  SetStartRange(CapSub(mi, duration_), CapSub(ma, duration_));
}

void FixedDurationPerformedIntervalVar::SetDurationRange(int64 mi, int64 ma) {
  if (mi > duration_ || ma < duration_ || mi > ma) {
    SetPerformed(false);
  }
}

bool FixedDurationPerformedIntervalVar::MustBePerformed() const { return true; }

bool FixedDurationPerformedIntervalVar::MayBePerformed() const { return true; }

void FixedDurationPerformedIntervalVar::SetPerformed(bool val) {
  if (!val) {
    solver()->Fail();
  }
}

void FixedDurationPerformedIntervalVar::Push() {
  DCHECK(!in_process_);
  EnqueueVar(&handler_);
  DCHECK(!in_process_);
}

string FixedDurationPerformedIntervalVar::DebugString() const {
  string out;
  const string& var_name = name();
  if (!var_name.empty()) {
    out = var_name + "(start = ";
  } else {
    out = "IntervalVar(start = ";
  }
  StringAppendF(&out, "%s, duration = %" GG_LL_FORMAT "d, performed = true)",
                start_.DebugString().c_str(), duration_);
  return out;
}

// ----- StartVarPerformedIntervalVar -----

class StartVarPerformedIntervalVar : public IntervalVar {
 public:
  StartVarPerformedIntervalVar(Solver* const s, IntVar* const start_var,
                               int64 duration, const string& name);
  virtual ~StartVarPerformedIntervalVar() {}

  virtual int64 StartMin() const;
  virtual int64 StartMax() const;
  virtual void SetStartMin(int64 m);
  virtual void SetStartMax(int64 m);
  virtual void SetStartRange(int64 mi, int64 ma);
  virtual int64 OldStartMin() const { return start_var_->OldMin(); }
  virtual int64 OldStartMax() const { return start_var_->OldMax(); }
  virtual void WhenStartRange(Demon* const d) { start_var_->WhenRange(d); }
  virtual void WhenStartBound(Demon* const d) { start_var_->WhenBound(d); }

  virtual int64 DurationMin() const;
  virtual int64 DurationMax() const;
  virtual void SetDurationMin(int64 m);
  virtual void SetDurationMax(int64 m);
  virtual void SetDurationRange(int64 mi, int64 ma);
  virtual int64 OldDurationMin() const { return duration_; }
  virtual int64 OldDurationMax() const { return duration_; }
  virtual void WhenDurationRange(Demon* const d) {}
  virtual void WhenDurationBound(Demon* const d) {}

  virtual int64 EndMin() const;
  virtual int64 EndMax() const;
  virtual void SetEndMin(int64 m);
  virtual void SetEndMax(int64 m);
  virtual void SetEndRange(int64 mi, int64 ma);
  virtual int64 OldEndMin() const { return CapAdd(OldStartMin(), duration_); }
  virtual int64 OldEndMax() const { return CapAdd(OldStartMax(), duration_); }
  virtual void WhenEndRange(Demon* const d) { start_var_->WhenRange(d); }
  virtual void WhenEndBound(Demon* const d) { start_var_->WhenBound(d); }

  virtual bool MustBePerformed() const;
  virtual bool MayBePerformed() const;
  virtual void SetPerformed(bool val);
  virtual bool WasPerformedBound() const { return true; }
  virtual void WhenPerformedBound(Demon* const d) {}
  virtual string DebugString() const;

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(this, "", 0, NullInterval());
  }

 private:
  IntVar* const start_var_;
  int64 duration_;
};

// TODO(user): Take care of overflows.
StartVarPerformedIntervalVar::StartVarPerformedIntervalVar(Solver* const s,
                                                           IntVar* const var,
                                                           int64 duration,
                                                           const string& name)
    : IntervalVar(s, name), start_var_(var), duration_(duration) {}

int64 StartVarPerformedIntervalVar::StartMin() const {
  return start_var_->Min();
}

int64 StartVarPerformedIntervalVar::StartMax() const {
  return start_var_->Max();
}

void StartVarPerformedIntervalVar::SetStartMin(int64 m) {
  start_var_->SetMin(m);
}

void StartVarPerformedIntervalVar::SetStartMax(int64 m) {
  start_var_->SetMax(m);
}

void StartVarPerformedIntervalVar::SetStartRange(int64 mi, int64 ma) {
  start_var_->SetRange(mi, ma);
}

int64 StartVarPerformedIntervalVar::DurationMin() const { return duration_; }

int64 StartVarPerformedIntervalVar::DurationMax() const { return duration_; }

void StartVarPerformedIntervalVar::SetDurationMin(int64 m) {
  if (m > duration_) {
    solver()->Fail();
  }
}

void StartVarPerformedIntervalVar::SetDurationMax(int64 m) {
  if (m < duration_) {
    solver()->Fail();
  }
}
int64 StartVarPerformedIntervalVar::EndMin() const {
  return start_var_->Min() + duration_;
}

int64 StartVarPerformedIntervalVar::EndMax() const {
  return start_var_->Max() + duration_;
}

void StartVarPerformedIntervalVar::SetEndMin(int64 m) {
  SetStartMin(m - duration_);
}

void StartVarPerformedIntervalVar::SetEndMax(int64 m) {
  SetStartMax(m - duration_);
}

void StartVarPerformedIntervalVar::SetEndRange(int64 mi, int64 ma) {
  SetStartRange(mi - duration_, ma - duration_);
}

void StartVarPerformedIntervalVar::SetDurationRange(int64 mi, int64 ma) {
  if (mi > duration_ || ma < duration_ || mi > ma) {
    solver()->Fail();
  }
}

bool StartVarPerformedIntervalVar::MustBePerformed() const { return true; }

bool StartVarPerformedIntervalVar::MayBePerformed() const { return true; }

void StartVarPerformedIntervalVar::SetPerformed(bool val) {
  if (!val) {
    solver()->Fail();
  }
}

string StartVarPerformedIntervalVar::DebugString() const {
  string out;
  const string& var_name = name();
  if (!var_name.empty()) {
    out = var_name + "(start = ";
  } else {
    out = "IntervalVar(start = ";
  }
  StringAppendF(&out, "%" GG_LL_FORMAT "d", start_var_->Min());
  if (!start_var_->Bound()) {
    StringAppendF(&out, " .. %" GG_LL_FORMAT "d", start_var_->Max());
  }

  StringAppendF(&out, ", duration = %" GG_LL_FORMAT "d, performed = true)",
                duration_);
  return out;
}

// ----- FixedInterval -----

class FixedInterval : public IntervalVar {
 public:
  FixedInterval(Solver* const s, int64 start, int64 duration,
                const string& name);
  virtual ~FixedInterval() {}

  virtual int64 StartMin() const { return start_; }
  virtual int64 StartMax() const { return start_; }
  virtual void SetStartMin(int64 m);
  virtual void SetStartMax(int64 m);
  virtual void SetStartRange(int64 mi, int64 ma);
  virtual int64 OldStartMin() const { return start_; }
  virtual int64 OldStartMax() const { return start_; }
  virtual void WhenStartRange(Demon* const d) {}
  virtual void WhenStartBound(Demon* const d) {}

  virtual int64 DurationMin() const { return duration_; }
  virtual int64 DurationMax() const { return duration_; }
  virtual void SetDurationMin(int64 m);
  virtual void SetDurationMax(int64 m);
  virtual void SetDurationRange(int64 mi, int64 ma);
  virtual int64 OldDurationMin() const { return duration_; }
  virtual int64 OldDurationMax() const { return duration_; }
  virtual void WhenDurationRange(Demon* const d) {}
  virtual void WhenDurationBound(Demon* const d) {}

  virtual int64 EndMin() const { return start_ + duration_; }
  virtual int64 EndMax() const { return start_ + duration_; }
  virtual void SetEndMin(int64 m);
  virtual void SetEndMax(int64 m);
  virtual void SetEndRange(int64 mi, int64 ma);
  virtual int64 OldEndMin() const { return start_ + duration_; }
  virtual int64 OldEndMax() const { return start_ + duration_; }
  virtual void WhenEndRange(Demon* const d) {}
  virtual void WhenEndBound(Demon* const d) {}

  virtual bool MustBePerformed() const { return true; }
  virtual bool MayBePerformed() const { return true; }
  virtual void SetPerformed(bool val);
  virtual bool WasPerformedBound() const { return true; }
  virtual void WhenPerformedBound(Demon* const d) {}
  virtual string DebugString() const;

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(this, "", 0, NullInterval());
  }

 private:
  const int64 start_;
  const int64 duration_;
};

FixedInterval::FixedInterval(Solver* const s, int64 start, int64 duration,
                             const string& name)
    : IntervalVar(s, name), start_(start), duration_(duration) {}

void FixedInterval::SetStartMin(int64 m) {
  if (m > start_) {
    solver()->Fail();
  }
}

void FixedInterval::SetStartMax(int64 m) {
  if (m < start_) {
    solver()->Fail();
  }
}

void FixedInterval::SetStartRange(int64 mi, int64 ma) {
  if (mi > start_ || ma < start_) {
    solver()->Fail();
  }
}

void FixedInterval::SetDurationMin(int64 m) {
  if (m > duration_) {
    solver()->Fail();
  }
}

void FixedInterval::SetDurationMax(int64 m) {
  if (m < duration_) {
    solver()->Fail();
  }
}

void FixedInterval::SetEndMin(int64 m) {
  if (m > start_ + duration_) {
    solver()->Fail();
  }
}

void FixedInterval::SetEndMax(int64 m) {
  if (m < start_ + duration_) {
    solver()->Fail();
  }
}

void FixedInterval::SetEndRange(int64 mi, int64 ma) {
  if (mi > start_ + duration_ || ma < start_ + duration_) {
    solver()->Fail();
  }
}

void FixedInterval::SetDurationRange(int64 mi, int64 ma) {
  if (mi > duration_ || ma < duration_) {
    solver()->Fail();
  }
}

void FixedInterval::SetPerformed(bool val) {
  if (!val) {
    solver()->Fail();
  }
}

string FixedInterval::DebugString() const {
  string out;
  const string& var_name = name();
  if (!var_name.empty()) {
    out = var_name + "(start = ";
  } else {
    out = "IntervalVar(start = ";
  }
  StringAppendF(&out, "%" GG_LL_FORMAT "d, duration = %" GG_LL_FORMAT
                "d, performed = true)",
                start_, duration_);
  return out;
}

// ----- VariableDurationIntervalVar -----

class VariableDurationIntervalVar : public BaseIntervalVar {
 public:
  VariableDurationIntervalVar(Solver* const s, int64 start_min, int64 start_max,
                              int64 duration_min, int64 duration_max,
                              int64 end_min, int64 end_max, bool optional,
                              const string& name)
      : BaseIntervalVar(s, name),
        start_(s, std::max(start_min, end_min - duration_max),
               std::min(start_max, end_max - duration_min)),
        duration_(s, std::max(duration_min, end_min - start_max),
                  std::min(duration_max, end_max - start_min)),
        end_(s, std::max(end_min, start_min + duration_min),
             std::min(end_max, start_max + duration_max)),
        performed_(s, optional) {}

  virtual ~VariableDurationIntervalVar() {}

  virtual int64 StartMin() const {
    CHECK(performed_.MayBeTrue());
    return start_.Min();
  }

  virtual int64 StartMax() const {
    CHECK(performed_.MayBeTrue());
    return start_.Max();
  }

  virtual void SetStartMin(int64 m) {
    if (performed_.MayBeTrue()) {
      if (in_process_) {
        if (!start_.SetMinInProcess(m)) {
          SetPerformed(false);
        }
      } else {
        ProcessModification(start_.SetMin(m));
      }
    }
  }

  virtual void SetStartMax(int64 m) {
    if (performed_.MayBeTrue()) {
      if (in_process_) {
        if (!start_.SetMaxInProcess(m)) {
          SetPerformed(false);
        }
      } else {
        ProcessModification(start_.SetMax(m));
      }
    }
  }

  virtual void SetStartRange(int64 mi, int64 ma) {
    if (performed_.MayBeTrue()) {
      if (in_process_) {
        if (!start_.SetRangeInProcess(mi, ma)) {
          SetPerformed(false);
        }
      } else {
        ProcessModification(start_.SetRange(mi, ma));
      }
    }
  }

  virtual int64 OldStartMin() const {
    CHECK(performed_.MayBeTrue());
    CHECK(in_process_);
    return start_.PreviousMin();
  }

  virtual int64 OldStartMax() const {
    CHECK(performed_.MayBeTrue());
    CHECK(in_process_);
    return start_.PreviousMax();
  }

  virtual void WhenStartRange(Demon* const d) {
    if (performed_.MayBeTrue()) {
      start_.WhenRange(d);
    }
  }

  virtual void WhenStartBound(Demon* const d) {
    if (performed_.MayBeTrue()) {
      start_.WhenBound(d);
    }
  }

  virtual int64 DurationMin() const {
    CHECK(performed_.MayBeTrue());
    return duration_.Min();
  }

  virtual int64 DurationMax() const {
    CHECK(performed_.MayBeTrue());
    return duration_.Max();
  }

  virtual void SetDurationMin(int64 m) {
    if (performed_.MayBeTrue()) {
      if (in_process_) {
        if (!duration_.SetMinInProcess(m)) {
          SetPerformed(false);
        }
      } else {
        ProcessModification(duration_.SetMin(m));
      }
    }
  }

  virtual void SetDurationMax(int64 m) {
    if (performed_.MayBeTrue()) {
      if (in_process_) {
        if (!duration_.SetMaxInProcess(m)) {
          SetPerformed(false);
        }
      } else {
        ProcessModification(duration_.SetMax(m));
      }
    }
  }

  virtual void SetDurationRange(int64 mi, int64 ma) {
    if (performed_.MayBeTrue()) {
      if (in_process_) {
        if (!duration_.SetRangeInProcess(mi, ma)) {
          SetPerformed(false);
        }
      } else {
        ProcessModification(duration_.SetRange(mi, ma));
      }
    }
  }

  virtual int64 OldDurationMin() const {
    CHECK(performed_.MayBeTrue());
    CHECK(in_process_);
    return duration_.PreviousMin();
  }

  virtual int64 OldDurationMax() const {
    CHECK(performed_.MayBeTrue());
    CHECK(in_process_);
    return duration_.PreviousMax();
  }

  virtual void WhenDurationRange(Demon* const d) {
    if (performed_.MayBeTrue()) {
      duration_.WhenRange(d);
    }
  }

  virtual void WhenDurationBound(Demon* const d) {
    if (performed_.MayBeTrue()) {
      duration_.WhenBound(d);
    }
  }

  virtual int64 EndMin() const {
    CHECK(performed_.MayBeTrue());
    return end_.Min();
  }

  virtual int64 EndMax() const {
    CHECK(performed_.MayBeTrue());
    return end_.Max();
  }

  virtual void SetEndMin(int64 m) {
    if (performed_.MayBeTrue()) {
      if (in_process_) {
        if (!end_.SetMinInProcess(m)) {
          SetPerformed(false);
        }
      } else {
        ProcessModification(end_.SetMin(m));
      }
    }
  }

  virtual void SetEndMax(int64 m) {
    if (performed_.MayBeTrue()) {
      if (in_process_) {
        if (!end_.SetMaxInProcess(m)) {
          SetPerformed(false);
        }
      } else {
        ProcessModification(end_.SetMax(m));
      }
    }
  }

  virtual void SetEndRange(int64 mi, int64 ma) {
    if (performed_.MayBeTrue()) {
      if (in_process_) {
        if (!end_.SetRangeInProcess(mi, ma)) {
          SetPerformed(false);
        }
      } else {
        ProcessModification(end_.SetRange(mi, ma));
      }
    }
  }

  virtual int64 OldEndMin() const {
    CHECK(performed_.MayBeTrue());
    CHECK(in_process_);
    return end_.PreviousMin();
  }

  virtual int64 OldEndMax() const {
    CHECK(performed_.MayBeTrue());
    CHECK(in_process_);
    return end_.PreviousMax();
  }

  virtual void WhenEndRange(Demon* const d) {
    if (performed_.MayBeTrue()) {
      end_.WhenRange(d);
    }
  }

  virtual void WhenEndBound(Demon* const d) {
    if (performed_.MayBeTrue()) {
      end_.WhenBound(d);
    }
  }

  virtual bool MustBePerformed() const { return (performed_.MustBeTrue()); }

  virtual bool MayBePerformed() const { return (performed_.MayBeTrue()); }

  virtual void SetPerformed(bool val) {
    if (in_process_) {
      performed_.SetValueInProcess(val);
    } else if (performed_.SetValue(val)) {
      Push();
    }
  }

  virtual bool WasPerformedBound() const {
    CHECK(in_process_);
    return performed_.WasBound();
  }

  virtual void WhenPerformedBound(Demon* const d) { performed_.WhenBound(d); }

  virtual void Process() {
    CHECK(!in_process_);
    in_process_ = true;
    start_.UpdatePostponedBounds();
    duration_.UpdatePostponedBounds();
    end_.UpdatePostponedBounds();
    performed_.UpdatePostponedBounds();
    set_queue_action_on_fail(&cleaner_);
    if (performed_.MayBeTrue()) {
      start_.ProcessDemons();
      duration_.ProcessDemons();
      end_.ProcessDemons();
    }
    performed_.ProcessDemons();
    clear_queue_action_on_fail();
    ClearInProcess();
    // TODO(user): Replace this enum by a callback.
    start_.UpdatePreviousBoundsAndApplyPostponedBounds(this, START);
    duration_.UpdatePreviousBoundsAndApplyPostponedBounds(this, DURATION);
    end_.UpdatePreviousBoundsAndApplyPostponedBounds(this, END);
    performed_.UpdatePreviousBoundsAndApplyPostponedBounds(this);
  }

  virtual string DebugString() const {
    const string& var_name = name();
    if (!performed_.MayBeTrue()) {
      if (!var_name.empty()) {
        return StringPrintf("%s(performed = false)", var_name.c_str());
      } else {
        return "IntervalVar(performed = false)";
      }
    } else {
      string out;
      if (!var_name.empty()) {
        out = var_name + "(start = ";
      } else {
        out = "IntervalVar(start = ";
      }

      StringAppendF(&out, "%s, duration = %s, end = %s, performed = %s)",
                    start_.DebugString().c_str(),
                    duration_.DebugString().c_str(),
                    end_.DebugString().c_str(),
                    performed_.DebugString().c_str());
      return out;
    }
  }

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(this, "", 0, NullInterval());
  }

 private:
  virtual void Push() {
    DCHECK(!in_process_);
    if (performed_.MayBeTrue()) {
      // Performs the intersection on all intervals before pushing the
      // variable onto the queue. This way, we make sure the interval variable
      // is always in a consistent minimal state.
      if (start_.SetRange(CapSub(end_.Min(), duration_.Max()),
                          CapSub(end_.Max(), duration_.Min())) ==
          INCONSISTENT) {
        performed_.SetValue(false);
      } else if (duration_.SetRange(CapSub(end_.Min(), start_.Max()),
                                    CapSub(end_.Max(), start_.Min())) ==
                 INCONSISTENT) {
        performed_.SetValue(false);
      } else if (end_.SetRange(CapAdd(start_.Min(), duration_.Min()),
                               CapAdd(start_.Max(), duration_.Max())) ==
                 INCONSISTENT) {
        performed_.SetValue(false);
      }
    }
    EnqueueVar(&handler_);
    DCHECK(!in_process_);
  }

  IntervalStorage start_;
  IntervalStorage duration_;
  IntervalStorage end_;
  BooleanStorage performed_;
};

// ----- Base synced interval var -----

class FixedDurationSyncedIntervalVar : public IntervalVar {
 public:
  FixedDurationSyncedIntervalVar(IntervalVar* const t, int64 duration,
                                 int64 offset, const string& name)
      : IntervalVar(t->solver(), name),
        t_(t),
        duration_(duration),
        offset_(offset) {}
  virtual ~FixedDurationSyncedIntervalVar() {}
  virtual int64 DurationMin() const { return duration_; }
  virtual int64 DurationMax() const { return duration_; }
  virtual void SetDurationMin(int64 m) {
    if (m > duration_) {
      solver()->Fail();
    }
  }
  virtual void SetDurationMax(int64 m) {
    if (m < duration_) {
      solver()->Fail();
    }
  }
  virtual void SetDurationRange(int64 mi, int64 ma) {
    if (mi > duration_ || ma < duration_ || mi > ma) {
      solver()->Fail();
    }
  }
  virtual int64 OldDurationMin() const { return duration_; }
  virtual int64 OldDurationMax() const { return duration_; }
  virtual void WhenDurationRange(Demon* const d) {}
  virtual void WhenDurationBound(Demon* const d) {}
  virtual int64 EndMin() const { return CapAdd(StartMin(), duration_); }
  virtual int64 EndMax() const { return CapAdd(StartMax(), duration_); }
  virtual void SetEndMin(int64 m) { SetStartMin(CapSub(m, duration_)); }
  virtual void SetEndMax(int64 m) { SetStartMax(CapSub(m, duration_)); }
  virtual void SetEndRange(int64 mi, int64 ma) {
    SetStartRange(CapSub(mi, duration_), CapSub(ma, duration_));
  }
  virtual int64 OldEndMin() const { return CapAdd(OldStartMin(), duration_); }
  virtual int64 OldEndMax() const { return CapAdd(OldStartMax(), duration_); }
  virtual void WhenEndRange(Demon* const d) { WhenStartRange(d); }
  virtual void WhenEndBound(Demon* const d) { WhenStartBound(d); }
  virtual bool MustBePerformed() const { return t_->MustBePerformed(); }
  virtual bool MayBePerformed() const { return t_->MayBePerformed(); }
  virtual void SetPerformed(bool val) { t_->SetPerformed(val); }
  virtual bool WasPerformedBound() const { return t_->WasPerformedBound(); }
  virtual void WhenPerformedBound(Demon* const d) { t_->WhenPerformedBound(d); }

 protected:
  IntervalVar* const t_;
  const int64 duration_;
  const int64 offset_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FixedDurationSyncedIntervalVar);
};

// ----- Fixed duration interval var synced on start -----

class FixedDurationIntervalVarStartSyncedOnStart :
    public FixedDurationSyncedIntervalVar {
 public:
  FixedDurationIntervalVarStartSyncedOnStart(IntervalVar* const t,
                                             int64 duration, int64 offset)
      : FixedDurationSyncedIntervalVar(
            t, duration, offset,
            StringPrintf(
                "IntervalStartSyncedOnStart(%s, duration = %" GG_LL_FORMAT
                "d, offset = %" GG_LL_FORMAT "d)",
                t->name().c_str(), duration, offset)) {}
  virtual ~FixedDurationIntervalVarStartSyncedOnStart() {}
  virtual int64 StartMin() const { return CapAdd(t_->StartMin(), offset_); }
  virtual int64 StartMax() const { return CapAdd(t_->StartMax(), offset_); }
  virtual void SetStartMin(int64 m) { t_->SetStartMin(CapSub(m, offset_)); }
  virtual void SetStartMax(int64 m) { t_->SetStartMax(CapSub(m, offset_)); }
  virtual void SetStartRange(int64 mi, int64 ma) {
    t_->SetStartRange(CapSub(mi, offset_), CapSub(ma, offset_));
  }
  virtual int64 OldStartMin() const {
    return CapAdd(t_->OldStartMin(), offset_);
  }
  virtual int64 OldStartMax() const {
    return CapAdd(t_->OldStartMax(), offset_);
  }
  virtual void WhenStartRange(Demon* const d) { t_->WhenStartRange(d); }
  virtual void WhenStartBound(Demon* const d) { t_->WhenStartBound(d); }
  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(
        this, ModelVisitor::kStartSyncOnStartOperation, offset_, t_);
  }
  virtual string DebugString() const {
    return StringPrintf(
        "IntervalStartSyncedOnStart(%s, duration = %" GG_LL_FORMAT
        "d, offset = %" GG_LL_FORMAT "d)",
        t_->DebugString().c_str(), duration_, offset_);
  }
};

// ----- Fixed duration interval start synced on end -----

class FixedDurationIntervalVarStartSyncedOnEnd :
    public FixedDurationSyncedIntervalVar {
 public:
  FixedDurationIntervalVarStartSyncedOnEnd(IntervalVar* const t, int64 duration,
                                           int64 offset)
      : FixedDurationSyncedIntervalVar(
            t, duration, offset,
            StringPrintf(
                "IntervalStartSyncedOnEnd(%s, duration = %" GG_LL_FORMAT
                "d, offset = %" GG_LL_FORMAT "d)",
                t->name().c_str(), duration, offset)) {}
  virtual ~FixedDurationIntervalVarStartSyncedOnEnd() {}
  virtual int64 StartMin() const { return CapAdd(t_->EndMin(), offset_); }
  virtual int64 StartMax() const { return CapAdd(t_->EndMax(), offset_); }
  virtual void SetStartMin(int64 m) { t_->SetEndMin(CapSub(m, offset_)); }
  virtual void SetStartMax(int64 m) { t_->SetEndMax(CapSub(m, offset_)); }
  virtual void SetStartRange(int64 mi, int64 ma) {
    t_->SetEndRange(CapSub(mi, offset_), CapSub(ma, offset_));
  }
  virtual int64 OldStartMin() const { return CapAdd(t_->OldEndMin(), offset_); }
  virtual int64 OldStartMax() const { return CapAdd(t_->OldEndMax(), offset_); }
  virtual void WhenStartRange(Demon* const d) { t_->WhenEndRange(d); }
  virtual void WhenStartBound(Demon* const d) { t_->WhenEndBound(d); }
  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(this, ModelVisitor::kStartSyncOnEndOperation,
                                   offset_, t_);
  }
  virtual string DebugString() const {
    return StringPrintf("IntervalStartSyncedOnEnd(%s, duration = %" GG_LL_FORMAT
                        "d, offset = %" GG_LL_FORMAT "d)",
                        t_->DebugString().c_str(), duration_, offset_);
  }
};
}  // namespace

// ----- API -----

IntervalVar* Solver::MakeMirrorInterval(IntervalVar* const t) {
  return RegisterIntervalVar(RevAlloc(new MirrorIntervalVar(this, t)));
}

IntervalVar* Solver::MakeIntervalRelaxedMax(IntervalVar* const interval_var) {
  if (interval_var->MustBePerformed()) {
    return interval_var;
  } else {
    return RegisterIntervalVar(
        RevAlloc(new IntervalVarRelaxedMax(interval_var)));
  }
}

IntervalVar* Solver::MakeIntervalRelaxedMin(IntervalVar* const interval_var) {
  if (interval_var->MustBePerformed()) {
    return interval_var;
  } else {
    return RegisterIntervalVar(
        RevAlloc(new IntervalVarRelaxedMin(interval_var)));
  }
}

void IntervalVar::WhenAnything(Demon* const d) {
  WhenStartRange(d);
  WhenDurationRange(d);
  WhenEndRange(d);
  WhenPerformedBound(d);
}

IntervalVar* Solver::MakeFixedInterval(int64 start, int64 duration,
                                       const string& name) {
  return RevAlloc(new FixedInterval(this, start, duration, name));
}

IntervalVar* Solver::MakeFixedDurationIntervalVar(int64 start_min,
                                                  int64 start_max,
                                                  int64 duration, bool optional,
                                                  const string& name) {
  if (start_min == start_max && !optional) {
    return MakeFixedInterval(start_min, duration, name);
  } else if (!optional) {
    return RegisterIntervalVar(RevAlloc(new FixedDurationPerformedIntervalVar(
        this, start_min, start_max, duration, name)));
  }
  return RegisterIntervalVar(RevAlloc(new FixedDurationIntervalVar(
      this, start_min, start_max, duration, optional, name)));
}

void Solver::MakeFixedDurationIntervalVarArray(int count, int64 start_min,
                                               int64 start_max, int64 duration,
                                               bool optional,
                                               const string& name,
                                               std::vector<IntervalVar*>* array) {
  CHECK_GT(count, 0);
  CHECK_NOTNULL(array);
  array->clear();
  for (int i = 0; i < count; ++i) {
    const string var_name = StringPrintf("%s%i", name.c_str(), i);
    array->push_back(MakeFixedDurationIntervalVar(
        start_min, start_max, duration, optional, var_name));
  }
}

IntervalVar* Solver::MakeFixedDurationIntervalVar(IntVar* const start_variable,
                                                  int64 duration,
                                                  const string& name) {
  CHECK_NOTNULL(start_variable);
  CHECK_GE(duration, 0);
  return RegisterIntervalVar(RevAlloc(
      new StartVarPerformedIntervalVar(this, start_variable, duration, name)));
}

void Solver::MakeFixedDurationIntervalVarArray(
    const std::vector<IntVar*>& start_variables, int64 duration, const string& name,
    std::vector<IntervalVar*>* array) {
  CHECK_NOTNULL(array);
  array->clear();
  for (int i = 0; i < start_variables.size(); ++i) {
    const string var_name = StringPrintf("%s%i", name.c_str(), i);
    array->push_back(
        MakeFixedDurationIntervalVar(start_variables[i], duration, var_name));
  }
}

// This method fills the vector with interval variables built with
// the corresponding start variables.
void Solver::MakeFixedDurationIntervalVarArray(
    const std::vector<IntVar*>& start_variables, const std::vector<int64>& durations,
    const string& name, std::vector<IntervalVar*>* array) {
  CHECK_NOTNULL(array);
  CHECK_EQ(start_variables.size(), durations.size());
  array->clear();
  for (int i = 0; i < start_variables.size(); ++i) {
    const string var_name = StringPrintf("%s%i", name.c_str(), i);
    array->push_back(MakeFixedDurationIntervalVar(start_variables[i],
                                                  durations[i], var_name));
  }
}

void Solver::MakeFixedDurationIntervalVarArray(
    const std::vector<IntVar*>& start_variables, const std::vector<int>& durations,
    const string& name, std::vector<IntervalVar*>* array) {
  CHECK_NOTNULL(array);
  CHECK_EQ(start_variables.size(), durations.size());
  array->clear();
  for (int i = 0; i < start_variables.size(); ++i) {
    const string var_name = StringPrintf("%s%i", name.c_str(), i);
    array->push_back(MakeFixedDurationIntervalVar(start_variables[i],
                                                  durations[i], var_name));
  }
}

// Variable Duration Interval Var

IntervalVar* Solver::MakeIntervalVar(int64 start_min, int64 start_max,
                                     int64 duration_min, int64 duration_max,
                                     int64 end_min, int64 end_max,
                                     bool optional, const string& name) {
  return RegisterIntervalVar(RevAlloc(new VariableDurationIntervalVar(
      this, start_min, start_max, duration_min, duration_max, end_min, end_max,
      optional, name)));
}

void Solver::MakeIntervalVarArray(int count, int64 start_min, int64 start_max,
                                  int64 duration_min, int64 duration_max,
                                  int64 end_min, int64 end_max, bool optional,
                                  const string& name,
                                  std::vector<IntervalVar*>* const array) {
  CHECK_GT(count, 0);
  CHECK_NOTNULL(array);
  array->clear();
  for (int i = 0; i < count; ++i) {
    const string var_name = StringPrintf("%s%i", name.c_str(), i);
    array->push_back(
        MakeIntervalVar(start_min, start_max, duration_min, duration_max,
                        end_min, end_max, optional, var_name));
  }
}

// Synced Interval Vars
IntervalVar* Solver::MakeFixedDurationStartSyncedOnStartIntervalVar(
    IntervalVar* const interval_var, int64 duration, int64 offset) {
  return RegisterIntervalVar(
      RevAlloc(new FixedDurationIntervalVarStartSyncedOnStart(
          interval_var, duration, offset)));
}

IntervalVar* Solver::MakeFixedDurationStartSyncedOnEndIntervalVar(
    IntervalVar* const interval_var, int64 duration, int64 offset) {
  return RegisterIntervalVar(
      RevAlloc(new FixedDurationIntervalVarStartSyncedOnEnd(interval_var,
                                                            duration, offset)));
}

IntervalVar* Solver::MakeFixedDurationEndSyncedOnStartIntervalVar(
    IntervalVar* const interval_var, int64 duration, int64 offset) {
  return RegisterIntervalVar(
      RevAlloc(new FixedDurationIntervalVarStartSyncedOnStart(
          interval_var, duration, offset - duration)));
}

IntervalVar* Solver::MakeFixedDurationEndSyncedOnEndIntervalVar(
    IntervalVar* const interval_var, int64 duration, int64 offset) {
  return RegisterIntervalVar(
      RevAlloc(new FixedDurationIntervalVarStartSyncedOnEnd(
          interval_var, duration, offset - duration)));
}
}  // namespace operations_research
