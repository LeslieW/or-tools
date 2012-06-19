// Copyright 2010-2012 Google
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

#include <string.h>
#include <vector>

#include "base/commandlineflags.h"
#include "base/integral_types.h"
#include "base/logging.h"
#include "base/scoped_ptr.h"
#include "base/stl_util.h"
#include "constraint_solver/constraint_solver.h"
#include "constraint_solver/constraint_solveri.h"
#include "util/const_int_array.h"

DECLARE_int32(cache_initial_size);

namespace operations_research {
// ----- ModelCache -----

ModelCache::ModelCache(Solver* const s)
    : solver_(s) {}

ModelCache::~ModelCache() {}

Solver* ModelCache::solver() const {
  return solver_;
}

namespace {
// ----- Helpers -----

template <class T> bool IsEqual(const T& a1, const T& a2) {
  return a1 == a2;
}

bool IsEqual(const ConstIntArray*& a1, const ConstIntArray*& a2) {
  return a1->Equals(*a2);
}

template<class T> bool IsEqual(const std::vector<T*>& a1,
                               const std::vector<T*>& a2) {
  if (a1.size() != a2.size()) {
    return false;
  }
  for (int i = 0; i < a1.size(); ++i) {
    if (a1[i] != a2[i]) {
      return false;
    }
  }
  return true;
}

template <class A1, class A2> uint64 Hash2(const A1& a1, const A2& a2) {
  uint64 a = Hash1(a1);
  uint64 b = GG_ULONGLONG(0xe08c1d668b756f82);   // more of the golden ratio
  uint64 c = Hash1(a2);
  mix(a, b, c);
  return c;
}

template <class A1, class A2, class A3> uint64 Hash3(const A1& a1,
                                                     const A2& a2,
                                                     const A3& a3) {
  uint64 a = Hash1(a1);
  uint64 b = Hash1(a2);
  uint64 c = Hash1(a3);
  mix(a, b, c);
  return c;
}

template <class A1, class A2, class A3, class A4> uint64 Hash4(const A1& a1,
                                                               const A2& a2,
                                                               const A3& a3,
                                                               const A4& a4) {
  uint64 a = Hash1(a1);
  uint64 b = Hash1(a2);
  uint64 c = Hash2(a3, a4);
  mix(a, b, c);
  return c;
}

template <class C> void Double(C*** array_ptr, int* size_ptr) {
  DCHECK(array_ptr != NULL);
  DCHECK(size_ptr != NULL);
  C** const old_cell_array = *array_ptr;
  const int old_size = *size_ptr;
  (*size_ptr) *= 2;
  (*array_ptr) = new C*[(*size_ptr)];
  memset(*array_ptr, 0, (*size_ptr) * sizeof(**array_ptr));
  for (int i = 0; i < old_size; ++i) {
    C* tmp = old_cell_array[i];
    while (tmp != NULL) {
      C* const to_reinsert = tmp;
      tmp = tmp->next();
      const uint64 position = to_reinsert->Hash() % (*size_ptr);
      to_reinsert->set_next((*array_ptr)[position]);
      (*array_ptr)[position] = to_reinsert;
    }
  }
  delete [] (old_cell_array);
}

// ----- Cache objects built with 1 object -----

template <class C, class A1> class Cache1 {
 public:
  Cache1()
      : array_(new Cell*[FLAGS_cache_initial_size]),
        size_(FLAGS_cache_initial_size),
        num_items_(0) {
    memset(array_, 0, sizeof(*array_) * size_);
  }

  ~Cache1() {
    for (int i = 0; i < size_; ++i) {
      Cell* tmp = array_[i];
      while (tmp != NULL) {
        Cell* const to_delete = tmp;
        tmp = tmp->next();
        delete to_delete;
      }
    }
    delete [] array_;
  }

  C* Find(const A1& a1) const {
    uint64 code = Hash1(a1) % size_;
    Cell* tmp = array_[code];
    while (tmp) {
      C* const result = tmp->ReturnsIfEqual(a1);
      if (result != NULL) {
        return result;
      }
      tmp = tmp->next();
    }
    return NULL;
  }

  void UnsafeInsert(const A1& a1, C* const c) {
    const int position = Hash1(a1) % size_;
    Cell* const cell = new Cell(a1, c, array_[position]);
    array_[position] = cell;
    if (++num_items_ > 2 * size_) {
      Double(&array_, &size_);
    }
  }

 private:
  class Cell {
   public:
    Cell(const A1& a1, C* const container, Cell* const next)
        : a1_(a1), container_(container), next_(next) {}

    C* ReturnsIfEqual(const A1& a1) const {
      if (IsEqual(a1_, a1)) {
        return container_;
      }
      return NULL;
    }

    uint64 Hash() const {
      return Hash1(a1_);
    }

    void set_next(Cell* const next) { next_ = next; }

    Cell* next() const { return next_; }

   private:
    const A1 a1_;
    C* const container_;
    Cell* next_;
  };

  Cell** array_;
  int size_;
  int num_items_;
};

// ----- Cache objects built with 2 objects -----

template <class C, class A1, class A2> class Cache2 {
 public:
  Cache2()
    : array_(new Cell*[FLAGS_cache_initial_size]),
      size_(FLAGS_cache_initial_size),
      num_items_(0) {
    memset(array_, 0, sizeof(*array_) * size_);
  }

  ~Cache2() {
    for (int i = 0; i < size_; ++i) {
      Cell* tmp = array_[i];
      while (tmp != NULL) {
        Cell* const to_delete = tmp;
        tmp = tmp->next();
        delete to_delete;
      }
    }
    delete [] array_;
  }

  C* Find(const A1& a1, const A2& a2) const {
    uint64 code = Hash2(a1, a2) % size_;
    Cell* tmp = array_[code];
    while (tmp) {
      C* const result = tmp->ReturnsIfEqual(a1, a2);
      if (result != NULL) {
        return result;
      }
      tmp = tmp->next();
    }
    return NULL;
  }

  void UnsafeInsert(const A1& a1, const A2& a2, C* const c) {
    const int position = Hash2(a1, a2) % size_;
    Cell* const cell = new Cell(a1, a2, c, array_[position]);
    array_[position] = cell;
    if (++num_items_ > 2 * size_) {
      Double(&array_, &size_);
    }
  }

 private:
  class Cell {
   public:
    Cell(const A1& a1, const A2& a2, C* const container, Cell* const next)
        : a1_(a1), a2_(a2), container_(container), next_(next) {}

    C* ReturnsIfEqual(const A1& a1, const A2& a2) const {
      if (IsEqual(a1_, a1) && IsEqual(a2_, a2)) {
        return container_;
      }
      return NULL;
    }

    uint64 Hash() const {
      return Hash2(a1_, a2_);
    }

    void set_next(Cell* const next) { next_ = next; }

    Cell* next() const { return next_; }

   private:
    const A1 a1_;
    const A2 a2_;
    C* const container_;
    Cell* next_;
  };

  Cell** array_;
  int size_;
  int num_items_;
};

// ----- Cache objects built with 2 objects -----

template <class C, class A1, class A2, class A3> class Cache3 {
 public:
  Cache3()
    : array_(new Cell*[FLAGS_cache_initial_size]),
      size_(FLAGS_cache_initial_size),
      num_items_(0) {
    memset(array_, 0, sizeof(*array_) * size_);
  }

  ~Cache3() {
    for (int i = 0; i < size_; ++i) {
      Cell* tmp = array_[i];
      while (tmp != NULL) {
        Cell* const to_delete = tmp;
        tmp = tmp->next();
        delete to_delete;
      }
    }
    delete [] array_;
  }

  C* Find(const A1& a1, const A2& a2, const A3& a3) const {
    uint64 code = Hash3(a1, a2, a3) % size_;
    Cell* tmp = array_[code];
    while (tmp) {
      C* const result = tmp->ReturnsIfEqual(a1, a2, a3);
      if (result != NULL) {
        return result;
      }
      tmp = tmp->next();
    }
    return NULL;
  }

  void UnsafeInsert(const A1& a1, const A2& a2, const A3& a3, C* const c) {
    const int position = Hash3(a1, a2, a3) % size_;
    Cell* const cell = new Cell(a1, a2, a3, c, array_[position]);
    array_[position] = cell;
    if (++num_items_ > 2 * size_) {
      Double(&array_, &size_);
    }
  }

 private:
  class Cell {
   public:
    Cell(const A1& a1,
         const A2& a2,
         const A3& a3,
         C* const container,
         Cell* const next)
      : a1_(a1), a2_(a2), a3_(a3), container_(container), next_(next) {}

    C* ReturnsIfEqual(const A1& a1, const A2& a2, const A3& a3) const {
      if (IsEqual(a1_, a1) && IsEqual(a2_, a2) && IsEqual(a3_, a3)) {
        return container_;
      }
      return NULL;
    }

    uint64 Hash() const {
      return Hash3(a1_, a2_, a3_);
    }

    void set_next(Cell* const next) { next_ = next; }

    Cell* next() const { return next_; }

   private:
    const A1 a1_;
    const A2 a2_;
    const A3 a3_;
    C* const container_;
    Cell* next_;
  };

  Cell** array_;
  int size_;
  int num_items_;
};

// ----- Model Cache -----

class NonReversibleCache : public ModelCache {
 public:
  typedef Cache1<IntExpr, IntExpr*> ExprIntExprCache;
  typedef Cache1<IntExpr, std::vector<IntVar*> > VarArrayIntExprCache;

