// Auto Deposit - the console command. See deposit_console.h.

#include "deposit_console.h"

#include "deposit_common.h"
#include "deposit_config.h"
#include "deposit_gem_bag.h"
#include "deposit_stash.h"

namespace {

void ReportLine(const D2RL::PluginContext* context, const D2RL::ConsoleCommandContext* command, const char* text) noexcept {
	context->LogInfo(text);
	if (command != nullptr && command->plugin != nullptr) {
		command->plugin->WriteConsoleMessage(text);
	}
}

}  // namespace

// One console command for both destinations. Both halves are automatic, so
// everything here is the by-hand sweep: the things that were already lying in
// the inventory before this plugin was loaded.
auto DepositCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* command, void* userData) noexcept
    -> D2RL::ConsoleCommandResult {

	(void)client;
	(void)userData;

	if (command == nullptr || command->plugin == nullptr || g_context == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}

	const D2RL::PluginContext* context = g_context;
	const char*                args    = command->args != nullptr ? command->args : "";

	// The read-only check: the same read the merge acts on, reported and thrown
	// away. Nothing is consumed, so it can be asked as often as wanted.
	if (MatchWord(args, "probe")) {
		if (!ScheduleProbe(context)) {
			ReportLine(context, command, "AutoDeposit: nothing was queued - no game to work in, or something else is already running.");
			return D2RL::ConsoleCommandResult::Handled;
		}
		ReportLine(context, command, "AutoDeposit: bag probe queued on the game thread. What the counter reads lands in the log in a moment.");
		return D2RL::ConsoleCommandResult::Handled;
	}

	if (MatchWord(args, "bag") || MatchWord(args, "merge")) {
		if (!ScheduleMerge(context, false, 0)) {
			ReportLine(context, command, "AutoDeposit: nothing was queued - no game to work in, or the bag merge is already running.");
			return D2RL::ConsoleCommandResult::Handled;
		}
		ReportLine(context, command, "AutoDeposit: bag merge queued on the game thread. The result lands in the log in a moment.");
		return D2RL::ConsoleCommandResult::Handled;
	}

	if (args[0] == 0 || MatchWord(args, "stash")) {
		if (!ScheduleSweep(context)) {
			ReportLine(context, command, "AutoDeposit: nothing was queued - no game to work in, or a sweep is already running.");
			return D2RL::ConsoleCommandResult::Handled;
		}
		ReportLine(context, command, "AutoDeposit: stash sweep queued on the game thread. The result lands in the log in a moment.");
		return D2RL::ConsoleCommandResult::Handled;
	}

	ReportLine(context,
	           command,
	           "AutoDeposit: unknown command. 'deposit' sweeps the inventory into the advanced stash, 'deposit bag' merges the loose gems into the Gem Bag. Both destinations are automatic on pickup.");
	return D2RL::ConsoleCommandResult::Handled;
}
