// Auto Deposit - the few things more than one module needs: the plugin context,
// the SDK's thread service, and how many "nothing to do" lines reach the log.

#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// How many "the automatic path had nothing to do" lines reach the log before it
// goes quiet.
inline constexpr uint32_t AutoNoopLogLimit = 8;

// Takes one from a budget, or reports that it is spent. The budgets are spent by
// lines that are true but not news, and the rule is the same at every one of
// them: say it while there is room, go quiet after that.
//
// The console's own lines do not come through here. A merge the player typed is
// a deliberate act and answers every time, which is what the `!work.autoMerge ||`
// in front of some of these call sites says.
auto ClaimNoopLog(uint32_t& budget) noexcept -> bool;

// Appends text to a comma-separated list being built in a fixed buffer, keeping
// it terminated. A full buffer truncates: every one of these lists is on its way
// into a log line, where a shortened list is worth more than a corrupted one.
auto AppendList(char* out, size_t capacity, size_t& used, const char* separator, const char* text) noexcept -> void;

// Whether text begins with word and then ends or breaks - the comparison the
// config file and the console both need, so that a search term matches a word
// rather than a prefix of a longer one.
auto MatchWord(const char* text, const char* word) noexcept -> bool;

// Whether a code appears in a list of them. The gem tables and the two ignore
// lists are all this question, and the four answers want to keep agreeing about
// it - so it is asked in one place. The caller masks the code when the list is
// one of the config's, whose entries are three characters wide.
auto ListHasCode(const uint32_t* codes, size_t count, uint32_t code) noexcept -> bool;

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
