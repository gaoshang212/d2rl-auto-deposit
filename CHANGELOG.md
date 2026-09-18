# Changelog

Auto Deposit, by version. Newest first.

## 0.1.2

**The Gem Bag merge counts from what the bag already holds again - and refuses to
merge at all when it cannot read that.**

The counter is read out of the bag's own stat list, and the loader this plugin now
runs on lays that record out two bytes further along than the one it was written
against: the stat id sits at +0x04 and the value at +0x08, not at +0x02 and +0x04.
The old offsets found no stat id there, so the read reported 0 for a bag holding
832, and every merge built its new bag as `0 + what was picked up`. A bag of 832
became a bag of 1, and stayed at 1.

The read now looks for the stat id and nothing else, and a record it cannot find is
a failed read: the merge says so and touches nothing, rather than building a new bag
on a number it does not have. If a future loader moves the record again, the count
on the bag survives and the log says why nothing happened.

**`deposit probe` is new.** It reports the bag and the number the read found,
consuming nothing. That is the same read the merge acts on, so it tells a read that
has gone wrong from a merge that did - before any gems are spent on the answer.

## 0.1.1

**The two rejuvenation potions are left in the inventory by default.**

`stash_ignore` now ships as `rvs rvl` - `rvs` the small rejuv, `rvl` the full one -
so neither is banked the moment it is picked up. The game's own stash check accepts
both, so this is the plugin's choice and not the game's rule: a rejuv is drunk from
the belt or the inventory in the middle of a fight, and one that was banked on pickup
is not there when it is wanted. Everything else the game counts as stash material
still goes in untouched. Empty the list to bank them like everything else.

**This default reaches fresh installs only.** D2RLoader writes the config file the
first time the plugin loads and never overwrites it afterwards, so an install that
already has `d2rl-auto-deposit.toml` keeps the empty list it was handed. Edit it by
hand to match, or leave it - nothing else changed.

## 0.1.0

First release.

Two destinations, decided per item:

* **Gems and clusters into Reimagined's Gem Bag.** Only the gem you picked up is
  taken; the other loose gems in the inventory stay where you left them.
* **Everything else into the advanced stash**, through the game's own deposit call -
  runes, materials, the cube, anything the game itself counts as stash material.

Both run as you pick something up, driven by a pickup hook and a game-thread pump, so
a burst of pickups is not limited to one at a time. The automatic path stands aside
while the shared stash panel is open, and picks its queue back up when it closes.

The Gem Bag itself, the 35 classic gems and the Cube are never swept up. The bag is
blind to colour and quality - every gem is worth +1 - so merging a Perfect Ruby would
cost its quality forever.

The stash half has no SDK route, so it calls the game's own routines by address. Every
address is checked against its expected bytes as a set before anything is called. A
build whose bytes do not match loses the stash half and keeps the bag half, which
needs none of it - the feature that is lost, not the game.

Config in `d2rloader/config/d2rl-auto-deposit.toml` carries a switch per half and an
ignore list per destination, everything on by default. In the console, `deposit`
sweeps the inventory into the advanced stash and `deposit bag` merges every loose gem.