  typedef Cache2<Constraint, IntVar*, int64> VarConstantConstraintCache;
  typedef Cache2<Constraint, IntVar*, IntVar*> VarVarConstraintCache;
  typedef Cache2<IntExpr, IntVar*, int64> VarConstantIntExprCache;
  typedef Cache2<IntExpr, IntExpr*, int64> ExprConstantIntExprCache;
  typedef Cache2<IntExpr, IntVar*, IntVar*> VarVarIntExprCache;
  typedef Cache2<IntExpr, IntExpr*, IntExpr*> ExprExprIntExprCache;
  typedef Cache2<IntExpr, IntVar*, ConstIntArray*> VarConstantArrayIntExprCache;
  typedef Cache2<IntExpr, std::vector<IntVar*>, ConstIntArray*> VarArrayConstantArrayIntExprCache;

  typedef Cache3<IntExpr, IntVar*, int64, int64>
      VarConstantConstantIntExprCache;
  typedef Cache3<Constraint, IntVar*, int64, int64>
      VarConstantConstantConstraintCache;

  explicit NonReversibleCache(Solver* const solver)
      : ModelCache(solver),
        void_constraints_(VOID_CONSTRAINT_MAX, NULL) {
    for (int i = 0; i < VAR_CONSTANT_CONSTRAINT_MAX; ++i) {
      var_constant_constraints_.push_back(new VarConstantConstraintCache);
    }
    for (int i = 0; i < VAR_VAR_CONSTRAINT_MAX; ++i) {
      var_var_constraints_.push_back(new VarVarConstraintCache);
    }
    for (int i = 0; i < VAR_CONSTANT_CONSTANT_CONSTRAINT_MAX; ++i) {
      var_constant_constant_constraints_.push_back(
          new VarConstantConstantConstraintCache);
    }
    for (int i = 0; i < EXPR_EXPRESSION_MAX; ++i) {
      expr_expressions_.push_back(new ExprIntExprCache);
    }
    for (int i = 0; i < VAR_CONSTANT_EXPRESSION_MAX; ++i) {
      var_constant_expressions_.push_back(new VarConstantIntExprCache);
    }
    for (int i = 0; i < EXPR_CONSTANT_EXPRESSION_MAX; ++i) {
      expr_constant_expressions_.push_back(new ExprConstantIntExprCache);
    }
    for (int i = 0; i < VAR_VAR_EXPRESSION_MAX; ++i) {
      var_var_expressions_.push_back(new VarVarIntExprCache);
    }
    for (int i = 0; i < EXPR_EXPR_EXPRESSION_MAX; ++i) {
      expr_expr_expressions_.push_back(new ExprExprIntExprCache);
    }
    for (int i = 0; i < VAR_CONSTANT_CONSTANT_EXPRESSION_MAX; ++i) {
      var_constant_constant_expressions_.push_back(
          new VarConstantConstantIntExprCache);
    }
    for (int i = 0; i < VAR_CONSTANT_ARRAY_EXPRESSION_MAX; ++i) {
      var_constant_array_expressions_.push_back(
          new VarConstantArrayIntExprCache);
    }
    for (int i = 0; i < VAR_ARRAY_EXPRESSION_MAX; ++i) {
      var_array_expressions_.push_back(new VarArrayIntExprCache);
    }
    for (int i = 0; i < VAR_ARRAY_CONSTANT_ARRAY_EXPRESSION_MAX; ++i) {
      var_array_constant_array_expressions_.push_back(
          new VarArrayConstantArrayIntExprCache);
    }
  }

