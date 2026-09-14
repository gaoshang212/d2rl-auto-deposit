// Auto Deposit - the plugin context, the SDK's thread service, and the log
// budgets that more than one module counts against.

#include "deposit_common.h"

const D2RL::PluginContext* g_context = nullptr;

// Ahead of the hook, because these go silent as they are spent: the pickup hook
// resets them when it is installed, so a reloaded plugin starts talking again.
uint32_t g_gemBagNoopLogs    = 0;
uint32_t g_stashNoopLogs    = 0;
uint32_t g_stashExplainLogs = 0;

auto ThreadServiceOf(const D2RL::PluginContext* context) noexcept -> const D2RL::ThreadServiceV1* {
	const D2RL::ThreadServiceV1* threads = nullptr;
	if (context == nullptr
	    || context->QueryService(D2RL::ServiceId::Thread, D2RL::ThreadServiceV1Version, &threads) != D2RL::ServiceQueryResult::Success
	    || threads == nullptr
	    || !D2RL::HasThreadServiceV1Field(threads, D2RL::ThreadServiceV1RequiredSize)) {
		return nullptr;
	}
	return threads;
}
