# HUD, menus and game flow

All screens are 320x240, palette indices only, hand-drawn (Aseprite) except where noted. Pixel
coordinates are (x, y) from the top-left, ranges inclusive.

## Fonts

| Font | Cell (px) | Glyph set | Use |
|---|---|---|---|
| Small | 6x8 (glyph 5x7, 1 px spacing) | ASCII 32-126 | status bar labels, ammo table, tally numbers, credits |
| Menu | 10x14 (glyph 9x12) | ASCII 32-95 (upper case, digits, punctuation) | menus, intro/intermission/ending text, titles |
| Big digits | 12x16 (glyph 11x16) | `0-9 % / -` | health, armour, current ammo |

Fonts are TEX files, one glyph per cell in a 16-column sheet, hand-drawn, with a 1 px dark shadow
(bottom-right) baked in. Text colour variants are made by remapping the font's ramp at load time
(a 256-byte translation table, not truecolor).

## Flow

```
title --any key--> main menu --New Game--> difficulty --> intro (2 screens) --> level 1
level N --exit--> intermission tally N --> (N<8) level N+1 | (N=8) ending (2 screens) --> credits --> title
play --Esc--> pause menu (Resume, Restart Level, Controls, Quit to Title)
death --> 70 tics of death view (camera drops to the floor, face shows dead) --> "PRESS USE TO TRY AGAIN"
          --> restart level with the inventory held at level entry
```

- **Title**: full-screen hand-drawn art: Mau silhouetted against a chrome pyramid, logo
  "VIBE SHOOTER 3D II", subtitle "PAWS OF FURY". "PRESS ANY KEY" blinks (35 tics on/off).
- **Main menu**: New Game, Controls, Quit. Cursor is a small ankh (12x16) left of the item.
  Items in menu font at x=104, y=96 + 20*i.
- **Difficulty**: Kitten ("I CAN HAS MERCY"), Tomcat ("LET'S DO THIS"), Sabretooth ("NINE LIVES IS
  PLENTY"). Default Tomcat.
- **Controls**: static list of bindings (W/S/A/D or arrows, mouse turn, left click/Ctrl fire, E/Space
  use, Shift run, 1-7 weapons, Esc pause). Rebinding is out of scope for Episode 1.
- **Intermission tally**: background panel; lines count up 1 per tic with a tick sound:
  `KILLS 87%`, `ITEMS 64%`, `SECRETS 2/3`, `TIME 4:12  PAR 4:00`. Any key skips counting, second
  key continues. Story text (`story.md`) follows on the next screen.
- **Ending, credits**: as `story.md`, credits scroll 1 px every 2 tics, then title.

## Status bar (y 208-239, 320x32)

Hand-drawn sandstone cartouche panel with gold inlay, steel rivets bolted over it by the Dominion.

| Element | Box (x0,y0)-(x1,y1) | Content |
|---|---|---|
| Face | (4,208)-(35,239) | 32x32 cat face, framed in a gold oval cartouche |
| Powerup bar | (38,236)-(53,237) | 16x2 draining bar (fullbright gold) while a timed powerup is active |
| Health | (40,210)-(103,225) | fish icon 12x12 at (40,212), big digits right-aligned to x=103 with `%` |
| Armour | (40,226)-(103,234) | scarab icon 8x8 at (40,226), small-font number right-aligned to x=103 |
| Weapon | (112,210)-(175,237) | current weapon icon 48x20 at (120,210), slot row `1234567` in small font at (122,231): owned slots bright, missing dark, current in gold |
| Current ammo | (180,210)-(227,225) | big digits right-aligned to x=227; blank for Claws |
| Keys | (232,212)-(275,235) | three ankh slots 12x16 at x=232, 248, 264 (green, blue, violet), empty slot drawn as a dark socket |
| Ammo table | (280,210)-(319,237) | 4 rows of small font at y=210,217,224,231: type icon 6x6 + current count, e.g. `C 120`, `Y  12`, `S   4`, `R 160` |

Fullscreen (no bar) is not offered. The 3D view is always 320x208.

## Cat face (32x32, hand-drawn, 2 frames per expression where it blinks)

Five health tiers decide fur state: 80+ sleek, 60-79 one whisker bent, 40-59 ear notch bleeding,
20-39 bandaged eye, 1-19 fur on end and one ear flat. Per tier:

| Expression | Trigger | Look |
|---|---|---|
| idle | default; glances left/right every 70-140 tics (game RNG not used: menu/render RNG) | eyes centre/left/right |
| hurt left / right / front | damage this tic, source direction relative to view | ears flat toward the side, hiss |
| big hit | 20+ damage in one hit | fur puffed, pupils pin-points (shown 35 tics) |
| fury | holding fire for 70+ tics | eyes narrowed, fangs |
| smug | weapon pickup | slow blink (cats' smile), 35 tics |
| dead | health 0 | X eyes, tongue out |
| frenzy | Catnip Frenzy active | dilated huge pupils (replaces idle) |

Total: 5 tiers x 7 + dead + god-mode (sunglasses, dev flag only) = 37 faces.

## Other HUD elements

- Crosshair: none (Episode 1 aims horizontally; the view centre is obvious).
- Pickup messages: small font at (4,2), e.g. `PICKED UP A TUNA TIN.`, shown 105 tics, max 2 lines.
- Damage feedback: no palette tint. The view border flashes: the top and side 2 px of the view are
  drawn in `red_12` for 4 tics per hit; pickups flash `gold_10` for 3 tics.
- Automap: out of scope for Episode 1 (the dev top-down view from M1 stays a dev flag).
