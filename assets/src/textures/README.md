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
| `eg_door_green` | door | Green Ankh door: 4 px green key band on each jamb (x 0-3, 60-63), green emblem |
| `eg_door_blue` | door | Blue Ankh door: 4 px blue key bands, blue emblem |
| `eg_door_violet` | door | Violet Ankh door: 4 px violet key bands, violet emblem |
| `eg_switch_off` | switch | lever up, dim red lamp (`fb_red_dim`): switch and `EXIT` cells before use |
| `eg_switch_on` | switch | lever down, green lamp (`fb_green`): the same switch after use |
| `eg_floor_tiles` | floor/ceiling | 2x2 sandstone flags: default EG floor and ceiling |
| `eg_sand` | floor | loose rippled sand: courtyards, outdoor EG (L1, L6) |
| `eg_planks` | floor/ceiling | narrow wooden boards: docks and catwalks (L3), museum floors, wooden ceilings |
| `eg_tomb_floor` | floor/ceiling | dark stone flag in a gold border: L4 tomb chambers, throne rooms |
| `eg_nile` | floor | Nile water, under `BLOCK_ALL` cells (L3) |

The four doors share every parameter except the `band` and `emblem` materials; the switch pair
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
| `glyph` | `png` (in `glyphs/`), `x`, `y`, `depth` (0.6), `region` (`glyph`) | inset 1-bit mask |

Art bible rules the parameters must keep: ramps only (no fullbright outside `brightmap`), no
outlines, dithering only through `dither`, never on detail under 16 px.
