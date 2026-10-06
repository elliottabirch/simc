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
// 261005-fight-list (quick 261005-mix, plan 02): the `rl_fight_list=` driver below hands the translog writer
// from entry to entry (rl_translog::carry_t) and empties the RL modules' process-wide caches between entries.
#include "sim/rl_target_select.hpp"
#include "sim/rl_translog.hpp"
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
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

// process_scope_t ==========================================================
//
// 261005-fight-list (quick 261005-mix, plan 02): the once-per-process start-up that sim_t::main used to do inline
// (http cache, API token, dbc::init, class modules, hotfix registration, special effects), extracted into a scope
// object so the `rl_fight_list=` driver below runs it ONCE and then builds one sim_t per fight inside it. The
// members are constructed and destroyed in exactly the order the original local variables were (cache,
// apitoken, [dbc::init, module_t::init, register_hotfixes], special effects; destruction in reverse), so the
// ordinary path executes the same operations in the same order (proof P-OFF in PLAN-A section 6.2).

struct process_data_init_t
{
  process_data_init_t()
  {
    dbc::init();
    module_t::init();
    unique_gear::register_hotfixes();
  }
};

struct process_scope_t
{
  cache_initializer_t cache_init;
  apitoken_initializer_t apitoken_init;
  process_data_init_t data_init;
  special_effect_initializer_t special_effect_init;

  process_scope_t() : cache_init( get_cache_directory() + "/simc_cache.dat" )
  {
  }
};

// The fight-list driver (rl_fight_list=) =====================================
//
// `simc rl_fight_list=<list file>` runs one fight per entry file, each in its OWN fresh sim_t (a sim_t cannot
// be reset or reused: sim.cpp setup() "cannot be repeated or reset"), writing ONE translog (format 15). The
// contract (grammar, refusals, notice lines, hand-over) is docs/FIGHT_LIST.md / PLAN-A section 2 and 3.

constexpr const char* FIGHT_LIST_ARG_PREFIX = "rl_fight_list=";
constexpr const char* FIGHT_LIST_HEADER = "rl-fight-list-v1";

[[noreturn]] void fight_list_refuse( const std::string& what )
{
  throw sc_runtime_error( "rl_fight_list: " + what );
}

struct fight_list_entry_t
{
  bool warmup = false;
  std::string path;
  std::uint32_t crc32 = 0;
  std::uint32_t bytes = 0;
  double parse_ms = 0.0;
  std::unique_ptr<sim_control_t> control;
};

// Options that must read the same in every entry (the driver compares the parsed values): they describe the
// PROCESS (one output file, one net, one lag model), not the fight.
const char* const FIGHT_LIST_PROCESS_OPTIONS[] = {
    "rl_translog",        "solver_policy",         "threads",          "per_source_rng",
    "output",             "queue_lag_stddev",      "gcd_lag_stddev",   "channel_lag_stddev",
    "default_world_lag_stddev", "solver_record_apl_choice" };

// Options refused in any entry, by name prefix (PLAN-A section 2.3). Each exists for one reason: the feature
// keeps process-global state this driver does not carry (solver_branch, rl_buff_*), reads only one process
// (rl_rng_*), or is a one-shot probe (rl_forward_probe, rl_obs_names_out), or would nest a list.
const char* const FIGHT_LIST_REFUSED_PREFIXES[] = {
    "solver_branch", "solver_teacher", "rl_buff_", "rl_rng_", "rl_forward_probe", "rl_obs_names_out", "rl_fight_list" };

// Options refused in any entry, by exact name.
const char* const FIGHT_LIST_REFUSED_NAMES[] = { "solver_control", "decision_dump" };

// The LAST value an entry's options give `name` (later options win in sim_t, as here), or nullptr when absent.
const std::string* fight_list_option( const sim_control_t& control, const std::string& name )
{
  const std::string* found = nullptr;
  for ( const auto& tuple : control.options )
  {
    if ( tuple.name == name )
      found = &tuple.value;
  }
  return found;
}

