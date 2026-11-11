# Levels: Episode 1 "Paws of Fury"

Eight levels. Counts are for skill 2 "Tomcat" (the reference); Kitten removes about 25% of
enemies via skill flags, Sabretooth adds about 20%. Map sizes in tiles (1 tile = 1 unit = 64 px).
Par times in m:ss. Every level ships `demos/levelN-complete.demo` (see `roadmap.md`).

Theme legend: **EG** Egyptian sandstone/gold/papyrus, **DT** dog-tech steel/cyan/olive scaffolding.

## Overview

| # | Name | Theme | Size | Keys (in order) | Introduces | Enemies | Par |
|---|---|---|---|---|---|---|---|
| 1 | Sandbox Gate | EG + first DT scaffolds | 24x24 | Green | Claws, Pistol, Mutt, doors, key, secret, exit switch | 14 | 2:30 |
| 2 | The Mew-seum | EG interior | 32x32 | Green, Blue | Scattergun, Chomper, ambush closets | 28 | 4:00 |
| 3 | Nile Catwalks | EG docks + DT pumps | 40x32 | Blue, Green | Repeater, Pomeranian, timed doors | 34 | 5:00 |
| 4 | Tomb of Tutankhamew | EG, dark | 40x40 | Violet, Green | Launcher, Dachshund, switch-rewired hub | 33 | 6:00 |
| 5 | Kibble Refinery | DT industrial | 48x40 | Blue, Violet | Lance, Mastiff, damaging floors, explosive barrels | 42 | 7:00 |
| 6 | Sphinx Under Construction | DT on EG, outdoor sky | 48x48 | Green, Blue | Howler, Shieldbearer, one-way drops | 52 | 8:00 |
| 7 | Pyramid Server Farm | DT chrome + EG, climax | 56x48 | Green, Blue, Violet | blackout switch rewires lights and walls | 64 | 9:00 |
| 8 | Throne of Woofu | chrome-EG kitsch, finale | 48x48 | Violet | boss | 32 + boss | 7:00 |

Sum of par: 48:30.

## Difficulty curve

"Ammo supplied" is the average damage of all ammo on the critical path plus guaranteed drops (secrets
excluded), using `weapons.md` per-ammo averages. Ratio = supplied / enemy HP pool. Ratios fall because
the player carries ammo over and gets more accurate.

| # | Enemy HP pool | Ammo supplied (avg dmg) | Ratio | Health supplied | Armour supplied | Target health at exit | Note |
|---|---|---|---|---|---|---|---|
| 1 | 280 | 1150 | 4.1 | 60 | 0 (+100 secret) | 80+ | tutorial |
| 2 | 510 | 1580 | 3.1 | 100 | 100 | 75 | |
| 3 | 830 | 2230 | 2.7 | 125 | 100 | 70 | |
| 4 | 1070 | 2460 | 2.3 | 150 | 100 | 75 | **breather**: fewer, spaced fights, puzzle focus |
| 5 | 2100 | 4600 | 2.2 | 175 | 200 | 60 | first spike (Mastiffs) |
| 6 | 2850 | 5700 | 2.0 | 200 | 200 | 65 | **breather** in the first third (scaffold climb, 6 Mutts) |
| 7 | 4130 | 7400 | 1.8 | 250 | 300 | 50 | hardest regular level |
| 8 | 4570 | 8200 (arena restocks) | 1.8 | 300 (+Golden Sardine) | 200 | 40 at the boss kill | climax |

Pools: L1 14 Mutt. L2 18 Mutt, 10 Chomper. L3 16 Mutt, 10 Chomper, 8 Pom. L4 12 Mutt, 8 Chomper, 8
Pom, 5 Dachshund. L5 14 Mutt, 10 Chomper, 10 Pom, 6 Dachshund, 2 Mastiff. L6 14 Mutt, 12 Chomper, 10
Pom, 6 Dachshund, 8 Shield, 2 Mastiff. L7 16 Mutt, 14 Chomper, 12 Pom, 8 Dachshund, 10 Shield, 4
Mastiff. L8 8 Mutt, 8 Chomper, 6 Pom, 4 Dachshund, 4 Shield, 2 Mastiff, Woofu 2400 + up to 12
summoned Chompers.

Balance PRs adjust item placement first, enemy counts second, stats last, and update this table.

## Door kinds used

Unkeyed auto-close, keyed (green/blue/violet), one-way (L6, L7), switch-only (L4, L7), timed (L3,
L5), `START_OPEN` doors that close on a tag (L4 and L7 layout changes), pushwall secrets (all).

---

## 1. Sandbox Gate

