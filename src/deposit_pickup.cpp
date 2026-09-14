// Auto Deposit - the pickup hook and the queue behind it. See deposit_pickup.h.

#include "deposit_pickup.h"

#include "deposit_common.h"
#include "deposit_config.h"
#include "deposit_gem_bag.h"
#include "deposit_native.h"
#include "deposit_stash.h"

#include <atomic>
#include <cstdio>
#include <windows.h>

namespace {

// ---------------------------------------------------------------------------
// Pickups that have not arrived yet
// ---------------------------------------------------------------------------
//
// A pickup is not the same event as the item's arrival. The game's pickup
// routine returns as soon as the click is accepted, and the item turns up in the
// inventory a moment later, on a later game update - so a deposit asked for in
// the update that follows the click can find nothing there at all.
//
// So the hook does not ask for a deposit. It writes the id down, and a pump on
// the game thread keeps looking for it for a while. Two things fall out of that
// which a one-shot request cannot have: an item that arrives late is still dealt
// with, and a burst of pickups no longer loses everything after the first,
// because ids are recorded rather than slotted into a queue that holds one job.
//
// The queue holds ids, never item pointers: an entry that never arrives costs an
// expired entry, not a stale pointer. Everything here is touched by the game
// thread alone (the hook and the pump both run on it); the lock is for the
// console thread, which reports the queue's length.

constexpr size_t MaxPendingPickups = 32;

// How long a pickup's id is worth looking for. Waiting costs nothing: nothing is
// held but the number.
constexpr uint64_t PendingLifetimeMs = 10000;

// How often the pump looks, and how long it may go quiet before a pickup starts
// a new one. The second is the only thing that notices a pump which stopped: a
// session change discards queued game work, and the next pickup is what brings
// it back.
constexpr uint64_t PumpIntervalMs     = 100;
constexpr uint64_t PumpRestartAfterMs = 1000;

struct PendingPickup {
	uint32_t id      = 0;
	uint64_t expires = 0;
};

PendingPickup g_pending[MaxPendingPickups] {};
size_t        g_pendingCount = 0;
SRWLOCK       g_pendingLock  = SRWLOCK_INIT;

// The pump's ticket, and when it last ran. Starting a pump takes a new ticket,
// which is how an older one that is still queued retires itself rather than
// piling up a second walker on the same queue.
std::atomic<uint64_t> g_pumpTicket { 0 };
std::atomic<uint64_t> g_pumpLastTick { 0 };
bool                  g_pumpWarningSaid = false;

auto RecordPickup(uint32_t id) noexcept -> void {
	if (id == 0) {
		return;
	}
	const uint64_t now = GetTickCount64();
	AcquireSRWLockExclusive(&g_pendingLock);
	for (size_t index = 0; index < g_pendingCount; ++index) {
		if (g_pending[index].id == id) {
			g_pending[index].expires = now + PendingLifetimeMs;
			ReleaseSRWLockExclusive(&g_pendingLock);
			return;
		}
	}
	if (g_pendingCount == MaxPendingPickups) {
		// Full. The entry that has waited longest is the one to lose; a queue
		// this deep has never been seen, and dropping the newest would lose the
		// pickup that just happened.
		size_t oldest = 0;
		for (size_t index = 1; index < g_pendingCount; ++index) {
			if (g_pending[index].expires < g_pending[oldest].expires) {
				oldest = index;
			}
		}
		g_pending[oldest] = PendingPickup { id, now + PendingLifetimeMs };
	} else {
		g_pending[g_pendingCount++] = PendingPickup { id, now + PendingLifetimeMs };
	}
	ReleaseSRWLockExclusive(&g_pendingLock);
}

auto RetirePending(const uint32_t* ids, size_t count) noexcept -> void {
	if (count == 0) {
		return;
	}
	AcquireSRWLockExclusive(&g_pendingLock);
	for (size_t at = 0; at < count; ++at) {
		for (size_t index = 0; index < g_pendingCount; ++index) {
			if (g_pending[index].id == ids[at]) {
				g_pending[index] = g_pending[g_pendingCount - 1];
				--g_pendingCount;
				break;
			}
		}
	}
	ReleaseSRWLockExclusive(&g_pendingLock);
}

// One look at the queue. Ids the container holds are handed to a deposit run;
// ids that have waited out their lifetime are dropped and said out loud; the
// rest keep their place.
void PumpOnce(const D2RL::PluginContext* context) noexcept {
	// Nothing native may be called when the signature check did not pass, and
	// nothing is retired either: an id dropped here is a pickup lost to a build
	// this plugin cannot act on, when leaving it queued costs only a number. Read
	// directly rather than through NativeUsable, which reports the failure by
	// name - at ten ticks a second, that report would be the log.
	if (!NativeReady()) {
		return;
	}

	PendingPickup pending[MaxPendingPickups] {};
	size_t        count = 0;
	AcquireSRWLockShared(&g_pendingLock);
	for (size_t index = 0; index < g_pendingCount; ++index) {
		pending[count++] = g_pending[index];
	}
	ReleaseSRWLockShared(&g_pendingLock);
	if (count == 0) {
		return;
	}

	// This is the check that matters for the open panel: the ids stay queued, so
	// closing the panel lets the deposit happen instead of losing the pickup.
	if (StashIsOpen()) {
		return;
	}

	Snapshot snapshot {};
	if (!CollectInventory(snapshot)) {
		// No player yet, or the container cannot be read this tick. Nothing is
		// retired: the next tick tries again.
		return;
	}

	// Every reason a run could not go ahead has now been ruled out - the
	// signatures, the open panel, and the container above - so a batch that is
	// queued here is a batch that runs. That is what makes retiring its ids safe:
	// an id dropped on a tick that could not act would be a pickup lost to a bad
	// moment rather than to a decision.
	const uint64_t now         = GetTickCount64();
	uint32_t       batch[DepositWork::MaxBatch] {};
	size_t         batchCount  = 0;
	uint32_t       retire[MaxPendingPickups] {};
	size_t         retireCount = 0;
	uint32_t       never[4] {};
	size_t         neverCount  = 0;

	for (size_t index = 0; index < count; ++index) {
		bool present = false;
		for (size_t at = 0; at < snapshot.count && !present; ++at) {
			present = snapshot.ids[at] == pending[index].id;
		}
		if (present) {
			if (batchCount < DepositWork::MaxBatch) {
				batch[batchCount++]   = pending[index].id;
				retire[retireCount++] = pending[index].id;
			}
			// A full batch leaves the rest queued: they are still in the
			// container, so the next tick takes them.
			continue;
		}
		if (now >= pending[index].expires) {
			retire[retireCount++] = pending[index].id;
			if (neverCount < sizeof(never) / sizeof(never[0])) {
				never[neverCount++] = pending[index].id;
			}
		}
	}

	RetirePending(retire, retireCount);

	// An id that never turned up is the one thing this design cannot deposit: the
	// pickup merged into a stack that was already in the inventory, so the unit
	// the id named stopped existing before it could be seen. Saying so is the
	// difference between a known miss and a silent one.
	for (size_t index = 0; index < neverCount; ++index) {
		char message[320] {};
		std::snprintf(message,
		              sizeof(message),
		              "AutoDeposit: the item picked up as id %u never appeared in the inventory, so it was left alone. The likely reason is that it merged into a stack that was already there - a merged pickup carries no unit of its own to deposit.",
		              static_cast<unsigned>(never[index]));
		context->LogInfo(message);
	}

	if (batchCount == 0) {
		return;
	}

	DepositWork work {};
	for (size_t index = 0; index < batchCount; ++index) {
		work.ids[index] = batch[index];
	}
	work.idCount = batchCount;
	RunDeposit(context, work);
}

void __cdecl PickupPump(const D2RL::PluginContext* context, void* userData) noexcept {
	const uint64_t ticket = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(userData));
	if (ticket != g_pumpTicket.load()) {
		return;  // A newer pump owns the queue now.
	}

