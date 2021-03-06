// Copyright 2020 The Tint Authors.
//
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

#ifndef SRC_AST_ARRAY_ACCESSOR_EXPRESSION_H_
#define SRC_AST_ARRAY_ACCESSOR_EXPRESSION_H_

#include "src/ast/expression.h"

namespace tint {
namespace ast {

/// An array accessor expression
class ArrayAccessorExpression
    : public Castable<ArrayAccessorExpression, Expression> {
 public:
  /// Constructor
  /// @param program_id the identifier of the program that owns this node
  /// @param source the array accessor source
  /// @param arr the array
  /// @param idx the index expression
  ArrayAccessorExpression(ProgramID program_id,
                          const Source& source,
                          const Expression* arr,
                          const Expression* idx);
  /// Move constructor
  ArrayAccessorExpression(ArrayAccessorExpression&&);
  ~ArrayAccessorExpression() override;

  /// Clones this node and all transitive child nodes using the `CloneContext`
  /// `ctx`.
  /// @param ctx the clone context
  /// @return the newly cloned node
  const ArrayAccessorExpression* Clone(CloneContext* ctx) const override;

  /// the array
  const Expression* const array;

  /// the index expression
  const Expression* const index;
};

}  // namespace ast
}  // namespace tint

#endif  // SRC_AST_ARRAY_ACCESSOR_EXPRESSION_H_
