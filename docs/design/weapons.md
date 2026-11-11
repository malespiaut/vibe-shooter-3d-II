# Weapons

Seven weapons, four ammo types, one job each. All damage is `base..base+spread` (see `tech.md`).
Fire interval is tics between shots (35 tics = 1 s). Spread is the maximum random horizontal angle
offset per shot (or pellet) in `angle_t` units (65536 = 360 deg), drawn from the gameplay RNG.
Hitscan range is 32 units. View-model sprites are drawn in screen pixels over the 320x208 view,
bottom-centred, hand-drawn with the HUD (they are not world sprites).

## Ammo types

| Ammo | Small pickup | Large pickup | Max (x2 with Litter Satchel) | Used by |
|---|---|---|---|---|
| Ankh charges | 10 (Charge Vial) | 50 (Charge Jar) | 200 / 400 | Ankh Pistol, Sistrum Repeater |
| Yarn shells | 4 (Yarn Bundle) | 20 (Yarn Basket) | 50 / 100 | Hairball Scattergun |
| Scarabs | 1 (Scarab) | 5 (Scarab Crate) | 50 / 100 | Scarab Launcher |
| Sun cells | 20 (Sun Shard) | 100 (Sun Orb) | 300 / 600 | Eye of Ra Lance, Sphinx Howler |

## Stats

| # | Weapon | Job | Damage | Interval (tics) | Spread | Type | Ammo / shot | Introduced | View sprite px |
|---|---|---|---|---|---|---|---|---|---|
| 1 | **Claws** | melee starter, finisher, ammo saver | 8..20 | 14 | 0 | melee, reach 0.75 u | none | start | 96x64 (both paws) |
| 2 | **Ankh Pistol** | accurate single target, early game | 9..14 | 12 | 255 (1.4 deg) | hitscan | 1 charge | start (50 charges) | 64x72 |
| 3 | **Hairball Scattergun** | close-range crowd, Chomper killer | 7 pellets x 5..9 | 32 | 1019 (5.6 deg) per pellet | hitscan | 1 shell | L2 | 112x64 |
| 4 | **Sistrum Repeater** | sustained mid-range DPS | 9..14 | 4 | 510 (2.8 deg) | hitscan | 1 charge | L3 | 96x80 |
| 5 | **Scarab Launcher** | splash, groups, over-cover vs Howitzers | direct 30..50 + splash 96 | 20 | 0 | projectile 14 u/s (26214/tic), splash radius 1.5 u, linear falloff, hurts the player | 1 scarab | L4 | 112x72 |
| 6 | **Eye of Ra Lance** | long range, tanks, pierces lines | 100..140, pierces up to 4 targets | 40 | 0 | hitscan beam | 5 cells | L5 | 96x80 |
| 7 | **Sphinx Howler** | emergency room-clear, breaks shields | 80 to every enemy in a 60 deg cone within 5 u (line of sight), knockback 1 u, forced pain 35 tics | 50 | n/a | cone | 25 cells | L6 (last weapon) | 128x80 |

Average damage per unit of ammo: charge 11.5, shell 49, scarab ~75 (direct plus typical splash),
cell 24 (Lance, single target) or 3.2 per enemy hit (Howler). These feed the difficulty table in
`levels.md`.

## Why no two are redundant

- **Claws vs Pistol**: Claws cost nothing and do more per hit, but you must be in bite range. The
  pistol is the only accurate option before L3.
- **Pistol vs Repeater**: same ammo and per-shot damage. The Repeater has 3x the fire rate and twice
  the spread, so it burns charges; the pistol stays the economical choice for single Mutts at range.
- **Scattergun vs Repeater**: Scattergun deletes Chompers and groups up close in one shot, useless
  past 4 units. Repeater is the mid-range hose.
- **Launcher vs Howler**: Launcher is a ranged, aimable splash weapon that can hit Howitzers behind
  cover and hurts you up close. Howler is point-blank, never hurts you, ignores Shieldbearer shields
  and knocks everything back: the "get off me" button.
- **Lance**: the only weapon that kills a Mastiff efficiently (4 shots) and the only piercing one.
  Shares cells with the Howler, so using it trades away emergencies.

## Behaviour notes

- Switching weapons takes 10 tics (lower 5, raise 5). Firing is blocked during the switch.
- When the current weapon runs dry, auto-switch to the highest owned slot with ammo, skipping 5
  (Launcher) and 7 (Howler) which are never auto-selected.
- Picking up a weapon gives its ammo: Scattergun 8 shells, Repeater 40 charges, Launcher 4 scarabs,
  Lance 60 cells, Howler 50 cells. Picking up a weapon you own gives only the ammo.
- Hitscan uses the DDA ray against tiles and closed doors, then the nearest enemy whose radius the
  ray crosses (piercing for the Lance). Vertical aim is not modelled.
- Muzzle flash: weapons 2-4 and 6 light the view for 3 tics (colormap row forced 4 rows brighter).

## View-model animation frames (hand-drawn)

| Weapon | Frames |
|---|---|
| Claws | idle, swipe x3 (left paw, right paw alternate) |
| Ankh Pistol | idle, fire x2 (flash, recoil) |
| Scattergun | idle, fire x2, pump x2 |
| Repeater | idle, fire x2 (alternating sistrum rattles) |
| Launcher | idle, fire x2, reload x1 |
| Lance | idle, charge x2, fire x2 |
| Howler | idle, roar x3 (sphinx muzzle opens, rings) |

Bob: view sprite moves on a figure-eight of +-6 px x, +-4 px y while walking, from the fine trig table.
