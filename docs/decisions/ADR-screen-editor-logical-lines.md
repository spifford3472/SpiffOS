# ADR draft — Stored logical lines and screen-position editing

Status: DESIGN FOR OWNER REVIEW, 2026-10-29. Not implemented by this document.
The owner authorized design/implementation in small verified stages, requested a
documented design before source changes and review of material interaction choices.
Owner explicitly chose default overwrite, with the keyboard Insert key toggling
insert/overwrite. That choice is accepted; the remaining detailed rules below
are recommendations for review, not claims that the behavior already exists.
Read AGENTS.md, accepted/open, overview, ADR-011, UEFI/recovery, keyboard ADR,
Developer's Guide and the 2026-10-29 keyboard/2026-10-28 workspace-logo records.

## Problem and definitions
Current code retains only length, capped at min(63, columns-1). Pixels cannot
supply editable command text. At the observed WHATQQQ limit it ignores printable
keys. This is a current diagnostic restriction, not a BASIC or display-width limit.

A physical row is one horizontal row of character cells. A logical input line
is a stored sequence of characters; it may occupy several physical rows.
Soft wrap maps further characters onto the next physical row without inserting
a newline or calling the submission boundary. Explicit Enter submits one counted,
complete logical line through a callback. BASIC does not execute in this step.

At the verified 80-column display, characters 1..80 use columns 0..79. Character
81 belongs at column 0 of the next row. A cursor may need a trailing blank cell
at an exact-width end; Enter must reuse that continuation position for the next
input, rather than accidentally adding another blank row.

## Recommended interaction rules

| Action/context | Proposed rule and reason |
|---|---|
| Default typing | Overwrite the character under the cursor; append at line end. Owner-approved. |
| Insert key | Toggle insert/overwrite on make only; break does not toggle. Owner-approved. |
| Insert typing | Insert before the cursor; move the remainder of the logical line right and reflow its rows. |
| Left/right | Move one visible character cell; crossing an edge moves to the neighboring visible row. Clamp at top-left/bottom-right. |
| Up/down | Move one visible row, retaining column; clamp at screen edges. No hidden-history navigation in this step. |
| Enter inside a wrapped line | Submit its entire stored text once, including characters on both sides of the cursor and scrolled-off prefix; move to the row immediately after its final character. |
| Enter on older text/output | Submit the stored logical line under the cursor as a new candidate; old output does not execute just because the cursor enters it. |
| Enter on blank row | Submit an empty logical line once and advance; no BASIC action is implied. |
| Typing past text in blank cells | Pad intervening positions with spaces and start/extend that row's logical line; blank rows are independently addressable. |
| Backspace within a line | Delete the character before the cursor, shift the remainder left and reflow, including across soft-wrap boundaries. |
| Backspace beyond text end | Move left through blank cells; do not delete unrelated text. |
| Backspace at logical offset zero | No-op; never merge separate submitted/output lines implicitly. |
| Selecting another line | Retain previous edits in memory without submitting them. A screen editor is not limited to the bottom prompt. |
| Growth into another row | RECOMMENDED: insert a physical row and push following content downward; preserve its logical-line identity. See material choice below. |
| Shrinking by a row | Remove the now-unused continuation row, move following rows upward and expose a blank bottom row. |
| Reaching bottom while typing | Scroll the whole viewport upward, including logo pixels; keep cursor and row mappings synchronized. |
| Leaving view | Retain complete text while any fragment of that logical line remains visible; once its last fragment leaves, reclaim its record. No general scrollback in this stage. |
| Asynchronous output | No concurrent output during this polling diagnostic. Later output must use a documented editor suspension/output path; never mutate editor pixels behind its model. |

## Material reflow choice for owner review
Recommended: when inserting or extending an earlier command needs another row,
push following visible text down. This preserves the next line as its own stored
logical line. Content pushed off the bottom is no longer visible; if its entire
logical line leaves, it may be reclaimed under the bounded viewport policy.

Alternative: let the growing command overwrite the cells/lines immediately below
it. That resembles painting onto a fixed screen but can destroy another command
and requires explicit splitting/merging rules. Do not implement that destructive
interaction silently. Confirm the recommended push-down behavior before the
screen-wide reflow stage. Independent logical-buffer and wrapping stages need
not wait for this choice.

## Memory, ownership and small interfaces
Use bounded caller-owned state, with no allocator or terminal framework.
Proposed diagnostic budget: at most 160 columns x 64 visible rows, 4096 characters
per logical line and 65 logical-line records (one more than maximum visible rows).
This is roughly 270 KiB of text plus row metadata, not a finalized user API limit.
Use static boot-owned storage for this state, not Limine's small entry stack.
Reject an unsupported viewport explicitly; do not silently use a narrower width.
A later user-space console can have a different capacity without changing BASIC.

