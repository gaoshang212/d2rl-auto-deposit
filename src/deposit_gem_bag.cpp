// Auto Deposit - the gem bag half. See deposit_gem_bag.h.
//
// The bag is a 1x1 item (code "bag") whose stored amount lives in ItemStatCost
// 386, "stacked_gem" - a 16-bit saved stat capped at 65535. Nothing about it is
// reached by address: this half is written entirely against the SDK, through an
// item transaction, and it is the half that survives a game build whose bytes
// the stash half no longer recognises.
//
// The counter cannot be written in place, so a merge is a transaction: the old
// bag plus the gems go in and a new bag comes out. If anything fails to
// validate, nothing is consumed.

#include "deposit_gem_bag.h"

#include "deposit_codes.h"
#include "deposit_common.h"
#include "deposit_config.h"

#include <cstdio>
#include <cstring>
#include <windows.h>

namespace {

// Every gem counter is 16 bits unsigned in ItemStatCost, so the whole range is
// in play and no value can be ruled out by size alone.
constexpr uint16_t StackedGemMaxValue = 65535;

// Properties.txt row "stacked/gem". A property is one Properties.txt row, not
// one ItemStatCost stat: this is the row whose *Id the SDK wants, and it is the
// row that carries stat 386.
constexpr uint32_t StackedGemPropertyId = 396;

// ItemStatCost 386, "stacked_gem": the one stat the bag's counter lives in.
constexpr uint16_t StackedGemStatId = 386;

constexpr uintptr_t ItemUnitStatListOffset = 0x88;
constexpr uintptr_t StatListRecordOffset   = 0xA8;
constexpr size_t    StatRecordIdOffset     = 0x02;
constexpr size_t    StatRecordValueOffset  = 0x04;

// The record the walk lands on holds the gem counter, but the stride and the
// record's own layout are the two things a future game build could change, so
// a miss falls back to looking for stat 386 in the records that follow.
constexpr size_t StatRecordScanBytes = 0x4000;

struct AmountRead {
	uintptr_t unit     = 0;
	uintptr_t statList = 0;
	uintptr_t record   = 0;
	uint16_t  statId   = 0;
	uint16_t  amount   = 0;
	bool      valid    = false;
};

// True when [address, address+size) is committed and readable. Scanning native
// memory is only safe once this passes.
auto IsReadableRange(uintptr_t address, size_t size) noexcept -> bool {
	uintptr_t cursor = address;
	uintptr_t end    = address + size;

	while (cursor < end) {
		MEMORY_BASIC_INFORMATION info {};
		if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info)) {
			return false;
		}
		if (info.State != MEM_COMMIT) {
			return false;
		}
		if ((info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0
		    || (info.Protect & PAGE_GUARD) != 0
		    || (info.Protect & PAGE_NOACCESS) != 0) {
			return false;
		}

		const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
		if (regionEnd <= cursor) {
			return false;
		}
		cursor = regionEnd;
	}

	return true;
}

auto LoadPointer(uintptr_t address, uintptr_t& out) noexcept -> bool {
	if (!IsReadableRange(address, sizeof(uintptr_t))) {
		return false;
	}
	std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof(uintptr_t));
	return out != 0;
}

// Reads the 16-bit amount that follows a 16-bit stat id at address, checking a
// readable range first so a stale pointer cannot fault the game thread.
auto LoadStatEntry(uintptr_t address, uint16_t& id, uint16_t& value) noexcept -> bool {
	constexpr size_t entryBytes = sizeof(uint16_t) * 2;
	if (!IsReadableRange(address, entryBytes)) {
		return false;
	}
	std::memcpy(&id, reinterpret_cast<const void*>(address), sizeof(uint16_t));
	std::memcpy(&value, reinterpret_cast<const void*>(address + sizeof(uint16_t)), sizeof(uint16_t));
	return true;
}

