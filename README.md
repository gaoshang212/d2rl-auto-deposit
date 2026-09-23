# Auto Deposit

A [D2RLoader](https://github.com/D2RLoader/PluginSDK) plugin for the **Reimagined** Diablo II:
Resurrected mod. What you pick up goes where it belongs, on its own - no key, no clicking, no cube
recipe.

Two destinations:

* **Gems into the mod's Gem Bag.** Only the gem you picked up is taken; the other loose gems in the
  inventory stay where you left them.
* **Everything else into the advanced stash.** Each item you pick up is handed to the game's own
  deposit call, if the game counts it among advanced-stash materials - gems, runes, materials, the
  cube, anything the game itself would let you drop into those tabs.

One plugin, because that is one decision. A gem belongs in the bag, so while the bag is switched on
the stash path does not consider gems at all and the two can never fight over the same item.

## Install

Copy `d2rl-auto-deposit.dll` into `<mod>/d2rloader/plugins/`. Merging needs the Gem Bag in the
inventory - with no bag there is nothing to merge into. Depositing needs the advanced stash to
exist for that character.

This replaces `d2rl-auto-gem-bag` and `d2rl-auto-stash`. Do not run them alongside it.

## Config

`<mod>/d2rloader/config/d2rl-auto-deposit.toml`

```toml
[auto-deposit]

gem_bag_merge_gems     = true    # gems into the bag on pickup
gem_bag_merge_clusters = true    # gem clusters into the bag on pickup
gem_bag_ignore         = ""      # codes the bag must not take

stash_deposit          = true    # everything else into the advanced stash
stash_ignore           = "rvs rvl"   # codes the stash must not take
```

Every key names its destination first, then the verb, then what it acts on - so `gem_bag_merge_gems`
and `stash_deposit` say which half they belong to without needing the surrounding comments, and the
two `*_ignore` lists are exact mirror images of each other.

Everything is on by default: what is not named in an ignore list goes where it belongs. The lists
are three-character item codes separated by spaces or commas, the same codes you would see in a
trade window, with anything after a `#` treated as a comment.

```toml
stash_ignore = "rvs rvl"    # rvs is the small rejuv potion, rvl the full one
```

`stash_ignore` ships as `"rvs rvl"`, the two rejuvenation potions, and that is the only thing the
plugin leaves alone out of the box. The game is happy to bank them - its own stash check accepts
both - so this is the plugin's choice, not the game's rule: a rejuv is drunk from the belt or the
inventory in the middle of a fight, and one banked the moment it was picked up is not there when it
is wanted. Clear the list if you would rather they went in like everything else.

D2RLoader writes this file the first time the plugin loads and never overwrites it afterwards, so
your edits survive updates. **Read once, at startup - restart D2R after editing.**

* The switches are the *automatic* paths, the ones that run as you pick something up. `deposit` is
  asked for by hand, so it always does both jobs whatever the file says.
* A value that isn't clearly `true` or `false` falls back to the default (on), so a typo can't
  silently disable anything.
* An entry that isn't exactly three characters is **counted and reported**, not truncated to three:
  a list that quietly means something other than what it says is how a plugin ends up ignoring the
  wrong things.
* Every start logs what it read: `AutoDeposit: config says gem_bag_merge_gems=true, ...`. If you
  see `could not read ... built-in defaults are in use`, the file didn't load and the switches are
  being ignored.

## Console

* `deposit` (or `deposit stash`) - sweeps everything on the inventory grid into the advanced stash
  at once. For the things lying around from before the plugin was loaded. A sweep you typed answers
  for every item it refuses, and the summary line names the codes it left standing.
* `deposit bag` (or `deposit merge`) - sweeps every loose gem and cluster into the bag.
* `deposit probe` - reads the Gem Bag's counter and logs it, consuming nothing.
* `deposit why` - a read-only report: every unit on the grid with the container the SDK puts it in
  and what the game answers about it, the units the walk sees and does not hand over, one line per
  item code, and the walk's count of the grid beside the SDK's own count of it - so a walk that
  misses something says so instead of looking like a game that refused an item. Reach for it when
  something is not going in and the log says nothing about it.

## What is always left alone

* **The Gem Bag itself.** Never deposited, not even by a bare `deposit` sweep. It is where the gems
  are; banking it would take every gem you ever collected with it. Not configurable, because it
  isn't a preference.
* **The 35 classic gems**, chipped to perfect. The bag is blind to colour and quality - every gem
  is worth +1 - and the Grabber hands back only the plain gem, so merging a Perfect Ruby would cost
  you its quality forever. They stay where they are; the log says how many it walked past.
* **Anything not on the inventory grid.** The belt, the equipped slots and the stash pages hang off
  the same container. Nothing that moves anything is ever called for a unit on another page.
* **The Cube.** A half-built recipe sitting in it is exactly the sort of thing that must not be
  swept up.

## Notes

* The automatic path stays out of the way while the shared stash panel is open: an item would
  disappear out of a grid that is being drawn. The pickup is not lost - it stays queued and goes in
  by itself once the panel is closed, or immediately with `deposit`. The console command is exempt,
  because you asked and you are looking at the result.
* A pickup is not the same event as the item's arrival. The game's pickup routine returns as soon
  as the click is accepted, and the item turns up a moment later - so the id is written down and
  looked for, which also means a burst of pickups is not limited to one at a time.
* A gem cluster is worth 20-30 gems - the mod's own recipe, rolled per cluster.
* The bag keeps its cell. The counter can't be edited in place, so the merge swaps the bag for an
  identical one holding the new total and puts it back where the old one was. Should that cell be
  refused, the new bag goes to the first free cell and the log says so.
* The bag caps at 65535. What doesn't fit stays in the inventory.
* Two Gem Bags in the inventory and the merge does nothing - it can't tell which one you mean.
* Deposit calls are silent by nature. `StashDeposit` returns void, so a run can only honestly claim
  the call returned without faulting - the inventory losing the item and the tab's stack going up
  is the confirmation.

## Build

Needs Visual Studio 2022 (x64), Ninja and CMake 3.28+. The
[SDK](https://github.com/D2RLoader/PluginSDK) needs no setting up: a checkout at
`external/d2r-pluginsdk` (or beside the project) is used if there is one, and otherwise it is
cloned from GitHub into `build/_deps`. For any other layout, pass
`-DD2RLPLUGIN_SDK_DIR=<path>`.

```bat
build.bat
```

To have the build copy the DLL into your mod, point it there once - CMake remembers:

```bat
cmake -S . -B build -DD2RL_AUTODEPOSIT_DEPLOY_DIR="<mod>/d2rloader/plugins"
```

Empty (the default) just builds. From Git Bash use
`MSYS_NO_PATHCONV=1 cmd.exe /c "cd /d <project> && .\build.bat"`, since MSYS rewrites arguments
that look like paths. If the game is running when it copies, the new DLL is left beside it as
`d2rl-auto-deposit.dll.new`, to rename over the target.

> Do not force-kill `D2RLoader.exe`. It owns the save files.

## How it works

The game's own pickup routine is hooked behind a 32-byte signature that is checked *before*
anything is patched. The pickup hands over the guid of the item that just arrived; both jobs are
queued onto the game thread - the only place item mutation is supported - and neither runs inside
the hook.

The bag half goes through a transaction: the counter (ItemStatCost 386, `stacked_gem`, 16-bit)
can't be written in place, so the old bag plus the gems go in and a new bag comes out. If anything
fails to validate, nothing is consumed.

The stash half has no SDK route at all - the SDK exposes no way to move an item into the advanced
stash - so the game's own routines are called directly, by address, on the game thread. Sixteen
addresses, each checked against its expected bytes as a set *before* anything is called. A run
starts from one walk of the player's item container, which is the belt, the equipped slots, the cube
and every stash page as well as the inventory: the walk visits all of it and hands over the units
standing on the grid, so nothing it does not hand over can be reached by anything that moves an
item. The class id that the game's "does this belong in the advanced stash" test takes is asked of
the game for the item in hand, rather than assembled from a second enumeration and matched up.

A build whose bytes don't match loses the stash half and keeps the bag half, which needs none of
it. Either way it is the feature that is lost, not the game.

Verified on D2R 3.3.0, build 93847, with Reimagined 3.0.10.

```
src/deposit_plugin.cpp    the entry point - the three exports, and nothing else
src/deposit_native.*      the addresses the stash half calls, and the calls
src/deposit_stash.*       one deposit run, and the sweep the console asks for
src/deposit_gem_bag.*     the bag transaction - SDK only, no address of its own
src/deposit_report.*      'deposit why' - what the run can see, and moves nothing
src/deposit_pickup.*      the pickup hook, and the queue that carries what it saw
src/deposit_console.*     the 'deposit' command
src/deposit_config.*      the config file, and the routing between the halves
src/deposit_codes.h       item codes, the gem tables, and their text form
src/deposit_common.*      the context, the thread service, the log budgets
auto-deposit.toml         the default config, embedded into the DLL
CMakeLists.txt            build, and the optional copy into a mod
build.bat                 find Visual Studio, configure, build
cmake/deploy_dll.cmake    the copy step
external/d2r-pluginsdk    the SDK, if you keep a checkout here (not committed)
```

Version 0.1.4 · by gaoshang212