std::vector<unsigned char> fight_list_read_bytes( const std::string& path, const std::string& what )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in.is_open() )
    fight_list_refuse( fmt::format( "{} '{}' is not readable", what, path ) );
  std::vector<unsigned char> bytes( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
  return bytes;
}

// Sets sim_signal_handler_t::global_sim for the life of one entry and clears it afterwards (also on a throw), so the
// signal handler never dereferences a destroyed sim_t (it already tests for null).
struct fight_list_signal_scope_t
{
  explicit fight_list_signal_scope_t( sim_t* sim ) { sim_signal_handler_t::global_sim = sim; }
  ~fight_list_signal_scope_t() { sim_signal_handler_t::global_sim = nullptr; }
};

double fight_list_ms( std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to )
{
  return std::chrono::duration<double, std::milli>( to - from ).count();
}

std::vector<fight_list_entry_t> fight_list_read_and_validate( const std::string& list_path )
{
  // --- the list file (PLAN-A section 2.2)
  const std::vector<unsigned char> raw = fight_list_read_bytes( list_path, "the list file" );
  std::string text( raw.begin(), raw.end() );
  if ( !text.empty() && text.back() == '\n' )
    text.pop_back();
  std::vector<std::string> lines;
  {
    std::size_t start = 0;
    while ( true )
    {
      const std::size_t nl = text.find( '\n', start );
      if ( nl == std::string::npos )
      {
        lines.push_back( text.substr( start ) );
        break;
      }
      lines.push_back( text.substr( start, nl - start ) );
      start = nl + 1;
    }
  }
  if ( lines.empty() || lines[ 0 ] != FIGHT_LIST_HEADER )
  {
    fight_list_refuse( fmt::format( "the first line of '{}' must be exactly '{}' (got '{}')", list_path,
                                    FIGHT_LIST_HEADER, lines.empty() ? std::string() : lines[ 0 ] ) );
  }

  std::vector<fight_list_entry_t> entries;
  std::size_t fight_lines = 0;
  for ( std::size_t i = 1; i < lines.size(); ++i )
  {
    const std::string& line = lines[ i ];
    const std::size_t line_number = i + 1;  // 1-based file line
    if ( line.empty() )
      fight_list_refuse( fmt::format( "line {} of '{}' is blank (no blank lines are allowed)", line_number, list_path ) );
    if ( line.find( '\r' ) != std::string::npos )
      fight_list_refuse( fmt::format( "line {} of '{}' contains a carriage return", line_number, list_path ) );
    const std::size_t space = line.find( ' ' );
    if ( space == std::string::npos )
    {
      fight_list_refuse( fmt::format( "line {} of '{}' is '{}': expected 'warmup <path>' or 'fight <path>'", line_number,
                                      list_path, line ) );
    }
    const std::string kind = line.substr( 0, space );
    const std::string path = line.substr( space + 1 );
    if ( kind != "warmup" && kind != "fight" )
    {
      fight_list_refuse( fmt::format( "line {} of '{}' starts with '{}': expected 'warmup' or 'fight'", line_number,
                                      list_path, kind ) );
    }
    if ( path.empty() || path.find( ' ' ) != std::string::npos )
    {
      fight_list_refuse( fmt::format( "line {} of '{}': the path '{}' is empty or contains a space (one space, no space "
                                      "inside a path)", line_number, list_path, path ) );
    }
    if ( path[ 0 ] != '/' )
      fight_list_refuse( fmt::format( "line {} of '{}': the path '{}' is not absolute", line_number, list_path, path ) );
    // an '=' would make the option parser read the path as an option, not a file name
    if ( path.find( '=' ) != std::string::npos )
      fight_list_refuse( fmt::format( "line {} of '{}': the path '{}' contains '='", line_number, list_path, path ) );
    if ( kind == "warmup" )
    {
      if ( !entries.empty() )
      {
        fight_list_refuse( fmt::format( "line {} of '{}': a warmup entry is only allowed as the FIRST entry (and at "
                                        "most one)", line_number, list_path ) );
      }
    }
    else
    {
      ++fight_lines;
    }
    fight_list_entry_t entry;
    entry.warmup = kind == "warmup";
    entry.path = path;
    entries.push_back( std::move( entry ) );
  }
  if ( fight_lines == 0 )
    fight_list_refuse( fmt::format( "'{}' has no 'fight' entry", list_path ) );

  // --- every entry file: bytes, CRC, parse, validate (all refusals fire here, before any fight)
  for ( std::size_t k = 0; k < entries.size(); ++k )
  {
    fight_list_entry_t& e = entries[ k ];
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<unsigned char> bytes = fight_list_read_bytes( e.path, fmt::format( "entry {}'s file", k ) );
    e.bytes = static_cast<std::uint32_t>( bytes.size() );
    e.crc32 = rl_translog::crc32( bytes.data(), bytes.size() );

    e.control = std::make_unique<sim_control_t>();
    try
    {
      e.control->options.parse_args( std::vector<std::string>{ e.path } );
    }
    catch ( const std::exception& )
    {
      std::throw_with_nested( std::invalid_argument(
          fmt::format( "rl_fight_list: entry {} ('{}') has an incorrect option format", k, e.path ) ) );
    }
    const sim_control_t& control = *e.control;

    for ( const auto& tuple : control.options )
    {
      for ( const char* prefix : FIGHT_LIST_REFUSED_PREFIXES )
      {
        if ( tuple.name.compare( 0, std::strlen( prefix ), prefix ) == 0 )
        {
          fight_list_refuse( fmt::format( "entry {} ('{}') sets '{}', which a fight list refuses (every option starting "
                                          "'{}' is refused)", k, e.path, tuple.name, prefix ) );
        }
      }
      for ( const char* name : FIGHT_LIST_REFUSED_NAMES )
      {
        if ( tuple.name == name )
        {
          fight_list_refuse( fmt::format( "entry {} ('{}') sets '{}', which a fight list refuses", k, e.path,
                                          tuple.name ) );
        }
      }
    }

    const std::string* iterations = fight_list_option( control, "iterations" );
    if ( iterations == nullptr || *iterations != "1" )
    {
      fight_list_refuse( fmt::format( "entry {} ('{}') must set iterations=1 (got {})", k, e.path,
                                      iterations == nullptr ? std::string( "no iterations= option" )
                                                            : "iterations=" + *iterations ) );
    }
    if ( fight_list_option( control, "solver_policy" ) == nullptr )
      fight_list_refuse( fmt::format( "entry {} ('{}') has no solver_policy= (a fight list plays a net)", k, e.path ) );
    if ( fight_list_option( control, "rl_translog" ) == nullptr )
      fight_list_refuse( fmt::format( "entry {} ('{}') has no rl_translog= (a fight list writes one translog)", k, e.path ) );

    bool has_player = false;
    for ( const auto& tuple : control.options )
    {
      const player_e type = util::parse_player_type( tuple.name );
      if ( tuple.name == "copy" || ( type >= DEATH_KNIGHT && type <= WARRIOR ) )
      {
        has_player = true;
        break;
      }
    }
    if ( !has_player )
      fight_list_refuse( fmt::format( "entry {} ('{}') has no player (nothing to simulate)", k, e.path ) );

    if ( k > 0 )
    {
      const sim_control_t& first = *entries[ 0 ].control;
      for ( const char* name : FIGHT_LIST_PROCESS_OPTIONS )
      {
        const std::string* mine = fight_list_option( control, name );
        const std::string* theirs = fight_list_option( first, name );
        const bool same = ( mine == nullptr && theirs == nullptr ) ||
                          ( mine != nullptr && theirs != nullptr && *mine == *theirs );
        if ( !same )
        {
          fight_list_refuse( fmt::format( "entry {} ('{}') sets {}='{}' but entry 0 sets {}='{}' (process-level options "
                                          "must be identical in every entry)", k, e.path, name,
                                          mine == nullptr ? std::string( "<absent>" ) : *mine, name,
                                          theirs == nullptr ? std::string( "<absent>" ) : *theirs ) );
        }
      }
    }
    e.parse_ms = fight_list_ms( t0, std::chrono::steady_clock::now() );
  }
  return entries;
}