- **Theme / mood**: the sandstone city gate at dawn, bright (`dark` 0-4 outside, 8 inside the
  gatehouse). Dog-tech scaffolds and floodlights bolted to the walls hint at what is coming.
- **Critical path**: start plaza -> unkeyed gatehouse door -> Litter Courtyard -> Shrine of the Green
  Ankh (key) -> back through the courtyard to the green door -> Gate Winch Room -> exit switch (a
  big paw lever on a wall, `EXIT`).
- **Teaches without text**: the first Mutt stands facing away in a corridor (shoot it from behind).
  The first door is directly ahead of the start. The green door is visible from the courtyard
  before the key, so the player sees the lock, then the key. The first secret wall has a scratched
  paw-mark texture that is used nowhere else on the map.
- **Set pieces**:
  1. *The Welcome Mat*: start plaza, 1 Mutt with its back turned 6 tiles ahead, a Fish Snack behind
     a pillar. Safe first kill.
  2. *Litter Courtyard*: open square around an obelisk, 4 Mutts behind sandstone pillars, waking
     together when the player enters. Pillars teach cover against the wind-up.
  3. *Shrine of the Green Ankh*: picking up the key fires a tag opening two `START_OPEN`-false
     ambush closets behind the player, 3 Mutts. Closets visibly have a different wall texture.
  4. *Gate Winch Room*: 3 Mutts on a raised-looking dais (decor) around the exit lever.
- **Secrets (2)**: scratched-paw pushwall in the courtyard -> Linen Wrap + 2 Fish Snacks; a lit
  alcove visible through a window behind the shrine, reached by a pushwall with a slightly lighter
  sandstone block -> Charge Jar.
- **Items**: 4 Fish Snacks, 1 Tuna Tin, 3 Charge Vials, 1 Charge Jar (critical path), 14 drops.

## 2. The Mew-seum

- **Theme / mood**: the Catlantis Museum of Natural Mewstory, interior, warm torchlight (`dark` 6-12),
  gift-shop humour on signage textures. The Kennel Dominion is looting it.
- **Critical path**: lobby -> Hall of Mummified Mice -> Scattergun gallery -> green key in the Gift
  Shop -> green door -> Hall of Pharaohs -> blue key in the Curator's Office -> blue door -> loading
  dock exit.
- **Introduces**: Scattergun (on a pedestal in the gallery), Chomper, ambush closets that open behind
  the player.
- **Set pieces**:
  1. *Hall of Mummified Mice*: rows of glass cases (solid props); 2 Chompers burst from sarcophagus
     closets when the player reaches the middle, before the Scattergun, so they are fought with the
     pistol or Claws: memorable and slightly scary.
  2. *Do Not Touch The Exhibit*: taking the Scattergun closes the gallery doors (tag closes two
     `START_OPEN` doors) and opens 3 closets: 6 Chompers. The player learns the Scattergun by using
     it. Doors reopen when all 6 are dead (tag fired by a kill-count trigger: M3 "kill trigger").
  3. *Gift Shop Brawl*: 5 Mutts among shelves, green key on the counter.
  4. *Hall of Pharaohs*: long hall of cat statues, Mutts on both ends, Chompers from the side
     doors; the blue door is at the far end.
- **Secrets (3)**: a statue with its head turned the wrong way marks a pushwall -> Scarab Plate; a
  "Staff Only" door texture in the gift shop is a pushwall -> Catnip Frenzy; a cracked display case
  wall in the office -> Tuna Tin x2.
- **Items**: 6 Fish Snacks, 2 Tuna Tins, 1 Linen Wrap, 4 Charge Vials, 1 Charge Jar, Scattergun,
  3 Yarn Bundles, 1 Yarn Basket.

## 3. Nile Catwalks

- **Theme / mood**: riverside docks at dusk, sky overhead on the piers (`dark` 4-10), the Nile as a
  water floor texture in cells the player cannot enter (`BLOCK` cells: solid for movement, drawn as
  floor, no wall; a M3 cell flag). Dog-tech pump houses sit on the piers.
- **Critical path**: pier 1 -> Customs House (blue key) -> blue door to the pump yard -> Pump House
  timed door -> green key at the top of the Lighthouse of Paw-ros -> green door -> barge gate exit.
- **Introduces**: Sistrum Repeater (Customs House), Pomeranian, timed doors (switch opens a door for
  8 s).
