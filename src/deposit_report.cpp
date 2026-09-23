// Auto Deposit - 'deposit why'. See deposit_report.h.
//
// One pass over the container walk, joined against the SDK's list of the
// player's items on the runtime id: the same number the pickup hook reports and
// a run matches its queued ids against, so the two halves are talking about the
// same unit and not merely about the same code.

#include "deposit_report.h"

#include "deposit_codes.h"
#include "deposit_common.h"
#include "deposit_native.h"

#include <atomic>
#include <cstdio>

namespace {

// How many units the report keeps, and how much one code's row remembers. The
// container walk's own ceiling is MaxSnapshot, so its half cannot overflow this;
// the SDK is asked about the whole player and can see more than the walk does -
// which is one of the things this is here to find out - so it gets the same room.
constexpr size_t MaxListed          = 160;
constexpr size_t MaxCodeRows        = 48;
constexpr size_t PagesPerRow        = 4;
constexpr size_t ContainersPerRow   = 4;
constexpr size_t MaxSdkOnlyLines    = 24;
constexpr size_t MaxElsewhereLines  = 8;

// Everything but the ground, which is not the player's and is every item in
// sight, and Unknown, which names nothing. The mask is the filter's whole job.
constexpr uint32_t ReportContainerMask = D2RL::Items::AllItemContainers
                                       & ~(D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Unknown) | D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Ground));

// One unit, as the container walk and the SDK each describe it.
struct UnitLine {
	uint32_t id   = 0;
	uint32_t code = 0;
	uint8_t  page = 0;

	int      blocked        = 0;
	int      nativeClass    = 0;
	int      nativeMaterial = 0;

	// False when the SDK reports no item with this id at all, which is an answer
	// in its own right: it is what an item the SDK cannot see looks like.
	bool     hasSdk       = false;
	uint32_t sdkContainer = 0;
	int32_t  sdkPage      = 0;
	int32_t  sdkX         = 0;
	int32_t  sdkY         = 0;
	uint32_t sdkClass     = 0;
	int      sdkMaterial  = 0;
};

// One code's row in the table at the end: where that kind of thing was found and
// what the game answered about it. This is the part to read first - one line per
// kind in the inventory, saying whether a run should have been able to take it.
struct CodeRow {
	uint32_t code           = 0;
	uint32_t units          = 0;
	uint32_t blocked        = 0;
	uint32_t materialByFile = 0;  // the class id TxtFileNo gave
	uint32_t materialBySdk  = 0;  // the class id the SDK reports
	uint8_t  pages[PagesPerRow] {};
	uint32_t pageCount      = 0;
	uint32_t containers[ContainersPerRow] {};
	uint32_t containerCount = 0;
};

// What the console thread fills in and the game thread reads.
struct ReportWork {
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
};

// The report's own storage. One report runs at a time - the queue refuses a
// second - and it runs on the game thread, so this is file scope rather than a
// hundred and sixty item copies carried on a game-thread stack.
UnitLine              g_units[MaxListed] {};
size_t                g_unitCount   = 0;
D2RL::Items::ItemInfo g_sdk[MaxListed] {};
size_t                g_sdkCount    = 0;
uint32_t              g_sdkOverflow = 0;
uint32_t              g_sdkGrid     = 0;  // how many of the player's items stand on the grid
uint32_t              g_sdkGridMissed = 0;  // ... and how many of those the walk did not hand over

ReportWork        g_report {};
std::atomic<bool> g_reportQueued { false };

// What the item is standing in, in words. The number in ItemInfo is the SDK's
// contract and the name is for the log line, where "container 9" would send the
// reader to the header.
auto ContainerName(D2RL::Items::ItemContainer container) noexcept -> const char* {
	switch (container) {
		case D2RL::Items::ItemContainer::Equipment:     return "the equipped slots";
		case D2RL::Items::ItemContainer::Cursor:        return "the cursor";
		case D2RL::Items::ItemContainer::Belt:          return "the belt";
		case D2RL::Items::ItemContainer::Inventory:     return "the inventory";
		case D2RL::Items::ItemContainer::Cube:          return "the cube";
		case D2RL::Items::ItemContainer::Trade:         return "a trade window";
		case D2RL::Items::ItemContainer::PersonalStash: return "the personal stash";
		case D2RL::Items::ItemContainer::SharedStash:   return "the shared stash";
		case D2RL::Items::ItemContainer::CustomPage:    return "a player page";
		case D2RL::Items::ItemContainer::Ground:        return "the ground";
		default:                                        return "an unnamed container";
	}
}

