// Auto Deposit - the game, called by address.
//
// Three things live here: the guarded calls themselves, what each one concluded,
// and the one read of the player's item container that a run starts from.
// Nothing here decides policy - that is deposit_config - and exactly one function
// here moves anything, DepositNative.
//
// Every entry point is wrapped in __try. A native call that raises is a bug in
// this plugin's addresses, not a reason to take the game down, so a fault comes
// back as a verdict or a false.

#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// What one call concluded. Faulted is the one to be suspicious of: it means a
// native call raised an exception rather than returning, which is the outcome
// the __try blocks exist to turn into a log line.
enum class Verdict : int {
	Faulted = 0,  // a native call raised, or there is no player to deposit for
	Blocked,      // the game flags this item as one it will not move
	NotMaterial,  // the game does not count this among advanced-stash materials
	Ignored,      // a code this plugin's own lists leave alone
	NoTarget,     // the player has no advanced-stash unit to deposit into
	Deposited,    // StashDeposit was called
};

auto VerdictName(Verdict verdict) noexcept -> const char*;

// Everything about one item that the run decides on. Read-only: filling this in
// calls nothing that changes anything, and nothing that costs more than the read
// the walk already did.
//
// StashItemOk is deliberately not asked here. It is asked in DepositNative, in
// the same breath as the move, because the gap between reading and moving is
// where a stale answer would come from and the call is not expensive.
struct ItemFacts {
	uint32_t id      = 0;
	uint32_t code    = 0;
	uint8_t  page    = 0;
	bool     hasData = false;
};

// The value ItemFacts::page reads as for the inventory grid. Where in the data
// block that byte sits is in the signature table - nothing outside
// deposit_native.cpp reads it - so only the value it is compared against is
// needed here.
//
// Page 0 is the grid; the belt, the equipped slots and the stash pages hang off
// the same container and read as other pages, so this is what keeps a run out of
// everything that is not the grid.
inline constexpr uint8_t MainInventoryPage = 0;

__declspec(noinline) auto InspectItem(void* item, ItemFacts& out) noexcept -> bool;

// Whether the shared stash panel is open.
//
// The automatic path refuses to deposit while it is. A deposit is a raw move: it
// does not tell any UI, so an item would disappear out of a grid that is being
// drawn, and the tab count behind the panel would not visibly move. The item
// would still be banked - nothing is lost - but a plugin that makes the stash
// look like it is eating things is a plugin nobody will trust.
//
// The console command ignores this. The player asked, and by definition they are
// looking at the result.
//
// A fault reads as "not open", because the only way this faults is a bad
// address, and the signature check has already ruled that out - if it faults
// anyway, everything else in the run will too.
__declspec(noinline) auto StashIsOpen() noexcept -> bool;

// The write half: this plugin's own list first, then the game's checks, then the
// move.
//
// The ignore list comes before the game is asked anything: a code the config
// says to leave alone is not the game's business, and asking about it first
// would put this plugin's policy behind the game's answer.
//
// The class id is asked for here, in the same breath as the move, rather than
// read earlier and carried in: the gap between reading and moving is where a
// stale answer would come from, and neither call is expensive.
__declspec(noinline) auto DepositNative(void* item) noexcept -> Verdict;

// Everything the player's item container holds, gathered up front - which is
// more than the inventory. The belt, the equipped slots and the stash pages hang
// off the same container, and the page byte (read later, per item) is what tells
// them apart. This walk does not filter on it: the run does, and it counts what
// it left alone.
//
// Gathering first matters because StashDeposit moves the item it is given, and
// walking a list while mutating it is how a plugin ends up holding a pointer to
// something that has been relocated. With the list in hand, each item is a
// separate unit and moving one does not invalidate the others - but the item
// that was moved must never be read again, and nothing here does.
//
// An inventory stack is one unit whatever its size, so the grid's own slots are
// the real ceiling and this is generous headroom rather than a working limit.
constexpr size_t MaxSnapshot = 128;

struct Snapshot {
	void*    items[MaxSnapshot] {};
	uint32_t ids[MaxSnapshot] {};
	size_t   count = 0;
};

__declspec(noinline) auto CollectInventory(Snapshot& out) noexcept -> bool;

// Whether the stash half may run at all, and the one line saying why not when it
// may not. Every path that could move an item asks this first: a build whose
// bytes did not match is a build where those addresses mean something else.
auto NativeUsable(const D2RL::PluginContext* context) noexcept -> bool;

// The same answer without the line. For the pump, which ticks ten times a second:
// a report per tick would be the log. Nothing that runs once has any reason to
// use this - an unusable build should say so, by name, when it is asked.
auto NativeReady() noexcept -> bool;

// Checks every address against its expected bytes and reports as a set. Called
// once, at load.
//
// A failure prints what is actually there. That is the difference between a log
// line that says "it did not work" and one a corrected table can be written
// from: D2R.exe's code is encrypted on disk, so the running image is the only
// place those bytes exist, and this is the one moment the plugin is looking at
// it with the address already known to be wrong.
//
// An address another plugin has hooked at its entry still counts as a match, and
// is named in the log when it does. See deposit_native_signatures.h for why.
auto CheckNative(const D2RL::PluginContext* context, uint64_t exeBase) noexcept -> void;
