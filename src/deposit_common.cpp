// Auto Deposit - the plugin context, the SDK's thread service, and the log
// budgets that more than one module counts against.

#include "deposit_common.h"

#include <cstdio>
#include <cstring>

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

auto ClaimNoopLog(uint32_t& budget) noexcept -> bool {
	if (budget >= AutoNoopLogLimit) {
		return false;
	}
	++budget;
	return true;
}

auto AppendList(char* out, size_t capacity, size_t& used, const char* separator, const char* text) noexcept -> void {
	if (used >= capacity) {
		return;
	}
	const int written = std::snprintf(out + used, capacity - used, "%s%s", used != 0 ? separator : "", text);
	if (written > 0) {
		const size_t added = static_cast<size_t>(written);
		used               = used + added < capacity ? used + added : capacity - 1;
	}
}

auto MatchWord(const char* text, const char* word) noexcept -> bool {
	const size_t length = std::strlen(word);
	if (std::strncmp(text, word, length) != 0) {
		return false;
	}

	const char next = text[length];
	return next == 0 || next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '#' || next == '"' || next == '\'' || next == ',';
}

auto ListHasCode(const uint32_t* codes, size_t count, uint32_t code) noexcept -> bool {
	for (size_t index = 0; index < count; ++index) {
		if (codes[index] == code) {
			return true;
		}
	}
	return false;
}