// Whether the SDK puts this unit on the inventory grid, which is the only place a
// run looks. Container and page are the SDK's own, and the walk reads the same
// two off the item itself, so the two agreeing is what makes a unit the walk did
// not hand over a finding rather than a page the run was never going to visit.
auto SdkOnGrid(const D2RL::Items::ItemInfo& item) noexcept -> bool {
	return item.container == D2RL::Items::ItemContainer::Inventory && item.inventoryPage == MainInventoryPage;
}

// Adds a value to a short list of distinct ones: a page or a container that turns
// up once per unit would otherwise be listed once per unit.
template <class T>
auto AddDistinct(T* values, uint32_t& count, uint32_t capacity, T value) noexcept -> void {
	for (uint32_t index = 0; index < count; ++index) {
		if (values[index] == value) {
			return;
		}
	}
	if (count < capacity) {
		values[count++] = value;
	}
}

// The walk's line for an id, and the SDK's line for the same id. Both are asked
// by every join the report makes, and both are linear over a list of at most a
// hundred and sixty - a run that is asked for once by hand is not the place to
// build a table to search.
auto FindUnitLine(uint32_t id) noexcept -> const UnitLine* {
	for (size_t index = 0; index < g_unitCount; ++index) {
		if (g_units[index].id == id) {
			return &g_units[index];
		}
	}
	return nullptr;
}

auto FindSdkLine(uint32_t id) noexcept -> const D2RL::Items::ItemInfo* {
	for (size_t index = 0; index < g_sdkCount; ++index) {
		if (g_sdk[index].runtimeId == id) {
			return &g_sdk[index];
		}
	}
	return nullptr;
}

// The SDK's half of the join. Nothing is logged from inside the walk - the log
// lines are the report's, and they are written where every number is already in
// hand.
auto CollectSdkLine(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void*) noexcept -> D2RL::Inventory::IterationAction {
	if (item == nullptr) {
		return D2RL::Inventory::IterationAction::Stop;
	}
	// Counted before the table's room is asked about: this number is compared
	// against what a run was handed, so it has to be the whole player's and not
	// the first tableful of it.
	if (SdkOnGrid(*item)) {
		++g_sdkGrid;
	}
	if (g_sdkCount >= MaxListed) {
		++g_sdkOverflow;
		return D2RL::Inventory::IterationAction::Continue;
	}
	g_sdk[g_sdkCount++] = *item;
	return D2RL::Inventory::IterationAction::Continue;
}

// The row for a code, added if this is the first unit of it. A table that runs
// out stops taking rows: the counts in the lines above are the numbers that have
// to be right, and the rows are what make them readable when there are few.
auto CodeRowFor(CodeRow* rows, size_t& rowCount, uint32_t code, CodeRow*& row) noexcept -> bool {
	for (size_t index = 0; index < rowCount; ++index) {
		if (rows[index].code == code) {
			row = &rows[index];
			return true;
		}
	}
	if (rowCount >= MaxCodeRows) {
		return false;
	}
	rows[rowCount].code = code;
	row                 = &rows[rowCount];
	++rowCount;
	return true;
}

// Every unit the walk found, one line each. This is the part that answers "where
// is it": the page is the walk's own answer to that, the SDK's container and
// page are the game's, and the two disagreeing is itself a finding.
void LogUnitLines(const D2RL::PluginContext* context) noexcept {
	char codeText[5] {};

	for (size_t index = 0; index < g_unitCount; ++index) {
		const UnitLine& unit = g_units[index];
		if (!unit.hasSdk) {
			D2RL::LogInfoF(context,
			               "AutoDeposit: why: %s (id %u) page %u, blocked %u, class %d, material %u. The SDK reports no item with this id at all.",
			               CodeText(unit.code, codeText),
			               static_cast<unsigned>(unit.id),
			               static_cast<unsigned>(unit.page),
			               static_cast<unsigned>(unit.blocked),
			               unit.nativeClass,
			               static_cast<unsigned>(unit.nativeMaterial));
			continue;
		}

		D2RL::LogInfoF(context,
		               "AutoDeposit: why: %s (id %u) page %u, blocked %u, class %d, material %u. The SDK: %s, page %d at %d,%d, class %u, material %u.",
		               CodeText(unit.code, codeText),
		               static_cast<unsigned>(unit.id),
		               static_cast<unsigned>(unit.page),
		               static_cast<unsigned>(unit.blocked),
		               unit.nativeClass,
		               static_cast<unsigned>(unit.nativeMaterial),
		               ContainerName(static_cast<D2RL::Items::ItemContainer>(unit.sdkContainer)),
		               unit.sdkPage,
		               unit.sdkX,
		               unit.sdkY,
		               static_cast<unsigned>(unit.sdkClass),
		               static_cast<unsigned>(unit.sdkMaterial));
	}
}

