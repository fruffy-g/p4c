#include "backends/p4tools/modules/testgen/targets/bmv2/p4_asserts_parser.h"

#include <optional>
#include <string>

#include "backends/p4tools/common/control_plane/symbolic_variables.h"
#include "backends/p4tools/common/lib/variables.h"
#include "ir/id.h"
#include "ir/ir.h"
#include "ir/irutils.h"
#include "lib/error.h"
#include "lib/exceptions.h"
#include "absl/status/statusor.h"
#include "p4_constraints/p4_constraints/ast.h"
#include "p4_constraints/p4_constraints/ast.proto.h"
#include "p4_constraints/p4_constraints/backend/constraint_info.h"
// #include
// "p4_constraints/p4_constraints/backend/type_checker.h"
#include "p4_constraints/p4_constraints/constraint_source.h"
#include "p4_constraints/p4_constraints/frontend/constraint_kind.h"
#include "p4_constraints/p4_constraints/frontend/parser.h"
#include "lib/big_int_util.h"
#include "lib/cstring.h"
#include "p4lang_p4runtime/proto/p4/config/v1/p4info.proto.h"

namespace P4Tools::P4Testgen::Bmv2 {

AssertsParser::AssertsParser(ConstraintsVector &output,
                             const p4::config::v1::P4Info &p4info)
    : restrictionsVec(output) {
  setName("AssertsParser");
  auto constraintsInfo = p4_constraints::P4ToConstraintInfo(p4info);
  if (!constraintsInfo.ok()) {
    ::error("Failed to parse P4Info: %1%",
            constraintsInfo.status().message().data());
  }
  constraintsInfo_ = *constraintsInfo;
}

std::optional<const IR::Operation_Binary *>
AssertsParser::p4ConstraintsBinaryExpressionToIrBinaryExpression(
    const p4_constraints::ast::BinaryExpression &binaryExpression,
    const DeclarationMap &declarationMap, const IdentifierTypeMap &typeMap) {
  auto left = p4ConstraintsExpressionToIrConstraint(binaryExpression.left(),
                                                    declarationMap, typeMap);
  if (!left.has_value()) {
    return std::nullopt;
  }
  auto right = p4ConstraintsExpressionToIrConstraint(binaryExpression.right(),
                                                     declarationMap, typeMap);
  if (!right.has_value()) {
    return std::nullopt;
  }
  if (left.value()->type->is<IR::Type_InfInt>() &&
      right.value()->type->is<IR::Type_InfInt>()) {
    ::error(
        "Both sides of the expression have an infinite precision integer type. "
        "Casting is currently not supported for this case.");
    return std::nullopt;
  }
  if (left.value()->type->is<IR::Type_InfInt>()) {
    left = new IR::Cast(right.value()->type, left.value());
  }
  if (right.value()->type->is<IR::Type_InfInt>()) {
    right = new IR::Cast(left.value()->type, right.value());
  }

  switch (binaryExpression.binop()) {
    case p4_constraints::ast::BinaryOperator::EQ:
      return new IR::Equ(left.value(), right.value());
    case p4_constraints::ast::BinaryOperator::NE:
      return new IR::Neq(left.value(), right.value());
    case p4_constraints::ast::BinaryOperator::GT:
      return new IR::Grt(left.value(), right.value());
    case p4_constraints::ast::BinaryOperator::GE:
      return new IR::Geq(left.value(), right.value());
    case p4_constraints::ast::BinaryOperator::LT:
      return new IR::Lss(left.value(), right.value());
    case p4_constraints::ast::BinaryOperator::LE:
      return new IR::Leq(left.value(), right.value());
    case p4_constraints::ast::BinaryOperator::AND:
      return new IR::LAnd(left.value(), right.value());
    case p4_constraints::ast::BinaryOperator::OR:
      return new IR::LOr(left.value(), right.value());
    case p4_constraints::ast::BinaryOperator::IMPLIES:
      return new IR::LOr(new IR::LNot(left.value()), right.value());
    case p4_constraints::ast::BinaryOperator::UNKNOWN_OPERATOR:
      ::error("Unknown binary operator: %1%", binaryExpression.DebugString());
      return std::nullopt;
  }
}

std::optional<const IR::Type *> AssertsParser::p4ConstraintsTypeToIrType(
    const p4_constraints::ast::Type &typeCast) {
  switch (typeCast.type_case()) {
    case p4_constraints::ast::Type::kBoolean:
      return IR::Type_Boolean::get();
    case p4_constraints::ast::Type::kArbitraryInt:
      return new IR::Type_InfInt();
    case p4_constraints::ast::Type::kFixedUnsigned:
      return IR::getBitType(typeCast.fixed_unsigned().bitwidth());
    // In general, we concern ourselves only with the key width for match types.
    case p4_constraints::ast::Type::kExact:
      return IR::getBitType(typeCast.exact().bitwidth());
    case p4_constraints::ast::Type::kLpm:
      return IR::getBitType(typeCast.lpm().bitwidth());
    case p4_constraints::ast::Type::kTernary:
      return IR::getBitType(typeCast.ternary().bitwidth());
    case p4_constraints::ast::Type::kRange:
      return IR::getBitType(typeCast.range().bitwidth());
    case p4_constraints::ast::Type::kOptionalMatch:
      return IR::getBitType(typeCast.optional_match().bitwidth());
  }
  ::error("Unsupported type: %1%", p4_constraints::ast::TypeName(typeCast));
  return std::nullopt;
}

std::optional<const IR::SymbolicVariable *>
AssertsParser::p4ConstraintFieldAccessToSymbolicVariable(
    const p4_constraints::ast::FieldAccess &fieldAccess,
    const DeclarationMap &declarationMap, const IdentifierTypeMap &typeMap) {
  if (fieldAccess.field() == "mask") {
    if (fieldAccess.expr().has_key()) {
      auto typeDecl = typeMap.find(fieldAccess.expr().key());
      if (typeDecl == typeMap.end()) {
        ::error("Field not found: %1%", fieldAccess.expr().key());
        return std::nullopt;
      }
      auto parentDecl = declarationMap.find(fieldAccess.expr().key());
      if (parentDecl == declarationMap.end()) {
        ::error("Parent identifier not found: %1%", fieldAccess.expr().key());
        return std::nullopt;
      }
      return ControlPlaneState::getTableTernaryMask(
          parentDecl->second->controlPlaneName(),
          fieldAccess.expr().key().c_str(), typeDecl->second);
    }
  } else if (fieldAccess.field() == "value") {
    if (fieldAccess.expr().has_key()) {
      auto typeDecl = typeMap.find(fieldAccess.expr().key());
      if (typeDecl == typeMap.end()) {
        ::error("Field not found: %1%", fieldAccess.expr().key());
        return std::nullopt;
      }
      auto parentDecl = declarationMap.find(fieldAccess.expr().key());
      if (parentDecl == declarationMap.end()) {
        ::error("Parent identifier not found: %1%", fieldAccess.expr().key());
        return std::nullopt;
      }
      return ControlPlaneState::getTableKey(
          parentDecl->second->controlPlaneName(),
          fieldAccess.expr().key().c_str(), typeDecl->second);
    }
  }
  ::error("Unsupported P4Constraints field access: %1%",
          fieldAccess.DebugString());
  return std::nullopt;
}

std::optional<const IR::SymbolicVariable *>
AssertsParser::p4ConstraintAttributeAccessToSymbolicVariable(
    const p4_constraints::ast::AttributeAccess &attribute,
    const p4_constraints::ast::SourceLocation &startLocation) {
  // TODO: Implement some form of symbolic priority in P4Testgen.
  if (attribute.attribute_name() == "priority") {
    auto name = cstring(startLocation.ShortDebugString().c_str()) + "_priority";
    return ToolsVariables::getSymbolicVariable(IR::getBitType(32), name);
  }
  ::error("Unsupported P4Constraints attribute access: %1%",
          attribute.DebugString());
  return std::nullopt;
}

std::optional<const IR::Expression *>
AssertsParser::p4ConstraintsExpressionToIrConstraint(
    const p4_constraints::ast::Expression &astExpression,
    const DeclarationMap &declarationMap, const IdentifierTypeMap &typeMap) {
  switch (astExpression.expression_case()) {
    case p4_constraints::ast::Expression::kBooleanConstant:
      return IR::getBoolLiteral(astExpression.boolean_constant());
    case p4_constraints::ast::Expression::kIntegerConstant:
      return IR::getConstant(new IR::Type_InfInt(),
                             big_int(astExpression.integer_constant()));
    case p4_constraints::ast::Expression::kKey: {
      auto typeDecl = typeMap.find(astExpression.key());
      if (typeDecl == typeMap.end()) {
        ::error("Key not found: %1%", astExpression.key());
        return std::nullopt;
      }
      return new IR::SymbolicVariable(typeDecl->second, astExpression.key());
    }
    case p4_constraints::ast::Expression::kActionParameter: {
      auto typeDecl = typeMap.find(astExpression.action_parameter());
      if (typeDecl == typeMap.end()) {
        ::error("Action parameter not found: %1%",
                astExpression.action_parameter());
        return std::nullopt;
      }
      return new IR::SymbolicVariable(typeDecl->second,
                                      astExpression.action_parameter());
    }

    case p4_constraints::ast::Expression::kBooleanNegation: {
      auto negation = p4ConstraintsExpressionToIrConstraint(
          astExpression.boolean_negation(), declarationMap, typeMap);
      if (!negation.has_value()) {
        return std::nullopt;
      }
      return new IR::LNot(negation.value());
    }
    case p4_constraints::ast::Expression::kArithmeticNegation: {
      auto negation = p4ConstraintsExpressionToIrConstraint(
          astExpression.arithmetic_negation(), declarationMap, typeMap);
      if (!negation.has_value()) {
        return std::nullopt;
      }
      return new IR::Neg(negation.value()->type, negation.value());
    }
    case p4_constraints::ast::Expression::kTypeCast: {
      auto type = p4ConstraintsTypeToIrType(astExpression.type());
      if (!type.has_value()) {
        return std::nullopt;
      }
      BUG_CHECK(type.value()->width_bits() > 0, "%1% unsupported type:",
                p4_constraints::ast::TypeName(astExpression.type()));
      auto typeExpression = p4ConstraintsExpressionToIrConstraint(
          astExpression.type_cast(), declarationMap, typeMap);
      if (!typeExpression.has_value()) {
        return std::nullopt;
      }
      return new IR::Cast(type.value(), typeExpression.value());
    }
    case p4_constraints::ast::Expression::kBinaryExpression:
      return p4ConstraintsBinaryExpressionToIrBinaryExpression(
          astExpression.binary_expression(), declarationMap, typeMap);
    case p4_constraints::ast::Expression::kFieldAccess:
      return p4ConstraintFieldAccessToSymbolicVariable(
          astExpression.field_access(), declarationMap, typeMap);
    case p4_constraints::ast::Expression::kAttributeAccess:
      return p4ConstraintAttributeAccessToSymbolicVariable(
          astExpression.attribute_access(), astExpression.start_location());
    case p4_constraints::ast::Expression::EXPRESSION_NOT_SET:
      ::error("Unsupported P4Constraints Expression: %1%",
              astExpression.DebugString());
      return std::nullopt;
  }
}

bool AssertsParser::preorder(const IR::P4Action *actionContext) {
  const auto *annotation = actionContext->getAnnotation("action_restriction");
  if (annotation == nullptr) {
    return false;
  }

  IdentifierTypeMap typeMap;
  DeclarationMap declarationMap;
  for (const auto *param : actionContext->parameters->parameters) {
    typeMap[param->controlPlaneName()] = param->type;
    declarationMap[param->controlPlaneName()] = actionContext;
  }

  std::string annotationString;
  for (const auto *subStr : annotation->body) {
    annotationString += subStr->text;
  }
  absl::StatusOr<p4_constraints::ast::Expression> astExpressionOpt =
      p4_constraints::ParseConstraint(
          p4_constraints::ConstraintKind::kActionConstraint,
          p4_constraints::ConstraintSource{
              annotationString,
              p4_constraints::ast::SourceLocation(),
          });
  if (!astExpressionOpt.ok()) {
    ::error("Failed to parse constraint annotation: %1%",
            astExpressionOpt.status().message().data());
    return false;
  }

  auto &astExpression = astExpressionOpt.value();
  // FIXME: We would like to use type inference, but this currently leads
  // to inference of invalid exact<0> types in some cases. Need to investigate.
  // for (const auto &actionInfo : constraintsInfo_.action_info_by_id) {
  //   if (actionInfo.second.name == actionContext->controlPlaneName()) {
  //     if (!p4_constraints::InferAndCheckTypes(&astExpression,
  //     actionInfo.second)
  //              .ok()) {
  //       ::error("Failed to type inference and checking: %1%",
  //               astExpression.DebugString());
  //       return false;
  //     }
  //   }
  // }

  auto expression = p4ConstraintsExpressionToIrConstraint(
      astExpression, declarationMap, typeMap);
  if (expression.has_value()) {
    restrictionsVec.push_back(expression.value());
  }

  return false;
}

bool AssertsParser::preorder(const IR::P4Table *tableContext) {
  const auto *annotation = tableContext->getAnnotation("entry_restriction");
  const auto *key = tableContext->getKey();
  if (annotation == nullptr || key == nullptr) {
    return false;
  }

  IdentifierTypeMap typeMap;
  DeclarationMap declarationMap;
  for (const auto *keyElement : tableContext->getKey()->keyElements) {
    const auto *nameAnnot = keyElement->getAnnotation("name");
    BUG_CHECK(nameAnnot != nullptr, "%1% table key without a name annotation",
              annotation->name.name);
    typeMap[nameAnnot->getName()] = keyElement->expression->type;
    declarationMap[nameAnnot->getName()] = tableContext;
  }
  std::string annotationString;
  for (const auto *subStr : annotation->body) {
    annotationString += subStr->text;
  }

  absl::StatusOr<p4_constraints::ast::Expression> astExpressionOpt =
      p4_constraints::ParseConstraint(
          p4_constraints::ConstraintKind::kTableConstraint,
          p4_constraints::ConstraintSource{
              annotationString,
              p4_constraints::ast::SourceLocation(),
          });
  if (!astExpressionOpt.ok()) {
    ::error("Failed to parse constraint annotation: %1%",
            astExpressionOpt.status().message().data());
    return false;
  }

  auto &astExpression = astExpressionOpt.value();
  // FIXME: We would like to use type inference, but this currently leads
  // to inference of invalid exact<0> types in some cases. Need to investigate.
  // for (const auto &tableInfo : constraintsInfo_.table_info_by_id) {
  //   if (tableInfo.second.name == tableContext->controlPlaneName()) {
  //     if (!p4_constraints::InferAndCheckTypes(&astExpression,
  //     tableInfo.second)
  //              .ok()) {
  //       ::error("Failed to type inference and checking: %1%",
  //               astExpression.DebugString());
  //       return false;
  //     }
  //   }
  // }

  auto expression = p4ConstraintsExpressionToIrConstraint(
      astExpression, declarationMap, typeMap);
  if (expression.has_value()) {
    restrictionsVec.push_back(expression.value());
  }
  return false;
}

}  // namespace P4Tools::P4Testgen::Bmv2
