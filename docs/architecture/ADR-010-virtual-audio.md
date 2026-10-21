# ADR-010: SpiffOS Virtual Sound and Music Architecture
Status: Proposed documentation of user-approved design, 2026-10-20. Detailed APIs remain open.

## Decision
- One virtual sound chip (VSC) per workspace, with **32 independent voices per workspace** in a shared local pool for music, effects, instruments and samples. Do not reserve fixed subsets for music or effects.
- One system-wide Audio Manager is authoritative for mixing and physical output; no separate per-workspace physical device ownership.
- Switching workspace focus does not automatically stop music or sound. Per-workspace volume and mute, plus system master volume. Programmers may opt into muting on loss of focus.
- Playback and sequencing run independently of BASIC execution so blocking I/O or calculations do not interrupt music.
- BASIC-first note, music, effect and instrument commands; built-in music composer and sound-effect editor; programmable waveform, envelope, pitch, volume, filter, and sample playback; documented protected virtual sound registers via PEEK/POKE.
- Voice allocation exhaustion within a workspace reports a clear error; no silent stealing or interruption of playing voices. Priority may influence allocation of *available* voices but must not preempt currently playing voices by default.
- Global CPU/memory/audio-mix limits require explicit admission/error behavior; a 32-voice virtual allowance per workspace is not a guarantee of unlimited simultaneous hardware mixing across arbitrarily many workspaces.
- System-wide versus workspace-local playback routing may be exposed as a policy, without introducing a second audio engine.

## Illustrative BASIC only — syntax not yet final
```basic
MUSIC.PLAY "BACKGROUND"
SOUND.PLAY "LASER"
SOUND.NOTE 1, 440, 500
MUSIC.STOP
```

## Architecture safeguards
- Unified Message Manager carries playback completion and error events; Audio Manager is sole mixer.
- Workspace lifecycle and permissions govern who may control each workspace's sounds.
- Protected Supervisor retains emergency scheduling and user control under runaway audio generation.
- Sound does not bypass process isolation, and virtual register writes are validated.
- Open: sample rate, output formats, scheduling accuracy, voice accounting for stereo/samples, cross-workspace fairness and admission, device loss/recovery, exact command grammar, register layout, persistence formats, and global resource ceiling.