// The units the SDK puts on the grid that the walk did not hand over. This is the
// finding the report exists for: a run can act on nothing but what the walk gives
// it, so a grid unit missing from that set is an item no deposit run can see,
// whatever the game would have answered about it.
//
// `everything` is for the build whose native half is off, where there is no walk
// to join against and every SDK item is an item the walk does not have.
void LogSdkOnlyLines(const D2RL::PluginContext* context, bool everything) noexcept {
	char     codeText[5] {};
	size_t   written = 0;
	uint32_t found   = 0;

	for (size_t index = 0; index < g_sdkCount; ++index) {
		const D2RL::Items::ItemInfo& item = g_sdk[index];
		if (!everything && (!SdkOnGrid(item) || FindUnitLine(item.runtimeId) != nullptr)) {
			continue;
		}
		++found;
		if (written >= MaxSdkOnlyLines) {
			continue;
		}
		++written;

		D2RL::LogInfoF(context,
		               "AutoDeposit: why: the container walk has no %s (id %u): %s, page %d at %d,%d, class %u, material %u.",
		               CodeText(item.code, codeText),
		               static_cast<unsigned>(item.runtimeId),
		               ContainerName(item.container),
		               item.inventoryPage,
		               item.x,
		               item.y,
		               static_cast<unsigned>(item.classId),
		               static_cast<unsigned>(IsMaterialClass(static_cast<int>(item.classId)) ? 1 : 0));
	}

	g_sdkGridMissed = everything ? 0 : found;

	// Said whether or not there was room to name them all: this is the answer to
	// "why is nothing happening to an item I can see in the inventory", and it
	// must not depend on how many other lines the report happened to write first.
	if (!everything && found != 0) {
		D2RL::LogInfoF(context,
		               "AutoDeposit: why: %u unit(s) the SDK puts on the grid are not in the walk's hands, so no deposit run can see them. The closing counts are the same finding, in numbers.",
		               static_cast<unsigned>(found));
	}
	if (found > written) {
		D2RL::LogInfoF(context,
		               "AutoDeposit: why: %u item(s) in all are outside the container walk; the first %u are the ones named above.",
		               static_cast<unsigned>(found),
		               static_cast<unsigned>(written));
	}
}

// The other half of what the SDK reports and the walk does not hand over: units
// that are not on the grid, so no run looks at them and none should. Named after
// the alarm above and only a few of them, because the question they answer is
// "where has this gone" - one line is the item in question, and the belt, the
// cube, a stash page and the shared stash are where it turns out to be.
void LogElsewhereLines(const D2RL::PluginContext* context) noexcept {
	char     codeText[5] {};
	size_t   written = 0;
	uint32_t found   = 0;

	for (size_t index = 0; index < g_sdkCount; ++index) {
		const D2RL::Items::ItemInfo& item = g_sdk[index];
		if (SdkOnGrid(item) || FindUnitLine(item.runtimeId) != nullptr) {
			continue;
		}
		++found;
		if (written >= MaxElsewhereLines) {
			continue;
		}
		++written;

		D2RL::LogInfoF(context,
		               "AutoDeposit: why: the container walk hands over no %s (id %u): %s, page %d at %d,%d, class %u, material %u. Not the inventory grid, so no run looks at it.",
		               CodeText(item.code, codeText),
		               static_cast<unsigned>(item.runtimeId),
		               ContainerName(item.container),
		               item.inventoryPage,
		               item.x,
		               item.y,
		               static_cast<unsigned>(item.classId),
		               static_cast<unsigned>(IsMaterialClass(static_cast<int>(item.classId)) ? 1 : 0));
	}

	if (found > written) {
		D2RL::LogInfoF(context,
		               "AutoDeposit: why: %u item(s) more are in containers no run looks at; the first %u are the ones named above.",
		               static_cast<unsigned>(found),
		               static_cast<unsigned>(written));
	}
}

