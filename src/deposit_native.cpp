// Auto Deposit - the game, called by address. See deposit_native.h.
//
// The addresses and the bytes they are checked against are in
// deposit_native_signatures.h; this file is what happens once they have been
// checked. Two things run through all of it:
//
//   * MSVC refuses __try in a function that has objects needing unwinding
//     (C2712), so every guarded body holds nothing but pointers and integers.
//     Anything that wants to build a string builds it outside the guard.
//
//   * Nothing is called until every address has been checked as a set, and the
//     check happens once, at load. A call is never the thing that discovers the
//     build does not match.

#include "deposit_native.h"

#include "deposit_common.h"
#include "deposit_native_signatures.h"

#include <cstdio>
#include <cstring>
#include <windows.h>

namespace {

// The image base the game was loaded at, and what the one check at load made of
// it. g_nativeMissing is the names of the addresses that did not match, comma
// separated, kept rather than just counted: it is what the "switched off" line
// reports, and what a corrected table is written from.
uint64_t g_exeBase     = 0;
bool     g_nativeReady = false;
char     g_nativeMissing[256] {};

// g_exeBase + rva, as the function the address is supposed to hold. Every call
// in this file goes through here, and every one of them is inside a __try: the
// signature check has already said the bytes are right, and this is what catches
// it being wrong anyway.
template <class F>
auto At(uint64_t rva) noexcept -> F {
	return reinterpret_cast<F>(g_exeBase + rva);
}

// What one address answered when it was read.
enum class SignatureState {
	Missing,   // not the function this table is looking for
	Present,   // the documented bytes, exactly
	Detoured,  // a jump covers the entry; the function is intact underneath it
};

// A five-byte jump - opcode and a four-byte displacement - which is what an
// inline hook writes over a function's first instruction.
constexpr uint32_t DetourBytes  = 5;
constexpr uint8_t  DetourOpcode = 0xE9;

// Whether the bytes read from an address are the ones the table expects.
//
// Two answers count as a match. The plain one is the documented bytes. The other
// is a five-byte jump sitting on the entry with the documented bytes from offset
// five on underneath it, which means another plugin hooked this function before
// this one loaded. That is the ordinary case on this install rather than a fault:
// BindAndSummon hooks TxtFileNo every session. Refusing to run because a
// neighbour hooked a game function would cost the whole stash half for nothing.
//
// It stays a bytes-against-bytes check - the eleven bytes below the jump identify
// the function as well as the sixteen did. Where the jump goes is not checked and
// does not need to be: this plugin calls these addresses and never patches them,
// so a wrong detour can only return a wrong answer, and a wrong class id shows up
// in the log as "not advanced-stash material" rather than as damage.
//
// Bytes are compared before the entry is classified, so an entry that genuinely
// does begin with E9 is matched on all of itself and never mistaken for a hook.
auto ClassifySignature(const uint8_t* expected, uint32_t size, const uint8_t* actual, uint32_t actualSize) noexcept -> SignatureState {
	if (size > actualSize) {
		return SignatureState::Missing;
	}
	if (std::memcmp(actual, expected, size) == 0) {
		return SignatureState::Present;
	}
	if (size <= DetourBytes || actual[0] != DetourOpcode) {
		return SignatureState::Missing;
	}
	for (uint32_t at = DetourBytes; at < size; ++at) {
		if (actual[at] != expected[at]) {
			return SignatureState::Missing;
		}
	}
	return SignatureState::Detoured;
}

}  // namespace

auto VerdictName(Verdict verdict) noexcept -> const char* {
	switch (verdict) {
		case Verdict::Blocked:     return "blocked by the game";
		case Verdict::NotMaterial: return "not advanced-stash material";
		case Verdict::NoTarget:    return "no advanced-stash unit to deposit into";
		case Verdict::Deposited:   return "deposited";
		default:                   return "faulted";
	}
}

