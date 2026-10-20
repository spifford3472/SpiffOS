# Accepted Decisions Register — v0.1
Date: 2026-10-20. Status: documentation draft for owner review; entries below reflect the owner's supplied accepted decisions, not approval of new implementation details.

| ID | Area | Accepted decision |
|---|---|---|
| A-001 | Mission | New native x86-64 OS; not Linux, emulator or compatibility layer |
| A-002 | UX | Rapid boot directly into interactive BASIC; no conventional desktop |
| A-003 | Boot | UEFI-based startup; do not depend indefinitely on UEFI boot services |
| A-004 | Testing | QEMU on Forge first; dedicated laptop for physical validation |
| A-005 | Protection | Kernel ring 0, BASIC user code ring 3; isolate kernel and program state |
| A-006 | Scheduling | Timer-driven preemptive multitasking with Background, Normal, Interactive, Time-sensitive priorities |
| A-007 | IPC | Named channels are primary IPC; subscribers receive only post-subscription messages; fan-out |
| A-008 | Events | Polling and event-driven handlers share messaging; callbacks run at safe interpreter points without unbounded recursion |
| A-009 | Workspaces | Nine independent BASIC virtual workspaces; tasks continue in background |
| A-010 | Keyboard | Ctrl+Alt+1–9 workspace; Ctrl+Alt+0 overview; Ctrl+Alt+Delete privileged supervisor |
| A-011 | Display | Foreground workspace alone controls physical display; background virtual buffers retained |
| A-012 | Audio | Foreground workspace audible; background playback continues muted |
| A-013 | Supervisor | Reserved-resource task control, suspend/resume/kill/shutdown/restart; not dependent on responsive BASIC |
| A-014 | BASIC | Modern structured BASIC without required line numbers, with SUB/FUNCTION/local variables |
| A-015 | Compatibility | No full C64 machine code, VIC-II or SID compatibility |
| A-016 | Editor | Screen-position command entry; EDIT upper source/lower immediate console, Tab focus, EXIT returns |
| A-017 | Editor UX | Active pane distinct by color and explicit cue; customizable themes; Turbo Pascal visual inspiration only |
| A-018 | Graphics | Hardware-independent logical resolution and documented safe low-level graphics interface |
| A-019 | Audio APIs | Simultaneous music and sound effects from BASIC |
| A-020 | Hardware | Modular support for video, sound, input, storage and network devices |
| A-021 | Storage | Directories and power-loss-resistant boot/storage design; no unsaved-data survival guarantee |
| A-022 | Network | Wired TCP/IP first; BASIC client/server network application capability; Wi-Fi later |
| A-023 | Governance | Documentation and approval before code; AI implementation; PR review; no unauthorized merge/deploy |

## Clarifications requiring approval
The four priorities are conceptual, not an approved algorithm. Workspace count does not establish a task-count limit. Virtual PEEK/POKE addressing is a recommendation, not a finalized mapping. C as implementation language remains PROPOSED. Bounded queue/drop-oldest policy remains PROPOSED. Turbo Pascal menu organization is OPEN. The preliminary development sequence is not a dependency-resolved plan.

## Change control
Add an ADR with motivation, options, consequences, validation and owner approval before replacing any accepted decision. Record source and approval date for subsequent entries.