  virtual ~NonReversibleCache() {
    STLDeleteElements(&var_constant_constraints_);
    STLDeleteElements(&var_var_constraints_);
    STLDeleteElements(&var_constant_constant_constraints_);
    STLDeleteElements(&expr_expressions_);
    STLDeleteElements(&var_constant_expressions_);
    STLDeleteElements(&var_var_expressions_);
    STLDeleteElements(&expr_constant_expressions_);
    STLDeleteElements(&expr_expr_expressions_);
    STLDeleteElements(&var_constant_constant_expressions_);
    STLDeleteElements(&var_constant_array_expressions_);
    STLDeleteElements(&var_array_expressions_);
    STLDeleteElements(&var_array_constant_array_expressions_);
  }

  // Void Constraint.-

  virtual Constraint* FindVoidConstraint(VoidConstraintType type) const {
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VOID_CONSTRAINT_MAX);
    return void_constraints_[type];
  }

  virtual void InsertVoidConstraint(Constraint* const ct,
                                    VoidConstraintType type) {
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VOID_CONSTRAINT_MAX);
    DCHECK(ct != NULL);
    if (solver()->state() != Solver::IN_SEARCH) {
      void_constraints_[type] = ct;
    }
  }

  // VarConstantConstraint.

  virtual Constraint* FindVarConstantConstraint(
      IntVar* const var,
      int64 value,
      VarConstantConstraintType type) const {
    DCHECK(var != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_CONSTANT_CONSTRAINT_MAX);
    return var_constant_constraints_[type]->Find(var, value);
  }