// POD locals only, and no logging: MSVC refuses __try in a function that has
// objects needing unwinding (C2712), and the way to keep that from ever being a
// question is to keep the guarded bodies free of anything but pointers and
// integers.
__declspec(noinline) auto InspectItem(void* item, ItemFacts& out) noexcept -> bool {
	out = ItemFacts {};
	__try {
		out.id   = static_cast<uint32_t>(At<UnitIntFn>(UnitIdRva)(item));
		out.code = At<ItemCodeFn>(ItemCodeRva)(item);

		const auto* data = static_cast<const uint8_t*>(At<UnitFn>(ItemDataRva)(item));
		if (data != nullptr) {
			out.page = data[ItemDataPageOffset];
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		out = ItemFacts {};
		return false;
	}
}

__declspec(noinline) auto StashIsOpen() noexcept -> bool {
	__try {
		return At<UiStateFn>(UiStateOpenRva)(StashInterfaceState) != 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

__declspec(noinline) auto DepositNative(void* player, void* item) noexcept -> Verdict {
	__try {
		if (player == nullptr) {
			return Verdict::Faulted;
		}
		if (At<UnitIntFn>(ItemBlockedRva)(item) != 0) {
			return Verdict::Blocked;
		}
		// The two trailing arguments are what the game reports a null item with,
		// and are only ever read on that path - the prologue branches on the item
		// before it looks at them. __FILE__ and __LINE__ are literals, so they are
		// safe to spell inside the guard.
		const int classId = At<TxtFileNoFn>(TxtFileNoRva)(item, __FILE__, __LINE__);
		if (!At<StashItemFn>(StashItemOkRva)(classId)) {
			return Verdict::NotMaterial;
		}

		// Null here is ordinary, not an error: it is what the advanced-stash
		// unit comes back as before there is one to deposit into. The item is
		// left exactly where it is, which is the outcome that cannot cost
		// anything.
		void* target = At<UnitFn>(StashTargetRva)(player);
		if (target == nullptr) {
			return Verdict::NoTarget;
		}

		At<DepositFn>(StashDepositRva)(item, target);
		return Verdict::Deposited;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return Verdict::Faulted;
	}
}

__declspec(noinline) auto CollectInventory(Snapshot& out) noexcept -> bool {
	out = Snapshot {};
	__try {
		void* player = At<PlayerFn>(LocalPlayerRva)(At<IndexFn>(LocalIndexRva)());
		if (player == nullptr) {
			return false;
		}
		void* inventory = At<UnitFn>(UnitInventoryRva)(player);
		if (inventory == nullptr) {
			return false;
		}
		out.player = player;

		void* item = At<UnitFn>(FirstItemRva)(inventory);
		while (item != nullptr && out.count < MaxSnapshot) {
			if (At<UnitIntFn>(UnitTypeRva)(item) == ItemUnitType && At<UnitFn>(ParentInventoryRva)(item) == inventory) {
				const int id = At<UnitIntFn>(UnitIdRva)(item);
				if (id >= 0) {
					out.items[out.count] = item;
					out.ids[out.count]   = static_cast<uint32_t>(id);
					++out.count;
				}
			}
			item = At<UnitFn>(NextItemRva)(item);
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		out = Snapshot {};
		return false;
	}
}

namespace {

// Reads bytes out of the running image.
//
// Guarded, because the only reason to call it is that the address is one whose
// contents are unknown: this is how a failed check is turned into something that
// can be acted on rather than a dead end. A fault is one of the answers.
__declspec(noinline) auto ReadImageBytes(uint64_t rva, uint8_t* out, uint32_t count) noexcept -> bool {
	__try {
		const auto* source = reinterpret_cast<const uint8_t*>(g_exeBase + rva);
		for (uint32_t at = 0; at < count; ++at) {
			out[at] = source[at];
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// Sixteen bytes as "48 83 EC 28 ...". Text is built here, outside the guard, so
// ReadImageBytes stays a function of pointers and integers.
auto HexText(const uint8_t* bytes, uint32_t count, char* out, size_t capacity) noexcept -> void {
	size_t used = 0;
	for (uint32_t at = 0; at < count && capacity > used + 4; ++at) {
		const int written = std::snprintf(out + used, capacity - used, at == 0 ? "%02X" : " %02X", static_cast<unsigned>(bytes[at]));
		if (written <= 0) {
			break;
		}
		used += static_cast<size_t>(written);
	}
}

}  // namespace

auto CheckNative(const D2RL::PluginContext* context, uint64_t exeBase) noexcept -> void {
	g_exeBase = exeBase;

	uint32_t missing  = 0;
	uint32_t detoured = 0;
	size_t   used     = 0;

	char   hooked[192] {};
	size_t hookedUsed = 0;

	for (const NativeSignature& signature : NativeTable) {
		// Read once and keep the bytes: a failure reports what is actually there,
		// which is the difference between a log line that says "it did not work"
		// and one a corrected table can be written from.
		uint8_t    actual[32] {};
		const bool readable = ReadImageBytes(signature.rva, actual, static_cast<uint32_t>(sizeof(actual)));
		const SignatureState state = readable
			? ClassifySignature(signature.bytes, signature.size, actual, static_cast<uint32_t>(sizeof(actual)))
			: SignatureState::Missing;

		if (state == SignatureState::Present) {
			continue;
		}
		if (state == SignatureState::Detoured) {
			// Counted and named, never silent: a hooked entry is somebody else's
			// business, but a plugin that quietly stopped checking would be worse
			// than one that never checked.
			++detoured;
			AppendList(hooked, sizeof(hooked), hookedUsed, ", ", signature.name);
			continue;
		}

		++missing;

		char expected[80] {};
		char found[80] {};
		HexText(signature.bytes, signature.size, expected, sizeof(expected));
		if (readable) {
			HexText(actual, static_cast<uint32_t>(sizeof(actual)), found, sizeof(found));
		} else {
			std::snprintf(found, sizeof(found), "(the address could not be read at all)");
		}

		D2RL::LogErrorF(context,
		                "AutoDeposit: signature %s (RVA 0x%llX) does not match. Expected %s , found %s .",
		                signature.name,
		                static_cast<unsigned long long>(signature.rva),
		                expected,
		                found);

		AppendList(g_nativeMissing, sizeof(g_nativeMissing), used, ", ", signature.name);
	}

	g_nativeReady = missing == 0;

	if (g_nativeReady) {
		D2RL::LogInfoF(context,
		               "AutoDeposit: all %u native signatures match this build (exeBase=0x%llX).",
		               static_cast<unsigned>(NativeTableCount),
		               static_cast<unsigned long long>(exeBase));

		if (detoured != 0) {
			D2RL::LogInfoF(context,
			               "AutoDeposit: %u of them are hooked at their entry by another plugin, so their first five bytes are a jump: %s. The bytes underneath are the ones this table expects and are what the check compared, so nothing is switched off - this plugin only calls these addresses, it never patches them, and a neighbour's hook in front of one does not change that.",
			               static_cast<unsigned>(detoured),
			               hooked);
		}
		return;
	}

	D2RL::LogErrorF(context,
	                "AutoDeposit: %u of %u native signatures do NOT match this build: %s. The stash half is switched off - nothing will be offered to the advanced stash. The gem bag half needs none of these and still works. The lines above print what is actually at each failing address, which is what a corrected table is written from.",
	                static_cast<unsigned>(missing),
	                static_cast<unsigned>(NativeTableCount),
	                g_nativeMissing);
}

auto NativeReady() noexcept -> bool {
	return g_nativeReady;
}

auto NativeUsable(const D2RL::PluginContext* context) noexcept -> bool {
	if (g_nativeReady) {
		return true;
	}
	D2RL::LogErrorF(context,
	                "AutoDeposit: the native signatures did not all match this build, so nothing was moved. Missing: %s",
	                g_nativeMissing[0] != 0 ? g_nativeMissing : "(not checked yet)");
	return false;
}
