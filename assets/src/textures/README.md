# Procedural textures (GAME.md 6.1)

Every `<name>.json` here is built by `tools/texgen.py` into `assets/gen/textures/<name>.tex`
(`tools/build.d/20-texgen.sh`, run by `tools/build_assets.sh`). Preview one by hand:

```
python3 tools/texgen.py assets/src/textures/dt_steel_panel.json -o /tmp/tex --preview /tmp/png
```

Names: `<set>_<what>`, `eg_` Egyptian, `dt_` dog-tech, at most 16 characters (the MAP texture
name table is `char[16]`, `tech.md`). 1-bit glyph masks (white = carved) live
in `glyphs/`.

## Texture list

| Name | Kind | Intended use |
|---|---|---|
| `eg_sand_blocks` | wall | default Egyptian sandstone, 4 staggered courses (all EG levels) |
| `eg_sand_blocks2` | wall | small paler blocks, 8 courses: variety next to `eg_sand_blocks`, inner rooms |
| `eg_ashlar` | wall | large dressed blocks, 2 courses: gatehouses, monumental halls, outer walls |
| `eg_glyph_band` | wall | carved hieroglyph band (seated cat, cat eye, fish): shrines, landmarks, room trims |
| `eg_gold_pillar` | wall | pillar face with gold-inlaid centre strip: 1x1 pillars, door surrounds |
| `eg_papyrus` | wall | green papyrus reed panel in a sandstone frame: shrines, L2 galleries |
| `eg_mural` | wall | painted Nile-blue mural strip with waves and fish: L3 Customs House, halls |
| `eg_sand_cracked` | wall | `eg_sand_blocks` with two fine cracks: pushwall secrets (subtle tell) |
| `eg_paw_wall` | wall | `eg_sand_blocks` with carved paws and claw scratches: L1 scratched-paw secret, used nowhere else on L1 |
| `eg_tomb_stone` | wall | dark rough brown blocks: L4 tomb, crypt corridors |
| `eg_museum_plaque` | wall | plaster between cornice and skirting with a gold plaque (cat emblem, illegible glyph lines): L2 gift shop, museum walls |
| `eg_door` | door | unkeyed door: cedar leaf, gold bands, cat-eye emblem, sandstone jambs |
| `eg_door_green` | door | Green Ankh door: 4 px green key band on each jamb (x 0-3, 60-63), green ankh in a dark niche on a gold plate |
| `eg_door_blue` | door | Blue Ankh door: 4 px blue key bands, pale blue ankh (lightest of the three) |
| `eg_door_violet` | door | Violet Ankh door: 4 px violet key bands, darker violet ankh with an `fb_violet` head (brightmap) |
| `eg_switch_off` | switch | lever up, dim red lamp (`fb_red_dim`): switch and `EXIT` cells before use |
| `eg_switch_on` | switch | lever down, green lamp (`fb_green`): the same switch after use |
| `eg_floor_tiles` | floor/ceiling | 2x2 sandstone flags: default EG floor and ceiling |
| `eg_sand` | floor | loose rippled sand: courtyards, outdoor EG (L1, L6) |
| `eg_planks` | floor/ceiling | narrow wooden boards: docks and catwalks (L3), museum floors, wooden ceilings |
| `eg_tomb_floor` | floor/ceiling | dark stone flag in a gold border: L4 tomb chambers, throne rooms |
| `eg_nile` | floor | Nile water, under `BLOCK_ALL` cells (L3) |