  virtual void InsertVarConstantConstraint(
      Constraint* const ct,
      IntVar* const var,
      int64 value,
      VarConstantConstraintType type) {
    DCHECK(ct != NULL);
    DCHECK(var != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_CONSTANT_CONSTRAINT_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        var_constant_constraints_[type]->Find(var, value) == NULL) {
      var_constant_constraints_[type]->UnsafeInsert(var, value, ct);
    }
  }

  // Var Constant Constant Constraint.

  virtual Constraint* FindVarConstantConstantConstraint(
      IntVar* const var,
      int64 value1,
      int64 value2,
      VarConstantConstantConstraintType type) const {
    DCHECK(var != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_CONSTANT_CONSTANT_CONSTRAINT_MAX);
    return var_constant_constant_constraints_[type]->Find(var, value1, value2);
  }

  virtual void InsertVarConstantConstantConstraint(
      Constraint* const ct,
      IntVar* const var,
      int64 value1,
      int64 value2,
      VarConstantConstantConstraintType type) {
    DCHECK(ct != NULL);
    DCHECK(var != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_CONSTANT_CONSTANT_CONSTRAINT_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        var_constant_constant_constraints_[type]->Find(var,
                                                       value1,
                                                       value2) == NULL) {
      var_constant_constant_constraints_[type]->UnsafeInsert(var,
                                                             value1,
                                                             value2,
                                                             ct);
    }
  }

  // Var Var Constraint.

  virtual Constraint* FindVarVarConstraint(
      IntVar* const var1,
      IntVar* const var2,
      VarVarConstraintType type) const {
    DCHECK(var1 != NULL);
    DCHECK(var2 != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_VAR_CONSTRAINT_MAX);
    return var_var_constraints_[type]->Find(var1, var2);
  }

  virtual void InsertVarVarConstraint(Constraint* const ct,
                                      IntVar* const var1,
                                      IntVar* const var2,
                                      VarVarConstraintType type) {
    DCHECK(ct != NULL);
    DCHECK(var1 != NULL);
    DCHECK(var2 != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_VAR_CONSTRAINT_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        var_var_constraints_[type]->Find(var1, var2) == NULL) {
      var_var_constraints_[type]->UnsafeInsert(var1, var2, ct);
    }
  }

  // Var Expression.

  virtual IntExpr* FindExprExpression(IntExpr* const expr,
                                      ExprExpressionType type) const {
    DCHECK(expr != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, EXPR_EXPRESSION_MAX);
    return expr_expressions_[type]->Find(expr);
  }

