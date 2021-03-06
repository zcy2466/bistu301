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

#ifndef SRC_AST_CALL_EXPRESSION_H_
#define SRC_AST_CALL_EXPRESSION_H_

#include "src/ast/expression.h"

namespace tint {
namespace ast {

// Forward declarations.
class IdentifierExpression;

/// A call expression
class CallExpression : public Castable<CallExpression, Expression> {
 public:
  /// Constructor
  /// @param program_id the identifier of the program that owns this node
  /// @param source the call expression source
  /// @param func the function
  /// @param args the arguments
  CallExpression(ProgramID program_id,
                 const Source& source,
                 const IdentifierExpression* func,
                 ExpressionList args);
  /// Move constructor
  CallExpression(CallExpression&&);
  ~CallExpression() override;

  /// Clones this node and all transitive child nodes using the `CloneContext`
  /// `ctx`.
  /// @param ctx the clone context
  /// @return the newly cloned node
  const CallExpression* Clone(CloneContext* ctx) const override;

  /// The target function
  const IdentifierExpression* const func;
  /// The arguments
  const ExpressionList args;
};

}  // namespace ast
}  // namespace tint

#endif  // SRC_AST_CALL_EXPRESSION_H_
