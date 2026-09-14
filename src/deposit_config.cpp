// Auto Deposit - reading the config file.

#include "deposit_config.h"
#include "deposit_codes.h"
#include "deposit_common.h"

#include <cstring>
#include <iterator>

namespace {

// ---------------------------------------------------------------------------
// The config file
// ---------------------------------------------------------------------------
//
// D2RLoader writes <scope>/d2rloader/config/d2rl-auto-deposit.toml out of the
// copy embedded in this DLL the first time the plugin loads, and leaves it alone
// from then on, so edits survive. It is the file the switches below come from,
// and it is read once, at load.
//
// The SDK hands over the file's raw text and nothing more - it carries no TOML
// parser - and there are five keys, so they are read right here rather than pull
// a parser in for the rest of a file this plugin will never look at.
// ---------------------------------------------------------------------------

// One byte of the buffer is held back for the terminator; the file itself is a
// few dozen lines of comments.
constexpr size_t ConfigMaxBytes = 4096;

// What the DLL ships as the defaults, and what is used if the file cannot be
// read at all. Everything is on: an item you did not ask to be left alone goes
// where it belongs.
constexpr bool        GemBagMergeGemsDefault     = true;
constexpr bool        GemBagMergeClustersDefault = true;
constexpr bool        StashDepositDefault        = true;
// The stash half leaves nothing alone out of the box: the list is empty.
//
// It shipped as "rvs rvl", the two rejuvenation potions, carried across from
// d2rl-auto-stash, which mirrored it from Paragon System 1.7.8, which took it
// from a reference implementation neither of them names. The exclusion arrived
// without its reason, and a fresh project reproducing an unexplained exception is
// how a wrong default outlives everyone who could have questioned it. Both codes
// are advanced-stash material - the game's own StashItemOk accepts them - so
// nothing here says the game wants them left alone.
//
// It is a preference, not a rule. Put the codes back, here or in the config file,
// if you would rather they stayed in the inventory: "rvs rvl".
constexpr const char* StashIgnoreDefault = "";

// How many codes one ignore list holds. A list this long has never been seen;
// the ceiling is here so a runaway file cannot walk off the end of an array.
constexpr size_t MaxIgnoreCodes = 32;

}  // namespace

namespace {

// True when line is a setting line for `key`; sets value to the first character
// of its value when it is. Line-oriented on purpose: a key is the first thing on
// its line, so a mention of one inside a comment cannot be mistaken for the real
// setting. Skipping '#' and '[' is what keeps commented-out settings and section
// headers from reading as settings of their own.
auto LineIsFor(const char* line, const char* lineEnd, const char* key, const char*& value) noexcept -> bool {
	const size_t keyLength = std::strlen(key);

	const char* cursor = line;
	while (cursor < lineEnd && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r')) {
		++cursor;
	}

	if (*cursor == '#' || *cursor == '[' || static_cast<size_t>(lineEnd - cursor) <= keyLength || std::strncmp(cursor, key, keyLength) != 0) {
		return false;
	}

	const char* at = cursor + keyLength;
	while (at < lineEnd && (*at == ' ' || *at == '\t')) {
		++at;
	}
	if (at >= lineEnd || *at != '=') {
		return false;
	}
	++at;
	while (at < lineEnd && (*at == ' ' || *at == '\t')) {
		++at;
	}

	value = at;
	return true;
}

// The value of `key` as the range [begin, end), or false when the file does not
// say. A range rather than a pointer because the caller reads a list of words:
// the value ends at the newline, and a caller handed only a pointer would walk
// off the end of the line and into the next setting.
auto ConfigValue(const char* text, const char* key, const char*& begin, const char*& end) noexcept -> bool {
	for (const char* line = text; line != nullptr && *line != 0;) {
		const char* lineEnd = std::strchr(line, '\n');
		if (lineEnd == nullptr) {
			lineEnd = line + std::strlen(line);
		}

		const char* value = nullptr;
		if (LineIsFor(line, lineEnd, key, value)) {
			// A value may be wrapped in quotes, and the two list keys are
			// documented that way. A setting that has to be written without them
			// to work is a trap, so both spellings read the same.
			if (value < lineEnd && (*value == '"' || *value == '\'')) {
				const char quote = *value;
				const char* closing = value + 1;
				while (closing < lineEnd && *closing != quote) {
					++closing;
				}
				begin = value + 1;
				end   = closing;
			} else {
				begin = value;
				end   = lineEnd;
			}
			return true;
		}

		line = *lineEnd == '\n' ? lineEnd + 1 : lineEnd;
	}

	return false;
}

// True when the value at the key is one of the two spellings of yes.
//
// Anything that is not plainly true or false falls back to the built-in default
// rather than to false, so a typo cannot quietly switch a feature off.
auto ConfigBool(const char* text, const char* key, bool fallback) noexcept -> bool {
	const char* begin = nullptr;
	const char* end   = nullptr;
	if (!ConfigValue(text, key, begin, end)) {
		return fallback;
	}

	if (MatchWord(begin, "true") || MatchWord(begin, "1")) {
		return true;
	}
	if (MatchWord(begin, "false") || MatchWord(begin, "0")) {
		return false;
	}
	return fallback;
}

struct CodeListParse {
	size_t count   = 0;
	size_t bad     = 0;  // not a three-character code
	size_t dropped = 0;  // past the ceiling, so not kept
};

// Reads one ignore list: the items to leave alone, separated by spaces or
// commas, with anything after a '#' ignored.
//
// Anything that is not exactly three characters is counted rather than quietly
// truncated to three: 'gc' and 'gmm1' are mistakes, and a list that silently
// means something other than what it says is how a plugin ends up ignoring the
// wrong things.
auto ParseCodeList(const char* begin, const char* end, uint32_t* out, size_t capacity, size_t& count) noexcept -> CodeListParse {
	CodeListParse result {};
	count = 0;

	const char* cursor = begin;
	while (cursor != nullptr && cursor < end && *cursor != 0) {
		while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ',')) {
			++cursor;
		}
		if (cursor >= end || *cursor == 0 || *cursor == '#') {
			break;
		}