// One row per code: where that kind of thing is and what the game answers about
// it. The three counts are what a run's verdicts came out as, so a code whose row
// says material 1/1 and blocked 0 is one the run should have deposited - and if
// the log says otherwise, the page and container columns are why.
void LogCodeTable(const D2RL::PluginContext* context) noexcept {
	CodeRow  rows[MaxCodeRows] {};
	size_t   rowCount = 0;
	uint32_t unlisted = 0;

	for (size_t index = 0; index < g_unitCount; ++index) {
		const UnitLine& unit = g_units[index];
		CodeRow*        row  = nullptr;
		if (!CodeRowFor(rows, rowCount, unit.code, row)) {
			++unlisted;
			continue;
		}

		++row->units;
		row->blocked += unit.blocked != 0 ? 1 : 0;
		row->materialByFile += unit.nativeMaterial != 0 ? 1 : 0;
		row->materialBySdk += unit.sdkMaterial != 0 ? 1 : 0;
		AddDistinct(row->pages, row->pageCount, static_cast<uint32_t>(PagesPerRow), unit.page);
		if (unit.hasSdk) {
			AddDistinct(row->containers, row->containerCount, static_cast<uint32_t>(ContainersPerRow), unit.sdkContainer);
		}
	}

	char codeText[5] {};
	for (size_t index = 0; index < rowCount; ++index) {
		const CodeRow& row = rows[index];

		char   pages[24] {};
		size_t pagesUsed = 0;
		for (uint32_t at = 0; at < row.pageCount; ++at) {
			char one[8] {};
			std::snprintf(one, sizeof(one), "%u", static_cast<unsigned>(row.pages[at]));
			AppendList(pages, sizeof(pages), pagesUsed, ",", one);
		}

		char   containers[96] {};
		size_t containersUsed = 0;
		for (uint32_t at = 0; at < row.containerCount; ++at) {
			AppendList(containers, sizeof(containers), containersUsed, ", ", ContainerName(static_cast<D2RL::Items::ItemContainer>(row.containers[at])));
		}

		D2RL::LogInfoF(context,
		               "AutoDeposit: why: %s x%u - pages [%s], in [%s], blocked %u, material %u by the item's own class id and %u by the SDK's.",
		               CodeText(row.code, codeText),
		               static_cast<unsigned>(row.units),
		               pagesUsed != 0 ? pages : "?",
		               containersUsed != 0 ? containers : "?",
		               static_cast<unsigned>(row.blocked),
		               static_cast<unsigned>(row.materialByFile),
		               static_cast<unsigned>(row.materialBySdk));
	}

	if (unlisted != 0) {
		D2RL::LogInfoF(context,
		               "AutoDeposit: why: %u unit(s) are of codes past the %u rows this table keeps; they are named in the lines above instead.",
		               static_cast<unsigned>(unlisted),
		               static_cast<unsigned>(MaxCodeRows));
	}
}

