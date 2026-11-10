/* Purpose: original 5x7 uppercase/digit/US-punctuation font in an 8x8 boot cell.
 * Dependencies: fixed integer types only. No hardware or allocations.
 * Invariants: immutable glyph data; unsupported bytes select '?'.
 * References: ADR-011 and docs/operations/console-2026-10-28.md. */
#include "console-font.h"
static const uint8_t glyphs[40][7] = {
    {14, 17, 17, 31, 17, 17, 17}, /* A */
    {30, 17, 17, 30, 17, 17, 30}, /* B */
    {14, 17, 16, 16, 16, 17, 14}, /* C */
    {30, 17, 17, 17, 17, 17, 30}, /* D */
    {31, 16, 16, 30, 16, 16, 31}, /* E */
    {31, 16, 16, 30, 16, 16, 16}, /* F */
    {14, 17, 16, 23, 17, 17, 15}, /* G */
    {17, 17, 17, 31, 17, 17, 17}, /* H */
    {14, 4, 4, 4, 4, 4, 14}, /* I */
    {7, 2, 2, 2, 18, 18, 12}, /* J */
    {17, 18, 20, 24, 20, 18, 17}, /* K */
    {16, 16, 16, 16, 16, 16, 31}, /* L */
    {17, 27, 21, 21, 17, 17, 17}, /* M */
    {17, 25, 21, 19, 17, 17, 17}, /* N */
    {14, 17, 17, 17, 17, 17, 14}, /* O */
    {30, 17, 17, 30, 16, 16, 16}, /* P */
    {14, 17, 17, 17, 21, 18, 13}, /* Q */
    {30, 17, 17, 30, 20, 18, 17}, /* R */
    {15, 16, 16, 14, 1, 1, 30}, /* S */
    {31, 4, 4, 4, 4, 4, 4}, /* T */
    {17, 17, 17, 17, 17, 17, 14}, /* U */
    {17, 17, 17, 17, 17, 10, 4}, /* V */
    {17, 17, 17, 17, 21, 21, 10}, /* W */
    {17, 17, 10, 4, 10, 17, 17}, /* X */
    {17, 17, 10, 4, 4, 4, 4}, /* Y */
    {31, 1, 2, 4, 8, 16, 31}, /* Z */
    {14, 17, 19, 21, 25, 17, 14}, /* 0 */
    {4, 12, 4, 4, 4, 4, 14}, /* 1 */
    {14, 17, 1, 2, 4, 8, 31}, /* 2 */
    {30, 1, 1, 14, 1, 1, 30}, /* 3 */
    {2, 6, 10, 18, 31, 2, 2}, /* 4 */
    {31, 16, 16, 30, 1, 1, 30}, /* 5 */
    {14, 16, 16, 30, 17, 17, 14}, /* 6 */
    {31, 1, 2, 4, 8, 8, 8}, /* 7 */
    {14, 17, 17, 14, 17, 17, 14}, /* 8 */
    {14, 17, 17, 15, 1, 1, 14}, /* 9 */
    {0, 0, 0, 0, 0, 0, 0}, /*   */
    {0, 0, 0, 0, 0, 0, 4}, /* . */
    {0, 4, 4, 0, 4, 4, 0}, /* : */
    {14, 17, 1, 2, 4, 0, 4}, /* ? */
};

/* US ASCII punctuation uses the same original five-bit row geometry. */
static const uint8_t punctuation[128][7] = {
    [33]={4,4,4,4,4,0,4},
    [34]={10,10,10,0,0,0,0},
    [35]={10,31,10,10,31,10,0},
    [36]={4,15,20,14,5,30,4},
    [37]={24,25,2,4,8,19,3},
    [38]={12,18,20,8,21,18,13},
    [39]={4,4,8,0,0,0,0},
    [40]={2,4,8,8,8,4,2},
    [41]={8,4,2,2,2,4,8},
    [42]={0,21,14,31,14,21,0},
    [43]={0,4,4,31,4,4,0},
    [44]={0,0,0,0,0,4,8},
    [45]={0,0,0,31,0,0,0},
    [46]={0,0,0,0,0,0,4},
    [47]={1,2,2,4,8,8,16},
    [58]={0,4,4,0,4,4,0},
    [59]={0,4,4,0,4,4,8},
    [60]={2,4,8,16,8,4,2},
    [61]={0,0,31,0,31,0,0},
    [62]={8,4,2,1,2,4,8},
    [63]={14,17,1,2,4,0,4},
    [64]={14,17,23,21,23,16,14},
    [91]={14,8,8,8,8,8,14},
    [92]={16,8,8,4,2,2,1},
    [93]={14,2,2,2,2,2,14},
    [94]={4,10,17,0,0,0,0},
    [95]={0,0,0,0,0,0,31},
    [96]={8,4,2,0,0,0,0},
    [123]={2,4,4,8,4,4,2},
    [124]={4,4,4,4,4,4,4},
    [125]={8,4,4,2,4,4,8},
    [126]={0,0,9,22,0,0,0},
};

/* Return a row with bit 7 at the left; row >=7 is blank cell spacing.
 * Unsupported characters map to '?' (entry 39). No memory ownership,
 * side effects, allocation or context restrictions. */
uint8_t console_font_row(unsigned char character, unsigned int row)
{
    if (row >= 7) {
        return 0;
    }
    if ((character >= 33 && character <= 47) ||
        (character >= 58 && character <= 64) ||
        (character >= 91 && character <= 96) ||
        (character >= 123 && character <= 126)) {
        return (uint8_t)(punctuation[character][row] << 2);
    }
    unsigned int index = 39;
    if (character >= 'A' && character <= 'Z') {
        index = character - 'A';
    } else if (character >= '0' && character <= '9') {
        index = 26 + character - '0';
    } else if (character == ' ') {
        index = 36;
    } else if (character == '.') {
        index = 37;
    } else if (character == ':') {
        index = 38;
    }
    /* Five-bit rows occupy the middle of the 8-pixel cell: shift by two leaves
     * one blank column on the left and two on the right. */
    return (uint8_t)(glyphs[index][row] << 2);
}
