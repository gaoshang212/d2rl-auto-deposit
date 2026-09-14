// Auto Deposit - the addresses the stash half calls directly, and the bytes each
// one should contain on the build this was written for.
//
// Nothing here drags, clicks, presses a key, finds a window, reads a screen or
// touches the stash through the SDK. These are the game's own routines, reached
// at fixed offsets into D2R.exe's text, on the game thread, and handed the
// native item pointer the plugin already has:
//
//   player = LocalPlayer(LocalIndex())
//   ItemBlocked(item)     -> the game will not move it
//   classId = TxtFileNo(item)      -> which row of the game's item tables it is
//   StashItemOk(classId)  -> is this advanced-stash material at all
//   target = StashTarget(player)   -> the player's advanced-stash unit
//   StashDeposit(item, target)     -> MOVES it
//
// Every address is checked against its expected bytes before anything is called,
// as a set: a deposit that gets three calls in and faults on the fourth is worse
// than one that never started. A build whose bytes do not match loses the stash
// half and keeps the gem bag half, which needs none of this.
//
// Another plugin's inline hook over one of these entries is not a mismatch. Such
// a hook writes a five-byte jump over the function's first instruction, leaving
// everything below it intact, and the check compares from just under the jump
// when it finds one. TxtFileNo is hooked that way on this install, every session,
// by BindAndSummon - so without that rule the whole stash half would be off.
// Which entries came back hooked is named in the log at load; it is never silent.
//
// The check is CheckNative, in deposit_native.cpp - the only file that includes
// this one, because the stash half is the only thing here that calls the game.

#pragma once

#include <cstddef>
#include <cstdint>

using IndexFn     = int(__fastcall*)();
using PlayerFn    = void*(__fastcall*)(int);
using UnitFn      = void*(__fastcall*)(void*);
using UnitIntFn   = int(__fastcall*)(void*);
using StashItemFn = bool(__fastcall*)(int);
using DepositFn   = void(__fastcall*)(void*, void*);
using ItemCodeFn  = uint32_t(__fastcall*)(void*);
using UiStateFn   = int(__fastcall*)(int);
// The two trailing arguments are for the failure path only - see TxtFileNo.
using TxtFileNoFn = int(__fastcall*)(void*, const char*, int);

// LocalIndex()                     the index of the local player, for LocalPlayer
inline constexpr uint64_t LocalIndexRva = 0x8b2d0;
inline constexpr uint8_t  LocalIndexBytes[] { 0x8b, 0x05, 0x2e, 0x84, 0x99, 0x02, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc };
// LocalPlayer(index)               the player unit
inline constexpr uint64_t LocalPlayerRva = 0x9a480;
inline constexpr uint8_t  LocalPlayerBytes[] { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x83, 0xf9, 0x08, 0x0f, 0x83, 0x85 };
// TxtFileNo(item, file, line)      the item's class id: its row in the game's
//                                  own item tables, counted across weapons, then
//                                  armor, then misc, with the rows that carry no
//                                  code left out. On Reimagined that puts Tal -
//                                  row 109 of misc.txt - at 631, which is what
//                                  the item's own data files say it should be.
//
//                                  Asked of the item itself, because StashItemOk
//                                  below takes exactly this number and refuses
//                                  the item. The SDK reports the same numbering
//                                  as ItemInfo::classId, which is where the Tal
//                                  figure above was read; a build where the two
//                                  disagreed would answer "not advanced-stash
//                                  material" for everything and move nothing,
//                                  which is visible and not dangerous.
//
//                                  The file and line are not part of the
//                                  question. This is a checked accessor whose
//                                  failure path reports the caller, so they are
//                                  only read if the item pointer is null, where
//                                  it returns 0 rather than raising.
inline constexpr uint64_t TxtFileNoRva = 0x349860;
inline constexpr uint8_t  TxtFileNoBytes[] { 0x48, 0x83, 0xec, 0x28, 0x48, 0x85, 0xc9, 0x75, 0x1d, 0x88, 0x4c, 0x24, 0x30, 0x48, 0x8d, 0x4c };
// StashItemOk(classId)             the game's own "does this belong in the
//                                  advanced stash" test. Takes the item's class
//                                  id, not the item - TxtFileNo above is where
//                                  it comes from - and this is the gate that
//                                  decides what this plugin will and will not
//                                  touch.
inline constexpr uint64_t StashItemOkRva = 0x15f320;
inline constexpr uint8_t  StashItemOkBytes[] { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7c, 0x24, 0x18, 0x48 };
// StashTarget(player)              the player's advanced-stash unit, or null
//                                  when there is not one to deposit into yet
inline constexpr uint64_t StashTargetRva = 0x46da50;
inline constexpr uint8_t  StashTargetBytes[] { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x85, 0xc9, 0x75, 0x1b, 0x88, 0x4c, 0x24, 0x30, 0x48 };
// StashDeposit(item, target)       moves the item into the advanced stash
inline constexpr uint64_t StashDepositRva = 0x159a30;
inline constexpr uint8_t  StashDepositBytes[] { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xda, 0x41, 0xb8, 0xec };
// ItemBlocked(item)                the game's own "do not move this" flag
inline constexpr uint64_t ItemBlockedRva = 0x1c7360;
inline constexpr uint8_t  ItemBlockedBytes[] { 0x48, 0x83, 0xec, 0x28, 0xba, 0x02, 0x00, 0x00, 0x00, 0xe8, 0xa2, 0xe4, 0x12, 0x00, 0x48, 0x85 };
// ItemData(item)                   the item's data block. The container page
//                                  byte is read out of it directly.
inline constexpr uint64_t ItemDataRva = 0x34a500;
inline constexpr uint8_t  ItemDataBytes[] { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x75, 0x1d, 0x88, 0x4c };
// UnitId(item)                     the id the pickup routine also reports
inline constexpr uint64_t UnitIdRva = 0x34a330;
inline constexpr uint8_t  UnitIdBytes[] { 0x48, 0x83, 0xec, 0x28, 0x48, 0x85, 0xc9, 0x75, 0x1d, 0x88, 0x4c, 0x24, 0x30, 0x48, 0x8d, 0x4c };
// UnitInventory(unit)              the inventory container a unit owns
inline constexpr uint64_t UnitInventoryRva = 0x34a360;
inline constexpr uint8_t  UnitInventoryBytes[] { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf1, 0x48, 0x85, 0xc9 };
// UnitType(unit)                   4 is an item
inline constexpr uint64_t UnitTypeRva = 0x34b9d0;
inline constexpr uint8_t  UnitTypeBytes[] { 0x48, 0x83, 0xec, 0x28, 0x48, 0x85, 0xc9, 0x75, 0x1d, 0x88, 0x4c, 0x24, 0x30, 0x48, 0x8d, 0x4c };
// FirstItem(inventory) / NextItem(item)   the inventory's item list
inline constexpr uint64_t FirstItemRva = 0x388c10;
inline constexpr uint8_t  FirstItemBytes[] { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x74, 0x2e, 0x81, 0x39 };
inline constexpr uint64_t NextItemRva = 0x38aba0;
inline constexpr uint8_t  NextItemBytes[] { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x75, 0x10, 0x88, 0x4c };
// ParentInventory(item)            which container holds it, so the list can be
//                                  filtered to the one that was asked for
inline constexpr uint64_t ParentInventoryRva = 0x38ac50;
inline constexpr uint8_t  ParentInventoryBytes[] { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x74, 0x0a, 0xe8, 0x6d };
// ItemCode(item)                   the four-character item code, packed
inline constexpr uint64_t ItemCodeRva = 0x36ef50;
inline constexpr uint8_t  ItemCodeBytes[] { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf9, 0x48, 0x85, 0xc9 };
// UiStateOpen(state)               whether a UI panel is up. 0x18 is the shared
//                                  stash, and the automatic path stays out of the
//                                  way while it is open - see StashIsOpen.
inline constexpr uint64_t UiStateOpenRva = 0xce500;
inline constexpr uint8_t  UiStateOpenBytes[] { 0x48, 0x63, 0xc1, 0x48, 0x8d, 0x0d, 0x96, 0xc8, 0x95, 0x02, 0x0f, 0xb6, 0x04, 0x08, 0xc3 };
inline constexpr int     StashInterfaceState = 0x18;

