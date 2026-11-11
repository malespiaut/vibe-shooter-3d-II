# Vibe Shooter 3D II

## Project Overview

Vibe Shooter 3D II is a retro-style, first-person shooter inspired by early 1990s PC games, particularly the VGA-era aesthetic of Doom and Duke Nukem 3D.

The project aims to recreate the technical and visual constraints of that era while benefiting from modern development tools, compilers, and platform abstraction.

The game is built from scratch, including its rendering engine, assets, sound mixing, and development tools.

**Primary objective:** Build a complete, polished, enjoyable game rather than a technical demonstration.

When modifying the project, prioritize consistency, determinism, performance, visual clarity, and maintainability.

---

## 1\. Fundamental Design Constraints

These constraints define the project's identity and must be respected.

### Graphics

- Target resolution: 320 × 240 pixels.
- Color depth: 256 colors maximum.
- Every pixel is represented by a single palette index.
- Rendering is performed entirely in software.
- No hardware-accelerated rendering or GPU shaders.
- All assets must conform to the project's palette.
- Maintain consistent pixel scaling across the entire game.

### Simulation

- Use fixed-point arithmetic for game logic.
- Game behavior must be deterministic and consistent across platforms.
- Floating-point arithmetic is permitted for rendering where determinism is not required.
- Avoid platform-dependent behavior in gameplay systems.

### Platform Abstraction

The platform abstraction layer should expose only the functionality necessary for the engine:

- Framebuffer access for writing pixels.
- Keyboard and mouse input.
- Audio buffer access for writing samples.
- Filesystem I/O.

Do not introduce unnecessary dependencies on modern graphics or game-engine frameworks.

### Development Philosophy

- Implement systems from scratch when required by the project constraints.
- Prefer simple, understandable algorithms.
- Optimize for the actual limitations of the software renderer.
- Avoid unnecessary abstraction and overengineering.
- Do not introduce AI-generated placeholder assets or AI slop.
- Do not compromise the intended aesthetic for development convenience.

---

## 2\. Rendering Architecture

### 2.1 Palette-Based Rendering

The renderer operates using indexed-color graphics.

Each pixel contains an 8-bit palette index rather than an RGB color.

The palette contains 256 RGB colors (768 bytes total).

When working with textures, sprites, or rendering code:

- Store pixel data as palette indices.
- Never introduce arbitrary truecolor values into runtime rendering.
- Preserve the established palette.
- Use palette-aware algorithms for shading and color conversion.

### 2.2 Palette Organization

The palette contains colors serving several purposes:

- A dedicated transparency color (vibrant pink).
- Pure white.
- Pure black.
- Red shades for blood and damage effects.
- Green and blue shades for keys and color-coded doors.
- Yellow and brown shades for the Egyptian-inspired environment.
- Gray shades for technological and industrial environments.
- Beige shades for warmer alternatives to gray.
- Additional colors required by individual assets.

Do not arbitrarily modify or replace palette entries.

When adding colors, consider the existing palette organization and the visual consistency of existing assets.

### 2.3 Colormap and Lighting

Lighting is implemented through a precomputed colormap.

The colormap maps an original palette index and a shade level to another palette index.

The current approach uses:

- 32 shade levels.
- Perceptual color matching in Oklab color space.
- Hue shifting toward warmer colors at darker levels.
- Precomputed palette lookup tables.

The goal is to produce visually pleasing gradients while retaining the limitations of indexed-color rendering.

#### Colormap Generation

For each palette color:

1. Calculate its target RGB intensity for each shade level.
2. Convert colors to Oklab.
3. Find the closest available palette color using perceptual distance.
4. Store the resulting palette index in the colormap.

Do not perform palette searches during pixel rendering.

#### Runtime Lighting

The shade index is determined by distance:

`colormap_row = 32 * fragment_distance / view_distance`

Use the resulting row and original palette index to retrieve the final color.

Avoid redundant calculations:

- Walls: calculate once per screen column.
- Floors and ceilings: calculate once per screen row.
- Sprites: calculate once per visible sprite.

The renderer should use constant-time palette lookups wherever possible.

---

## 3\. World Rendering

The game uses a traditional tile-based raycasting renderer.

### Walls

- The world consists of equally sized map tiles.
- Use DDA ray traversal to determine wall intersections.
- Render walls as vertical screen columns.
- Sample textures using the appropriate texture coordinates.
- Apply distance-based colormap shading.