Keyed doors (EG and DT) carry the ankh (`glyphs/ankh.png`, the key's shape, items.md); unkeyed
doors never do (`eg_door` keeps the cat eye, `dt_door` the hazard chevrons). Blue and violet differ in
value as well as hue so they separate at half size: the blue ankh is light (`blue_7`..`blue_11`), the
violet ankh is darker (`violet_4`..`violet_9`) with its head in `fb_violet` (`glyphs/ankh_head.png`,
the loop rows of `ankh.png`), like the key sprite's fullbright head. The three keyed EG doors share
every parameter except the `band` and `ankh` materials and the violet brightmap; the switch pair
shares everything except the lever glyph and the lamp's fullbright entry, so the switch does not
shift when it flips.

## Pipeline

1. Source maps from `layers`: heightmap, region map (which material a texel uses), per-block tone.
2. Normals from the heightmap; Lambert from the one key light (top-left, 45 degrees, in front of
   the viewer, art bible); heightfield shadows cast toward the bottom-right.
3. Tileable value noise (`noise`) and a grime map (`grime`: cavity + faces turned away from the
   light + noise).
4. Each region's ramp gives the colour; where grime passes `base2.threshold` the second base
   colour's ramp is used instead.
5. Brightmap texels keep their fullbright entry.
6. Quantized through `tools/quantize.py` (libbast Oklab nearest; every texel is already an exact
   ramp entry, so this is the identity and texgen fails if it is not).
7. Written as TEX through libbast.

Everything wraps at the tile edge (rolls, wrapped noise lattice, primitives placed modulo the
size), so every texture tiles in both axes. Same file, same `seed`: byte-identical output.

## Schema

All coordinates are texels from the top-left; anything may cross the tile edge (it wraps).

| Key | Meaning |
|---|---|
| `desc` | one line describing the look |
| `tile` | `wall` or `floor` (informational; both tile in both axes) |
| `size` | texture size, default 64 (1 world unit) |
| `seed` | integer; all randomness (block jitter, noise lattices) derives from it |
| `layers` | list of primitives, applied in order (below) |
| `noise` | `cells` (lattice cells per tile, divides size, default 16), `octaves` (3), `height` (grain added to the heightmap before lighting, 0), `amount` (grain added to the shade, 0.15), `blotch_cells` (4), `blotch` (low-frequency shade variation, 0.1) |
| `light` | `relief` (heightmap gradient scale, 1), `contrast` (how far bevels move along the ramp, 0.6), `shadow` (darkening of cast shadow, 0.25), `shadow_steps` (ray length in texels, 3), `shadow_slope` (height drop per texel of the light ray, 0.35) |
| `grime` | `radius` (blur radius for cavities, 2), `cavity` (0.6), `shade` (weight of faces turned away from the key light and texels in cast shadow, so dirt collects on bottom-right edges and never on lit top-left ones, 0), `noise` (blotch weight, 0.4), `grain` (0.2), `darken` (shade loss at full grime, 0.15) |
| `materials` | region name -> `{ramp, lo, hi, level, base2, dither}`: entries `<ramp>_<lo>`..`<ramp>_<hi>`, `level` = ramp position (0..1) of a flat, lit, clean texel, `base2: false` keeps this region out of the second base colour, `base2: <number>` gives this region its own threshold, `dither: true` allows the 2x2 checker between two adjacent entries where the shade falls between them |
| `base2` | second base colour: `{ramp, lo, hi, threshold, max_light}` used where grime > threshold and the Lambert term is at most `max_light` (1.0 = flat ground, so faces turned toward the key light stay clean; default 1.0) |
| `brightmap` | list of `{rect: [x, y, w, h], entry: "fb_*"}` or `{glyph: "x.png", x, y, entry}` |

Layer primitives (`type`):

| Type | Keys | Effect |
|---|---|---|
| `bricks` | `rows`, `cols` (divide size), `mortar` (px, 2), `bevel` (px, 2), `offset` (row stagger as a fraction of a block, 0.5), `jitter` (block height variation), `tone` (block shade variation), `region` (`face`), `mortar_region` (`mortar`) | fills the whole tile with bevelled blocks |
| `panel` | `rect`, `bevel`, `depth` (> 0 raised, < 0 inset), `region`, `tone` | bevelled plate added to the current height |
| `rivets` | `points`, `radius` (1.5), `height` (0.8), `region` (`rivet`) | domes |
| `grooves` | `lines` ([x0, y0, x1, y1], horizontal or vertical), `width` (1), `depth` (0.6), `region` (`groove`) | cut lines |
| `glyph` | `png` (in `glyphs/`), `x`, `y`, `depth` (0.6, negative raises), `region` (`glyph`) | inset 1-bit mask |
| `stripes` | `rect`, `period` (divides size), `width`, `dir` (`/` or `\`), `depth` (0), `region` (`stripe`) | diagonal hazard bands inside `rect`: texels with (x + y) or (x - y) mod `period` < `width` |

Art bible rules the parameters must keep: ramps only (no fullbright outside `brightmap`), no
outlines, dithering only through `dither`, never on detail under 16 px.

## Texture list: dog-tech (and the proof texture)

| Name | Kind | Use |
|---|---|---|
| `eg_sandstone_blocks` | wall | Egyptian base wall (L1-L4, L6) |
| `dt_steel_panel` | wall | dog-tech base wall: two plates, stamped paw, cyan LED (proof texture) |
| `dt_steel_panel_b` | wall | steel variant: four square plates; corridors, filler between features |
| `dt_steel_panel_c` | wall | steel variant: three tall plates with foot vents; rooms, L5/L7 halls |
| `dt_riveted_plate` | wall | heavy riveted slab with centre strap; bulkheads, vault walls (L5 Lance Vault) |
| `dt_scaffold_sandstone` | wall | mixed theme: olive girders bolted over sandstone (L1 scaffolds, L6 construction) |
| `dt_banner` | wall | Dominion banner, olive cloth, original gold paw emblem; set dressing on DT walls |
| `dt_screen_f0` / `dt_screen_f1` | wall, 2 frames | flickering screen; the frames differ only in the readout lines and cursor (L5 secret tell, L7 control rooms) |
| `dt_server_rack` | wall | server rack with fullbright LED rows (L7 server farm) |
| `dt_rack_odd` | wall | `dt_server_rack` with the cyan/amber LEDs off and two red LEDs per unit in a shifted pattern: L7 Scarab Plate pushwall, used once |
| `dt_conveyor` | wall | conveyor belt under a hazard hood (L5 Packaging Line, secret tell "belt running into a wall") |
| `dt_pump` | wall | pump drum between steel pipes, amber lamp (L3 pump houses, L5) |
| `dt_hazard` | wall | steel with a gold/black diagonal warning band; edges of sludge pits, drops, timed doors |
| `dt_steel_cracked` | wall | `dt_steel_panel_b` with one cracked plate: pushwall tell, used nowhere else on a map |
| `dt_door` | door | unkeyed DT door: split leaves, olive jambs, hazard chevrons |
| `dt_door_green` / `dt_door_blue` / `dt_door_violet` | door | keyed DT doors: 4 px key-ramp band on both jambs and a key-coloured ankh plate (`items.md`) |
| `dt_switch_off` / `dt_switch_on` | switch pair | lever up + red lamp / lever down + cyan lamp; exit, timed-door and tag switches |
| `dt_grate_floor` | floor | slotted grate over a dark pit; catwalks, gantries (L3, L5) |
| `dt_diamond_plate` | floor | tread plate with alternating raised bars; DT rooms and stairs |
| `dt_kibble_sludge` | floor | orange-brown kibble slurry for `DAMAGE` cells (L5 Sludge Pits, Grinder Hall) |
| `dt_concrete` | floor/ceiling | four poured slabs with saw joints; yards, L6 construction, generic ceiling |
| `dt_ceiling_light` | ceiling | light panel: four `fb_white` tubes behind a cyan diffuser; lit DT rooms |

Fullbright use in the DT set: `fb_cyan`, `fb_amber` and `fb_red` only for LEDs, screens and warning
lamps. The ceiling light core uses `fb_white`, the light source entry the art bible names for light
sources, because a cyan or amber tube would tint every lit room.

`glyphs/`: `paw.png` (stamped paw), `paw_big.png` (banner emblem, original four-toe dog paw),
`ankh.png` (key door plate), `crack_dt.png` (dog-tech pushwall crack; `crack.png` is the
Egyptian one used by `eg_sand_cracked`).