		const char* tokenStart = cursor;
		while (cursor < end && *cursor != 0 && *cursor != ' ' && *cursor != '\t' && *cursor != '\r'
		       && *cursor != '\n' && *cursor != ',' && *cursor != '#') {
			++cursor;
		}

		const size_t length = static_cast<size_t>(cursor - tokenStart);
		if (length != 3) {
			++result.bad;
		} else if (count < capacity) {
			out[count++] = PackCodeToken(tokenStart);
			++result.count;
		} else {
			++result.dropped;
		}
	}

	return result;
}

// The two ignore lists, as the three-byte codes above. Written once at load,
// read on the game thread, so they need no lock.
uint32_t g_gemBagIgnore[MaxIgnoreCodes] {};
size_t   g_gemBagIgnoreCount = 0;
uint32_t g_stashIgnore[MaxIgnoreCodes] {};
size_t   g_stashIgnoreCount  = 0;

// The shipped default for stash_ignore, applied whenever the file does not name
// it - or cannot be read at all.
auto ParseDefaultStashIgnore() noexcept -> CodeListParse {
	const size_t length = std::strlen(StashIgnoreDefault);
	return ParseCodeList(StashIgnoreDefault, StashIgnoreDefault + length, g_stashIgnore, MaxIgnoreCodes, g_stashIgnoreCount);
}

}  // namespace

// The switches. Read at load and never written again, so the game thread can
// read them without a lock.
bool                       g_gemBagMergeGems     = GemBagMergeGemsDefault;
bool                       g_gemBagMergeClusters = GemBagMergeClustersDefault;
bool                       g_stashDeposit        = StashDepositDefault;

