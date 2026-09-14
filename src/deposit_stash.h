// Auto Deposit - one deposit run: what it was asked for, what it did, and what it
// said about it.
//
// The stash half has no SDK route at all. The SDK exposes no way to move an item
// into the advanced stash, so the game's own routines are called directly, by
// address, on the game thread. That is what deposit_native is for; this file is
// the decision above it - which items a run is even about, and what to say after.
//
// Runs are queued, never called inline. A run reads the whole item container and
// then moves things in it, and neither is something to do from inside a hook or
// from the console's own thread.

#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// What one run was asked to do. A pickup names the ids it saw arrive; the
// console command that sweeps the inventory names nothing and takes the grid.
struct DepositWork {
	// How many pickups one run can carry. The pump takes them in batches; the
	// rest keep their place in the queue and go in the next tick.
	static constexpr size_t MaxBatch = 8;

	uint32_t ids[MaxBatch] {};
	size_t   idCount  = 0;
	// Set by the console sweep, which means "everything on the inventory grid"
	// rather than "these ids".
	bool     sweepAll = false;
};

// The run itself. Must be called on the game thread.
//
// The automatic path stays out of the way while the shared stash panel is open -
// see StashIsOpen - and a run that came from a pickup is the automatic path. The
// console sweep sets sweepAll and is exempt, because the player asked.
void RunDeposit(const D2RL::PluginContext* context, const DepositWork& work) noexcept;

// Queues a sweep of the whole inventory grid. The console asks for this; the
// pickup path never does, because a pickup that takes the whole inventory would
// bank things the player put there on purpose.
auto ScheduleSweep(const D2RL::PluginContext* context) noexcept -> bool;