auto ReadAmountFromNative(void* nativeItem, AmountRead& out) noexcept -> bool {
	out = AmountRead {};

	const uintptr_t unit = reinterpret_cast<uintptr_t>(nativeItem);
	if (unit == 0) {
		return false;
	}
	out.unit = unit;

	uintptr_t statList = 0;
	if (!LoadPointer(unit + ItemUnitStatListOffset, statList)) {
		return false;
	}
	out.statList = statList;

	uintptr_t record = 0;
	if (!LoadPointer(statList + StatListRecordOffset, record)) {
		return false;
	}
	out.record = record;

	uint16_t id    = 0;
	uint16_t value = 0;
	if (LoadStatEntry(record + StatRecordIdOffset, id, value) && id == StackedGemStatId) {
		out.statId = id;
		out.amount = value;
		out.valid  = true;
		return true;
	}

	// Layout moved: sweep the records for the stat id instead. The amount sits
	// in the two bytes after it either way, which is what makes stat 386
	// self-identifying.
	if (!IsReadableRange(record, StatRecordScanBytes)) {
		return false;
	}

	const auto* bytes = reinterpret_cast<const uint8_t*>(record);
	for (size_t offset = 0; offset + 4 <= StatRecordScanBytes; offset += 2) {
		uint16_t candidate = 0;
		std::memcpy(&candidate, bytes + offset, sizeof(candidate));
		if (candidate != StackedGemStatId) {
			continue;
		}

		out.statId = candidate;
		std::memcpy(&out.amount, bytes + offset + sizeof(uint16_t), sizeof(out.amount));
		out.record = record + offset - StatRecordIdOffset;
		out.valid  = true;
		return true;
	}

	return false;
}

struct AmountProbe {
	AmountRead read {};
};

void __cdecl AmountCallback(const D2RL::PluginContext* context, void* nativeItem, void* userData) noexcept {
	(void)context;
	if (userData == nullptr) {
		return;
	}
	ReadAmountFromNative(nativeItem, static_cast<AmountProbe*>(userData)->read);
}

// The console handler fills this in on its own thread and the game thread reads
// it, so it is a snapshot: GameThreadBagWork copies it before doing anything.
struct BagWorkState {
	D2RL::PlayerHandle player     = D2RL::InvalidPlayerHandle;
	// Set when the pickup hook asked for this merge rather than the console. It
	// narrows the sweep to the one item just picked up, named by pickupGuid, and
	// changes what is written to the log when nothing matched.
	bool               autoMerge  = false;
	uint32_t           pickupGuid = 0;
	bool               scheduled  = false;
	bool               ready      = false;
};

BagWorkState g_bagWork {};

auto ReadBagAmount(const D2RL::PluginContext* context, D2RL::ItemServiceV1 const* items, D2RL::ItemHandle bag, AmountRead& out) noexcept -> bool {
	AmountProbe probe {};
	if (items->editNativeItem(context, bag, AmountCallback, &probe) != D2RL::Items::Result::Success) {
		return false;
	}
	out = probe.read;
	return probe.read.valid;
}

// A Gem Cluster is worth 20 to 30 gems: that spread is Reimagined's own
// (1gc_desc says "Bag + Cluster = 20-30 Gems"), not a number invented here.
constexpr uint32_t GemClusterValueMin = 20;
constexpr uint32_t GemClusterValueMax = 30;

// D2RLoader takes at most MaxTransactionInputs items in one exchange, and the
// bag is one of them, so 63 gems and clusters go in per run. This is the SDK's
// own ceiling, not the mod's, and it is a ceiling rather than a suggestion: an
// exchange asking for more is rejected whole, which would merge nothing at all.
// Anything past it waits for the next run instead.
constexpr uint32_t MaxExchangeInputs = D2RL::Items::MaxTransactionInputs;
constexpr uint32_t MaxMergeInputs    = MaxExchangeInputs - 1;

auto RollClusterGems() noexcept -> uint32_t {
	static uint64_t state = 0x2545F4914F6CDD1DULL;
	state                  = state * 6364136223846793005ULL + 1442695040888963407ULL;
	const uint32_t span    = GemClusterValueMax - GemClusterValueMin + 1;
	return GemClusterValueMin + static_cast<uint32_t>((state >> 33) % span);
}

struct MergeCollect {
	D2RL::ItemHandle bag      = D2RL::InvalidItemHandle;
	uint32_t         bagCount = 0;
	uint32_t         bagsSeen = 0;

	D2RL::ItemHandle gems[MaxMergeInputs] {};
	uint32_t         gemCount = 0;

	D2RL::ItemHandle clusters[MaxMergeInputs] {};
	uint32_t         clusterCount = 0;