// UnitType of an item, and where in the item data block the container page
// lives. The page is what tells the inventory grid from the belt, the equipped
// slots and the stash pages, which all hang off the same container - the value
// the grid reads as is in deposit_native.h, where the run compares against it.
inline constexpr int     ItemUnitType       = 4;
inline constexpr size_t  ItemDataPageOffset = 0x55;

struct NativeSignature {
	const char*    name;
	uint64_t       rva;
	const uint8_t* bytes;
	uint32_t       size;
};

// Every address the deposit path uses, checked as a set at load.
constexpr NativeSignature NativeTable[] {
	{ "LocalIndex",      LocalIndexRva,      LocalIndexBytes,      sizeof(LocalIndexBytes)      },
	{ "LocalPlayer",     LocalPlayerRva,     LocalPlayerBytes,     sizeof(LocalPlayerBytes)     },
	{ "TxtFileNo",       TxtFileNoRva,       TxtFileNoBytes,       sizeof(TxtFileNoBytes)       },
	{ "StashItemOk",     StashItemOkRva,     StashItemOkBytes,     sizeof(StashItemOkBytes)     },
	{ "StashTarget",     StashTargetRva,     StashTargetBytes,     sizeof(StashTargetBytes)     },
	{ "StashDeposit",    StashDepositRva,    StashDepositBytes,    sizeof(StashDepositBytes)    },
	{ "ItemBlocked",     ItemBlockedRva,     ItemBlockedBytes,     sizeof(ItemBlockedBytes)     },
	{ "ItemData",        ItemDataRva,        ItemDataBytes,        sizeof(ItemDataBytes)        },
	{ "UnitId",          UnitIdRva,          UnitIdBytes,          sizeof(UnitIdBytes)          },
	{ "UnitInventory",   UnitInventoryRva,   UnitInventoryBytes,   sizeof(UnitInventoryBytes)   },
	{ "UnitType",        UnitTypeRva,        UnitTypeBytes,        sizeof(UnitTypeBytes)        },
	{ "FirstItem",       FirstItemRva,       FirstItemBytes,       sizeof(FirstItemBytes)       },
	{ "NextItem",        NextItemRva,        NextItemBytes,        sizeof(NextItemBytes)        },
	{ "ParentInventory", ParentInventoryRva, ParentInventoryBytes, sizeof(ParentInventoryBytes) },
	{ "ItemCode",        ItemCodeRva,        ItemCodeBytes,        sizeof(ItemCodeBytes)        },
	{ "UiStateOpen",     UiStateOpenRva,     UiStateOpenBytes,     sizeof(UiStateOpenBytes)     },
};

constexpr size_t NativeTableCount = sizeof(NativeTable) / sizeof(NativeTable[0]);
