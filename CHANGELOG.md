# Changelog

Auto Deposit, by version. Newest first.

## 0.1.4

**A run sees the whole inventory grid again: the container walk no longer stops
after 128 units.**

The walk a deposit run starts from visits the player's item container - which is
the belt, the equipped slots, the cube and every stash page as well as the
inventory - and it used to stop when its table of 128 units filled. On a character
whose belt, cube and stash pages came to 128 units between them, the table filled
up inside them and the walk never reached the end of the container's list. That is
where a pickup lands: the runes and potions that had just been picked up were past
the window, and no run ever looked at them. Nothing about those items was wrong -
the game called the Tal a Tal and counted it among advanced-stash materials -
which is what made it look like the game refusing one item.

The walk now visits every unit, keeps the page byte as its test, and hands over
the units standing on the grid. The grid's own slots are what the table has to
hold, so 128 is headroom for it rather than a limit; the rest of the container is
counted instead, so a run's summary is about the whole container and not only
about the part it could act on. A grid that ever did outgrow the table is counted
and said out loud, because a walk that quietly looks at part of the grid is the
shape of this bug, not a way to avoid it.

The automatic path was blind in the same way and for the same reason: the pump
that looks for a picked-up id read the same truncated walk, so an item that had
arrived past the window never turned up within the ten seconds it is looked for,
and the run was never asked in the first place.

**`deposit why` counts the grid against the SDK's own view of it.** The report's
per-unit lines are the units a run is handed - the grid - and its closing line
prints the walk's count beside the number of units the SDK puts on page 0, so a
walk that misses something says so in one line instead of looking like a game
that refused an item. The units the walk sees and does not hand over - the belt,
the equipped slots, the cube, the stash pages - are named after that, as context
for where an item has gone.

## 0.1.3

**`deposit why` is new: a read-only report of everything the stash half can see.**

One line per unit in the player's container - which page the container walk puts it
on, what the game answers about it (the "do not move this" flag, the class id, and
whether that id counts as advanced-stash material) and what the SDK says about the
same unit, including which container it is really in - then one line per item code,
then the page totals. Nothing is moved, so it is safe to run with the inventory
exactly as it stands.

It answers the question a run's own log cannot. The per-item "stays put" lines are
rationed, and a unit a run never looked at is not counted among the ones it did: an
item sitting in the grid that is never mentioned looks the same whether the game
refused it or the run never saw it. Page and container are what tell those apart -
and an item on a page the run never looks at was never refused in the first place.

**A `deposit` you typed answers for every item it refuses.** Those per-item lines
draw on a budget meant for the automatic path, where most of what is picked up is
not stash material and is not news. The console sweep no longer draws on it, and the
summary line names the codes it left standing instead of only counting them.

The load line now carries the plugin's version, so a log answers which build wrote
it.

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