  virtual void InsertExprExpression(IntExpr* const expression,
                                   IntExpr* const expr,
                                   ExprExpressionType type) {
    DCHECK(expression != NULL);
    DCHECK(expr != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, EXPR_EXPRESSION_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        expr_expressions_[type]->Find(expr) == NULL) {
      expr_expressions_[type]->UnsafeInsert(expr, expression);
    }
  }

  // Var Constant Expression.

  virtual IntExpr* FindVarConstantExpression(
      IntVar* const var,
      int64 value,
      VarConstantExpressionType type) const {
    DCHECK(var != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_CONSTANT_EXPRESSION_MAX);
    return var_constant_expressions_[type]->Find(var, value);
  }

  virtual void InsertVarConstantExpression(
      IntExpr* const expression,
      IntVar* const var,
      int64 value,
      VarConstantExpressionType type) {
    DCHECK(expression != NULL);
    DCHECK(var != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_CONSTANT_EXPRESSION_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        var_constant_expressions_[type]->Find(var, value) == NULL) {
      var_constant_expressions_[type]->UnsafeInsert(var, value, expression);
    }
  }

  // Var Var Expression.

  virtual IntExpr* FindVarVarExpression(
      IntVar* const var1,
      IntVar* const var2,
      VarVarExpressionType type) const {
    DCHECK(var1 != NULL);
    DCHECK(var2 != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_VAR_EXPRESSION_MAX);
    return var_var_expressions_[type]->Find(var1, var2);
  }

  virtual void InsertVarVarExpression(
      IntExpr* const expression,
      IntVar* const var1,
      IntVar* const var2,
      VarVarExpressionType type) {
    DCHECK(expression != NULL);
    DCHECK(var1 != NULL);
    DCHECK(var2 != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_VAR_EXPRESSION_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        var_var_expressions_[type]->Find(var1, var2) == NULL) {
      var_var_expressions_[type]->UnsafeInsert(var1, var2, expression);
    }
  }

   // Expr Constant Expressions.

  virtual IntExpr* FindExprConstantExpression(
      IntExpr* const expr,
      int64 value,
      ExprConstantExpressionType type) const {
    DCHECK(expr != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, EXPR_CONSTANT_EXPRESSION_MAX);
    return expr_constant_expressions_[type]->Find(expr, value);
  }

  virtual void InsertExprConstantExpression(
      IntExpr* const expression,
      IntExpr* const expr,
      int64 value,
      ExprConstantExpressionType type) {
    DCHECK(expression != NULL);
    DCHECK(expr != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, EXPR_CONSTANT_EXPRESSION_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        expr_constant_expressions_[type]->Find(expr, value) == NULL) {
      expr_constant_expressions_[type]->UnsafeInsert(expr, value, expression);
    }
  }

  // Expr Expr Expression.

  virtual IntExpr* FindExprExprExpression(
      IntExpr* const var1,
      IntExpr* const var2,
      ExprExprExpressionType type) const {
    DCHECK(var1 != NULL);
    DCHECK(var2 != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, EXPR_EXPR_EXPRESSION_MAX);
    return expr_expr_expressions_[type]->Find(var1, var2);
  }

  virtual void InsertExprExprExpression(
      IntExpr* const expression,
      IntExpr* const var1,
      IntExpr* const var2,
      ExprExprExpressionType type) {
    DCHECK(expression != NULL);
    DCHECK(var1 != NULL);
    DCHECK(var2 != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, EXPR_EXPR_EXPRESSION_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        expr_expr_expressions_[type]->Find(var1, var2) == NULL) {
      expr_expr_expressions_[type]->UnsafeInsert(var1, var2, expression);
    }
  }

  // Var Constant Constant Expression.

  virtual IntExpr* FindVarConstantConstantExpression(
      IntVar* const var,
      int64 value1,
      int64 value2,
      VarConstantConstantExpressionType type) const {
    DCHECK(var != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_CONSTANT_CONSTANT_EXPRESSION_MAX);
    return var_constant_constant_expressions_[type]->Find(var, value1, value2);
  }

