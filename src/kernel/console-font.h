/* Purpose: provide original boot-console glyph rows. No hardware or allocation.
 * Invariant: 8x8 cells, five visible columns and seven rows, unsupported bytes '?'.
 * References: ADR-011 and docs/operations/console-2026-10-28.md. */
#ifndef SPIFFOS_CONSOLE_FONT_H
#define SPIFFOS_CONSOLE_FONT_H
#include <stdint.h>
/* Borrow no memory; return one row with bit 7 at the left. Rows >=8 return zero.
 * Unsupported bytes use '?'. Pure calculation, safe in any context, no errors. */
uint8_t console_font_row(unsigned char character, unsigned int row);
#endif
