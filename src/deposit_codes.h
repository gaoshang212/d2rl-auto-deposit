// Auto Deposit - item codes: the three-byte comparisons the ignore lists are
// made of, and the gem tables the two halves route on.

#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// The three bytes of a code, packed little-endian: 'rvs' is 0x00737672.
//
// Three bytes are read and no more, so the caller is the one that has to know
// there are three - and both of them do, because three characters is the only
// token length either accepts.
//
// Three and not four. The fourth byte of a real item code is a space, and
// dropping it is what lets 'rvs' match an item whose code is 'rvs ' - see
// Code3Mask, which every comparison against an item's own code applies.
constexpr auto PackCodeToken(const char* text) noexcept -> uint32_t {
	uint32_t packed = 0;
	for (size_t index = 0; index < 3; ++index) {
		packed |= static_cast<uint32_t>(static_cast<unsigned char>(text[index])) << (index * 8);
	}
	return packed;
}

inline constexpr uint32_t Code3Mask = 0x00FFFFFFu;

// The same packing, for the places that write a code out as a literal so it can
// be read the way a human reads it. Code3 stays the readable spelling of the
// spec - "a three-byte code is 0x00737672" for 'rvs' - with the array bound
// holding it to three characters plus the terminator.
constexpr uint32_t Code3(const char (&text)[4]) noexcept {
	return PackCodeToken(text);
}

// Reimagined item codes, padded to D2 item-table four-byte form.

inline constexpr uint32_t GemBagCode     = D2RL::Items::MakeItemCode("bag");
inline constexpr uint32_t GemClusterCode = D2RL::Items::MakeItemCode("1gc");
// The eight tradeable gems Reimagined banks and crafts with. These are real
// item_gem rows in misc.txt ("Amethyst" .. "Chaos Onyx").
// Reimagined's own gems, misc.txt type2 = pgem. These are what the merge
// sweeps. The bag's total is blind to both colour and quality: the mod credits
// every gem +1 and taking one back out needs the matching Grabber, which hands
// back the plain gem. That is why the 35 classic codes are left alone - merging
// a Perfect Ruby would trade it for one credit and its quality would never come
// back.
inline constexpr uint32_t GemCodes[] {
	D2RL::Items::MakeItemCode("gmm"), // Amethyst
	D2RL::Items::MakeItemCode("gmt"), // Topaz
	D2RL::Items::MakeItemCode("gms"), // Sapphire
	D2RL::Items::MakeItemCode("gme"), // Emerald
	D2RL::Items::MakeItemCode("gmr"), // Ruby
	D2RL::Items::MakeItemCode("gmd"), // Diamond
	D2RL::Items::MakeItemCode("gmk"), // Skull
	D2RL::Items::MakeItemCode("gmo"), // Chaos Onyx
};

// The classic gems, misc.txt type2 = gem0..gem4. Not merged, but recognised so
// the merge can report what it walked past instead of leaving the player to
// wonder whether it missed them.
inline constexpr uint32_t ClassicGemCodes[] {
	D2RL::Items::MakeItemCode("gcv"), // Chipped Amethyst
	D2RL::Items::MakeItemCode("gfv"), // Flawed Amethyst
	D2RL::Items::MakeItemCode("gsv"), // Amethyst
	D2RL::Items::MakeItemCode("gzv"), // Flawless Amethyst
	D2RL::Items::MakeItemCode("gpv"), // Perfect Amethyst
	D2RL::Items::MakeItemCode("gcy"), // Chipped Topaz
	D2RL::Items::MakeItemCode("gfy"), // Flawed Topaz
	D2RL::Items::MakeItemCode("gsy"), // Topaz
	D2RL::Items::MakeItemCode("gly"), // Flawless Topaz
	D2RL::Items::MakeItemCode("gpy"), // Perfect Topaz
	D2RL::Items::MakeItemCode("gcb"), // Chipped Sapphire
	D2RL::Items::MakeItemCode("gfb"), // Flawed Sapphire
	D2RL::Items::MakeItemCode("gsb"), // Sapphire
	D2RL::Items::MakeItemCode("glb"), // Flawless Sapphire
	D2RL::Items::MakeItemCode("gpb"), // Perfect Sapphire
	D2RL::Items::MakeItemCode("gcg"), // Chipped Emerald
	D2RL::Items::MakeItemCode("gfg"), // Flawed Emerald
	D2RL::Items::MakeItemCode("gsg"), // Emerald
	D2RL::Items::MakeItemCode("glg"), // Flawless Emerald
	D2RL::Items::MakeItemCode("gpg"), // Perfect Emerald
	D2RL::Items::MakeItemCode("gcr"), // Chipped Ruby
	D2RL::Items::MakeItemCode("gfr"), // Flawed Ruby
	D2RL::Items::MakeItemCode("gsr"), // Ruby
	D2RL::Items::MakeItemCode("glr"), // Flawless Ruby
	D2RL::Items::MakeItemCode("gpr"), // Perfect Ruby
	D2RL::Items::MakeItemCode("gcw"), // Chipped Diamond
	D2RL::Items::MakeItemCode("gfw"), // Flawed Diamond
	D2RL::Items::MakeItemCode("gsw"), // Diamond
	D2RL::Items::MakeItemCode("glw"), // Flawless Diamond
	D2RL::Items::MakeItemCode("gpw"), // Perfect Diamond
	D2RL::Items::MakeItemCode("skc"), // Chipped Skull
	D2RL::Items::MakeItemCode("skf"), // Flawed Skull
	D2RL::Items::MakeItemCode("sku"), // Skull
	D2RL::Items::MakeItemCode("skl"), // Flawless Skull
	D2RL::Items::MakeItemCode("skz"), // Perfect Skull
};

// D2 item codes are four bytes padded with spaces, and the log reads better with
// them spelled out than as a number. Anything unprintable becomes '.', because a
// log line is not the place to find out that a byte was 0x00.
inline auto CodeText(uint32_t code, char (&out)[5]) noexcept -> const char* {
	for (uint32_t index = 0; index < 4; ++index) {
		const char byte = static_cast<char>((code >> (index * 8)) & 0xFF);
		out[index] = byte >= 32 && byte < 127 ? byte : '.';
	}
	out[4] = 0;
	return out;
}
