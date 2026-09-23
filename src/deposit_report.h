// Auto Deposit - the read-only report behind 'deposit why'.
//
// The automatic path says what it did. This says what it saw: every unit in the
// player's container, which page the container walk puts it on, what the game
// answers about it - the "do not move this" flag, the class id, whether that id
// counts as advanced-stash material - and what the SDK says about the same unit,
// because the SDK's answer includes the thing the container walk cannot say:
// which container the item is actually in.
//
// It exists for the run that did nothing and said nothing. A per-item line is
// rationed, and a unit the run never looked at is not counted among the ones it
// did - so an item that is visibly on the inventory grid and is still never
// mentioned is the case where the log cannot tell "the game refused it" from
// "the run never saw it". Placement is what tells those apart, and placement is
// what this reports.
//
// Nothing here moves anything, not even through a call that would have: it is
// safe to run with the inventory exactly as it stands. It is also the one
// command that still answers on a build whose native signatures did not match -
// the SDK half needs none of them and is reported either way.

#pragma once

#include <D2RLPlugin/api.h>

// Queues the report on the game thread, and returns whether it was queued:
// false means there is no game to work in, or a report is already running.
auto ScheduleReport(const D2RL::PluginContext* context) noexcept -> bool;
