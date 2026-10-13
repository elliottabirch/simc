// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#pragma once

#include "config.hpp"
#include "player/player.hpp"
#include "player/player_event.hpp"
#include "sim/rl_rng_record.hpp"
#include "sim/sim.hpp"
#include "util/timespan.hpp"

/* Event which will demise the player
 * - Reason for it are that we need to finish the current action ( eg. a dot tick ) without
 * killing off target dependent things ( eg. dot state ).
 */
struct player_demise_event_t : public player_event_t
{
  player_demise_event_t(player_t& p, timespan_t delta_time = timespan_t::zero() /* Instantly kill the player */) :
    player_event_t(p, delta_time)
  {
    p.demise_event = this;
    if (sim().debug)
      sim().out_debug.printf("New Player-Demise Event: %s", p.name());
  }
  virtual const char* name() const override
  {
    return "Player-Demise";
  }
  virtual void execute() override
  {
    p()->demise_event = nullptr;
    // 288.9: everything the demise does, including a class module's override after the base, is caused by the unit going away.
    rl_rng_record::cause_scope_t rl_demise_cause( &sim(), nullptr, rl_rng_record::FAMILY_NONE, rl_rng_record::CAUSE_DEMISE );
    p()->demise();
  }
};