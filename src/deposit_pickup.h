// Auto Deposit - the pickup hook, and the queue that carries what it saw to the
// two halves.
//
// The routine the game runs when something on the ground is picked up. It fires
// on the player's own click to pick up, not only on an auto-pickup path, which
// is what Auto Belt Refill's comment claimed for this address and what the first
// run of this hook disproved - so it is the pickup event the SDK does not have.
//
//   bool __fastcall(void* player, uint32_t guid, bool, uint32_t, bool, bool)
//
// A pickup is not the same event as the item's arrival. The game's pickup
// routine returns as soon as the click is accepted, and the item turns up a
// moment later - so the id is written down and looked for, rather than assumed
// to be there. That also means a burst of pickups is not limited to one at a
// time: the queue carries all of them and the pump takes them in batches.
//
// Nothing in this file reads, moves or creates an item. It is not the place for
// it - the hook runs inside the game's own routine - so both jobs are queued and
// run later on the game thread, which is the only place item mutation is
// supported.

#pragma once

#include <D2RLPlugin/api.h>

// Patches the pickup routine, behind a 32-byte signature checked before anything
// is written. A build whose bytes do not match costs the feature, not the game:
// nothing is patched and the log says so.
//
// False means the hook is not in place, for one of those two reasons.
auto InstallPickupHook(const D2RL::PluginContext* context) noexcept -> bool;

// Starts the pump that drains the pickup queue, if it is not already running.
// Called once at load, so the queue is being drained before the first pickup
// rather than only from it, and again after a failed restart.
auto StartPump(const D2RL::PluginContext* context) noexcept -> bool;