Each line record owns its bytes, length, identity and current row span. A physical
row mapping identifies the line record and byte offset displayed there. This
mapping preserves a scrolled-off prefix without relying on framebuffer readback.
Reclaim a record only when no visible row refers to it and it is not being used
by a synchronous submission. A selected long line keeps all 4096 possible bytes
even when its beginning scrolls out. Never submit only its visible suffix.

Separate a pure logical-buffer module (append, overwrite, insert, delete, bounded
padding, counted submission) from row mapping and drawing. Keep PS/2 transport,
scan-code decoder, logical editor, text console and framebuffer responsibilities
separate. Decoder should return typed events for arrows/Insert instead of magic
ASCII control values. E0 make/break sequences must be tested independently of
transport; preserve modifier/chord filtering and E1 handling.

Proposed interfaces:
- logical_line_edit(line, offset, operation, byte): return OK, AT_BOUNDARY or FULL;
  validate capacity before mutation, never truncate the old line.
- logical_line_submit(line, callback, context): borrow a counted immutable byte
  view for that synchronous callback only; no ownership transfer or hidden run.
- screen_editor_event(editor, event): update selected line, cursor and row maps,
  then draw only affected cells/rows; return explicit error/result.
- screen_editor_suspend/resume: future output integration, not implemented here.

Capacity exhaustion rejects the whole edit, leaves existing bytes intact, retains
navigation/Backspace/Enter, emits a distinct serial INPUT_FULL diagnostic and shows
a documented visible full indication at the cursor. Submission remains possible
only for the complete stored line. No partial command is dispatched as recovery.

## Rendering, cursor and branding
Use a steady underline in overwrite mode and a vertical bar in insert mode, so
mode and position are visible without a timer or fixed status/header row. Hide
it before drawing/scrolling and restore it afterward. Preserve the underlying
pixels exactly (e.g. reversible color inversion validated by framebuffer helpers),
including initial logo artwork under a moved cursor. A full-capacity indication
must be visually distinct and cleared after a successful deletion frees capacity.

A new downward framebuffer row-copy helper must copy bottom-to-top to handle
source/destination overlap. Existing upward copies remain top-to-bottom. Both
honor pitch, padding and guards. Redrawing a line must clear only cells displaced
or removed by that edit, never redraw a screen background layer containing the
logo. Whole-viewport scrolling moves the initial logo away permanently; only the
existing explicit reset or boot may draw it again. Preserve original SVG/bitmap.

## Implementation checkpoints and validation
1. Pure stored logical text and soft wrapping: full width, edge+1, >=3 rows,
   no callback before Enter and explicit capacity result. Keep earlier source
   tree protected; add sanitizer host tests for buffer mutations/guards.
2. Submission and cross-wrap Backspace: synchronous counted callback exactly
   once, complete byte order, no synthetic Enter, exact-width cursor case,
   scrolling long input without losing its prefix.
3. Visible cursor, extended arrows, Insert toggle and screen-wide editing:
   use owner-reviewed reflow policy, retain old output, blanks and row identities.
4. Real QEMU QMP key injection and serial SUBMIT evidence, independent full-frame
   expectations updated for cursor/wraps, representative capture inspection.
   Do not replace real guest checks with host tests or claim stage completion
   when only the model passes. Run all existing boot/framebuffer/workspace tests.

Required cases: exact width; edge+1; three rows; pre-Enter silence; complete
single submission; Backspace on both sides of wrap; edits earlier in text;
arrow/Insert E0 make+break and unsupported chords; screen/line boundaries;
wrapped editing through scroll; safe capacity exhaustion; framebuffer padding/
guards; logo disappearance with no reappearance; existing boot/display regressions.

## First walkthrough for the implementation review
At 80 columns, type 79 A characters followed by B and X. The first physical row
contains 79 As plus B; the next row contains X. No callback has run. Backspace
removes X and moves to the continuation start. Backspace again removes B and
moves to column 79 of the first row. Type C then D: C ends the first row, D starts
the second. Enter delivers 79 As followed by CD as one 81-byte command exactly
once. Neither crossing the edge nor either Backspace can synthesize Enter.
This is an acceptance example, not a claim that the current kernel supports it.

## Authorization, limits and rollback
Default overwrite and Insert toggle are owner-approved in this conversation.
The screen-wide reflow choice is pending review. This draft is separate from the
implemented safe-launcher checkpoint. No screen-editor source changes or new
screen-editor test results are claimed. BASIC, ring 3, interrupt-driven input,
USB input and final console ABI remain outside this diagnostic.
Build prior bbb2d4d or the launcher checkpoint in isolated worktrees for rollback;
preserve the original index, unrelated drafts, backups and history. No merge or
physical deployment without the separate project authorization step.
