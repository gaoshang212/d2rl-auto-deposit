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

// True when text starts with this word and the word ends there: "on" must not
// match "only", and a trailing comment is fine after it. A quote ends a word
// too, so a value that is wrapped in them reads the same as a bare one.
//
// The console command matches its own words with this: 'deposit bag' must not
// match a word that merely starts with "bag".
auto MatchWord(const char* text, const char* word) noexcept -> bool;

// The routing, and the two halves' reasons to leave something alone.
//
// A gem belongs in the bag, so while the bag half is switched on its codes are
// added to the stash half's ignore set here. That is what keeps the two
// destinations from ever wanting the same item.
auto IsGemCode(uint32_t code) noexcept -> bool;
auto IsClassicGemCode(uint32_t code) noexcept -> bool;
auto GemBagIgnores(uint32_t code) noexcept -> bool;
auto StashIgnores(uint32_t code) noexcept -> bool;
