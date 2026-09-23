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
//
// Every member is the game's answer. This plugin's own reasons to leave an item
// alone are not here, because they are not the game's to give: the run asks
// StashIgnores before it asks anything below.
enum class Verdict : int {
	Faulted = 0,  // a native call raised, or there is no player to deposit for
	Blocked,      // the game flags this item as one it will not move
	NotMaterial,  // the game does not count this among advanced-stash materials
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
// where a stale answer would come from and the call is not expensive. The code
// is different: it is what the item *is*, so it cannot go stale between the two.
struct ItemFacts {
	uint32_t id   = 0;
	uint32_t code = 0;
};

// The page the inventory grid reads as. Where in the data block that byte sits is
// in the signature table - nothing outside deposit_native.cpp reads it - so only
// the value it is compared against is needed here.
//
// Page 0 is the grid; the belt, the equipped slots, the cube and the stash pages
// hang off the same container and read as other pages. It is the walk that tests
// it, once per unit, before anything is handed over: that is what keeps a run out
// of everything that is not the grid.
inline constexpr uint8_t MainInventoryPage = 0;

__declspec(noinline) auto InspectItem(void* item, ItemFacts& out) noexcept -> bool;

// Everything the game answers about one item without moving it: what 'deposit
// why' reports, and the questions a run asks before it decides anything.
//
// The two class ids are the point of it. TxtFileNo counts an item's class across
// the game's item tables and the SDK reports the same number as
// ItemInfo::classId, so a report that shows both is how a build where the two
// disagree - the one way a run could answer "not advanced-stash material" for
// everything - is told apart from an item the game really does refuse.
struct ItemAnswers {
	uint32_t id             = 0;
	uint32_t code           = 0;
	uint8_t  page           = 0;
	int      blocked        = 0;  // ItemBlocked != 0: the game will not move it
	int      nativeClass    = 0;  // TxtFileNo(item)
	int      nativeMaterial = 0;  // StashItemOk(nativeClass)
};

// Read-only, and the only entry point here that is: it asks the same questions
// the deposit path asks and calls nothing that moves anything, so the inventory
// can be reported exactly as it stands. A fault comes back as false, with
// nothing written.
__declspec(noinline) auto InspectAnswers(void* item, ItemAnswers& out) noexcept -> bool;

// Whether the game counts a class id - the number StashItemOk takes - among
// advanced-stash materials. The report asks it with both ids, which is the
// comparison ItemAnswers is built around.
__declspec(noinline) auto IsMaterialClass(int classId) noexcept -> bool;

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

// The write half: the game's checks, then the move.
//
// This plugin's own ignore list is not here. It is asked by the run, before this
// is called, because it is the run's policy and not the game's answer - see the
// note on Verdict. The order that matters is kept either way: a code the config
// leaves alone is never offered to the game at all.
//
// The class id is asked for here, in the same breath as the move, rather than
// read earlier and carried in: the gap between reading and moving is where a
// stale answer would come from, and neither call is expensive. The player is the
// opposite - it is the same for every item in a run, so it is resolved by the
// walk and passed in.
__declspec(noinline) auto DepositNative(void* player, void* item) noexcept -> Verdict;

// The grid's worth of the player's item container, gathered up front.
//
// The container is more than the inventory: the belt, the equipped slots, the
// cube and every stash page hang off it, and how many units it may hold is the
// mod's to decide, not this build's. The walk visits all of them and hands over
// the ones on the grid, because the grid is what a run acts on. An inventory
// stack is one unit whatever its size, so the grid's own slots are the real
// ceiling and MaxSnapshot is headroom for them rather than a working limit.
//
// Filling up and stopping there instead was how the newest items in the
// inventory became invisible to a run. The container's list has a tail, a pickup
// lands on it, and a walk that stops when the table is full stops before it:
// this character's stash pages and belt filled the table, and the runes picked
// up afterwards were never reached. What the table holds is counted either way,
// so a build that does run out says so rather than going quiet.
//
// Gathering first matters because StashDeposit moves the item it is given, and
// walking a list while mutating it is how a plugin ends up holding a pointer to
// something that has been relocated. With the list in hand, each item is a
// separate unit and moving one does not invalidate the others - but the item
// that was moved must never be read again, and nothing here does.
constexpr size_t MaxSnapshot = 128;

struct Snapshot {
	// Whose container this is, resolved once by the walk that filled it in. The
	// run needs the player for every item it goes on to move, and asking again
	// per item would be the same answer fetched N times.
	void*    player = nullptr;

	// The units on the grid, and only those.
	void*    items[MaxSnapshot] {};
	uint32_t ids[MaxSnapshot] {};
	size_t   count = 0;

	// The units the walk saw and did not hand over. `elsewhere` is the ordinary
	// case and is what makes a run's summary complete: the belt, the equipped
	// slots, the cube and the stash pages are in the same container, and nothing
	// that moves an item is ever called for one of them. `over` is the one that
	// must never happen - more grid units than the table can hold - and it is a
	// count rather than a silent stop, because a run that quietly looks at part
	// of the grid is the bug this shape exists to end.
	size_t   over      = 0;
	size_t   elsewhere = 0;
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
