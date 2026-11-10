# Keyboard punctuation, Delete and keypad checkpoint

Owner reported missing special characters, Delete and number pad during M6
acceptance. Implement US ASCII punctuation across decoder, editor acceptance
and original 5x7 glyphs. Preserve existing uppercase alphabet behavior and all
existing letter/digit glyphs. Unicode, alternate layouts and lowercase remain
separate decisions. Delete removes at the cursor, preserves its logical offset,
shrinks wrapped rows and leaves following records intact; end/blank is a no-op.

Numeric keypad starts in numeric mode. NumLock make toggles once until release;
Shift temporarily reverses numeric/navigation mode. Digits/decimal in numeric
mode, arrows/Insert/Delete in navigation mode; keypad arithmetic and Enter work
independently. Keypad Home/End/PageUp/PageDown require their named navigation
behavior and will be evaluated separately if absent. Ctrl/Alt suppression remains.
No PS/2 LED command is added; the software NumLock state is authoritative here.

Use host scan-code, modifier, delete-boundary/reflow and font/guard checks plus
real QMP injection and full independent pixel checks. Preserve original editor,
memory, launcher and clean-build checks. No physical input/USB or user ABI claim.
Prepare a tested PR; the prior M6 merge permission does not authorize this new
keyboard scope automatically. Preserve owner files and running emulator.