	// Non-zero when only one item may be taken: the one the pickup hook watched
	// arrive. Everything else loose stays where the player left it. Zero means no
	// restriction, which is what the console command wants.
	uint32_t wantedGuid = 0;
	uint32_t heldBack   = 0;

	// The config switches as they apply to this run. The console command sweeps
	// everything loose whatever they say - a merge asked for by hand is not the
	// automatic merge they are about - so these are only ever false on the pickup
	// path.
	bool takeGems     = true;
	bool takeClusters = true;

	// Set when the item the pickup hook named is of a kind the config switched
	// off. It is not counted as held back: it was never a candidate, and the line
	// that would otherwise report it as nowhere in the inventory has to say this
	// instead.
	bool skippedByConfig = false;

	// The runtime ids of the first few loose gems seen, for the log line that
	// has to say why a wanted guid was not among them.
	uint32_t seenGuids[4] {};
	uint32_t seenCount = 0;

	uint32_t overflow    = 0;
	uint32_t classicGems = 0;
};

// True when this item is the one the pickup hook is after, or when nothing in
// particular is being asked for. Notes the runtime ids it turns down, so the log
// can show what the sweep did see when the wanted one is not there.
auto WantsItem(MergeCollect& collect, const D2RL::Items::ItemInfo* item) noexcept -> bool {
	if (collect.wantedGuid == 0 || item->runtimeId == collect.wantedGuid) {
		return true;
	}
	if (collect.seenCount < 4) {
		collect.seenGuids[collect.seenCount] = item->runtimeId;
	}
	++collect.seenCount;
	return false;
}

auto CollectMergeItem(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void* userData) noexcept -> D2RL::Inventory::IterationAction {
	if (item == nullptr || userData == nullptr) {
		return D2RL::Inventory::IterationAction::Stop;
	}

	auto* collect = static_cast<MergeCollect*>(userData);

	if (item->code == GemBagCode) {
		++collect->bagsSeen;
		if (collect->bag == D2RL::InvalidItemHandle) {
			collect->bag = item->handle;
		}
		return D2RL::Inventory::IterationAction::Continue;
	}

	if (item->code == GemClusterCode) {
		if (!collect->takeClusters || GemBagIgnores(item->code)) {
			if (collect->wantedGuid != 0 && item->runtimeId == collect->wantedGuid) {
				collect->skippedByConfig = true;
			}
			return D2RL::Inventory::IterationAction::Continue;
		}
		if (!WantsItem(*collect, item)) {
			++collect->heldBack;
			return D2RL::Inventory::IterationAction::Continue;
		}
		if (collect->clusterCount >= MaxMergeInputs) {
			++collect->overflow;
			return D2RL::Inventory::IterationAction::Continue;
		}
		collect->clusters[collect->clusterCount++] = item->handle;
		return D2RL::Inventory::IterationAction::Continue;
	}

	if (IsGemCode(item->code)) {
		if (!collect->takeGems || GemBagIgnores(item->code)) {
			if (collect->wantedGuid != 0 && item->runtimeId == collect->wantedGuid) {
				collect->skippedByConfig = true;
			}
			return D2RL::Inventory::IterationAction::Continue;
		}
		if (!WantsItem(*collect, item)) {
			++collect->heldBack;
			return D2RL::Inventory::IterationAction::Continue;
		}
		if (collect->gemCount >= MaxMergeInputs) {
			++collect->overflow;
			return D2RL::Inventory::IterationAction::Continue;
		}
		collect->gems[collect->gemCount++] = item->handle;
	} else if (IsClassicGemCode(item->code)) {
		// Counted only when it is an item this run is about. On a console sweep
		// that is every classic gem in the inventory, which is the honest answer
		// to "how many did you walk past"; on a pickup it is one or none, because
		// the other thirty already in the inventory are not what just arrived and
		// reporting them would read as though the merge had cleared them out.
		if (collect->wantedGuid == 0 || item->runtimeId == collect->wantedGuid) {
			++collect->classicGems;
		}
	}

	return D2RL::Inventory::IterationAction::Continue;
}

