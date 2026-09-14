// Auto Deposit - the config file: three switches and two ignore lists.
//
// Everything is on unless the file says otherwise, and the file says otherwise
// only by naming codes to leave alone. Read once, at load, and never written
// again - so the game thread can read the switches without a lock.

#pragma once

#include <D2RLPlugin/api.h>

#include <cstdint>

// Read the config D2RLoader wrote out of the copy embedded in this DLL, and say
// what was in it. Called once, at load, before the pickup hook goes in.
auto LoadConfig(const D2RL::PluginContext* context) noexcept -> void;

// The switches, as the config file left them.
extern bool g_gemBagMergeGems;
extern bool g_gemBagMergeClusters;
extern bool g_stashDeposit;

// True when every automatic path is switched off, which is the config saying
// there is nothing for this plugin to do on its own. Asked once, at load: it is
// what decides whether the pickup hook is worth installing at all, so the set of
// switches it covers has to grow with the switches themselves rather than being
// spelled out again at the call site.
auto AnyAutomaticWorkEnabled() noexcept -> bool;

// The routing, and the two halves' reasons to leave something alone.
//
// A gem belongs in the bag, so while the bag half is switched on its codes are
// added to the stash half's ignore set here. That is what keeps the two
// destinations from ever wanting the same item.
auto IsGemCode(uint32_t code) noexcept -> bool;
auto IsClassicGemCode(uint32_t code) noexcept -> bool;
auto GemBagIgnores(uint32_t code) noexcept -> bool;
auto StashIgnores(uint32_t code) noexcept -> bool;