	const uint64_t now = GetTickCount64();
	if (now >= g_pumpLastTick.load() + PumpIntervalMs) {
		g_pumpLastTick.store(now);
		PumpOnce(context);
	}

	// The pump re-posts itself, which is how it keeps running without anything
	// having to tick it. It stops by not being re-posted: a session change
	// discards queued game work and an unload does the same, and the stale
	// timestamp is what the next pickup notices.
	const D2RL::ThreadServiceV1* threads = ThreadServiceOf(context);
	if (threads == nullptr
	    || threads->runOnGameThread(context, PickupPump, userData) != D2RL::Threads::Result::Success) {
		g_pumpLastTick.store(0);
	}
}

}  // namespace

// Defined outside the anonymous namespace above, and declared in the header: the
// entry point starts the pump at load, before the first pickup has happened.
auto StartPump(const D2RL::PluginContext* context) noexcept -> bool {
	const D2RL::ThreadServiceV1* threads = ThreadServiceOf(context);
	if (threads == nullptr) {
		return false;
	}

	const uint64_t ticket = g_pumpTicket.fetch_add(1) + 1;
	g_pumpLastTick.store(GetTickCount64());
	if (threads->runOnGameThread(context, PickupPump, reinterpret_cast<void*>(static_cast<uintptr_t>(ticket))) != D2RL::Threads::Result::Success) {
		g_pumpLastTick.store(0);
		return false;
	}
	return true;
}