// The counter is an item stat and the SDK cannot write stats, so the merge
// cannot edit the bag in place: the old bag goes into the exchange and a
// replacement comes out. Asked for automatically, the replacement lands in the
// first free cell - and the pickup that set the merge going has just put its gem
// in exactly that cell, so the new bag is dropped wherever the next gap is. The
// bag visibly hops around the inventory as gems are picked up.
//
// The bag's own cell is in its item info, so the replacement is asked for at
// that cell instead. The grid is the mod's to size - Reimagined's inventory is
// 10x8 where the stock game's is 10x4 - and the SDK exposes no way to ask how big
// it is, so neither width nor height appears here. A cell that is not a cell is
// D2RLoader's to reject, and a rejected exchange rolls back and is made again
// without the cell, which costs the position and never the gems.
auto BagCell(const D2RL::Items::ItemInfo& info, uint32_t& x, uint32_t& y) noexcept -> bool {
	if (info.container != D2RL::Items::ItemContainer::Inventory || info.x < 0 || info.y < 0) {
		return false;
	}

	x = static_cast<uint32_t>(info.x);
	y = static_cast<uint32_t>(info.y);
	return true;
}

// Sweeps the loose gems and clusters out of the inventory and into the bag, in
// one atomic exchange: the old bag and everything being merged go in, a bag
// holding the new total comes out. Nothing commits unless all of it validates,
// so a failure leaves every item where it was.
void RunMerge(const D2RL::PluginContext* context, const BagWorkState& work) noexcept {
	const D2RL::ItemServiceV1* items = nullptr;
	if (context->QueryService(D2RL::ServiceId::Item, D2RL::ItemServiceV1Version, &items) != D2RL::ServiceQueryResult::Success || items == nullptr) {
		context->LogError("AutoDeposit: Item service unavailable.");
		return;
	}

	const D2RL::InventoryServiceV1* inventory = nullptr;
	if (context->QueryService(D2RL::ServiceId::Inventory, D2RL::InventoryServiceV1Version, &inventory) != D2RL::ServiceQueryResult::Success
	    || inventory == nullptr) {
		context->LogError("AutoDeposit: Inventory service unavailable.");
		return;
	}

	// Inventory only. The Cube is deliberately left alone: a half-built recipe
	// sitting in there is exactly the sort of thing that must not be swept up.
	MergeCollect                    collect;
	collect.wantedGuid              = work.pickupGuid;
	// The switches narrow the automatic merge only. 'deposit bag' is asked for by
	// hand and sweeps both kinds whatever the file says.
	collect.takeGems                = !work.autoMerge || g_gemBagMergeGems;
	collect.takeClusters            = !work.autoMerge || g_gemBagMergeClusters;
	const D2RL::Inventory::ItemFilter filter {
		.structSize    = D2RL::Inventory::ItemFilterSize,
		.flags         = 0,
		.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory),
		.reserved      = 0,
	};
	if (inventory->forEachInventoryItem(context, work.player, &filter, CollectMergeItem, &collect) != D2RL::Inventory::Result::Success) {
		context->LogError("AutoDeposit: could not enumerate the inventory.");
		return;
	}

	char message[320] {};

	if (collect.bagsSeen == 0) {
		if (!work.autoMerge || g_gemBagNoopLogs < AutoNoopLogLimit) {
			++g_gemBagNoopLogs;
			context->LogError("AutoDeposit: no Gem Bag in the inventory; nothing to merge into.");
		}
		return;
	}
	if (collect.bagsSeen > 1) {
		std::snprintf(message,
		              sizeof(message),
		              "AutoDeposit: %u Gem Bags in the inventory. The merge cannot tell which one you mean, so it did nothing - keep one and drop the rest.",
		              static_cast<unsigned>(collect.bagsSeen));
		context->LogError(message);
		return;
	}
	if (collect.gemCount == 0 && collect.clusterCount == 0) {
		if (!work.autoMerge) {
			context->LogInfo("AutoDeposit: no loose gems or clusters in the inventory; nothing to merge.");
			return;
		}
		// A kind the config switched off is not a miss. The item arrived and is
		// sitting in the inventory exactly where it should be, and reporting it as
		// nowhere in the inventory would send the player looking for a bug.
		if (collect.skippedByConfig) {
			if (g_gemBagNoopLogs < AutoNoopLogLimit) {
				++g_gemBagNoopLogs;
				context->LogInfo("AutoDeposit: the item you picked up is one the config tells this plugin to leave alone, so it stays in the inventory where it landed.");
			}
			return;
		}
		// An auto merge that finds nothing is the normal case for everything that
		// is not a gem, so it stays quiet after the first few - but not silent,
		// because "the item I picked up is not here" is also what a gem would say
		// if it never reached the inventory at all.
		if (g_gemBagNoopLogs < AutoNoopLogLimit) {
			++g_gemBagNoopLogs;
			char   ids[64] {};
			size_t at = 0;
			for (uint32_t index = 0; index < collect.seenCount && index < 4; ++index) {
				const int written = std::snprintf(ids + at,
				                                  sizeof(ids) - at,
				                                  "%s%u",
				                                  index == 0 ? "" : ",",
				                                  static_cast<unsigned>(collect.seenGuids[index]));
				if (written <= 0 || static_cast<size_t>(written) >= sizeof(ids) - at) {
					break;
				}
				at += static_cast<size_t>(written);
			}
			std::snprintf(message,
			              sizeof(message),
			              "AutoDeposit: auto merge: the picked-up item (guid %u) is not a loose gem or cluster in the inventory. %u loose one(s) were left alone; their ids are [%s].",
			              static_cast<unsigned>(work.pickupGuid),
			              static_cast<unsigned>(collect.heldBack),
			              ids);
			context->LogInfo(message);
		}
		return;
	}

	AmountRead read {};
	if (!ReadBagAmount(context, items, collect.bag, read)) {
		context->LogError("AutoDeposit: could not read the bag's counter, so the merge did nothing.");
		return;
	}

	const uint32_t room = read.amount < StackedGemMaxValue ? StackedGemMaxValue - read.amount : 0;
	if (room == 0) {
		if (!work.autoMerge || g_gemBagNoopLogs < AutoNoopLogLimit) {
			++g_gemBagNoopLogs;
			std::snprintf(message, sizeof(message), "AutoDeposit: the bag already holds %u gems, its maximum; nothing was merged.", static_cast<unsigned>(read.amount));
			context->LogInfo(message);
		}
		return;
	}

	D2RL::Items::TransactionInput inputs[MaxExchangeInputs] {};
	uint32_t                       inputCount = 0;
	uint32_t                       tookGems   = 0;
	uint32_t                       tookClusters = 0;
	uint32_t                       gained     = 0;
	uint32_t                       leftOver   = 0;

	// The bag is consumed by the exchange just like the gems are.
	inputs[inputCount++] = {
		.item               = collect.bag,
		.quantity           = 1,
		.socketedItemPolicy = D2RL::Items::SocketedItemPolicy::RejectIfNotEmpty,
	};

	// Gems first. They are worth exactly one each, so taking them before the
	// cluster rolls leaves the least value behind when the bag runs out of room.
	for (uint32_t index = 0; index < collect.gemCount; ++index) {
		if (gained + 1 > room || inputCount >= MaxExchangeInputs) {
			leftOver += collect.gemCount - index;
			break;
		}
		inputs[inputCount++] = {
			.item               = collect.gems[index],
			.quantity           = 1,
			.socketedItemPolicy = D2RL::Items::SocketedItemPolicy::RejectIfNotEmpty,
		};
		++tookGems;
		++gained;
	}

	for (uint32_t index = 0; index < collect.clusterCount; ++index) {
		const uint32_t worth = RollClusterGems();
		if (gained + worth > room || inputCount >= MaxExchangeInputs) {
			leftOver += collect.clusterCount - index;
			break;
		}
		inputs[inputCount++] = {
			.item               = collect.clusters[index],
			.quantity           = 1,
			.socketedItemPolicy = D2RL::Items::SocketedItemPolicy::RejectIfNotEmpty,
		};
		++tookClusters;
		gained += worth;
	}

	if (inputCount <= 1) {
		std::snprintf(message,
		              sizeof(message),
		              "AutoDeposit: only %u gems of room left, too little for a cluster (%u-%u) and there are no loose gems. Nothing was merged.",
		              static_cast<unsigned>(room),
		              static_cast<unsigned>(GemClusterValueMin),
		              static_cast<unsigned>(GemClusterValueMax));
		context->LogInfo(message);
		return;
	}

	// Carry the existing bag's identity into its replacement so the exchange
	// changes the count and nothing else.
	D2RL::Items::ItemInfo info { .structSize = D2RL::Items::ItemInfoSize };
	if (items->getItemInfo(context, collect.bag, &info) != D2RL::Items::Result::Success) {
		context->LogError("AutoDeposit: could not read the Gem Bag's item info; nothing was merged.");
		return;
	}

	const uint32_t                  newTotal = read.amount + gained;
	const D2RL::Items::PropertySpec property = D2RL::Items::MakeExactProperty(StackedGemPropertyId, static_cast<int32_t>(newTotal));

	// The bag's own cell, when the grid can name it. Naming it is what keeps the
	// bag from hopping to the first free cell every time a gem goes in.
	uint32_t   bagX     = 0;
	uint32_t   bagY     = 0;
	const bool keepCell = BagCell(info, bagX, bagY);

	D2RL::Items::ItemCreateSpec outputBag {
		.structSize      = D2RL::Items::ItemCreateSpecSize,
		.flags           = 0,
		.code            = GemBagCode,
		.quality         = info.quality,
		.qualityRecordId = D2RL::Items::RandomQualityRecord,
		.itemLevel       = info.itemLevel,
		.seedMode        = D2RL::Items::SeedMode::Random,
		.quantity        = D2RL::Items::DefaultValue,
		.durability      = D2RL::Items::DefaultValue,
		.socketCount     = 0,
		.stateFlags      = D2RL::Items::ItemStateIdentified,
		.propertyCount   = 1,
		.properties      = &property,
		.destination     = {
						.structSize = D2RL::Items::ItemDestinationSize,
						.container  = D2RL::Items::ItemContainer::Inventory,
						.placement  = keepCell ? D2RL::Items::Placement::Exact : D2RL::Items::Placement::Automatic,
						.x          = bagX,
						.y          = bagY,
					},
	};

	D2RL::ItemHandle               merged = D2RL::InvalidItemHandle;
	const D2RL::Items::Transaction exchange {
		   .structSize     = D2RL::Items::TransactionSize,
		   .flags          = 0,
		   .player         = work.player,
		   .inputCount     = inputCount,
		   .outputCount    = 1,
		   .inputs         = inputs,
		   .outputs        = &outputBag,
		   .outputItems    = &merged,
		   .outputCapacity = 1,
		   .reserved       = 0,
	   };
	D2RL::Items::TransactionResult result { .structSize = D2RL::Items::TransactionResultSize, };
	D2RL::Items::Result            status = items->executeTransaction(context, &exchange, &result);

	if (status != D2RL::Items::Result::Success && keepCell) {
		// The cell was refused. A rejected exchange rolls every staged item back and
		// writes no output handle, so the same exchange can simply be made again with
		// the destination the merge used before this was added. Losing the merge over
		// a cell would trade the thing the player asked for - the gems - for the
		// thing they merely prefer, so the fallback wins that trade.
		const D2RL::Items::Result refused = status;
		outputBag.destination.placement   = D2RL::Items::Placement::Automatic;
		outputBag.destination.x           = 0;
		outputBag.destination.y           = 0;
		status                            = items->executeTransaction(context, &exchange, &result);
		if (status == D2RL::Items::Result::Success) {
			std::snprintf(message,
			              sizeof(message),
			              "AutoDeposit: the bag's own cell (%u,%u) was refused (code %u), so the new bag went to the first free cell instead. The gems were merged either way.",
			              static_cast<unsigned>(bagX),
			              static_cast<unsigned>(bagY),
			              static_cast<unsigned>(refused));
			context->LogWarn(message);
		}
	}

	if (status != D2RL::Items::Result::Success) {
		context->LogError("AutoDeposit: the exchange was rejected; every gem and the bag were left untouched.");
		return;
	}

	AmountRead verify {};
	const bool readBack = ReadBagAmount(context, items, merged, verify);
	std::snprintf(message,
	              sizeof(message),
	              "AutoDeposit: %smerged %u gem(s) and %u cluster(s) for +%u: %u -> %u gems. New bag 0x%llX reads back %s%u.",
	              work.autoMerge ? "auto " : "",
	              static_cast<unsigned>(tookGems),
	              static_cast<unsigned>(tookClusters),
	              static_cast<unsigned>(gained),
	              static_cast<unsigned>(read.amount),
	              static_cast<unsigned>(newTotal),
	              static_cast<unsigned long long>(merged),
	              readBack ? "" : "FAILED - ",
	              readBack ? static_cast<unsigned>(verify.amount) : 0U);
	context->LogInfo(message);

	if (readBack && verify.amount != newTotal) {
		context->LogError("AutoDeposit: the new bag does not read back the amount it was built with.");
	}
	// The console command sweeps everything loose, so nothing is ever
	// held back there. The pickup takes one item and walks past the rest, so say
	// so - that count is the difference the player actually feels.
	if (work.autoMerge && collect.heldBack > 0) {
		std::snprintf(message,
		              sizeof(message),
		              "AutoDeposit: %u other loose gem(s) or cluster(s) were left where they are; only the item you picked up was merged.",
		              static_cast<unsigned>(collect.heldBack));
		context->LogInfo(message);
	}
	if (collect.classicGems > 0) {
		std::snprintf(message,
		              sizeof(message),
		              "AutoDeposit: left %u classic gem(s) where they are. They are worth one credit each like any other gem, but the Grabber only hands back the plain gem, so merging a perfect one would cost you its quality.",
		              static_cast<unsigned>(collect.classicGems));
		context->LogInfo(message);
	}
	if (leftOver > 0 || collect.overflow > 0) {
		std::snprintf(message,
		              sizeof(message),
		              "AutoDeposit: %u item(s) were left in the inventory: %u did not fit in the room left or in the %u inputs one exchange takes, %u were past the %u of one kind a sweep holds. Run the merge again for the rest.",
		              static_cast<unsigned>(leftOver + collect.overflow),
		              static_cast<unsigned>(leftOver),
		              static_cast<unsigned>(MaxMergeInputs),
		              static_cast<unsigned>(collect.overflow),
		              static_cast<unsigned>(MaxMergeInputs));
		context->LogInfo(message);
	}
}

