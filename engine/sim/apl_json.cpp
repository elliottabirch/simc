// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "apl_json.hpp"

#include "action/action.hpp"
#include "action/variable.hpp"
#include "buff/buff.hpp"
#include "dbc/spell_data.hpp"
#include "interfaces/sc_js.hpp"
#include "player/action_priority_list.hpp"
#include "player/action_variable.hpp"
#include "player/player.hpp"
#include "player/talent.hpp"
#include "sim/expressions.hpp"
#include "sim/sim.hpp"
#include "util/io.hpp"
#include "util/util.hpp"

#include "rapidjson/document.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"

#include <array>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rapidjson;
using namespace js;

namespace
{

// ============================================================================
// Bare identifiers that implicitly reference the containing action
// ============================================================================

static const std::vector<std::string> action_implicit_properties = {
  "cast_time", "execute_time", "cooldown_react", "cooldown", "cost",
  "cost_affordable", "ready", "usable", "usable_in", "travel_time",
  "available_targets", "tick_time", "new_tick_time", "cast_delay",
  "tick_multiplier", "energize_amount", "persistent_multiplier",
  "charges", "charges_fractional", "max_charges", "recharge_time",
  "full_recharge_time", "damage", "hit_damage", "crit_damage",
  "hit_heal", "crit_heal", "tick_damage", "hit_tick_damage",
  "crit_tick_damage", "tick_heal", "crit_tick_heal", "crit_pct_current",
  "multiplier", "primary_target", "enabled", "casting", "cast_remains",
  "channeling", "channel_remains", "executing", "execute_remains",
  "last_used", "gcd",
};

bool is_action_implicit( const std::string& name )
{
  for ( const auto& p : action_implicit_properties )
    if ( util::str_compare_ci( p, name ) )
      return true;
  return false;
}

// ============================================================================
// Qualify bare action-implicit identifiers with action.<name>.<property>
// ============================================================================

void qualify_action_identifiers( apl_ast_node_t& node, const std::string& action_name )
{
  switch ( node.node_type )
  {
    case apl_ast_node_t::IDENTIFIER:
      if ( node.category.empty() && node.property.empty() &&
           is_action_implicit( node.name ) )
      {
        node.property = node.name;
        node.name = action_name;
        node.category = "action";
        node.raw = "action." + action_name + "." + node.property;
      }
      break;
    case apl_ast_node_t::UNARY_OP:
      if ( node.operand )
        qualify_action_identifiers( *node.operand, action_name );
      break;
    case apl_ast_node_t::BINARY_OP:
      if ( node.left )
        qualify_action_identifiers( *node.left, action_name );
      if ( node.right )
        qualify_action_identifiers( *node.right, action_name );
      break;
    default:
      break;
  }
}

// ============================================================================
// Token-to-operator-string mapping
// ============================================================================

const char* token_to_op_string( expression::token_e t )
{
  switch ( t )
  {
    case expression::TOK_PLUS:  return "+";   // unary plus
    case expression::TOK_MINUS: return "-";   // unary minus
    case expression::TOK_NOT:   return "!";
    case expression::TOK_ABS:   return "abs";
    case expression::TOK_FLOOR: return "floor";
    case expression::TOK_CEIL:  return "ceil";
    case expression::TOK_ADD:   return "+";
    case expression::TOK_SUB:   return "-";
    case expression::TOK_MULT:  return "*";
    case expression::TOK_DIV:   return "%";
    case expression::TOK_MOD:   return "%%";
    case expression::TOK_AND:   return "&&";
    case expression::TOK_OR:    return "||";
    case expression::TOK_XOR:   return "^^";
    case expression::TOK_EQ:    return "==";
    case expression::TOK_NOTEQ: return "!=";
    case expression::TOK_LT:    return "<";
    case expression::TOK_LTEQ:  return "<=";
    case expression::TOK_GT:    return ">";
    case expression::TOK_GTEQ:  return ">=";
    case expression::TOK_IN:    return "~";
    case expression::TOK_NOTIN: return "!~";
    case expression::TOK_MAX:   return "<?";
    case expression::TOK_MIN:   return ">?";
    default: return "?";
  }
}

// ============================================================================
// Build AST from RPN token stream
// ============================================================================

std::unique_ptr<apl_ast_node_t> build_ast( std::vector<expression::expr_token_t>& rpn_tokens )
{
  std::vector<std::unique_ptr<apl_ast_node_t>> stack;

  for ( auto& t : rpn_tokens )
  {
    if ( t.type == expression::TOK_NUM )
    {
      auto node = std::make_unique<apl_ast_node_t>();
      node->node_type = apl_ast_node_t::NUMBER;
      node->value = std::stod( t.label );
      stack.push_back( std::move( node ) );
    }
    else if ( t.type == expression::TOK_STR )
    {
      auto node = std::make_unique<apl_ast_node_t>();
      node->node_type = apl_ast_node_t::IDENTIFIER;
      node->raw = t.label;

      // Split dot-notation: category.name.property[.extra...]
      auto dot1 = t.label.find( '.' );
      if ( dot1 != std::string::npos )
      {
        node->category = t.label.substr( 0, dot1 );
        auto dot2 = t.label.find( '.', dot1 + 1 );
        if ( dot2 != std::string::npos )
        {
          node->name = t.label.substr( dot1 + 1, dot2 - dot1 - 1 );
          node->property = t.label.substr( dot2 + 1 );
        }
        else
        {
          node->name = t.label.substr( dot1 + 1 );
        }
      }
      else
      {
        node->name = t.label;
      }

      stack.push_back( std::move( node ) );
    }
    else if ( expression::is_unary( t.type ) )
    {
      if ( stack.empty() )
        return nullptr;

      auto operand = std::move( stack.back() );
      stack.pop_back();

      auto node = std::make_unique<apl_ast_node_t>();
      node->node_type = apl_ast_node_t::UNARY_OP;
      node->op = token_to_op_string( t.type );
      node->operand = std::move( operand );
      stack.push_back( std::move( node ) );
    }
    else if ( expression::is_binary( t.type ) )
    {
      if ( stack.size() < 2 )
        return nullptr;

      auto right = std::move( stack.back() );
      stack.pop_back();
      auto left = std::move( stack.back() );
      stack.pop_back();

      auto node = std::make_unique<apl_ast_node_t>();
      node->node_type = apl_ast_node_t::BINARY_OP;
      node->op = token_to_op_string( t.type );
      node->left = std::move( left );
      node->right = std::move( right );
      stack.push_back( std::move( node ) );
    }
  }

  if ( stack.size() != 1 )
    return nullptr;

  return std::move( stack.back() );
}

// ============================================================================
// Parse expression string to AST
// ============================================================================

std::unique_ptr<apl_ast_node_t> parse_expression_to_ast( util::string_view expr_str )
{
  if ( expr_str.empty() )
    return nullptr;

  auto tokens = expression::parse_tokens( nullptr, expr_str );
  if ( !expression::convert_to_rpn( tokens ) )
    return nullptr;

  return build_ast( tokens );
}

// ============================================================================
// Normalize aliased properties to canonical forms
// ============================================================================

std::string normalize_cooldown_property( const std::string& prop )
{
  if ( prop == "ready" )
    return "up";
  if ( prop == "remains_guess" )
    return "remains_expected";
  if ( prop == "duration_guess" )
    return "duration_expected";
  return {};
}

// Resource expressions are two-part (e.g. mana.pct), so the alias lives in
// the "name" field rather than "property".
std::string normalize_resource_name( const std::string& name )
{
  if ( name == "percent" )
    return "pct";
  return {};
}

// Forward declarations — called from resolve_identifier for variable inlining
void resolve_ast( apl_ast_node_t& node, const player_t& player, std::set<std::string>& resolving_vars );
void desugar_ast( std::unique_ptr<apl_ast_node_t>& node_ptr );

void resolve_and_desugar( std::unique_ptr<apl_ast_node_t>& ast, const player_t& player,
                          std::set<std::string>& resolving_vars )
{
  if ( !ast )
    return;
  resolve_ast( *ast, player, resolving_vars );
  desugar_ast( ast );
}

// Convenience overload — creates a fresh recursion guard
void resolve_and_desugar( std::unique_ptr<apl_ast_node_t>& ast, const player_t& player )
{
  std::set<std::string> resolving_vars;
  resolve_and_desugar( ast, player, resolving_vars );
}

// ============================================================================
// Resolve identifiers — look up spell IDs
// ============================================================================

void resolve_identifier( apl_ast_node_t& node, const player_t& player,
                         std::set<std::string>& resolving_vars )
{
  if ( node.name.empty() )
    return;

  // Talent-specific resolution
  if ( node.category == "talent" )
  {
    auto t = player.find_talent_spell( talent_tree::SPECIALIZATION, node.name, player.specialization(), true );
    if ( t.invalid() )
      t = player.find_talent_spell( talent_tree::HERO, node.name, player.specialization(), true );
    if ( t.invalid() )
      t = player.find_talent_spell( talent_tree::CLASS, node.name, player.specialization(), true );
    if ( !t.invalid() && t.spell()->ok() )
      node.spell_id = t.spell()->id();

    // Fallback: try without passing specialization (matches how class modules look up talents)
    if ( node.spell_id == 0 )
    {
      t = player.find_talent_spell( talent_tree::SPECIALIZATION, node.name, SPEC_NONE, true );
      if ( t.invalid() )
        t = player.find_talent_spell( talent_tree::HERO, node.name, SPEC_NONE, true );
      if ( t.invalid() )
        t = player.find_talent_spell( talent_tree::CLASS, node.name, SPEC_NONE, true );
      if ( !t.invalid() && t.spell()->ok() )
        node.spell_id = t.spell()->id();
    }
    return;
  }

  // Variable inlining — attach all variable action definitions
  if ( node.category == "variable" )
  {
    // Guard against infinite recursion when variables reference themselves
    if ( resolving_vars.count( node.name ) )
      return;

    for ( const auto* v : player.variables )
    {
      if ( v && util::str_compare_ci( v->name_, node.name ) )
      {
        resolving_vars.insert( node.name );
        for ( const auto* act : v->variable_actions )
        {
          auto* va = dynamic_cast<const variable_t*>( act );
          if ( !va )
            continue;

          apl_variable_action_t entry;
          entry.operation = std::string( variable_t::operation_str( va->operation ) );

          if ( !va->option.if_expr_str.empty() )
          {
            entry.if_expr = va->option.if_expr_str;
            entry.if_ast = parse_expression_to_ast( va->option.if_expr_str );
            resolve_and_desugar( entry.if_ast, player, resolving_vars );
          }

          if ( !va->value_str.empty() )
          {
            entry.value_expr = va->value_str;
            entry.value_ast = parse_expression_to_ast( va->value_str );
            resolve_and_desugar( entry.value_ast, player, resolving_vars );
          }

          if ( va->operation == OPERATION_SETIF )
          {
            if ( !va->condition_str.empty() )
            {
              entry.condition_expr = va->condition_str;
              entry.condition_ast = parse_expression_to_ast( va->condition_str );
              resolve_and_desugar( entry.condition_ast, player, resolving_vars );
            }
            if ( !va->value_else_str.empty() )
            {
              entry.value_else_expr = va->value_else_str;
              entry.value_else_ast = parse_expression_to_ast( va->value_else_str );
              resolve_and_desugar( entry.value_else_ast, player, resolving_vars );
            }
          }

          node.variable_actions.push_back( std::move( entry ) );
        }
        resolving_vars.erase( node.name );
        return;
      }
    }
    return;
  }

  // Try player.find_spell by name
  const auto* spell = player.find_spell( node.name );
  if ( spell && spell->ok() )
  {
    node.spell_id = spell->id();
    return;
  }

  // Search buff list
  for ( const auto* b : player.buff_list )
  {
    if ( b && util::str_compare_ci( b->name_str, node.name ) )
    {
      if ( b->data().ok() )
        node.spell_id = b->data().id();
      return;
    }
  }
}

void resolve_ast( apl_ast_node_t& node, const player_t& player,
                  std::set<std::string>& resolving_vars )
{
  switch ( node.node_type )
  {
    case apl_ast_node_t::NUMBER:
      break;
    case apl_ast_node_t::IDENTIFIER:
      resolve_identifier( node, player, resolving_vars );
      // Normalize aliased properties to canonical forms
      if ( node.category == "cooldown" && !node.property.empty() )
      {
        auto canonical = normalize_cooldown_property( node.property );
        if ( !canonical.empty() )
        {
          node.original_property = node.property;
          node.property = std::move( canonical );
        }
      }
      else if ( !node.category.empty() && node.property.empty() )
      {
        // Two-part resource expressions: e.g. mana.percent -> mana.pct
        auto canonical = normalize_resource_name( node.name );
        if ( !canonical.empty() )
        {
          node.original_name = node.name;
          node.name = std::move( canonical );
        }
      }
      break;
    case apl_ast_node_t::UNARY_OP:
      if ( node.operand )
        resolve_ast( *node.operand, player, resolving_vars );
      break;
    case apl_ast_node_t::BINARY_OP:
      if ( node.left )
        resolve_ast( *node.left, player, resolving_vars );
      if ( node.right )
        resolve_ast( *node.right, player, resolving_vars );
      break;
  }
}

// ============================================================================
// AST helpers for desugaring
// ============================================================================

std::unique_ptr<apl_ast_node_t> make_buff_ident( const apl_ast_node_t& src,
                                                  const std::string& prop )
{
  auto n = std::make_unique<apl_ast_node_t>();
  n->node_type = apl_ast_node_t::IDENTIFIER;
  n->category = src.category;
  n->name = src.name;
  n->property = prop;
  n->spell_id = src.spell_id;
  n->raw = src.category + "." + src.name + "." + prop;
  return n;
}

std::unique_ptr<apl_ast_node_t> make_number( double val )
{
  auto n = std::make_unique<apl_ast_node_t>();
  n->node_type = apl_ast_node_t::NUMBER;
  n->value = val;
  return n;
}

std::unique_ptr<apl_ast_node_t> make_binop( const std::string& op,
                                             std::unique_ptr<apl_ast_node_t> left,
                                             std::unique_ptr<apl_ast_node_t> right )
{
  auto n = std::make_unique<apl_ast_node_t>();
  n->node_type = apl_ast_node_t::BINARY_OP;
  n->op = op;
  n->left = std::move( left );
  n->right = std::move( right );
  return n;
}

// ============================================================================
// Desugar buff/debuff property aliases into explicit AST comparisons
// ============================================================================

void desugar_ast( std::unique_ptr<apl_ast_node_t>& node_ptr )
{
  if ( !node_ptr )
    return;

  auto& node = *node_ptr;

  switch ( node.node_type )
  {
    case apl_ast_node_t::IDENTIFIER:
      if ( node.category == "buff" || node.category == "debuff" )
      {
        if ( node.property == "up" )
        {
          // buff.X.up → buff.X.stack > 0
          node_ptr = make_binop( ">", make_buff_ident( node, "stack" ), make_number( 0 ) );
        }
        else if ( node.property == "down" )
        {
          // buff.X.down → buff.X.stack == 0
          node_ptr = make_binop( "==", make_buff_ident( node, "stack" ), make_number( 0 ) );
        }
        else if ( node.property == "at_max_stacks" )
        {
          // buff.X.at_max_stacks → buff.X.stack >= buff.X.max_stack
          node_ptr = make_binop( ">=", make_buff_ident( node, "stack" ),
                                       make_buff_ident( node, "max_stack" ) );
        }
        else if ( node.property == "react" )
        {
          // buff.X.react → buff.X.stack (react is stack minus temporal gating)
          node_ptr = make_buff_ident( node, "stack" );
          node_ptr->original_property = "react";
        }
        else if ( node.property == "stack_pct" || node.property == "react_pct" )
        {
          // buff.X.stack_pct / react_pct → (buff.X.stack % buff.X.max_stack) * 100
          node_ptr = make_binop( "*",
            make_binop( "%", make_buff_ident( node, "stack" ),
                             make_buff_ident( node, "max_stack" ) ),
            make_number( 100 ) );
        }
      }
      break;

    case apl_ast_node_t::UNARY_OP:
      desugar_ast( node.operand );
      break;

    case apl_ast_node_t::BINARY_OP:
      desugar_ast( node.left );
      desugar_ast( node.right );
      break;

    default:
      break;
  }
}

// ============================================================================
// Serialize AST node to JSON
// ============================================================================

void serialize_node( Document& doc, JsonOutput root, const apl_ast_node_t& node )
{
  switch ( node.node_type )
  {
    case apl_ast_node_t::NUMBER:
      root[ "node_type" ] = "number";
      root[ "value" ] = node.value;
      break;

    case apl_ast_node_t::IDENTIFIER:
      root[ "node_type" ] = "identifier";
      root[ "raw" ] = node.raw;
      if ( !node.category.empty() )
        root[ "category" ] = node.category;
      root[ "name" ] = node.name;
      if ( !node.original_name.empty() )
        root[ "original_name" ] = node.original_name;
      if ( !node.property.empty() )
        root[ "property" ] = node.property;
      if ( !node.original_property.empty() )
        root[ "original_property" ] = node.original_property;
      if ( node.spell_id != 0 )
        root[ "spell_id" ] = node.spell_id;
      if ( !node.variable_actions.empty() )
      {
        auto va_arr = root[ "variable_actions" ];
        va_arr.make_array();
        for ( const auto& va : node.variable_actions )
        {
          auto va_obj = va_arr.add();
          va_obj[ "operation" ] = va.operation;
          if ( !va.if_expr.empty() )
          {
            va_obj[ "if_expr" ] = va.if_expr;
            if ( va.if_ast )
              serialize_node( doc, va_obj[ "if_ast" ], *va.if_ast );
          }
          if ( !va.value_expr.empty() )
          {
            va_obj[ "value_expr" ] = va.value_expr;
            if ( va.value_ast )
              serialize_node( doc, va_obj[ "value_ast" ], *va.value_ast );
          }
          if ( !va.condition_expr.empty() )
          {
            va_obj[ "condition_expr" ] = va.condition_expr;
            if ( va.condition_ast )
              serialize_node( doc, va_obj[ "condition_ast" ], *va.condition_ast );
          }
          if ( !va.value_else_expr.empty() )
          {
            va_obj[ "value_else_expr" ] = va.value_else_expr;
            if ( va.value_else_ast )
              serialize_node( doc, va_obj[ "value_else_ast" ], *va.value_else_ast );
          }
        }
      }
      break;

    case apl_ast_node_t::UNARY_OP:
      root[ "node_type" ] = "unary_op";
      root[ "op" ] = node.op;
      if ( node.operand )
        serialize_node( doc, root[ "operand" ], *node.operand );
      break;

    case apl_ast_node_t::BINARY_OP:
      root[ "node_type" ] = "binary_op";
      root[ "op" ] = node.op;
      if ( node.left )
        serialize_node( doc, root[ "left" ], *node.left );
      if ( node.right )
        serialize_node( doc, root[ "right" ], *node.right );
      break;
  }
}

// ============================================================================
// Serialize a single condition expression
// ============================================================================

void serialize_condition( Document& doc, JsonOutput cond_root,
                          util::string_view raw_str, const player_t& player,
                          const std::string& action_name = {} )
{
  cond_root[ "raw" ] = raw_str;
  auto ast = parse_expression_to_ast( raw_str );
  if ( ast )
  {
    if ( !action_name.empty() )
      qualify_action_identifiers( *ast, action_name );
    resolve_and_desugar( ast, player );
    serialize_node( doc, cond_root[ "ast" ], *ast );
  }
}

// ============================================================================
// Match action_priority_t entries to action_t pointers
// ============================================================================

action_t* find_action_for_priority( const action_priority_t& pri,
                                    const player_t& player )
{
  for ( auto* a : player.action_list )
  {
    if ( a->signature == &pri )
      return a;
  }
  return nullptr;
}

// ============================================================================
// Serialize one action entry
// ============================================================================

void serialize_action( Document& doc, JsonOutput action_root,
                       const action_t* action, const action_priority_t& pri,
                       const player_t& player )
{
  action_root[ "raw" ] = pri.action_;
  if ( !pri.comment_.empty() )
    action_root[ "comment" ] = pri.comment_;

  if ( action )
  {
    action_root[ "name" ] = util::string_view( action->name() );
    unsigned sid = action->data().id();
    if ( sid != 0 )
      action_root[ "spell_id" ] = sid;
  }
  else
  {
    // No action_t matched — extract action name from raw string
    auto comma = pri.action_.find( ',' );
    std::string action_name = ( comma != std::string::npos )
                                  ? pri.action_.substr( 0, comma )
                                  : pri.action_;
    action_root[ "name" ] = action_name;
  }

  // Serialize condition expressions
  if ( action )
  {
    std::string act_name = action->name_str;

    auto has_conditions = false;
    if ( !action->option.if_expr_str.empty() ||
         !action->option.target_if_str.empty() ||
         !action->option.interrupt_if_expr_str.empty() ||
         !action->option.early_chain_if_expr_str.empty() ||
         !action->option.cancel_if_expr_str.empty() )
    {
      has_conditions = true;
    }

    if ( has_conditions )
    {
      auto conditions = action_root[ "conditions" ];

      if ( !action->option.if_expr_str.empty() )
        serialize_condition( doc, conditions[ "if" ], action->option.if_expr_str, player, act_name );

      if ( !action->option.target_if_str.empty() )
        serialize_condition( doc, conditions[ "target_if" ], action->option.target_if_str, player, act_name );

      if ( !action->option.interrupt_if_expr_str.empty() )
        serialize_condition( doc, conditions[ "interrupt_if" ], action->option.interrupt_if_expr_str, player, act_name );

      if ( !action->option.early_chain_if_expr_str.empty() )
        serialize_condition( doc, conditions[ "early_chain_if" ], action->option.early_chain_if_expr_str, player, act_name );

      if ( !action->option.cancel_if_expr_str.empty() )
        serialize_condition( doc, conditions[ "cancel_if" ], action->option.cancel_if_expr_str, player, act_name );
    }
  }
}

// ============================================================================
// Serialize player variables
// ============================================================================

void serialize_variables( Document& doc, JsonOutput vars_root, const player_t& player )
{
  for ( const auto* v : player.variables )
  {
    if ( !v )
      continue;

    auto var_obj = vars_root[ v->name_ ];

    var_obj[ "default" ] = v->default_value_;

    auto actions_arr = var_obj[ "actions" ];
    actions_arr.make_array();

    for ( const auto* act : v->variable_actions )
    {
      auto* var_action = dynamic_cast<const variable_t*>( act );
      if ( !var_action )
        continue;

      auto act_obj = actions_arr.add();
      act_obj[ "operation" ] = variable_t::operation_str( var_action->operation );

      if ( !var_action->option.if_expr_str.empty() )
      {
        act_obj[ "if_expr" ] = var_action->option.if_expr_str;
        auto if_ast = parse_expression_to_ast( var_action->option.if_expr_str );
        resolve_and_desugar( if_ast, player );
        if ( if_ast )
          serialize_node( doc, act_obj[ "if_ast" ], *if_ast );
      }

      if ( !var_action->value_str.empty() )
      {
        act_obj[ "value_expr" ] = var_action->value_str;
        auto val_ast = parse_expression_to_ast( var_action->value_str );
        resolve_and_desugar( val_ast, player );
        if ( val_ast )
          serialize_node( doc, act_obj[ "value_ast" ], *val_ast );
      }

      // SETIF: also serialize condition and value_else
      if ( var_action->operation == OPERATION_SETIF )
      {
        if ( !var_action->condition_str.empty() )
        {
          act_obj[ "condition_expr" ] = var_action->condition_str;
          auto cond_ast = parse_expression_to_ast( var_action->condition_str );
          resolve_and_desugar( cond_ast, player );
          if ( cond_ast )
            serialize_node( doc, act_obj[ "condition_ast" ], *cond_ast );
        }

        if ( !var_action->value_else_str.empty() )
        {
          act_obj[ "value_else_expr" ] = var_action->value_else_str;
          auto else_ast = parse_expression_to_ast( var_action->value_else_str );
          resolve_and_desugar( else_ast, player );
          if ( else_ast )
            serialize_node( doc, act_obj[ "value_else_ast" ], *else_ast );
        }
      }
    }
  }
}

}  // anonymous namespace

