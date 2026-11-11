# Procedural textures (GAME.md 6.1)

Every `<name>.json` here is built by `tools/texgen.py` into `assets/gen/textures/<name>.tex`
(`tools/build.d/20-texgen.sh`, run by `tools/build_assets.sh`). Preview one by hand:

```
python3 tools/texgen.py assets/src/textures/dt_steel_panel.json -o /tmp/tex --preview /tmp/png
```

Names: `<set>_<what>`, `eg_` Egyptian, `dt_` dog-tech. 1-bit glyph masks (white = carved) live
in `glyphs/`.

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