// Fills the report's storage from the container walk and the SDK's list, and
// reports what it read. Everything that moves an item lives a call away from
// here, and nothing in this path takes it.
void RunReport(const D2RL::PluginContext* context, const ReportWork& work) noexcept {
	const D2RL::InventoryServiceV1* inventory = nullptr;
	if (context->QueryService(D2RL::ServiceId::Inventory, D2RL::InventoryServiceV1Version, &inventory) != D2RL::ServiceQueryResult::Success
	    || inventory == nullptr) {
		context->LogError("AutoDeposit: why: the Inventory service is unavailable, so there is nothing to report.");
		return;
	}

	g_unitCount     = 0;
	g_sdkCount      = 0;
	g_sdkOverflow   = 0;
	g_sdkGrid       = 0;
	g_sdkGridMissed = 0;

	const D2RL::Inventory::ItemFilter filter {
		.structSize    = D2RL::Inventory::ItemFilterSize,
		.flags         = 0,
		.containerMask = ReportContainerMask,
		.reserved      = 0,
	};
	if (inventory->forEachInventoryItem(context, work.player, &filter, CollectSdkLine, nullptr) != D2RL::Inventory::Result::Success) {
		context->LogError("AutoDeposit: why: the SDK could not enumerate the player's items, so its half of the report is empty.");
	}

	if (!NativeReady()) {
		context->LogError("AutoDeposit: why: the native signatures did not all match this build, so the container walk, the page numbers and the game's own answers are not in this report. The SDK's half follows.");
		LogSdkOnlyLines(context, true);
		D2RL::LogInfoF(context, "AutoDeposit: why: the SDK reports %u item(s) for this player. Nothing was moved.", static_cast<unsigned>(g_sdkCount + g_sdkOverflow));
		return;
	}

	Snapshot snapshot {};
	if (!CollectInventory(snapshot)) {
		context->LogError("AutoDeposit: why: the player's container could not be read, so there is nothing to report.");
		return;
	}

	for (size_t index = 0; index < snapshot.count && g_unitCount < MaxListed; ++index) {
		ItemAnswers answers {};
		if (!InspectAnswers(snapshot.items[index], answers)) {
			D2RL::LogErrorF(context, "AutoDeposit: why: the unit at %u in the container raised while it was read and is not in this report.", static_cast<unsigned>(index));
			continue;
		}

		UnitLine& unit      = g_units[g_unitCount++];
		unit.id             = answers.id;
		unit.code           = answers.code;
		unit.page           = answers.page;
		unit.blocked        = answers.blocked;
		unit.nativeClass    = answers.nativeClass;
		unit.nativeMaterial = answers.nativeMaterial;

		const D2RL::Items::ItemInfo* sdk = FindSdkLine(answers.id);
		if (sdk != nullptr) {
			unit.hasSdk       = true;
			unit.sdkContainer = static_cast<uint32_t>(sdk->container);
			unit.sdkPage      = sdk->inventoryPage;
			unit.sdkX         = sdk->x;
			unit.sdkY         = sdk->y;
			unit.sdkClass     = sdk->classId;
			unit.sdkMaterial  = IsMaterialClass(static_cast<int>(sdk->classId)) ? 1 : 0;
		}
	}

	D2RL::LogInfoF(context,
	               "AutoDeposit: why: the container walk visited %u unit(s): %u on the grid (page %u), which is what a run is handed over, and %u on another page of the same container. This report reads and moves nothing.",
	               static_cast<unsigned>(snapshot.count + snapshot.over + snapshot.elsewhere),
	               static_cast<unsigned>(snapshot.count + snapshot.over),
	               static_cast<unsigned>(MainInventoryPage),
	               static_cast<unsigned>(snapshot.elsewhere));

	if (snapshot.over != 0) {
		D2RL::LogErrorF(context,
		                "AutoDeposit: why: %u grid unit(s) did not fit this build's table (%u), so this report is about the rest of them. That is a bug worth reporting.",
		                static_cast<unsigned>(snapshot.over),
		                static_cast<unsigned>(MaxSnapshot));
	}

	LogUnitLines(context);
	LogSdkOnlyLines(context, false);
	LogElsewhereLines(context);
	LogCodeTable(context);

	// The walk's count against the SDK's, which is the one comparison that answers
	// "can a run see the grid at all": the first number is what a run is handed,
	// the second is how many units the SDK counts standing on page 0. They agree
	// when nothing is wrong, and the lines above name the units that make up the
	// difference when they do not.
	D2RL::LogInfoF(context,
	               "AutoDeposit: why: the walk handed over %u unit(s) from the grid; the SDK counts %u item(s) on it, and %u of those are not in the walk's hands. %u item(s) in all are in this player's containers.",
	               static_cast<unsigned>(snapshot.count),
	               static_cast<unsigned>(g_sdkGrid),
	               static_cast<unsigned>(g_sdkGridMissed),
	               static_cast<unsigned>(g_sdkCount + g_sdkOverflow));

	if (g_sdkOverflow != 0) {
		D2RL::LogInfoF(context,
		               "AutoDeposit: why: %u SDK item(s) are past the first %u the report keeps; they are counted and not named.",
		               static_cast<unsigned>(g_sdkOverflow),
		               static_cast<unsigned>(MaxListed));
	}

	D2RL::LogInfo(context, "AutoDeposit: why: end of report. Nothing was moved.");
}

void __cdecl GameThreadReport(const D2RL::PluginContext* context, void* userData) noexcept {
	auto* work = static_cast<ReportWork*>(userData);
	if (work == nullptr || context == nullptr) {
		g_reportQueued.store(false);
		return;
	}

	const ReportWork copy = *work;
	RunReport(context, copy);

	g_reportQueued.store(false);
}

}  // namespace

// The same shape as the sweep's queue: the player is resolved on the thread that
// asked - the console's - and the work runs on the game thread, which is the only
// thread the SDK's item calls may be made from.
auto ScheduleReport(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	bool expected = false;
	if (!g_reportQueued.compare_exchange_strong(expected, true)) {
		return false;
	}

	const D2RL::ThreadServiceV1* threads = ThreadServiceOf(context);
	if (threads == nullptr) {
		g_reportQueued.store(false);
		return false;
	}

	const D2RL::InventoryServiceV1* inventory = nullptr;
	if (context->QueryService(D2RL::ServiceId::Inventory, D2RL::InventoryServiceV1Version, &inventory) != D2RL::ServiceQueryResult::Success
	    || inventory == nullptr) {
		g_reportQueued.store(false);
		return false;
	}

	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (inventory->getLocalPlayer(context, &player) != D2RL::Inventory::Result::Success) {
		g_reportQueued.store(false);
		return false;
	}

	g_report        = ReportWork {};
	g_report.player = player;

	if (threads->runOnGameThread(context, GameThreadReport, &g_report) != D2RL::Threads::Result::Success) {
		g_reportQueued.store(false);
		return false;
	}
	return true;
}
