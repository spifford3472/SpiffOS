# SpiffOS BASIC Graphics and Sound — Draft Reference
Status: Illustrative commands only; not frozen language syntax.

## Graphics examples (illustrative)
```basic
SPRITE.LOAD hero, "HERO"
SPRITE.MOVE hero, 100, 120
SPRITE.LAYER hero, 4
SPRITE.COLLISION hero, TRUE
TILE.SET 12, "STONE"
```
Sprite default 32x32; full per-pixel RGBA and transparency. Tile definition changes update all uses. Sprite collisions enabled by default with pair/group exclusions and optional events. Collision filtering is independent of visual layer ordering.

## Audio examples (illustrative)
```basic
MUSIC.PLAY "THEME"
SOUND.PLAY "LASER"
SOUND.NOTE 1, 440, 500
MUSIC.STOP
```
Each workspace has its own shared pool of 32 virtual voices, including background music and sound effects. Switching workspaces does not interrupt playback. A single system Audio Manager mixes output. Workspace mute/volume and master volume are distinct. No automatic preemption on voice exhaustion.

## Memory and protection
Graphics and audio virtual registers may be accessed by PEEK/POKE through protected address mappings; exact maps remain to be designed. BASIC and compiled SPX programs use the same OS APIs, permissions, and error semantics.

## Decisions pending
Final command names and signatures, detailed data structures, error codes, display default, viewport behavior, sprite/tile limits, register map, event payloads, audio mixing budget and scheduling guarantees.
