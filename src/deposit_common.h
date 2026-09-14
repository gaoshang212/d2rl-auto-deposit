// Auto Deposit - the few things more than one module needs: the plugin context,
// the SDK's thread service, and how many "nothing to do" lines reach the log.

#pragma once

#include <D2RLPlugin/api.h>

#include <cstdint>

// How many "the automatic path had nothing to do" lines reach the log before it
// goes quiet.
inline constexpr uint32_t AutoNoopLogLimit = 8;

// How many "the auto merge found nothing to do" lines have been written. The
// merge the console command runs is a deliberate act, so it says so every time;
// the one the pickup hook triggers is not, and a line per stone picked up would
// bury the log. The first few are kept, because whether the gem is in the
// inventory by the time the sweep runs is exactly what those lines answer.
extern uint32_t g_gemBagNoopLogs;

// The same idea for the stash half, which has its own reason to be quiet: most
// of what a player picks up is not advanced-stash material, and that is not news.
//
// Two budgets rather than one. The per-item lines - this one stays put, that one
// was blocked - are the ones that would bury the log, so they go quiet after a
// few. The two lines that explain a run which did nothing at all - the panel was
// open, the item never arrived - are rare, are the ones a player actually wants,
// and must not be spent by an hour of picking up boots.
extern uint32_t g_stashNoopLogs;
extern uint32_t g_stashExplainLogs;

// The context the loader handed over at load, and takes away at unload. Every
// module reaches the game through it, and the pickup hook - which runs on the
// game's own thread, in the middle of its pickup routine - checks it before
// touching anything, because queued work can outlive the load that posted it.
extern const D2RL::PluginContext* g_context;

// The SDK's thread service, or nullptr when it is missing or too old to have the
// fields this plugin uses. The only place item work may be queued from.
auto ThreadServiceOf(const D2RL::PluginContext* context) noexcept -> const D2RL::ThreadServiceV1*;