namespace {

// Called from the pickup hook, on the game thread, once per pickup. Starting a
// pump is cheap and the ticket makes an extra one harmless, so the only thing
// held back is starting one while another is demonstrably alive.
auto EnsurePump(const D2RL::PluginContext* context) noexcept -> void {
	const uint64_t last = g_pumpLastTick.load();
	if (last != 0 && GetTickCount64() < last + PumpRestartAfterMs) {
		return;
	}
	if (!StartPump(context)) {
		if (!g_pumpWarningSaid) {
			g_pumpWarningSaid = true;
			context->LogWarn("AutoDeposit: the game thread service is not available, so what you pick up will not be deposited by itself. It is the only thread an item may be moved on, and a remote TCP/IP client has none. 'deposit' still works.");
		}
		return;
	}
	// Only reached when the pump was not demonstrably alive, so this is the
	// restart being said out loud - including the one at the first pickup of a
	// session, when the load-time start had nothing to post to yet.
	context->LogInfo("AutoDeposit: the game-thread pump is running; a pickup is written down and looked for until it turns up, which is what covers a burst of pickups and an item the client delivers a moment late.");
}

// ---------------------------------------------------------------------------
// The pickup hook
// ---------------------------------------------------------------------------
//
// The routine the game runs when something on the ground is picked up. It fires
// on the player's own click to pick up, not only on an auto-pickup path, which
// is what Auto Belt Refill's comment claimed for this address and what the first
// run of this hook disproved - so it is the pickup event the SDK does not have.
//
// The signature is the one that plugin documented for build 92777. It is checked
// before anything is patched, and matched this build (3.3.0, 93847) as well.
//   bool __fastcall(void* player, uint32_t guid, bool, uint32_t, bool, bool)
//
// A wrong address costs a crash on the next pickup, not a damaged save: nothing
// is patched unless the bytes match.

constexpr uint8_t PickupBytes[32] {
	0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
	0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
	0x48, 0x8D, 0x6C, 0x24, 0xE9, 0x48, 0x81, 0xEC,
	0xA0, 0x00, 0x00, 0x00, 0x45, 0x33, 0xE4, 0x44,
};

using PickupFn = bool(__fastcall*)(void* player, uint32_t guid, bool arg3, uint32_t arg4, bool arg5, bool arg6) noexcept;

PickupFn  g_originalPickup = nullptr;
bool      g_pickupHookLive = false;

// Called by the game, on its own thread, in the middle of its pickup routine.
// It does three small things and nothing else: hand the call to the original,
// write the item's id down for each half that wants it, and return.
//
// It reads no item, moves no item and creates no item. Both jobs it asks for are
// queued, and run later on the game thread - that is the only place item
// mutation is allowed, and this is not that place.
//
// guid names the item that arrived, and it is the one thing that separates it
// from the things deliberately left loose in the inventory.
auto __fastcall HookPickup(void* player, uint32_t guid, bool arg3, uint32_t arg4, bool arg5, bool arg6) noexcept -> bool {

	const bool result = g_originalPickup != nullptr ? g_originalPickup(player, guid, arg3, arg4, arg5, arg6) : false;

	if (result && g_context != nullptr) {
		const D2RL::PluginContext* context = g_context;

		// The bag first, and deliberately: it takes the gem out of the inventory
		// itself, so the merge is the one that has to win the race when both
		// halves look at the same item. The routing in StashIgnores means they
		// never actually want the same code, and this is the belt to that braces.
		if (g_gemBagMergeGems || g_gemBagMergeClusters) {
			ScheduleMerge(context, true, guid);
		}

		if (g_stashDeposit) {
			RecordPickup(guid);
			EnsurePump(context);
		}
	}

	return result;
}

}  // namespace

auto InstallPickupHook(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	if (g_pickupHookLive) {
		context->LogInfo("AutoDeposit: the pickup hook is already installed.");
		return true;
	}
	if (!context->CheckExpectedBytes(0x471950, PickupBytes, sizeof(PickupBytes))) {
		context->LogError("AutoDeposit: the Pickup signature does not match this build, so nothing was patched. Picked-up items will land in the inventory as usual.");
		return false;
	}

	void* original = nullptr;
	const auto registration = D2RL::MakeInlineHook(0x471950, PickupBytes, static_cast<uint32_t>(sizeof(PickupBytes)), reinterpret_cast<void*>(&HookPickup), &original);
	if (!context->InstallInlineHook(registration)) {
		context->LogError("AutoDeposit: the loader refused the pickup hook.");
		return false;
	}
	g_originalPickup = reinterpret_cast<PickupFn>(original);
	g_pickupHookLive = true;
	// Give this run its full quota of "found nothing to do" lines; the count is
	// there to keep a long session quiet, not to keep a test quiet.
	g_gemBagNoopLogs       = 0;
	g_stashNoopLogs    = 0;
	g_stashExplainLogs = 0;
	context->LogInfo("AutoDeposit: pickup hook installed. What you pick up now goes where it belongs on its own.");
	return true;
}
