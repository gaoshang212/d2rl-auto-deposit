// Auto Deposit - what you pick up goes where it belongs, on its own.
//
// Two destinations, one pickup:
//
//   Gem Bag   Reimagined's Gem Bag is a single 1x1 item (code "bag") whose stored
//             amount lives in ItemStatCost 386, "stacked_gem" - a 16-bit saved
//             stat capped at 65535. A gem or a gem cluster that is picked up is
//             merged into it through an item transaction.
//   Stash     The advanced stash tabs. An item that is picked up is handed to the
//             game's own deposit call, if the game counts it among the things
//             that belong there. Nothing is dragged and no key is pressed: the
//             game's own routine is called directly, by address, on the game
//             thread.
//
// The gem bag half comes from d2rl-auto-gem-bag; the stash half is the deposit
// path lifted out of d2rl-auto-stash. They live in one plugin because they are
// one decision - what happens to the thing you just picked up - and because the
// split between them is a routing question that only makes sense answered in one
// place. A gem belongs in the bag, so while the bag is switched on the stash path
// does not consider gems at all and the two can never fight over the same item.
//
// Everything either half can be told not to touch is a code in the config file.
// The lists are ignore lists: what is not named goes where it belongs.
//
// This file is the entry point and nothing else. Where things live:
//
//   deposit_common           the context, the thread service, the log budgets
//   deposit_config           the config file, and the routing between the halves
//   deposit_codes            item codes, the gem tables, and their text form
//   deposit_native_signatures  the addresses the stash half calls, and the bytes
//                            each should hold - nothing else includes this
//   deposit_native           those calls, guarded; what each concluded
//   deposit_stash            one deposit run, and the sweep the console asks for
//   deposit_gem_bag          the bag transaction. Reaches the game through the SDK
//                            alone, so it survives a build the stash half cannot
//   deposit_pickup           the pickup hook, and the queue carrying what it saw
//   deposit_console          the 'deposit' command

#include <D2RLPlugin/api.h>

#include <cstdio>

#include "deposit_common.h"
#include "deposit_config.h"
#include "deposit_console.h"
#include "deposit_native.h"
#include "deposit_pickup.h"

namespace {

constexpr D2RL::PluginInfo DepositPluginInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.apiVersion  = D2RL_PLUGIN_API_VERSION,
	.id          = "d2rl-auto-deposit",
	.name        = "Auto Deposit",
	.version     = "0.1.1",
	.author      = "gaoshang212",
	.description = "What you pick up goes where it belongs: gems into Reimagined's Gem Bag, everything the game counts as stash material into the advanced stash. Ignore lists in the config file.",
	// NativeHooks is required for both halves. Items::editNativeItem is the only
	// way to reach the bag's stat array - the SDK exposes no stat read or write -
	// and the deposit calls D2R.exe directly, which is what the flag is for.
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::ModScopedOnly | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &DepositPluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {

	if (context == nullptr) {
		return false;
	}

	g_context = context;

	char message[256] {};
	std::snprintf(message,
			sizeof(message),
			"AutoDeposit: loaded. mod=%s build=%s exeBase=0x%llX",
			context->activeMod != nullptr ? context->activeMod : "(none)",
			context->buildName != nullptr ? context->buildName : "?",
			static_cast<unsigned long long>(context->exeBase));
	context->LogInfo(message);

	LoadConfig(context);

	// Checked whatever the switches say. It is sixteen byte comparisons, and the
	// console sweep is allowed to run with the automatic paths switched off - so
	// a table that was never checked would turn that command into a line saying
	// nobody had looked yet.
	CheckNative(context, context->exeBase);

	if (!context->RegisterConsoleCommand("deposit",
			DepositCommand,
			"Auto Deposit: 'deposit' sweeps the inventory into the advanced stash, 'deposit bag' merges loose gems into the Gem Bag. Both happen on their own as you pick things up.")) {
		context->LogError("AutoDeposit: failed to register the 'deposit' console command.");
		return false;
	}

	if (!AnyAutomaticWorkEnabled()) {
		context->LogInfo("AutoDeposit: every switch is off in the config, so nothing is patched and what you pick up stays where it lands. The console command still works on request.");
		return true;
	}

	// The point of the plugin: both destinations, with no key at all. The
	// signature is checked inside, so a game build this address does not belong
	// to costs the feature, not the game.
	//
	// Started here so the pump is alive before the first pickup rather than only
	// from it: a session load has nothing to post to yet, and the next pickup is
	// what starts it then. StartPump asks for the thread service itself and
	// reports whether it got it, so asking on its behalf would be the same
	// question twice and the answer is not used either way.
	if (InstallPickupHook(context)) {
		StartPump(context);
	}

	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	// Queued work that fires after this would run against a context that is gone.
	// The hook itself stays patched - the loader owns that - but it finds no
	// context and does nothing. The pump retires itself the same way: its ticket
	// is never renewed, so the next tick finds no context to run against and
	// stops re-posting.
	g_context = nullptr;
}
