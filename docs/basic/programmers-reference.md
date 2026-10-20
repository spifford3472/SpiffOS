# SpiffOS BASIC Programmer's Reference — Outline v0.1
Status: OUTLINE ONLY; commands shown are candidate names, not approved syntax. Date: 2026-10-20.

## Reference template for every command
Name; category; status (ACCEPTED CONCEPT / PROPOSED SYNTAX / OPEN); purpose; syntax/grammar; parameters and types; return value; state and side effects; blocking and concurrency behavior; errors and recovery; permissions; immediate vs program use; version; executable examples; conformance tests.

## Part I — Language fundamentals
Interactive BASIC screen; source files and EDIT; identifiers and literals; types and coercion; expressions/operators; local/global scope; arrays and collections; SUB, FUNCTION, parameters and returns; IF/ELSE, SELECT, FOR, WHILE, DO; exceptions/errors; modules/libraries; comments; execution and debugging. Unnumbered structured BASIC is the accepted direction; detailed grammar is OPEN.

## Part II — Core BASIC command catalog (candidate families)
PRINT, INPUT; LET/assignment; IF, THEN, ELSE; FOR, NEXT; GOTO/GOSUB (legacy status OPEN); DIM; DATA, READ, RESTORE (legacy status OPEN); SUB, END SUB, FUNCTION, END FUNCTION, RETURN; LOAD, SAVE, RUN, STOP, LIST, NEW, EDIT, EXIT; string/numeric functions; time/date; error handling.

## Part III — System, tasks and workspaces
TASK START/STOP/STATUS (syntax OPEN); workspace select/query; task priority/state; safe memory introspection; supervisor-only commands distinguished from user calls.

## Part IV — Named messages and events
SUBSCRIBE channel; SEND channel, payload; RECEIVE channel, variable; ON MESSAGE channel CALL handler. All syntax illustrative only. Define subscription timing, queue overflow, event handler safety and error conditions.

## Part V — Graphics and text
Screen mode, color, text cursor, pixel, line, shape, sprite, animation and frame sync; logical coordinates; virtual graphics register PEEK/POKE contract (OPEN).

## Part VI — Sound and music
Music start/stop, notes, instruments, sound effects, channels/voices, playback position, mixing and mute rules; foreground workspace ownership.

## Part VII — Files and directories
Open/close/read/write/seek, list, create/remove directories, rename, atomic save, storage status and error reporting; filesystem syntax OPEN.

## Part VIII — TCP/IP networking
Network status/configuration; DNS resolve; TCP connect/listen/accept/send/receive/close; UDP send/receive; timeouts, nonblocking operations and message-driven notifications; illustrative HTTP client and server. Exact commands OPEN.

## Part IX — Devices and input
Keyboard, mouse, USB controller, timers, device capabilities, safe access and event channels.

## Part X — Learning and migration appendices
A. Original Commodore BASIC commands retained or modernized (to classify individually).
B. Original Commodore BASIC commands intentionally omitted (to classify individually; hardware-specific PEEK/POKE behavior cannot be copied directly).
C. New SpiffOS BASIC commands (to enumerate once syntax is approved).
D. Full runnable tutorials: hello world, editor, sprites, music plus effects, IPC, files, TCP client/server.
E. Error codes and reference index.

## Illustrative event example — NOT finalized syntax
```basic
SUBSCRIBE "SYS.KEYBOARD"
ON MESSAGE "SYS.KEYBOARD" CALL HandleKey
SUB HandleKey(key)
    PRINT key
END SUB
```

## Acceptance gate
Before implementation, approve grammar, command signatures, scope, errors, safe-point behavior and minimum conformance examples. Every final command requires a complete reference entry, not merely this outline.