int run_fight_list( const std::vector<std::string>& args )
{
  int8_t exit_code = 0;

  try
  {
    // --- arguments: the list, and nothing else (PLAN-A section 2.1)
    std::string list_path;
    bool have_list = false;
    for ( const std::string& arg : args )
    {
      if ( !have_list && arg.compare( 0, std::strlen( FIGHT_LIST_ARG_PREFIX ), FIGHT_LIST_ARG_PREFIX ) == 0 )
      {
        list_path = arg.substr( std::strlen( FIGHT_LIST_ARG_PREFIX ) );
        have_list = true;
      }
      else
      {
        fight_list_refuse( fmt::format( "the only argument allowed with rl_fight_list= is the list itself (got '{}')",
                                        arg ) );
      }
    }
    if ( list_path.empty() )
      fight_list_refuse( "rl_fight_list= needs the path of a list file" );

    const auto process_start = std::chrono::steady_clock::now();

    process_scope_t process_scope;

    std::vector<fight_list_entry_t> entries = fight_list_read_and_validate( list_path );

    // Hotfixes are applied once, after every entry has been parsed and before the first sim_t is built (the
    // ordinary path applies them right after its single parse, before setup).
    hotfix::apply();

    const std::size_t entry_count = entries.size();
    std::size_t kept_count = 0;
    for ( const auto& e : entries )
      kept_count += e.warmup ? 0 : 1;
    const bool has_warmup = entries[ 0 ].warmup;
    const std::string* translog_value = fight_list_option( *entries[ 0 ].control, "rl_translog" );
    fmt::print( stderr, "[RL_FIGHT_LIST] start entries={} warmup={} translog={} format={}\n", entry_count,
                has_warmup ? 1 : 0, *translog_value, rl_translog::FORMAT_VERSION_FIGHT_LIST );
    std::fflush( stderr );

    rl_translog::carry_t carry;
    std::shared_ptr<rl_policy::rl_weights_t> shared_weights;

    for ( std::size_t k = 0; k < entry_count; ++k )
    {
      fight_list_entry_t& e = entries[ k ];
      const bool last = k + 1 == entry_count;
      const auto t_entry = std::chrono::steady_clock::now();

      std::unique_ptr<sim_t> sim( new sim_t() );
      fight_list_signal_scope_t signal_scope( sim.get() );

      sim->rl_list_active = true;
      sim->rl_list_warmup = e.warmup;
      sim->rl_list_last = last;
      sim->rl_list_index = static_cast<int>( k );
      sim->rl_list_crc32 = e.crc32;
      sim->rl_list_bytes = e.bytes;
      if ( k > 0 )
      {
        rl_translog::adopt( sim.get(), carry );
        sim->rl_list_shared_weights = shared_weights;
      }

      // No process-wide cache may still hold an entry from a previous fight (a recycled player_t / action_t / buff_t
      // address would otherwise be served stale pointers): measured on the freshly built sim_t, THROWN on, then
      // emptied anyway.
      const std::size_t caches_at_start =
          rl_policy::process_cache_entries() + rl_target_select::process_cache_entries();
      if ( caches_at_start != 0 )
      {
        fight_list_refuse( fmt::format( "entry {} ('{}') started with {} stale process-wide cache entries (caches_at_start "
                                        "must be 0)", k, e.path, caches_at_start ) );
      }
      rl_policy::clear_process_caches();
      rl_target_select::clear_process_caches();

      const auto t_setup = std::chrono::steady_clock::now();
      try
      {
        sim->setup( e.control.get() );
      }
      catch ( const std::exception& )
      {
        print_version_info( *sim->dbc );
        fmt::print( "\n" );
        std::throw_with_nested(
            std::runtime_error( fmt::format( "rl_fight_list: entry {} ('{}'): Setup failure", k, e.path ) ) );
      }
      const double setup_ms = fight_list_ms( t_setup, std::chrono::steady_clock::now() );

      if ( sim->canceled )
      {
        fight_list_refuse( fmt::format( "entry {} ('{}') was canceled in setup (nothing to simulate)", k, e.path ) );
      }
      if ( k == 0 )
      {
        shared_weights = sim->solver_policy_weights;
        // the version banner, once (the ordinary path prints it after setup)
        print_version_info( *sim->dbc );
        fmt::print( "\n" );
      }

      sim->report_progress = 0;
      sim->progress_bar.set_base( "Baseline" );

      const auto t_run = std::chrono::steady_clock::now();
      if ( !( sim->execute() && !sim->rethrow_exception_queue() ) )
      {
        fight_list_refuse( fmt::format( "entry {} ('{}') was canceled or failed during the fight", k, e.path ) );
      }
      const double execute_ms = fight_list_ms( t_run, std::chrono::steady_clock::now() );
      const double init_ms = chrono::to_fp_seconds( sim->init_time ) * 1000.0;

      const auto t_close = std::chrono::steady_clock::now();
      // Everything the entry's objects own is emptied while they are still alive, then the sim is destroyed, then
      // the caches are emptied once more (the next entry's first act is to require them empty).
      rl_policy::clear_process_caches();
      rl_target_select::clear_process_caches();
      const unsigned shape = static_cast<unsigned>( sim->rl_fight_shape_index );
      const std::uint32_t tag_setup = sim->rl_tag_setup;
      const std::uint32_t tag_stat = sim->rl_tag_stat;
      const std::uint32_t tag_slot = sim->rl_tag_slot;
      if ( !last )
        rl_translog::release( sim.get(), carry );
      sim.reset();
      rl_policy::clear_process_caches();
      rl_target_select::clear_process_caches();
      const double close_ms = fight_list_ms( t_close, std::chrono::steady_clock::now() );
      const double total_ms = fight_list_ms( t_entry, std::chrono::steady_clock::now() );

      fmt::print( stderr,
                  "[RL_FIGHT_LIST] entry={} kind={} shape={} setup={} stat={} slot={} crc32={:08x} bytes={} "
                  "caches_at_start={} parse_ms={:.1f} setup_ms={:.1f} init_ms={:.1f} run_ms={:.1f} close_ms={:.1f} "
                  "total_ms={:.1f}\n",
                  k, e.warmup ? "warmup" : "fight", shape, tag_setup, tag_stat, tag_slot, e.crc32, e.bytes,
                  caches_at_start, e.parse_ms, setup_ms, init_ms, std::max( 0.0, execute_ms - init_ms ), close_ms,
                  total_ms + e.parse_ms );
      std::fflush( stderr );
    }

    fmt::print( stderr, "[RL_FIGHT_LIST] done entries={} kept={} wall_ms={:.1f}\n", entry_count, kept_count,
                fight_list_ms( process_start, std::chrono::steady_clock::now() ) );
    std::fflush( stderr );
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

} // anonymous namespace ====================================================

// sim_t::main ==============================================================

int sim_t::main( const std::vector<std::string>& args )
{
  int8_t exit_code = 0;

  try
  {
    // 261005-fight-list: the once-per-process start-up, same operations in the same order as before it was
    // extracted into process_scope_t (see its comment).
    process_scope_t process_scope;

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

  const io::utf8_args args( argc, argv );

  // 261005-fight-list (quick 261005-mix, plan 02): `rl_fight_list=<list>` runs one fight per entry file in one
  // process (see run_fight_list). Any other invocation falls through to the unchanged single-sim path.
  for ( const std::string& arg : args )
  {
    if ( arg.compare( 0, std::strlen( FIGHT_LIST_ARG_PREFIX ), FIGHT_LIST_ARG_PREFIX ) == 0 )
      return run_fight_list( args );
  }

  sim_t sim;
  sim_signal_handler_t::global_sim = &sim;

  return sim.main( args );
}