- **Set pieces**:
  1. *Barge Crossing*: a long pier with crates; 3 Pomeranians at the far end. Their bolts are slow:
     the player learns to strafe. The Repeater is picked up just before.
  2. *Pump House Sprint*: a switch opens the pump-room door for 8 s (280 tics); the path runs past 2
     Chompers. Failing just means pressing the switch again.
  3. *Lighthouse of Paw-ros*: spiral of rooms around a central pillar, Mutts and Poms on each turn,
     green key at the top.
  4. *Barge Gate*: the exit yard fills with 6 Mutts and 2 Poms from closets when the green door opens.
- **Secrets (3)**: a crate stack with a gap leads to a dead-end pier that is a pushwall -> Litter
  Satchel; a lit window above the water in the customs house -> Scarab Launcher + 4 scarabs; a fish
  painted on the pump wall -> Golden Sardine.
- **Items**: 6 Fish Snacks, 3 Tuna Tins, 1 Linen Wrap, 6 Charge Vials, 2 Charge Jars, 4 Yarn
  Bundles, 1 Yarn Basket.

## 4. Tomb of Tutankhamew

- **Theme / mood**: the royal tomb, the darkest map (`dark` 12-20, braziers as fullbright islands).
  Pure Egyptian: no dog-tech except the looters' lamps. A breather with fewer, spaced fights and a
  layout puzzle.
- **Critical path**: descent hall -> the Hall of Four Doors (hub) -> violet key in the Antechamber of
  Yarn -> violet switch room: the **Shifting Hall** switch fires a tag that closes the hub's open
  north passage and opens its sealed east and west walls (`START_OPEN` doors and switch-only doors
  with wall textures) -> back through the same hub, now a different route -> green key in the
  Burial Chamber -> green door -> exit stair.
- **Layout reuse**: the hub is crossed three times; after the switch it is the same room with a
  different set of open walls, so the player re-reads a familiar space.
- **Introduces**: Scarab Launcher (Antechamber), Dachshund Howitzer.
- **Set pieces**:
  1. *Brazier Walk*: a dark corridor lit only by braziers; Mutts stand in the dark between them,
     their red LEDs visible first.
  2. *First Shell*: a single Dachshund on a far ledge across a wide hall: its reticle forces the
     player off the obvious line. Launcher picked up just before.
  3. *Shifting Hall*: pulling the switch shuts the way back and opens two side walls, releasing 4
     Mutts and 2 Poms that were sealed in.
  4. *Burial Chamber*: 3 Dachshunds behind sarcophagi, Chompers around the green key.
- **Secrets (3)**: a hieroglyph wall showing a cat pushing a block -> Ninth Life; a dark corner with
  a single lit floor tile (`dark` lower) -> Tuna Tin x2 + Scarab Crate; a fake sarcophagus wall in
  the Burial Chamber -> Cat's Eye Amulet.
- **Items**: 6 Fish Snacks, 4 Tuna Tins, 1 Linen Wrap, Scarab Launcher, 4 Scarabs, 1 Scarab
  Crate, 4 Charge Vials, 1 Charge Jar, 4 Yarn Bundles.

## 5. Kibble Refinery

- **Theme / mood**: the Dominion's dog-food plant built into a quarry: steel, olive scaffolding,
  cyan screens, conveyor textures, `DAMAGE` kibble sludge pools (5 per 16 tics). Harsh floodlit areas
  next to black shadows (`dark` 0-16).
- **Critical path**: loading bay -> Sludge Pits -> blue key at the Control Tower -> blue door ->
  Grinder Hall (timed door, 10 s) -> violet key in the Lance vault -> violet door -> Packaging Line
  -> exit.
- **Introduces**: Eye of Ra Lance, Mastiff Juggernaut, damaging floors, explosive kibble barrels.
- **Set pieces**:
  1. *Sludge Pits*: walkways over sludge; Poms on the far side; barrels next to Mutt groups invite
     chain explosions.
  2. *The Lance Vault*: the Lance lies in a vault; when picked up, a wall slides (tag) and the first
     Mastiff walks in at 12 tiles. The vault is long enough to land 4 Lance shots.
  3. *Grinder Hall*: timed door sprint across sludge with 2 Dachshunds shelling from the gantry.
  4. *Packaging Line*: second Mastiff plus mixed squad, ammo cache in a side room before it.
- **Secrets (3)**: a conveyor belt texture running into a wall -> Scarab Plate; a flickering screen
  (2-frame texture) -> Sun Orb; a barrel cluster hiding a pushwall -> Catnip Frenzy.
- **Items**: 6 Fish Snacks, 5 Tuna Tins, 1 Scarab Plate, 6 Charge Vials, 2 Charge Jars, 4 Yarn
  Bundles, 1 Yarn Basket, 4 Scarabs, 1 Scarab Crate, Lance, 3 Sun Shards.

