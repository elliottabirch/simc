// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "class_modules/class_module.hpp"
#include "dbc/dbc.hpp"
#include "dbc/spell_query/spell_data_expr.hpp"
#include "interfaces/bcp_api.hpp"
#include "interfaces/sc_http.hpp"
#include "lib/fmt/format.h"
#include "player/player.hpp"
#include "player/unique_gear.hpp"
#include "report/reports.hpp"
#include "sim/plot.hpp"
#include "sim/reforge_plot.hpp"
#include "sim/profileset.hpp"
#include "sim/apl_json.hpp"
// Phase 222, plan 222-04 (NET-02's cross-path receipt, rl_forward_probe).
// sim.hpp deliberately does NOT include rl_policy.hpp (only forward-declares
// the namespace) -- see sim.hpp's own comment beside that forward
// declaration -- so this TU includes it directly, the same way
// solver_control.cpp does.
#include "sim/rl_policy.hpp"
#include "sim/sim.hpp"
#include "sim/scale_factor_control.hpp"
#include "sim/sim_control.hpp"
#include "util/git_info.hpp"
#include "util/io.hpp"

// Phase 222, plan 222-04 (rl_forward_probe): <cmath> for std::isfinite,
// <cstdlib> for std::strtof (istream's own float extraction fails on
// "nan"/"inf" and clamps overflow to a finite value on this libstdc++ --
// verified this session -- so strtof is what makes the non-finite refusal
// reachable), <fstream> for the probe's plain-text input file.
// 260920-cvf stage B, Task 1: <chrono> for std::chrono::steady_clock, used
// only by the rl_forward_probe_repeats timing loop below.
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <locale>

#ifdef SC_SIGACTION
#include <csignal>
#include <utility>
#endif

namespace { // anonymous namespace ==========================================

#ifdef SC_SIGACTION
// POSIX-only signal handler ================================================

struct sim_signal_handler_t
{
  static sim_t* global_sim;

  static void report( int signal )
  {
    const char* name = strsignal( signal );
    fmt::print( stderr, "sim_signal_handler: {}!", name );
    const sim_t* crashing_child = nullptr;

#ifndef SC_NO_THREADING
    if ( signal == SIGSEGV )
    {
      for ( auto child : global_sim -> children )
      {
        if ( std::this_thread::get_id() == child ->  thread_id() )
        {
          crashing_child = child;
          break;
        }
      }
    }
#endif

    if ( crashing_child )
    {
      fmt::print(stderr, " Thread={} Iteration={} Seed={} ({}) TargetHealth={}\n",
          crashing_child -> thread_index, crashing_child -> current_iteration, crashing_child -> seed,
          ( crashing_child -> seed + crashing_child -> thread_index ),
          crashing_child -> target -> resources.initial[ RESOURCE_HEALTH ]);
    }
    else
    {
      fmt::print(stderr, " Iteration={} Seed={} TargetHealth={}\n",
          global_sim -> current_iteration, global_sim -> seed,
          global_sim -> target -> resources.initial[ RESOURCE_HEALTH ]);
    }

    auto profileset = global_sim -> profilesets->current_profileset_name();
    if ( ! profileset.empty() )
    {
      fmt::print( stderr, " ProfileSet={}", profileset );
    }

    fmt::print( stderr, "\n" );
    std::fflush( stderr );
  }

  static void sigint( int signal )
  {
    if ( global_sim )
    {
      report( signal );
      if( global_sim -> scaling -> calculate_scale_factors || global_sim -> reforge_plot -> current_reforge_sim || global_sim -> plot -> current_plot_stat != STAT_NONE )
      {
        global_sim -> cancel();
      }
      else if ( global_sim -> single_actor_batch )
      {
        global_sim -> cancel();
      }
      else if ( ! global_sim -> profileset_map.empty() )
      {
        global_sim -> cancel();
      }
      else
      {
        global_sim -> interrupt();
      }
    }
  }

  static void sigsegv( int signal )
  {
    if ( global_sim )
    {
      report( signal );
    }
    exit( signal );
  }

  sim_signal_handler_t()
  {
    assert ( ! global_sim );

    struct sigaction sa;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = 0;

    sa.sa_handler = sigsegv;
    sigaction( SIGSEGV, &sa, nullptr );

    sa.sa_handler = sigint;
    sigaction( SIGINT,  &sa, nullptr );
  }