// Reads the switches and the lists and says what they are. Called once, at load,
// before the pickup hook goes in.
auto LoadConfig(const D2RL::PluginContext* context) noexcept -> void {
	// Zeroed, so a full read still leaves the text terminated.
	char     text[ConfigMaxBytes] {};
	uint32_t required = 0;
	if (!context->ReadConfig(text, static_cast<uint32_t>(sizeof(text) - 1), &required)) {
		D2RL::LogWarnF(context, "AutoDeposit: could not read d2rloader/config/d2rl-auto-deposit.toml; the built-in defaults are in use (every switch on, both ignore lists empty).");
		ParseDefaultStashIgnore();
		return;
	}

	g_gemBagMergeGems     = ConfigBool(text, "gem_bag_merge_gems", GemBagMergeGemsDefault);
	g_gemBagMergeClusters = ConfigBool(text, "gem_bag_merge_clusters", GemBagMergeClustersDefault);
	g_stashDeposit        = ConfigBool(text, "stash_deposit", StashDepositDefault);

	const char* begin = nullptr;
	const char* end   = nullptr;

	CodeListParse gemParse {};
	if (ConfigValue(text, "gem_bag_ignore", begin, end)) {
		gemParse = ParseCodeList(begin, end, g_gemBagIgnore, MaxIgnoreCodes, g_gemBagIgnoreCount);
	} else {
		g_gemBagIgnoreCount = 0;
	}

	CodeListParse stashParse {};
	if (ConfigValue(text, "stash_ignore", begin, end)) {
		stashParse = ParseCodeList(begin, end, g_stashIgnore, MaxIgnoreCodes, g_stashIgnoreCount);
	} else {
		stashParse = ParseDefaultStashIgnore();
	}

	D2RL::LogInfoF(context,
	               "AutoDeposit: config says gem_bag_merge_gems=%s, gem_bag_merge_clusters=%s, stash_deposit=%s, %u gem-bag ignore code(s), %u stash ignore code(s).",
	               g_gemBagMergeGems ? "true" : "false",
	               g_gemBagMergeClusters ? "true" : "false",
	               g_stashDeposit ? "true" : "false",
	               static_cast<unsigned>(g_gemBagIgnoreCount),
	               static_cast<unsigned>(g_stashIgnoreCount));

	// A list that says something other than what it means is the one failure a
	// config reader must not pass over in silence.
	if (gemParse.bad != 0 || gemParse.dropped != 0) {
		D2RL::LogWarnF(context,
		               "AutoDeposit: gem_bag_ignore has %u entr(ies) that are not three-character codes and %u past the %u a list holds; those were not applied.",
		               static_cast<unsigned>(gemParse.bad),
		               static_cast<unsigned>(gemParse.dropped),
		               static_cast<unsigned>(MaxIgnoreCodes));
	}
	if (stashParse.bad != 0 || stashParse.dropped != 0) {
		D2RL::LogWarnF(context,
		               "AutoDeposit: stash_ignore has %u entr(ies) that are not three-character codes and %u past the %u a list holds; those were not applied.",
		               static_cast<unsigned>(stashParse.bad),
		               static_cast<unsigned>(stashParse.dropped),
		               static_cast<unsigned>(MaxIgnoreCodes));
	}
}

auto AnyAutomaticWorkEnabled() noexcept -> bool {
	return g_gemBagMergeGems || g_gemBagMergeClusters || g_stashDeposit;
}

auto IsGemCode(uint32_t code) noexcept -> bool {
	return ListHasCode(GemCodes, std::size(GemCodes), code);
}

auto IsClassicGemCode(uint32_t code) noexcept -> bool {
	return ListHasCode(ClassicGemCodes, std::size(ClassicGemCodes), code);
}

// True when the config tells the merge to leave this code where it is.
auto GemBagIgnores(uint32_t code) noexcept -> bool {
	return ListHasCode(g_gemBagIgnore, g_gemBagIgnoreCount, code & Code3Mask);
}

// True when the stash half must leave this code alone.
//
// Three reasons, in the order they are decided. The Gem Bag itself is never
// deposited - it is where the gems are, and banking it would take every gem the
// player ever collected with it - and that one is not configurable, because it
// is not a preference. A gem the bag is allowed to take belongs to the bag: the
// two destinations must never be able to want the same item, or which one wins
// comes down to timing. Everything else is the player's own list.
auto StashIgnores(uint32_t code) noexcept -> bool {
	if (code == GemBagCode) {
		return true;
	}
	if (g_gemBagMergeGems && IsGemCode(code)) {
		return true;
	}
	if (g_gemBagMergeClusters && code == GemClusterCode) {
		return true;
	}

	return ListHasCode(g_stashIgnore, g_stashIgnoreCount, code & Code3Mask);
}

// The packing, pinned: 'r' 0x72, 'v' 0x76, 's' 0x73, little-endian, fourth byte
// dropped. Both the tables' codes and the config's tokens go through this, so a
// change to either is caught here rather than by the two silently disagreeing.
static_assert(Code3("rvs") == 0x00737672u);
static_assert(PackCodeToken("rvs") == Code3("rvs"));