### Floors and Ceilings

- Render floors and ceilings as horizontal scanlines.
- Fill the remaining screen area after wall rendering.
- Calculate lighting once per scanline where possible.

### Sprites

- Render sprites as flat billboards.
- Use raycasting visibility information for occlusion.
- Apply distance-based shading.
- Preserve consistent sprite scale.

Avoid introducing rendering techniques that contradict the software-rendering architecture.

---

## 4\. Asset Creation Pipeline

Assets belong to three primary categories:

1. Pre-rendered sprites.
2. Hand-drawn sprites and textures.
3. Procedurally generated sprites and textures.

Choose the appropriate technique based on the asset's visual requirements.

All final runtime assets must be compatible with the game's indexed-color format.

### 4.1 Pre-rendered Sprites

Complex animated objects and enemies can be created using Blender.

Workflow:

1. Create a 3D model.
2. Rig and animate the model.
3. Render animation frames using Blender's Python API.
4. Apply compositing to improve contrast and edge clarity.
5. Quantize rendered images to the game palette.
6. Convert the results into the engine's texture format.

Pre-rendering should make iteration faster without sacrificing the visual style.

#### Enemy Animations

Enemy sprites support eight viewing directions.

Each animation must provide frames for all eight directions.

Animation names and frame filenames must consistently encode:

- Sprite name.
- Animation/action name.
- Direction.
- Frame index.

Use automation to render all animation directions and frames.

Generated sprite assets should not need to be committed to the repository if they can reliably be regenerated from source models and scripts.

### 4.2 Palette Quantization

Every generated image must be converted into the game's indexed-color representation.

For each source pixel:

1. Read its source color.
2. Find the perceptually closest color in the game palette.
3. Store the corresponding palette index.

Use Oklab distance rather than naive RGB or Euclidean distance.

The resulting texture must contain:

- Width.
- Height.
- One byte per pixel containing palette indices.

Use the existing TEX format and serialization implementation.

Do not invent alternative runtime texture formats without a compelling architectural reason.

### 4.3 Hand-Drawn Sprites

Some assets should be drawn manually rather than generated from 3D models.

This is particularly important for:

- Small sprites.
- HUD elements.
- Character faces.
- Pickups.
- Emotion-heavy animations.
- Assets where individual pixels carry significant visual meaning.

Prioritize clarity, personality, and deliberate pixel placement.

Avoid using pre-rendered graphics simply because they are easier to produce.

### 4.4 Consistent Pixel Scale

Pixel scale must remain consistent across all assets.

The world uses the following convention:

**1 world unit = 64 pixels.**

Calculate sprite dimensions relative to this scale.

For example:

- 1/2 world unit = 32 pixels.
- 1/4 world unit = 16 pixels.
- 1/8 world unit = 8 pixels.

Do not arbitrarily increase sprite resolution to compensate for poor-looking artwork.

Inconsistent pixel scales create visual discontinuity and undermine the retro aesthetic.

---

## 5\. HUD and Interface

The HUD is predominantly hand-designed.

It includes:

- Bottom status bar.
- Transition panels.
- Menus and screens.
- Fonts.
- Score screens.
- Other interface elements.

The artwork can be composed using modern image-editing software, but the final result must be pixel-correct.

### Recommended Workflow

1. Compose elements in truecolor if convenient.
2. Use layer effects and compositing for visual treatments.
3. Export the artwork.
4. Perform pixel-correction passes in Aseprite.
5. Ensure text and boundaries are pixel-perfect.
6. Quantize the final result to the game palette.

Useful compositing techniques include:

- Emboss effects.
- Noise overlays.
- Color overlays.
- Blending modes.
- Glow effects.

Avoid uncontrolled antialiasing and blurred boundaries.

The final HUD should look intentionally pixel-designed, not like a modern interface scaled down.

---

## 6\. Procedural Asset Generation

Procedural generation is appropriate for assets sharing a common structure with variable surface details.

Examples include:

- Walls.
- Floors.
- Industrial surfaces.
- Damaged materials.
- Environmental objects.
- Gib animations.
- Particle effects.

Generation should be deterministic when possible and controlled through explicit parameters.

### 6.1 Procedural Textures

The texture-generation pipeline can use:

- Heightmap: defines surface relief.
- Normal map: derived from the heightmap and used for lighting.
- Noise map: introduces surface variation.
- Grime map: controls dirt and wear.
- Two base colors: define the primary material appearance.
- Brightmap: identifies areas retaining their color.

The generator should:

1. Load source maps.
2. Generate surface lighting and shadows.
3. Apply noise and grime.
4. Combine material colors.
5. Preserve brightmap regions.
6. Quantize the result to the game palette.
7. Export a runtime-compatible TEX texture.

Prefer parameter adjustment over manually repainting every texture variation.

### 6.2 Gib Animations

Gib animations are generated offline using Python scripts.

The pipeline consists of three stages.

**Stage 1: Voronoi Decomposition**

- Select K seed pixels from the opaque sprite.
- Assign each pixel to its nearest seed.
- Treat each resulting cell as an independent chunk.

**Stage 2: Wound Bleeding**

- Identify chunk boundaries.
- Mark boundary pixels as wounds.
- Use breadth-first search to calculate depth from boundaries.
- Blend boundary colors toward a configurable blood palette ramp.
- Preserve original colors deeper inside each chunk.

Blood color should be configurable to support different enemy types.

**Stage 3: Physics**

Each chunk receives:

- A centroid.
- An outward velocity.
- Randomized directional spread.
- Spin.
- Gravity.
- Drag.

Simulate the chunks over a configurable number of frames.

Full collision detection is unnecessary for this effect. Chunks can stop when they reach the floor.

Expose parameters such as:

- Random seed.
- Frame count.
- Chunk count.
- Explosion force.
- Gravity.
- Drag.
- Spread.
- Spin.
- Wound depth.

The same approach can be reused for destructible environmental objects such as barrels, crates, and flower pots.

### 6.3 Pre-rendered Particle Effects

Particle effects should be baked offline whenever possible.

Do not introduce a runtime particle simulation system when a pre-rendered animation can achieve the same result more efficiently.

Effects can be generated using independent radial energy layers:

- Core: expanding soft disc.
- Rays: configurable radial spikes with randomized lengths.
- Ring: optional expanding shockwave.
- Noise: irregularities in the energy field.
- Sparkles: optional outward-moving crosses.

Accumulate the energy of each pixel, then map the result to a palette ramp.

Use palette-index operations rather than runtime alpha blending.

Support two animation modes:

**One-shot**

- Energy increases.
- Effect reaches peak intensity.
- Energy decays.

**Loop**

- Sample the noise field along a circular path.
- Ensure the first and last frames match.
- Produce seamless looping effects.

All generated frames must be quantized to TEX format.

---

## 7\. Map Editing and Data

Maps are tile-based and contain more than basic wall/floor information.

They also support:

- Per-cell light levels.
- Cell flags.
- Cell properties.
- Entities.
- Entity properties.
- Game-specific data.

### Custom Map Editor

The project uses a custom map editor rather than relying on Tiled.

The editor is built using wxPython.

Its purpose is to provide a development and end-user experience that understands the game's native data model.

The editor should support:

- Painting map cells.
- Painting light levels.
- Editing cell flags.
- Editing cell properties.
- Placing and editing entities.
- Editing entity properties.
- Saving and loading native map data.
- Launching levels directly from the editor.

Avoid introducing external conversion steps when the editor can work directly with the engine's data model.

### Editor Architecture

Use the Model-View-Presenter (MVP) pattern.

Keep UI logic separate from map data and game-specific logic.

Prefer clear boundaries between:

- Model: map data and serialization.
- View: widgets and presentation.
- Presenter: interaction and application logic.

### Engine Integration

The editor should reuse engine functionality through the existing Python bindings (`pybast`).

These bindings provide access to:

- Game data archives.
- Game textures.
- Fixed-point coordinate types.
- Serialization.

Do not reimplement engine functionality in Python when an existing C++ implementation is available through the bindings.

The engine and its tooling should remain a cohesive ecosystem.

---

## 8\. Performance Guidelines

The project targets a software renderer with a small framebuffer.

Performance decisions should reflect this constraint.

Prioritize:

- Precomputed lookup tables.
- Palette-index operations.
- Offline asset generation.
- Fixed-size data structures where appropriate.
- Avoiding unnecessary per-pixel calculations.
- Reusing engine functionality.
- Pre-rendering expensive visual effects.
- Efficient sprite visibility and occlusion.

