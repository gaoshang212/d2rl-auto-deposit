// Auto Deposit - one deposit run. See deposit_stash.h.

#include "deposit_stash.h"

#include "deposit_codes.h"
#include "deposit_common.h"
#include "deposit_config.h"
#include "deposit_native.h"

#include <atomic>

namespace {

// What one run concluded, for the summary line.
struct DepositTally {
	uint32_t seen      = 0;
	uint32_t deposited = 0;
	uint32_t ignored   = 0;
	uint32_t skipped   = 0;
	uint32_t noTarget  = 0;
	uint32_t faulted   = 0;
	uint32_t elsewhere = 0;  // in the same container, but not on the inventory grid
};

// Whether the run can already tell, from an item's id alone, that it is not one
// it was asked about. This is the cheap half and it exists for the common case:
// a pickup names one id and every other item in the inventory is ruled out
// without calling anything native about it.
auto SelectedById(const DepositWork& work, uint32_t id) noexcept -> bool {
	return work.sweepAll || ListHasCode(work.ids, work.idCount, id);
}

}  // namespace

void RunDeposit(const D2RL::PluginContext* context, const DepositWork& work, const Snapshot* gathered) noexcept {
	if (!NativeUsable(context)) {
		return;
	}

	// A run that came from a pickup is the automatic path, and it keeps off the
	// stash while the panel is open; the console sweep is exempt, because the
	// player asked and is looking at the result. The pump checks this too, so
	// that the ids stay queued instead of being retired - this is the second
	// look, because the panel is on the player's hands and not on our clock.
	if (!work.sweepAll && StashIsOpen()) {
		if (ClaimNoopLog(g_stashExplainLogs)) {
			D2RL::LogInfo(context, "AutoDeposit: the shared stash is open, so nothing was deposited. What was picked up stays in the inventory and goes in by itself once the panel is closed; 'deposit' does it now.");
		}
		return;
	}

	Snapshot       own {};
	const Snapshot& snapshot = gathered != nullptr ? *gathered : own;
	if (gathered == nullptr && !CollectInventory(own)) {
		D2RL::LogError(context, "AutoDeposit: could not read the inventory.");
		return;
	}

	char         codeText[5] {};
	DepositTally tally {};

	for (size_t index = 0; index < snapshot.count; ++index) {
		if (!SelectedById(work, snapshot.ids[index])) {
			continue;
		}

		ItemFacts facts {};
		if (!InspectItem(snapshot.items[index], facts)) {
			++tally.faulted;
			continue;
		}

		// The grid is the whole of this half's business, and it is the first
		// thing ruled out. A bare 'deposit' takes "everything in the inventory",
		// and the walk's container is larger than the inventory: without this,
		// that command would reach into the belt, the equipped slots and the
		// stash pages and deposit any material it found there. Nothing that
		// moves anything is called for a unit on another page, ever.
		if (facts.page != MainInventoryPage) {
			++tally.elsewhere;
			continue;
		}

		++tally.seen;

		// This plugin's own reason to leave the item alone, decided before the
		// game is asked anything about it. It comes first because it is the run's
		// policy and not the game's answer, and because the code it tests is the
		// one already read above - the native half never has to ask for it.
		if (StashIgnores(facts.code)) {
			++tally.ignored;
			continue;
		}

		const Verdict verdict = DepositNative(snapshot.player, snapshot.items[index]);

		switch (verdict) {
			case Verdict::Deposited:
				++tally.deposited;
				// StashDeposit returns void, so all this can honestly claim is
				// that the call returned without faulting. The item cannot be
				// read back here either: the call relocates the unit, and the
				// container goes on listing it for a moment afterwards. The bag
				// losing it and the tab's stack going up is the confirmation.
				D2RL::LogInfoF(context,
				               "AutoDeposit: %s (id %u) was handed to the game's deposit call, which returned. StashDeposit is void, so that is not an answer - the inventory losing it and the tab's stack going up is the confirmation.",
				               CodeText(facts.code, codeText),
				               static_cast<unsigned>(facts.id));
				break;
			case Verdict::NoTarget:
				++tally.noTarget;
				D2RL::LogInfoF(context,
				               "AutoDeposit: %s (id %u) stays put - the game gave no advanced-stash unit to deposit into. It was left where it is; nothing was destroyed.",
				               CodeText(facts.code, codeText),
				               static_cast<unsigned>(facts.id));
				break;
			case Verdict::Faulted:
				++tally.faulted;
				D2RL::LogErrorF(context,
				                "AutoDeposit: %s (id %u) raised inside a native call and was left alone. That is a bug worth reporting.",
				                CodeText(facts.code, codeText),
				                static_cast<unsigned>(facts.id));
				break;
			default:
				// The game saying no: blocked, or not a material. A class id that
				// could not be read never gets this far.
				++tally.skipped;
				if (ClaimNoopLog(g_stashNoopLogs)) {
					D2RL::LogInfoF(context,
					               "AutoDeposit: %s (id %u) stays put - %s.",
					               CodeText(facts.code, codeText),
					               static_cast<unsigned>(facts.id),
					               VerdictName(verdict));
				}
				break;
		}
	}

	// A pickup run that found none of the items it named had nothing to offer.
	// The pump only hands over ids it has just seen in the container, so this is
	// the narrow case of an item leaving between that read and this one - and it
	// is said rather than passed over, because a run that deposits nothing and
	// logs nothing is indistinguishable from a run that never happened.
	if (!work.sweepAll && tally.seen == 0) {
		if (ClaimNoopLog(g_stashExplainLogs)) {
			D2RL::LogInfo(context, "AutoDeposit: the item just picked up was not in the inventory by the time it was looked at, so there was nothing to offer. It may have gone somewhere else, or the pickup may not have put it in the inventory at all.");
		}
		return;
	}

	D2RL::LogInfoF(context,
	               "AutoDeposit: run done. %u item(s) on the inventory grid were looked at: %u deposited, %u ignored by config, %u skipped, %u with no stash to take them, %u faulted. %u more unit(s) in the same container are not grid items and were not looked at.",
	               static_cast<unsigned>(tally.seen),
	               static_cast<unsigned>(tally.deposited),
	               static_cast<unsigned>(tally.ignored),
	               static_cast<unsigned>(tally.skipped),
	               static_cast<unsigned>(tally.noTarget),
	               static_cast<unsigned>(tally.faulted),
	               static_cast<unsigned>(tally.elsewhere));
}

namespace {

DepositWork       g_sweepWork {};
std::atomic<bool> g_sweepQueued { false };

void __cdecl GameThreadSweep(const D2RL::PluginContext* context, void* userData) noexcept {
	auto* work = static_cast<DepositWork*>(userData);
	if (work == nullptr || context == nullptr) {
		return;
	}

	const DepositWork copy = *work;
	RunDeposit(context, copy);

	g_sweepQueued.store(false);
}

}  // namespace

auto ScheduleSweep(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	// One sweep at a time: the slot the game thread reads from is the one this
	// would overwrite.
	bool expected = false;
	if (!g_sweepQueued.compare_exchange_strong(expected, true)) {
		return false;
	}

	const D2RL::ThreadServiceV1* threads = ThreadServiceOf(context);
	if (threads == nullptr) {
		g_sweepQueued.store(false);
		return false;
	}

	g_sweepWork          = DepositWork {};
	g_sweepWork.sweepAll = true;

	if (threads->runOnGameThread(context, GameThreadSweep, &g_sweepWork) != D2RL::Threads::Result::Success) {
		g_sweepQueued.store(false);
		return false;
	}
	return true;
}