  virtual void InsertVarConstantConstantExpression(
      IntExpr* const expression,
      IntVar* const var,
      int64 value1,
      int64 value2,
      VarConstantConstantExpressionType type) {
    DCHECK(expression != NULL);
    DCHECK(var != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_CONSTANT_CONSTANT_EXPRESSION_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        var_constant_constant_expressions_[type]->Find(var,
                                                       value1,
                                                       value2) == NULL) {
      var_constant_constant_expressions_[type]->UnsafeInsert(var,
                                                             value1,
                                                             value2,
                                                             expression);
    }
  }

  // Var Constant Array Expression.

  virtual IntExpr* FindVarConstantArrayExpression(
      IntVar* const var,
      ConstIntArray* const values,
      VarConstantArrayExpressionType type) const {
    DCHECK(var != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_CONSTANT_ARRAY_EXPRESSION_MAX);
    return var_constant_array_expressions_[type]->Find(var, values);
  }

  virtual void InsertVarConstantArrayExpression(
      IntExpr* const expression,
      IntVar* const var,
      ConstIntArray* const values,
      VarConstantArrayExpressionType type) {
    DCHECK(expression != NULL);
    DCHECK(var != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_CONSTANT_ARRAY_EXPRESSION_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        var_constant_array_expressions_[type]->Find(var, values) == NULL) {
      var_constant_array_expressions_[type]->UnsafeInsert(var,
                                                          values,
                                                          expression);
    }
  }

  // Var Array Expression.

  virtual IntExpr* FindVarArrayExpression(
      const std::vector<IntVar*>& vars,
      VarArrayExpressionType type) const {
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_ARRAY_EXPRESSION_MAX);
    return var_array_expressions_[type]->Find(vars);
  }

  virtual void InsertVarArrayExpression(
      IntExpr* const expression,
      const std::vector<IntVar*>& vars,
      VarArrayExpressionType type) {
    DCHECK(expression != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_ARRAY_EXPRESSION_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        var_array_expressions_[type]->Find(vars) == NULL) {
      var_array_expressions_[type]->UnsafeInsert(vars, expression);
    }
  }

  // Var Array Constant Array Expressions.

  virtual IntExpr* FindVarArrayConstantArrayExpression(
      const std::vector<IntVar*>& vars,
      ConstIntArray* const values,
      VarArrayConstantArrayExpressionType type) const {
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_ARRAY_CONSTANT_ARRAY_EXPRESSION_MAX);
    return var_array_constant_array_expressions_[type]->Find(vars, values);
  }


  virtual void InsertArrayVarConstantArrayExpression(
      IntExpr* const expression,
      const std::vector<IntVar*>& vars,
      ConstIntArray* const values,
      VarArrayConstantArrayExpressionType type) {
    DCHECK(expression != NULL);
    DCHECK_GE(type, 0);
    DCHECK_LT(type, VAR_ARRAY_CONSTANT_ARRAY_EXPRESSION_MAX);
    if (solver()->state() != Solver::IN_SEARCH &&
        var_array_constant_array_expressions_[type]->Find(vars,
                                                          values) == NULL) {
      var_array_constant_array_expressions_[type]->UnsafeInsert(
          vars, values, expression);
    }
  }


 private:
  std::vector<Constraint*> void_constraints_;
  std::vector<VarConstantConstraintCache*> var_constant_constraints_;
  std::vector<VarVarConstraintCache*> var_var_constraints_;
  std::vector<VarConstantConstantConstraintCache*>
      var_constant_constant_constraints_;
  std::vector<ExprIntExprCache*> expr_expressions_;
  std::vector<VarConstantIntExprCache*> var_constant_expressions_;
  std::vector<VarVarIntExprCache*> var_var_expressions_;
  std::vector<ExprConstantIntExprCache*> expr_constant_expressions_;
  std::vector<ExprExprIntExprCache*> expr_expr_expressions_;
  std::vector<VarConstantConstantIntExprCache*> var_constant_constant_expressions_;
  std::vector<VarConstantArrayIntExprCache*> var_constant_array_expressions_;
  std::vector<VarArrayIntExprCache*> var_array_expressions_;
  std::vector<VarArrayConstantArrayIntExprCache*> var_array_constant_array_expressions_;
};
}  // namespace

ModelCache* BuildModelCache(Solver* const solver) {
  return new NonReversibleCache(solver);
}

ModelCache* Solver::Cache() const {
  return model_cache_.get();
}
}  // namespace operations_research