Do expensive work during asset compilation rather than at runtime whenever practical.

Do not introduce optimizations that significantly complicate the code without measurable benefit.

---

## 9\. Repository and Asset Management

Generated assets should generally be reproducible from their source files and generation scripts.

Examples include:

- Blender-rendered sprites.
- Procedural textures.
- Gib animations.
- Baked particle animations.

These generated outputs can remain Git-ignored when the repository already supports reliable regeneration.

When modifying generation pipelines:

1. Preserve source models and source artwork.
2. Preserve generation scripts.
3. Keep generation parameters reproducible.
4. Avoid introducing undocumented manual steps.
5. Verify that generated output is compatible with the engine.
6. Ensure a fresh checkout can regenerate required assets.

Do not assume generated assets are available locally.

---

## 10\. Development Guidelines for AI Agents

When working on this project, follow these rules.

### Before Making Changes

1. Inspect the existing repository structure.
2. Read relevant source files and existing documentation.
3. Identify the existing implementation and conventions.
4. Reuse established formats, libraries, and abstractions.
5. Understand the interaction between engine code and tooling.
6. Avoid assuming that implementation details described here are more current than the actual source code.

### When Implementing Features

- Respect all fundamental project constraints.
- Prefer small, focused changes.
- Preserve deterministic gameplay behavior.
- Keep rendering code compatible with indexed-color output.
- Use fixed-point arithmetic for game logic.
- Reuse existing serialization and asset formats.
- Keep asset generation reproducible.
- Avoid unnecessary dependencies.
- Preserve visual consistency.
- Do not introduce GPU rendering or modern rendering shortcuts.

### When Modifying Assets

- Check palette compatibility.
- Check pixel scale.
- Check transparency handling.
- Check sprite dimensions.
- Check animation naming conventions.
- Verify that the output can be loaded by the engine.
- Prefer hand-drawn artwork where individual pixel placement matters.
- Prefer procedural generation where consistent variations are required.

### When Modifying Tools

- Keep editor and engine data models synchronized.
- Reuse Python bindings for engine functionality.
- Maintain separation between UI and model logic.
- Avoid redundant serialization implementations.
- Preserve the ability to launch levels directly from the editor.

### Testing and Validation

After implementing changes:

1. Build the affected components.
2. Run relevant existing tests.
3. Verify serialization and deserialization where applicable.
4. Test generated assets in the actual engine.
5. Check rendering at the target resolution.
6. Verify palette correctness.
7. Verify deterministic behavior for gameplay-related changes.
8. Check that generated outputs can be reproduced.

Do not claim that a feature works without validating it.

---

## 11\. Visual Direction

The intended aesthetic is inspired by early 1990s PC shooters.

Important characteristics:

- Crisp, deliberately placed pixels.
- Restricted color palette.
- Strong contrast.
- Warm-shifted dark shades.
- Readable silhouettes.
- Consistent pixel scale.
- Deliberate use of color.
- Atmospheric distance shading.
- Stylized, expressive artwork.
- Industrial and ancient Egyptian-inspired environments.

The setting is Catlantis, a parody of ancient Egypt inhabited by cats and occupied by cybernetic dog-men.

The visual identity should balance humor, retro shooter aesthetics, and technological environments.

Do not blindly reproduce modern pixel-art conventions. The goal is a coherent rendering and asset style rooted in the technical limitations of early VGA-era games.

---

## 12\. Core Principles

When making architectural or implementation decisions, use the following priorities:

1. **Authenticity:** Preserve the technical and visual identity of early 1990s graphics.
2. **Determinism:** Gameplay must behave consistently across platforms.
3. **Simplicity:** Prefer straightforward implementations over unnecessary complexity.
4. **Performance:** Optimize for the software-rendering architecture.
5. **Consistency:** All assets must share a coherent palette and pixel scale.
6. **Reproducibility:** Generated assets must be regeneratable from source.
7. **Maintainability:** Engine code and development tools must remain understandable.
8. **Polish:** The objective is a complete and enjoyable game, not merely a technical experiment.

**The most important rule: Do not sacrifice the project's identity or technical constraints for convenience.**

When uncertain, inspect the existing implementation and choose the solution that best preserves the architecture and visual direction described in this document.
