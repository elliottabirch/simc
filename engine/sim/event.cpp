// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "event.hpp"
#include "player/actor.hpp"
#include "sim.hpp" // replace with event manager dependency
#include "sim/rl_buff_ledger.hpp"
#include "sim/rl_rng_record.hpp"

// ==========================================================================
// Event
// ==========================================================================

event_t::event_t( sim_t& s, actor_t* a )
  : _sim( s ),
    next( nullptr ),
    time( 0_ms ),
    reschedule_time( no_reschedule ),
    id( 0 ),
    canceled( false ),
    recycled( false ),
    scheduled( false )
#ifdef ACTOR_EVENT_BOOKKEEPING
    ,
    actor( a )
#endif
{
#ifndef ACTOR_EVENT_BOOKKEEPING
  (void)a;
#endif
}

event_t::event_t( actor_t& a ) : event_t( *a.sim, &a )
{
}

timespan_t event_t::remains() const
{ return occurs() - _sim.event_mgr.current_time; }

rng::rng_t& event_t::rng()
{ return sim().rng(); }

/// Placement-new operator for creating events. Do not use in user-code.
void* event_t::operator new( std::size_t size, sim_t& sim )
{
  // 261001-bac plan 12 (MJ-04): an event made inside a ledger pass gets memory the event manager never sees (one bool test).
  if ( sim.rl_bl_shadow )
    return rl_buff_ledger::scratch_event_block( &sim, size );
  return sim.event_mgr.allocate_event( size );
}

void event_t::reschedule( timespan_t delta_time )
{
  // 261001-bac plan 12 (MJ-04): inside a ledger pass a counted no-op (the event keeps its time and id).
  if ( _sim.rl_bl_shadow )
    return rl_buff_ledger::blocked( &_sim, "event.reschedule" );

  // Study-only swing trace/pin (tstl-sylvanas phase 257, plan 257-09, D-18, owner ruling A-125):
  // observes/overrides ONLY the one designated player's own mh/oh execute_event -- a cheap
  // passthrough for every other event the moment the option is inactive (root->rl_rng_recorder
  // null) or `this` names neither hand (rl_rng_record.cpp's own identity check). Runs at the TOP
  // of this function, before delta_time is consumed below, so a pin override lands on the SAME
  // reschedule operation the engine was already performing -- the operation itself (and so
  // every event id, assigned right after) is always the engine's own.
  delta_time = rl_rng_record::swing_event_rescheduled( &_sim, this, delta_time );

  id = ++_sim.event_mgr.global_event_id;
  delta_time += _sim.event_mgr.current_time;

  if ( _sim.debug )
  {
    if ( reschedule_time == no_reschedule )
    {
      _sim.print_debug("Rescheduling event {} from {} to {}",
          *this, time, delta_time );
    }
    else
    {
      _sim.print_debug( "Adjusting reschedule of event {} from {} to {} time={}",
          *this, reschedule_time, delta_time, time );
    }
  }

  reschedule_time = delta_time;
}

// event_t::cancel ==========================================================

void event_t::cancel( event_t*& e )
{
  if ( !e )
    return;

  // 261001-bac plan 12 (MJ-04): inside a ledger pass a counted no-op (the event stays as it is, the pointer is not cleared).
  if ( e->_sim.rl_bl_shadow )
    return rl_buff_ledger::blocked( &e->_sim, "event.cancel" );

  // Study-only swing trace/pin (tstl-sylvanas phase 257, plan 257-09, D-18): a cheap passthrough
  // for every event but the one designated player's own mh/oh execute_event -- observes the
  // cancel (a `cancel` row/state transition), never overrides it (a cancel has no time argument
  // to override). Runs BEFORE `e->canceled` is set below, for a non-null, not-yet-canceled event.
  if ( !e->canceled )
    rl_rng_record::swing_event_canceled( &e->_sim, e );

#ifdef ACTOR_EVENT_BOOKKEEPING
  if ( e->_sim.debug && e->actor && !e->canceled )
  {
    e->actor->event_counter--;
    if ( e->actor->event_counter < 0 )
    {
      fmt::print(stderr, "event_t::cancel assertion error: e -> player -> events < 0. event '{}' from '{}'.\n", e->name(), e->actor->name());
      assert( false );
    }
  }
#endif

  e->canceled = true;
  e           = nullptr;
}

void sc_format_to( const event_t& e, fmt::format_context::iterator out )
{
  fmt::format_to( out, "{}(#{})", e.name(), e.id );
}