// Runs the work against a snapshot of the request. The slot stays busy until the
// very last write, so the console handler can tell a finished job from one that
// is still queued, and only then reuse it.
void __cdecl GameThreadBagWork(const D2RL::PluginContext* context, void* userData) noexcept {
	auto* state = static_cast<BagWorkState*>(userData);
	if (state == nullptr || context == nullptr) {
		return;
	}

	const BagWorkState work = *state;
	RunMerge(context, work);

	state->ready = true;
}

}  // namespace

// Queues a merge. The pickup hook and the console command both come through
// here, so the one path is what the log describes either way.
//
// autoMerge marks the pickup's merge, which takes only the item named by
// pickupGuid. The console command passes false and zero: it sweeps every loose
// gem, because that is what asking for a merge means.

auto ScheduleMerge(const D2RL::PluginContext* context, bool autoMerge, uint32_t pickupGuid) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	// One merge at a time: the slot the game thread reads from is the one this
	// would overwrite.
	if (g_bagWork.scheduled && !g_bagWork.ready) {
		return false;
	}

	const D2RL::InventoryServiceV1* inventory = nullptr;
	if (context->QueryService(D2RL::ServiceId::Inventory, D2RL::InventoryServiceV1Version, &inventory) != D2RL::ServiceQueryResult::Success
	    || inventory == nullptr) {
		return false;
	}

	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (inventory->getLocalPlayer(context, &player) != D2RL::Inventory::Result::Success) {
		return false;
	}

	const D2RL::ThreadServiceV1* threads = nullptr;
	if (context->QueryService(D2RL::ServiceId::Thread, D2RL::ThreadServiceV1Version, &threads) != D2RL::ServiceQueryResult::Success
	    || threads == nullptr) {
		return false;
	}

	g_bagWork            = BagWorkState {};
	g_bagWork.player     = player;
	g_bagWork.autoMerge  = autoMerge;
	g_bagWork.pickupGuid = pickupGuid;
	g_bagWork.scheduled  = true;

	if (threads->runOnGameThread(context, GameThreadBagWork, &g_bagWork) != D2RL::Threads::Result::Success) {
		g_bagWork = BagWorkState {};
		return false;
	}
	return true;
}
