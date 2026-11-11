# Items and pickups

All pickups are hand-drawn world sprites (`d0` only), bottom-anchored, with a 2-frame 18-tic
shimmer where noted. Pickup radius 0.5 units. Sizes in units / px at 64 px per unit.

## Health

| Item | Effect | Cap | Size | Notes |
|---|---|---|---|---|
| Fish Snack | +10 health | 100 | 0.25 / 16x12 | common |
| Tuna Tin | +25 health | 100 | 0.375 / 24x16 | |
| Golden Sardine | +100 health | 200 | 0.375 / 24x16, shimmer, fullbright gold | rare, secrets and boss arena only |

Player starts each new game at 100 health, 0 armour. Carried over between levels (health, armour,
weapons, ammo; keys and powerups are not). After death the level restarts with the inventory the
player had when entering it.

## Armour

| Item | Effect | Size |
|---|---|---|
| Linen Wrap | armour to 100 if below; absorbs 1/3 of damage (rounded down) | 0.375 / 24x16 |
| Scarab Plate | armour to 200 if below; absorbs 1/2 of damage | 0.5 / 32x20, shimmer |
| Bandage Scrap | +2 armour, cap 200; keeps current absorption class, Linen if none | 0.125 / 8x8 |

## Ammo

See `weapons.md` for amounts. Sprites: Charge Vial 8x16, Charge Jar 16x20, Yarn Bundle 16x12, Yarn
Basket 24x20, Scarab 12x10, Scarab Crate 28x20, Sun Shard 12x16 (fullbright), Sun Orb 20x20
(fullbright, shimmer). **Litter Satchel**: doubles max ammo and gives one small pickup of each
type, 24x20; one per episode before L5 (in a L3 secret) and one on the L6 critical path.

## Keys

Ankh-shaped keys, 16x24 px (0.25 x 0.375 u), shimmer, the ankh head in the key's fullbright colour.
Doors show the same colour on a 4 px band along both jambs and an ankh glyph on the door face.

| Key | Ramp | Why it reads |
|---|---|---|
| Green Ankh | `green` | reserved by GAME.md; contrasts all sandstone and steel |
| Blue Ankh | `blue` | reserved by GAME.md |
| Violet Ankh | `violet` | no other ramp is purple: yellow/gold would vanish on sandstone, red is blood |

Keys are lost at level end. HUD shows owned keys (`hud.md`).

## Powerups (rare, 2-4 per episode each)

| Powerup | Effect | Duration | Size |
|---|---|---|---|
| Catnip Frenzy | fire intervals halved (round up), movement x1.25, face shows dilated pupils | 1050 tics (30 s) | 16x20, shimmer |
| Cat's Eye Amulet | whole view drawn at colormap row 0 (full light); `PUSHWALL` faces glow with a 2-frame shimmer | 2100 tics (60 s) | 16x16, fullbright |
| Ninth Life | carried (max 1). The next lethal hit leaves you at 50 health and armour unchanged; 70 tics of invulnerability follow | until used | 16x24 ankh-cat, fullbright |

Screen tint is not used (no palette swaps). The status bar shows remaining powerup time as a
draining 16 px bar under the face (`hud.md`).

## Secret-only rewards

Secrets always pay off. Each secret contains at least one of: Golden Sardine, Scarab Plate, a
powerup, the Litter Satchel, or an early weapon (the only one: the Scarab Launcher, normally
introduced in L4, can be found early in an L3 secret with 4 scarabs).
Secrets are counted when the player enters a cell flagged `SECRET`.

## Decorations (non-pickup, some solid)

Hand-drawn or procedural props per theme: obelisk stub, sandstone pillar, papyrus pot (breakable via
gib tool, drops nothing), brazier (fullbright, animated), cat statue, dog-tech floodlight, crate,
kibble barrel (explodes: 64 splash, radius 1.5 u, chains), hanging dog banner, server rack. Solid
props have radius 0.25 u.
