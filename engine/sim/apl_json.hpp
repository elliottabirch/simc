// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#pragma once

#include "config.hpp"

#include <memory>
#include <string>
#include <vector>

struct sim_t;
struct apl_ast_node_t;

// Represents one variable action definition (set, setif, add, etc.)
struct apl_variable_action_t
{
  std::string operation;
  std::string if_expr;             // action if= guard
  std::unique_ptr<apl_ast_node_t> if_ast;
  std::string value_expr;
  std::unique_ptr<apl_ast_node_t> value_ast;
  std::string condition_expr;      // SETIF only
  std::unique_ptr<apl_ast_node_t> condition_ast;
  std::string value_else_expr;     // SETIF only
  std::unique_ptr<apl_ast_node_t> value_else_ast;
};

struct apl_ast_node_t
{
  enum node_type_e { NUMBER, IDENTIFIER, UNARY_OP, BINARY_OP };

  node_type_e node_type;

  // NUMBER
  double value = 0;

  // IDENTIFIER
  std::string raw;       // "buff.arcane_charge.stack"
  std::string category;  // "buff"
  std::string name;      // "arcane_charge"
  std::string property;           // "stack" (canonical after normalization)
  std::string original_name;      // original name before normalization, empty if unchanged
  std::string original_property;  // original property before normalization, empty if unchanged
  unsigned spell_id = 0; // resolved spell ID, 0 if unknown

  // Variable inlining (for category=="variable")
  std::vector<apl_variable_action_t> variable_actions;

  // UNARY_OP / BINARY_OP
  std::string op;                            // "+", "-", "!", etc.
  std::unique_ptr<apl_ast_node_t> operand;   // unary
  std::unique_ptr<apl_ast_node_t> left;      // binary
  std::unique_ptr<apl_ast_node_t> right;     // binary
};

namespace apl_json
{
void dump( sim_t& sim, const std::string& filename );
}
