#ifndef THIRD_PARTY_P4LANG_P4C_BACKENDS_P4TOOLS_MODULES_TESTGEN_TARGETS_BMV2_GOOGLE3_P4_ASSERTS_PARSER_H_
#define THIRD_PARTY_P4LANG_P4C_BACKENDS_P4TOOLS_MODULES_TESTGEN_TARGETS_BMV2_P4_AARSER_H_

#include <map>
#include <optional>

#include "backends/p4tools/common/lib/variables.h"
#include "ir/ir.h"
#include "ir/node.h"
#include "ir/visitor.h"
#include "lib/cstring.h"
#include "p4_constraints/p4_constraints/ast.proto.h"
#include "p4_constraints/p4_constraints/backend/constraint_info.h"
#include "p4runtime/proto/p4/config/v1/p4info.proto.h"

namespace P4Tools::P4Testgen::Bmv2 {

class AssertsParser : public Inspector {
  p4_constraints::ConstraintInfo constraintsInfo_;

  /// Maps control plane identifiers to their corresponding IR type.
  using IdentifierTypeMap = std::map<cstring, const IR::Type *>;
  /// Maps control plane identifiers to their corresponding declaration.
  using DeclarationMap = std::map<cstring, const IR::Declaration *>;

  /// A vector of restrictions imposed on the control-plane.
  ConstraintsVector &restrictionsVec;

  /// Convert a P4Constraints binary expression to a P4C IR Operation_Binary.
  static std::optional<const IR::Operation_Binary *>
  p4ConstraintsBinaryExpressionToIrBinaryExpression(
      const p4_constraints::ast::BinaryExpression &binaryExpression,
      const DeclarationMap &declarationMap, const IdentifierTypeMap &typeMap);

  /// Convert a P4Constraints Type to a P4C IR Type.
  static std::optional<const IR::Type *> p4ConstraintsTypeToIrType(
      const p4_constraints::ast::Type &typeCast);

  /// Convert a P4Constraints field access member to a symbolic variable.
  /// This symbolic variable is typically matched with a table match entry.
  static std::optional<const IR::SymbolicVariable *>
  p4ConstraintFieldAccessToSymbolicVariable(
      const p4_constraints::ast::FieldAccess &fieldAccess,
      const DeclarationMap &declarationMap, const IdentifierTypeMap &typeMap);

  /// Convert a P4Constraints attribute access member to a symbolic variable.
  /// This symbolic variable is typically matched with a table match entry.
  static std::optional<const IR::SymbolicVariable *>
  p4ConstraintAttributeAccessToSymbolicVariable(
      const p4_constraints::ast::AttributeAccess &attribute,
      const p4_constraints::ast::SourceLocation &startLocation);

  /// Convert a P4Constraints expression to a IR Expression.
  static std::optional<const IR::Expression *>
  p4ConstraintsExpressionToIrConstraint(
      const p4_constraints::ast::Expression &astExpression,
      const DeclarationMap &declarationMap, const IdentifierTypeMap &typeMap);

 public:
  explicit AssertsParser(ConstraintsVector &output,
                         const p4::config::v1::P4Info &p4Info);
  bool preorder(const IR::P4Action *actionContext) override;
  bool preorder(const IR::P4Table *tableContext) override;
};

}  // namespace P4Tools::P4Testgen::Bmv2

#endif  // THIRD_PARTY_P4LANG_P4C_BACKENDS_P4TOOLS_MODULES_TESTGEN_TARGETS_BMV2_P4_ASSERTS_PARSER_H_
