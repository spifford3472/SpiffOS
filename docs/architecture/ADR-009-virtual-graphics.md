# ADR-009: SpiffOS Virtual Graphics Architecture
Status: Proposed documentation of user-approved design, 2026-10-20. Implementation details remain open.

## Goals
Hardware-independent virtual graphics chip with beginner-friendly BASIC APIs, documented PEEK/POKE interface, and driver-backed physical output. Workspace Manager retains authority over focus and composition. Graphics features must not violate ADR-008 recovery/isolation requirements.

## Display
- Logical resolution choices include 640x480 and classic 320x200; final default and resolution-selection semantics remain open (640x400 was suggested but not agreed as final).
- Prefer preserving pixel aspect ratio and expanding the visible world horizontally on widescreen monitors rather than distorting pixels or always showing borders. Specify viewport and game-world visibility rules before implementation.
- Full modern per-pixel color (RGBA), transparency, optional indexed palette; no C64 per-sprite/per-tile color restriction.

## Sprites
- Default size 32x32 pixels, with other dimensions supported; no per-sprite color count restriction.
- Per-pixel access, simple BASIC manipulation, documented virtual graphics memory interface.
- Built-in movement, animation and collision detection.
- Collision detection on by default; allow per-sprite, per-pair and group filters, applied before events. Composite sprites move together with configurable combined or component collision bounds.
- Transparency excluded from optional pixel-accurate collision tests; bounding-box tests offered for performance.
- Collision checks and collision events are distinct: programs can poll without subscribing to events.

## Tiles and animation
- Built-in Tile Editor and Tile Palette Manager, full-color user-defined tiles, tile properties (e.g. solid/water), tile maps and smooth scrolling.
- Runtime tile definition changes propagate to every visible use of that tile, synchronized to presentation.
- Shared animation engine for tiles and sprites; per-animation speed, repeated frames, looping, reverse playback, stop, and completion events.

## Layers
- Simple numeric z-order, higher numbers in front, runtime changes supported. Sixteen friendly initial layer numbers 0–15 proposed, not a confirmed hard limit.
- Visual layering must not implicitly change collision filtering.

## Ownership and compatibility
- One Graphics/Presentation ownership contract: per-workspace virtual surfaces, Workspace Manager controls physical display composition and input focus.
- Events use Unified Message Manager; no competing IPC mechanism.
- PEEK/POKE targets virtual graphics address space via validated, protected access, not unrestricted physical memory.
- Kernel/Supervisor emergency display remains available under graphics overload.
- Open: finalized logical resolution/default, layer limit, exact BASIC grammar, register map, sprite counts, tile sizes, timing, viewport rules, resource limits, and error codes.