## 6. Sphinx Under Construction

- **Theme / mood**: the Great Sphinx wrapped in steel scaffolding while the Dominion grinds its face
  into a dog's. Outdoor sky, midday, bright (`dark` 0-6), steel and sandstone in equal measure.
- **Critical path**: workers' camp -> scaffold climb (one-way drops) -> Satchel and green key on the
  Sphinx's paw -> green door into the Sphinx's chest -> Howler in the Heart Chamber -> blue key in
  the Grinding Room (the face) -> blue door -> exit lift.
- **Introduces**: Sphinx Howler (last weapon), Shepherd Shieldbearer, one-way doors (drop-downs you
  cannot climb back).
- **Breather**: the first third (camp and scaffold) has only 6 Mutts and supplies.
- **Set pieces**:
  1. *Shield Wall*: in the Heart Chamber 3 Shieldbearers advance in a line toward the Howler's
     pedestal. Taking the Howler and using it on them is the lesson.
  2. *Grinding Room*: around a huge face carving, Dachshunds on scaffolds above, Chompers below.
  3. *Paw Plaza*: an open plaza with a Mastiff and 2 Shieldbearers, flanking routes around the paws.
- **Secrets (2)**: a sandstone block with a fresh-cut edge on the scaffold -> Golden Sardine; a gap
  in the scaffold boards seen from below -> Cat's Eye Amulet + Sun Orb.
- **Items**: 7 Fish Snacks, 5 Tuna Tins, 1 Scarab Plate, Litter Satchel, ammo of all types.

## 7. Pyramid Server Farm

- **Theme / mood**: inside a chrome-clad pyramid turned into Woofu's broadcast server farm: server
  rack textures, cyan fullbright LEDs, cold steel with gold Egyptian trim. Bright at first.
- **Critical path**: entry atrium -> green key in the Cooling Hall -> green door -> Server Maze ->
  the **Blackout** switch: a tag closes the maze's lit main aisle (`START_OPEN` doors), opens its
  maintenance walls, and the map's dark values rise (lights are a second `dark` layer selected by the
  tag, M3 "light switch" feature) -> the same maze is crossed again in darkness by a different route
  -> blue key in the Uplink Core -> blue door -> violet key on the Broadcast Bridge (one-way drop) ->
  violet door -> exit.
- **Introduces**: no new enemy or weapon: the level is the remix exam. Mechanic: blackout switch.
- **Set pieces**:
  1. *Cooling Hall*: fans and pipes, 2 Mastiffs at long range across a hall: Lance duel.
  2. *Blackout*: in the dark maze, the only light is enemy wind-up glows and fullbright LEDs.
  3. *Uplink Core*: arena ring around a core pillar, waves of Shieldbearers and Chompers from 4
     closets (two kill triggers).
  4. *Broadcast Bridge*: a narrow bridge with Dachshund reticles landing on it, Poms on the far end.
- **Secrets (4)**: a server rack with a different LED pattern -> Scarab Plate; a gold trim block
  out of place -> Golden Sardine; a maintenance hatch visible only in the blackout -> Ninth Life;
  a dead-end cable trench -> Sun Orb + Scarab Crate.
- **Items**: 8 Fish Snacks, 6 Tuna Tins, 1 Scarab Plate, 1 Linen Wrap, ammo of all types.

## 8. Throne of Woofu

- **Theme / mood**: the pyramid's apex, chrome-and-gold kitsch, giant dog-headed statues of Woofu,
  then an open-sky throne arena at sunset (`dark` 2-8).
- **Critical path**: Processional Way (short gauntlet) -> violet key in the Hall of Tributes ->
  violet door -> arena. Entering the arena closes the door behind (tag). Killing Woofu opens the exit.
- **Set pieces**:
  1. *Processional Way*: a long hall lined with statues, every type of enemy once, in order of
     introduction: a victory lap.
  2. *Hall of Tributes*: 2 Mastiffs and Shieldbearers guarding the violet key.
  3. *The Throne Arena*: 24x24 open area with 4 sandstone pillar clusters for cover and 4 restock
     alcoves (each gives a Sun Shard, Charge Vial, Yarn Bundle and Scarab, respawning every 1050 tics
     while Woofu lives; the only respawning items in the game). Kennel pods in the north and south
     walls.
- **Secrets (2)**: a statue with a raised paw in the Processional Way -> Golden Sardine; a tribute
  pile in the Hall of Tributes -> Catnip Frenzy (for the boss).
- **Items**: Golden Sardine on the arena's south side, 6 Fish Snacks, 4 Tuna Tins, 1 Scarab Plate.