// ============================================================================
// apl_json::dump — top-level entry point
// ============================================================================

void apl_json::dump( sim_t& sim, const std::string& filename )
{
  Document doc;
  doc.SetObject();
  JsonOutput root( doc, doc );

  root[ "version" ] = "1.0";

  auto players_arr = root[ "players" ];
  players_arr.make_array();

  for ( const auto* player : sim.player_no_pet_list )
  {
    auto p = players_arr.add();

    p[ "name" ] = util::string_view( player->name() );
    p[ "class" ] = util::string_view( util::player_type_string( player->type ) );
    p[ "specialization" ] = util::string_view( util::specialization_string( player->specialization() ) );
    p[ "level" ] = player->true_level;

    auto lists_arr = p[ "action_lists" ];
    lists_arr.make_array();

    for ( const auto* apl : player->action_priority_list )
    {
      auto list_obj = lists_arr.add();
      list_obj[ "name" ] = apl->name_str;
      if ( !apl->action_list_comment_str.empty() )
        list_obj[ "comment" ] = apl->action_list_comment_str;

      auto actions_arr = list_obj[ "actions" ];
      actions_arr.make_array();

      for ( const auto& pri : apl->action_list )
      {
        action_t* act = find_action_for_priority( pri, *player );
        auto act_obj = actions_arr.add();
        serialize_action( doc, act_obj, act, pri, *player );
      }
    }

    // Serialize variable definitions
    if ( !player->variables.empty() )
    {
      serialize_variables( doc, p[ "variables" ], *player );
    }
  }

  // Write JSON to file
  io::cfile fp( filename, "w" );
  if ( !fp )
  {
    throw std::runtime_error( fmt::format( "Failed to open APL JSON output file '{}'.", filename ) );
  }

  std::array<char, 16384> buffer;
  FileWriteStream stream( fp, buffer.data(), buffer.size() );
  PrettyWriter<FileWriteStream> writer( stream );
  doc.Accept( writer );
}
