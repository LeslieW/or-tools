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
#include "base/map_util.h"
#include "base/stl_util.h"
#include "flatzinc2/presolve.h"

namespace operations_research {

// For the author's reference, here is an indicative list of presolve rules that
// should eventually be implemented.
//
// Presolve rule:
//   - int_le -> propagate bounds
//   - int_eq -> merge with integer value
//   - int_ne -> remove value
//   - set_in -> merge domain
//   - array_bool_and -> assign all if === true
//   - array_bool_or -> assign all if == false
//   - reif -> unreify if boolean var is bound to true or false
//   - store all different
//   - array_var_int_element -> value bound => array_var_int_position
//   - int_abs -> store info
//   - int_eq_reif, int_ne_reif, int_ne -> simplify abs(x) ==/!= 0
//   - int_lin_xx -> replace all negative by opposite
//   - int_lin_le/eq -> all >0 -> bound propagation on max
//   - bool_eq|ne_reif -> simplify if argument is bound
//   - int_div|times -> propagate if arg0/arg1 bound
//   - int_lin_lt/gt -> transform to le/ge
//   - mark target variables

bool FzPresolver::PresolveBool2Int(FzConstraint* input) {
  MarkVariablesAsEquivalent(input->arguments[0].variable,
                            input->arguments[1].variable);
  MarkAsTriviallyTrue(input);
  return true;
}

bool FzPresolver::PresolveIntEq(FzConstraint* input) {
  if (input->arguments[0].type == FzArgument::INT_VAR_REF) {
    if (input->arguments[1].type == FzArgument::INT_VAR_REF) {
      MarkVariablesAsEquivalent(input->arguments[0].variable,
                                input->arguments[1].variable);
    } else {
      const int64 value = input->arguments[1].integer_value;
      input->arguments[0].variable->domain.ReduceDomain(value, value);
    }
    MarkAsTriviallyTrue(input);
    return true;
  } else {  // Arg0 is an integer value.
    const int64 value = input->arguments[0].integer_value;
    if (input->arguments[1].type == FzArgument::INT_VAR_REF) {
      input->arguments[1].variable->domain.ReduceDomain(value, value);
      MarkAsTriviallyTrue(input);
      return true;
    } else {
      if (value == input->arguments[1].integer_value) {
        // No-op, removing.
        MarkAsTriviallyTrue(input);
        return false;
      } else {
        // TODO(user): Mark model as inconsistent.
        return false;
      }
    }
  }
}

bool FzPresolver::PresolveOneConstraint(FzConstraint* ct) {
  bool changed = false;
  if (ct->type == "int_eq") {
    changed = PresolveIntEq(ct);
  } else if (ct->type == "bool2int") {
    changed = PresolveBool2Int(ct);
  }
  return changed;
}

bool FzPresolver::Run(FzModel* model) {
  bool changed_since_start = false;
  for (;;) {
    bool changed = false;
    var_representative_map_.clear();
    for (FzConstraint* const ct : model->constraints()) {
      if (!ct->is_trivially_true) {
        changed |= PresolveOneConstraint(ct);
      }
    }
    if (!var_representative_map_.empty()) {
      // Some new substitutions were introduced. Let's process them.
      DCHECK(changed);
      changed = true;  // To be safe in opt mode.
      SubstituteEverywhere(model);
    }
    changed_since_start |= changed;
    if (!changed) break;
  }
  return changed_since_start;
}

void FzPresolver::MarkAsTriviallyTrue(FzConstraint* ct) {
  ct->is_trivially_true = true;
  // TODO(user): Reclaim arguments and memory.
}

void FzPresolver::MarkVariablesAsEquivalent(FzIntegerVariable* from,
                                            FzIntegerVariable* to) {
  CHECK(from != nullptr);
  CHECK(to != nullptr);
  // Apply the substitutions, if any.
  from = FindRepresentativeOfVar(from);
  to = FindRepresentativeOfVar(to);
  if (to->temporary) {
    // Let's switch to keep a non temporary as representative.
    FzIntegerVariable* tmp = to;
    to = from;
    from = tmp;
  }
  if (from != to) {
    CHECK(to->Merge(from->name, from->domain, from->defining_constraint,
                    from->temporary));
    var_representative_map_[from] = to;
  }
}

FzIntegerVariable* FzPresolver::FindRepresentativeOfVar(
    FzIntegerVariable* var) {
  if (var == nullptr) return nullptr;
  FzIntegerVariable* start_var = var;
  // First loop: find the top parent.
  for (;;) {
    FzIntegerVariable* parent =
        FindWithDefault(var_representative_map_, var, var);
    if (parent == var) break;
    var = parent;
  }
  // Second loop: attach all the path to the top parent.
  while (start_var != var) {
    FzIntegerVariable* const parent = var_representative_map_[start_var];
    var_representative_map_[start_var] = var;
    start_var = parent;
  }
  return FindWithDefault(var_representative_map_, var, var);
}

void FzPresolver::SubstituteEverywhere(FzModel* model) {
  // Rewrite the constraints.
  for (FzConstraint* const ct : model->constraints()) {
    if (ct != nullptr && !ct->is_trivially_true) {
      for (int i = 0; i < ct->arguments.size(); ++i) {
        FzArgument* argument = &ct->arguments[i];
        switch (argument->type) {
          case FzArgument::INT_VAR_REF: {
            argument->variable = FindRepresentativeOfVar(argument->variable);
            break;
          }
          case FzArgument::INT_VAR_REF_ARRAY: {
            for (int i = 0; i < argument->variables.size(); ++i) {
              argument->variables[i] =
                  FindRepresentativeOfVar(argument->variables[i]);
            }
            break;
          }
          default: {}
        }
      }
      ct->target_var = FindRepresentativeOfVar(ct->target_var);
    }
  }
  // Rewrite the search.
  for (FzAnnotation* const ann : model->mutable_search_annotations()) {
    SubstituteAnnotation(ann);
  }
  // Rewrite the output.
  for (FzOnSolutionOutput* const output : model->mutable_output()) {
    output->variable = FindRepresentativeOfVar(output->variable);
    for (int i = 0; i < output->flat_variables.size(); ++i) {
      output->flat_variables[i] =
          FindRepresentativeOfVar(output->flat_variables[i]);
    }
  }
}

void FzPresolver::SubstituteAnnotation(FzAnnotation* ann) {
  // TODO(user): Remove recursion.
  switch (ann->type) {
    case FzAnnotation::ANNOTATION_LIST:
    case FzAnnotation::FUNCTION_CALL: {
      for (int i = 0; i < ann->annotations.size(); ++i) {
        SubstituteAnnotation(&ann->annotations[i]);
      }
      break;
    }
    case FzAnnotation::INT_VAR_REF: {
      ann->variable = FindRepresentativeOfVar(ann->variable);
      break;
    }
    case FzAnnotation::INT_VAR_REF_ARRAY: {
      for (int i = 0; i < ann->variables.size(); ++i) {
        ann->variables[i] = FindRepresentativeOfVar(ann->variables[i]);
      }
      break;
    }
    default: {}
  }
}
}  // namespace operations_research