  ~sim_signal_handler_t()
  { global_sim = nullptr; }
};
#else
struct sim_signal_handler_t
{
  static sim_t* global_sim;
};
#endif

sim_t* sim_signal_handler_t::global_sim = nullptr;

[[maybe_unused]] sim_signal_handler_t handler;

// need_to_save_profiles ====================================================

bool need_to_save_profiles( sim_t* sim )
{
  if ( sim->save_profiles )
  {
    return true;
  }

  for ( auto& player : sim->player_list )
  {
    if ( !player->report_information.save_str.empty() )
    {
      return true;
    }
  }

  return false;
}

/* Obtain a platform specific place to store the http cache file
 */
std::string get_cache_directory()
{
  std::string s = ".";

  const char* env; // store desired environemental variable in here. getenv returns a null pointer if specified
  // environemental variable cannot be found.
#if defined(__linux__) || defined(__APPLE__)
  env = getenv( "XDG_CACHE_HOME" );
  if ( ! env )
  {
    env = getenv( "HOME" );
    if ( env ) {
      s = std::string( env ) + "/.cache";
    } else {
      s = "/tmp"; // back out
}
  }
  else {
    s = std::string( env );
}
#endif
#ifdef _WIN32
#pragma warning( push )
  // Disable security warning
#pragma warning( disable : 4996 )
  env = std::getenv( "TMP" );
  if ( !env )
  {
    env = std::getenv( "TEMP" );
    if ( ! env )
    {
      env = std::getenv( "HOME" );
    }
  }
  s = std::string( env );
#pragma warning( pop )
#endif

  return s;
}

// RAII-wrapper for http cache load / save
struct cache_initializer_t {
  cache_initializer_t( std::string  fn ) :
    _file_name(std::move( fn ))
  { http::cache_load( _file_name ); }
  ~cache_initializer_t()
  { http::cache_save( _file_name ); }
private:
  std::string _file_name;
};

struct apitoken_initializer_t
{
  apitoken_initializer_t()
  { bcp_api::token_load(); }

  ~apitoken_initializer_t()
  { bcp_api::token_save(); }
};

struct special_effect_initializer_t
{
  special_effect_initializer_t()
  {
    unique_gear::register_special_effects();
    unique_gear::sort_special_effects();
  }

  ~special_effect_initializer_t()
  { unique_gear::unregister_special_effects(); }
};

void print_version_info( const dbc_t& dbc )
{
  fmt::print( "{}", util::version_info_str( &dbc ) );
}

void print_build_info( const dbc_t& dbc, int display_level )
{
  fmt::print( " ({})", util::build_info_str( &dbc, display_level ) );
}

} // anonymous namespace ====================================================

// sim_t::main ==============================================================

