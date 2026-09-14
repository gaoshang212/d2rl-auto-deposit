// Auto Deposit - the gem bag half: Reimagined's Gem Bag, and the merge that puts
// a picked-up gem into it.
//
// The SDK has no stat read, so the bag's counter is read out of the item's own
// stat array. The path is taken from the native item pointer, which the SDK
// hands over on every editNativeItem call, so nothing here depends on a fixed
// address or on a particular session:
//
//     unit + 0x88 -> the item's stat-list object
//     that  + 0xA8 -> its stat records, 0x30 bytes each
//     record + 0x02 is the u16 stat id, record + 0x04 the u16 value
//
// Everything is read through IsReadableRange first: these are live game pointers
// and a bad one must fail the read, not the process.
//
// This half needs none of deposit_native_signatures.h. A build whose stash
// addresses do not match still merges gems.

#pragma once

#include <D2RLPlugin/api.h>

#include <cstdint>

// Queues a merge. The pickup hook and the console command both come through
// here, so the one path is what the log describes either way.
//
// autoMerge marks the pickup's merge, which takes only the item named by
// pickupGuid. The console command passes false and zero: it sweeps every loose
// gem, because that is what asking for a merge means.
//
// False means nothing was queued - no game to work in, or a merge is already
// running.
auto ScheduleMerge(const D2RL::PluginContext* context, bool autoMerge, uint32_t pickupGuid) noexcept -> bool;
