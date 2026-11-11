# Enemies: the Kennel Dominion

Six dog-man types plus the boss. Each is readable apart by **silhouette first, dominant ramp
second** (ramp names from `art-bible.md`). All are pre-rendered from Blender (`art-bible.md`), 8
directions for every animation. Speeds in units/s and 16.16 per tic. Pain chance is out of 256 per
damaging hit. Wind-up is the telegraph before the attack resolves: during it the enemy stops,
faces the player and shows a distinct pose plus a fullbright glow, and plays a sound.

## Stats

| Enemy | Role | Size (w x h units / px) | HP | Speed | Attack | Wind-up (tics) | Pain | Counter | Intro |
|---|---|---|---|---|---|---|---|---|---|
| **Mutt Trooper** | fodder, hitscan | 0.5 x 0.875 / 32x56 | 20 | 1.5 (2809) | rifle hitscan 3..12, one shot, max range 20 u | 20 (rifle raised, muzzle LED glows red) | 200 | Pistol, Claws | L1 |
| **Chihuahua Chomper** | fast melee | 0.375 x 0.4375 / 24x28 | 15 | 5.5 (10299) | bite 4..10 at 0.6 u | 8 (jaws open wide, yip) | 160 | Scattergun | L2 |
| **Pomeranian Plasmagunner** | ranged projectile | 0.625 x 0.75 / 40x48 | 45 | 2.0 (3745) | 2 plasma bolts 8 u/s (14980), 10 tics apart, 8..16 each, dodgeable sideways | 16 (fur ruffs up, cyan glow on arm cannon) | 128 | Repeater | L3 |
| **Dachshund Howitzer** | area denial, forces movement | 1.0 x 0.5 / 64x32 (side) | 70 | 1.2 (2247) | mortar: marks the player's current position with a floor reticle, shell lands 42 tics later, splash 40 max, radius 1.5 u. Headbutt 5..10 under 1.5 u | 14 before launch (tube rises) + 42 reticle | 100 | Launcher, rush with Scattergun | L4 |
| **Mastiff Juggernaut** | tank | 0.9 x 1.0 / 58x64 | 400 | 1.0 (1872) | bone missile 6 u/s (11235), direct 20..40 + splash 32 r1.0; fires 2 alternating shoulders | 24 (shoulder pods open, red fullbright) | 40 | Lance, Launcher | L5 |
| **Shepherd Shieldbearer** | flank-or-die | 0.625 x 0.9375 / 40x60 | 90 | 2.2 (4119) | shock baton 12..20 at 1.0 u; walks at you with shield up | 16 (shield drops aside, baton crackles) | 64 | Howler, flanking, splash | L6 |
| **Woofu, Chrome Pharaoh** (boss) | finale | 1.5 x 1.5 / 96x96 | 2400 | 0.8 (1498) | see below | 20-30 | 0 (scripted flinch) | Lance, Launcher | L8 |

## Per-enemy notes

**Mutt Trooper.** Basset-ish dog-man in olive armour vest (`olive` ramp, fur `beige`), droopy ears,
rifle held across chest. The slow wind-up makes hitscan fair: when you see the red LED, step behind
cover. Drops a Charge Vial with 5 charges on death. Silhouette: upright, droopy ears, long rifle.

**Chihuahua Chomper.** Tiny, huge ears, oversized cyber-jaw, `beige` fur with `red` visor, runs on
four legs. Arrives in packs of 3-6, often from ambush closets. Low HP but hard to track: the
Scattergun's spread is the answer. Silhouette: tiny, two huge triangle ears.

**Pomeranian Plasmagunner.** A ball of `orange` fur with a `cyan` arm cannon. Bolts are fullbright
cyan, travel at 8 u/s (the player walks 4, runs 7), so strafing dodges them at range. Silhouette:
round puffball with a thin cannon.

**Dachshund Howitzer.** Long low body with a mortar tube on its back, `brown` fur, `steel` tube. Keeps
6-10 units away, retreats if closer than 4. The reticle is a fullbright red ring sprite on the floor
under where you stood: the only correct answer is to move. This is the enemy that changes how you
move: no camping behind a pillar. Silhouette: horizontal sausage, vertical tube.

**Mastiff Juggernaut.** Massive chrome-plated dog torso on tracked legs, `steel` and `gray` with
`red` fullbright eye and pod lights. Missiles are slow and big; side-step them. Never placed where
the player can't retreat. Silhouette: wide box with shoulder pods.

**Shepherd Shieldbearer.** Black-and-tan (`brown` dark end plus `beige`) German shepherd in riot
gear with a tall `blue`-tinted energy shield. The shield blocks all hitscan and direct projectile
damage arriving within +-60 deg of its facing; splash damage passes at half; the Howler passes in
full and forces a 70-tic shield drop. During the wind-up the shield swings aside and the front is
exposed: baiting the swing is the skill. Silhouette: upright with a tall rectangle in front.

## Boss: Woofu, the Chrome Pharaoh

A cybernetic greyhound pharaoh fused into a walking chrome sarcophagus (`gold`, `blue` lapis,
`steel`), crook and flail replaced by a plasma sceptre and a chain mace. Only appears under sky in
the level 8 arena.

| Phase | HP | Behaviour |
|---|---|---|
| 1 "Royal Decree" | 2400..1201 | Alternates: plasma fan (5 cyan bolts, 9 u/s (16852), 2048 angle units apart, 10..20 each, wind-up 24 tics with sceptre raised) and every 700 tics opens two kennel pods in the arena walls releasing 2 Chompers each (max 8 alive). |
| 2 "Unleashed" | 1200..0 | Sarcophagus lid blows off (scripted 35-tic flinch, then faster: speed x1.5). Adds a 3-reticle mortar barrage (reticles on the player and 1.5 u either side, 42 tics) between fans; fans fire twice in a row. Pods every 525 tics. |

Death: 105-tic death animation, then the arena's exit door (tag) opens to the ending.

## Animation list (every enemy, 8 directions each)

| Anim | Frames | Notes |
|---|---|---|
| `idle` | 2 | breathing loop, 18 tics/frame |
| `walk` | 4 | |
| `windup` | 2 | telegraph pose, glow on frame 1 |
| `attack` | 2 | muzzle/bite frame then recover |
| `pain` | 1 | |
| `death` | 5 | last frame is the corpse, stays forever |
| `gib` | 8 | generated by the gib tool from `pain` frames (GAME.md 6.2), triggered when a hit leaves HP <= -(max HP / 2) |

Boss adds `phase` (6 frames, lid blowing off) and has no `gib`. Chomper and Dachshund `windup` are
single-frame holds repeated (2 identical frames are fine).

## AI (shared)

States: `idle` (asleep; wakes on sight within 120 deg FOV or on hearing the player fire within the
same connected area, unless flagged deaf) -> `chase` (move toward the player on the tile grid,
re-evaluating every 8 tics; prefers 8 compass directions; avoids `BLOCK_MONSTER` cells) -> `windup`
-> `attack` -> `chase`. Decision to attack: line of sight and a range check, then `rng() % 256 <
aggression` where aggression per enemy is Mutt 64, Pom 48, Dachshund 40, Mastiff 32, checked every
8 tics; melee enemies attack whenever in reach. Enemies do not open keyed doors; they open unkeyed
doors. Infighting: none (keeps encounters readable and deterministic to design).
