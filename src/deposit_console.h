// Auto Deposit - the console command.
//
// Both halves are automatic, so everything here is the by-hand sweep: the things
// that were already lying in the inventory before this plugin was loaded.
//
// One command for both destinations, because the two are one decision. Which one
// is asked for is the first word: 'deposit' or 'deposit stash' for the advanced
// stash, 'deposit bag' or 'deposit merge' for the Gem Bag.
//
// The command never runs a job itself. It queues one on the game thread - the
// only place item mutation is supported - and says so, because the answer
// arrives in the log a moment later and a command that looked like it did
// nothing would be reported as broken.
//
// Sweeping the stash is exempt from the "stash panel is open" rule the automatic
// path follows: the player asked, and they are looking at the result.

#pragma once

#include <D2RLPlugin/api.h>

auto DepositCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* command, void* userData) noexcept
    -> D2RL::ConsoleCommandResult;