int sim_t::main( const std::vector<std::string>& args )
{
  int8_t exit_code = 0;

  try
  {
    cache_initializer_t cache_init( get_cache_directory() + "/simc_cache.dat" );
    apitoken_initializer_t apitoken_init;
    dbc::init();
    module_t::init();
    unique_gear::register_hotfixes();

    special_effect_initializer_t special_effect_init;

    sim_control_t control;

    try
    {
      control.options.parse_args( args );
    }
    catch ( const std::exception& )
    {
      print_version_info( *dbc );
      fmt::print( "\n" );
      std::throw_with_nested( std::invalid_argument( "Incorrect option format" ) );
    }

    // Hotfixes are applies right before the sim context (control) is created, and simulator setup begins
    hotfix::apply();

    try
    {
      setup( &control );
    }
    catch ( const std::exception& )
    {
      print_version_info( *dbc );
      fmt::print( "\n" );
      std::throw_with_nested( std::runtime_error( "Setup failure" ) );
    }

    // print version info if it hasn't been displayed already
    print_version_info( *dbc );

    if ( display_build > 0 )
      print_build_info( *dbc, display_build );

    // newline as print_version_info does not include it
    fmt::print( "\n" );

    if ( display_build > 1 )
      return 0;

    if ( display_hotfixes )
    {
      fmt::print( "{}", hotfix::to_str( dbc->ptr ) );
      return 0;
    }

    if ( display_bonus_ids )
    {
      fmt::print( "{}", dbc::bonus_ids_str( *dbc ) );
      return 0;
    }

    if ( canceled )
    {
      return 1;
    }

    if ( spell_query )
    {
      try
      {
        spell_query->evaluate();
        print_spell_query();
      }
      catch ( const std::exception& )
      {
        std::throw_with_nested( std::runtime_error( "Spell query error" ) );
      }
    }
    else if ( !apl_json_file_str.empty() )
    {
      try
      {
        init();
        apl_json::dump( *this, apl_json_file_str );
      }
      catch ( const std::exception& )
      {
        std::throw_with_nested( std::runtime_error( "APL JSON generation" ) );
      }
    }
    // Phase 222, plan 222-04 (NET-02's cross-path receipt). Modelled on the
    // apl_json branch immediately above but WITHOUT init(): the weights are
    // already loaded+validated in setup()'s solver_policy= fail-closed
    // block, and the probe needs only RL_OBS_DIM/RL_ACTION_DIM from the
    // generated header -- no player init, no fight. Tested BEFORE
    // need_to_save_profiles so it is reachable; sim.cpp's amended "Nothing
    // to sim!" predicate lets this branch run with ZERO players configured.
    else if ( !rl_forward_probe_str.empty() )
    {
      try
      {
        if ( !solver_policy_weights )
        {
          throw sc_runtime_error(
              "rl_forward_probe: no solver_policy= given -- there are no weights to run" );
        }
        if ( rl_forward_probe_out_str.empty() )
        {
          throw sc_runtime_error( "rl_forward_probe: rl_forward_probe_out= must also be given" );
        }

        std::ifstream probe_in( rl_forward_probe_str );
        if ( !probe_in.is_open() )
        {
          throw sc_runtime_error( fmt::format(
              "rl_forward_probe: could not open input file '{}'", rl_forward_probe_str ) );
        }

        // P-13: the IN file is plain whitespace-separated TEXT tokens --
        // line 1 is RL_OBS_DIM floats, line 2 is RL_ACTION_DIM values in
        // {0, 1} -- read with ONE std::ifstream loop (obs first, then
        // mask), so the probe adds no JSON parser to the fork. Tokens are
        // read as strings and parsed with std::strtof rather than
        // istream's own float extraction: libstdc++'s operator>>(float&)
        // FAILS extraction outright on "nan"/"inf" and CLAMPS an
        // overflowing literal to a finite FLT_MAX rather than producing an
        // actual non-finite value (verified this session with a standalone
        // probe) -- so a raw `>>` into a float can never reach the
        // isfinite() refusal below at all. strtof DOES parse "nan"/"inf"
        // per the C standard, which is what makes that refusal reachable
        // and testable. A shortfall during the first RL_OBS_DIM
        // extractions is attributable to line 1; a shortfall during the
        // next RL_ACTION_DIM is attributable to line 2.
        float probe_obs[ RL_OBS_DIM ];
        for ( std::size_t i = 0; i < RL_OBS_DIM; ++i )
        {
          std::string tok;
          if ( !( probe_in >> tok ) )
          {
            throw sc_runtime_error( fmt::format(
                "rl_forward_probe: input file '{}' line 1 has fewer than RL_OBS_DIM={} values "
                "(failed at value {})",
                rl_forward_probe_str, RL_OBS_DIM, i ) );
          }
          char* endptr = nullptr;
          const float v = std::strtof( tok.c_str(), &endptr );
          if ( endptr == tok.c_str() || *endptr != '\0' )
          {
            throw sc_runtime_error( fmt::format(
                "rl_forward_probe: input file '{}' line 1 value '{}' at index {} is not a valid "
                "number",
                rl_forward_probe_str, tok, i ) );
          }
          if ( !std::isfinite( v ) )
          {
            throw sc_runtime_error( fmt::format(
                "rl_forward_probe: input file '{}' observation value '{}' at index {} is not "
                "finite",
                rl_forward_probe_str, tok, i ) );
          }
          probe_obs[ i ] = v;
        }

        std::uint8_t probe_mask[ RL_ACTION_DIM ];
        for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
        {
          std::string tok;
          if ( !( probe_in >> tok ) )
          {
            throw sc_runtime_error( fmt::format(
                "rl_forward_probe: input file '{}' line 2 has fewer than RL_ACTION_DIM={} values "
                "(failed at value {})",
                rl_forward_probe_str, RL_ACTION_DIM, i ) );
          }
          if ( tok != "0" && tok != "1" )
          {
            throw sc_runtime_error( fmt::format(
                "rl_forward_probe: input file '{}' mask value '{}' at index {} must be 0 or 1",
                rl_forward_probe_str, tok, i ) );
          }
          probe_mask[ i ] = static_cast<std::uint8_t>( tok == "1" ? 1 : 0 );
        }

        std::string probe_trailing;
        if ( probe_in >> probe_trailing )
        {
          throw sc_runtime_error( fmt::format(
              "rl_forward_probe: input file '{}' has more than RL_OBS_DIM={} + RL_ACTION_DIM={} "
              "values",
              rl_forward_probe_str, RL_OBS_DIM, RL_ACTION_DIM ) );
        }

        float probe_q[ RL_ACTION_DIM ];
        // 260920-cvf stage B, Task 1: rl_forward_probe_repeats= (default 1)
        // repeats the SAME inputs N times in a tight loop and times it with
        // steady_clock. N=1 is byte-identical to the pre-existing single
        // call below (out_json always reports the LAST call's Q, same as
        // today). The stderr readout is the real per-decision cost of the
        // production forward (LayerNorm included), not a microbenchmark
        // harness guess.
        if ( rl_forward_probe_repeats <= 1 )
        {
          rl_policy::forward( *solver_policy_weights, probe_obs, probe_mask, probe_q );
        }
        else
        {
          const auto probe_repeat_start = std::chrono::steady_clock::now();
          for ( int rep = 0; rep < rl_forward_probe_repeats; ++rep )
          {
            rl_policy::forward( *solver_policy_weights, probe_obs, probe_mask, probe_q );
          }
          const auto probe_repeat_end = std::chrono::steady_clock::now();
          const double probe_total_us =
              std::chrono::duration<double, std::micro>( probe_repeat_end - probe_repeat_start )
                  .count();
          fmt::print( stderr, "rl_forward_probe: N={} total_us={:.3f} us_per_forward={:.6f}\n",
                      rl_forward_probe_repeats, probe_total_us,
                      probe_total_us / rl_forward_probe_repeats );
        }
        const int probe_argmax = rl_policy::masked_argmax( probe_q, probe_mask );

        // NET-01: hidden_sizes is read through the SAME derived
        // hidden_layer_count() helper load_rlw1/forward already validated
        // the blob against -- never re-derived a third time here.
        const std::size_t n_hidden = rl_policy::hidden_layer_count(
            solver_policy_weights->body, solver_policy_weights->layers.size() );
        std::string hidden_sizes_json = "[";
        for ( std::size_t i = 0; i < n_hidden; ++i )
        {
          if ( i > 0 )
            hidden_sizes_json += ",";
          hidden_sizes_json += fmt::format( "{}", solver_policy_weights->layers[ i ].out_features );
        }
        hidden_sizes_json += "]";

        std::string q_json = "[";
        for ( std::size_t i = 0; i < RL_ACTION_DIM; ++i )
        {
          if ( i > 0 )
            q_json += ",";
          q_json += fmt::format( "{:.9g}", probe_q[ i ] );
        }
        q_json += "]";

        const std::string out_json = fmt::format(
            "{{\"obs_dim\":{},\"action_dim\":{},\"body\":\"{}\",\"n_layers\":{},"
            "\"hidden_sizes\":{},\"n_input_slots\":{},\"allowed_actions\":{},\"q\":{},"
            "\"argmax\":{}}}\n",
            RL_OBS_DIM, RL_ACTION_DIM, rl_policy::body_name( solver_policy_weights->body ),
            solver_policy_weights->layers.size(), hidden_sizes_json,
            solver_policy_weights->input_slots.size(), solver_policy_weights->allowed_actions,
            q_json, probe_argmax );

        io::ofstream probe_out;
        probe_out.open( rl_forward_probe_out_str );
        if ( !probe_out.is_open() )
        {
          throw sc_runtime_error( fmt::format(
              "rl_forward_probe: could not open output file '{}'", rl_forward_probe_out_str ) );
        }
        probe_out << out_json;
      }
      catch ( const std::exception& )
      {
        std::throw_with_nested( std::runtime_error( "rl_forward_probe" ) );
      }
    }
    else if ( need_to_save_profiles( this ) )
    {
      try
      {
        init();
        fmt::print( "\nGenerating profiles... \n" );
        report::print_profiles( this );
      }
      catch ( const std::exception& )
      {
        std::throw_with_nested( std::runtime_error( "Generating profiles" ) );
      }
    }
    else
    {
      fmt::print(
        "\nSimulating... ( iterations={}, threads={}, target_error={:.3f}, max_time={:.0f}, "
        "vary_combat_length={:0.2f}, optimal_raid={}, fight_style={} )\n\n",
        iterations, threads, target_error, max_time.total_seconds(), vary_combat_length, optimal_raid, fight_style );

      progress_bar.set_base( "Baseline" );

      if ( execute() && !rethrow_exception_queue() )
      {
        scaling->analyze();
        plot->analyze();
        reforge_plot->analyze();

        if ( canceled == 0 && !profilesets->iterate( this ) )
          canceled = true;
        else
          report::print_suite( this );
      }
      else
      {
        fmt::print( "Simulation was canceled.\n" );
        canceled = true;
      }
    }

    exit_code = canceled;
  }
  catch ( const sc_exception& e )
  {
    exit_code = 1;
    fmt::print( stderr, "Error: " );
    util::print_chained_exception( e, stderr, exit_code );
    fmt::print( stderr, "\n" );
  }
  catch ( const std::exception& e )
  {
    exit_code = 1;
    fmt::print( stderr, "Error: " );
    util::print_chained_exception( e, stderr, exit_code );
    fmt::print( stderr, "\n" );
  }

  return exit_code;
}

// ==========================================================================
// MAIN
// ==========================================================================

int main( int argc, char** argv )
{
  std::locale::global( std::locale( "C" ) );

  sim_t sim;
  sim_signal_handler_t::global_sim = &sim;

  return sim.main( io::utf8_args( argc, argv ) );
}
