/* Purpose: evaluate one complete line of interactive SpiffOS BASIC.
 * Dependencies: standard integer and size types and caller-owned storage.
 * Invariants: bounded ASCII input, checked signed arithmetic, and no
 * device I/O or allocation. Commit an assignment only after full success.
 * References: docs/basic/interactive-preview.md and ADR-005. */
#include "interactive.h"
#include <stdbool.h>

enum basic_error {
    BASIC_OK,
    BASIC_SYNTAX,
    BASIC_INVALID_BYTE,
    BASIC_OVERFLOW,
    BASIC_ZERO_DIVISOR,
    BASIC_UNDEFINED,
    BASIC_TOO_DEEP,
    BASIC_LONG_NAME,
    BASIC_LONG_LINE,
    BASIC_TOO_MANY
};

struct parser {
    const char *line;
    size_t length;
    size_t position;
    const struct spiffos_basic_session *session;
    bool evaluate;
    enum basic_error error;
    size_t error_column;
    char missing[SPIFFOS_BASIC_NAME_MAX + 1];
};

/* Return true for an ASCII letter without consulting the host locale. */
static bool letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* Return true for an ASCII decimal digit. */
static bool digit(char c)
{
    return c >= '0' && c <= '9';
}

/* Recognize characters allowed after the first letter of a name. */
static bool name_char(char c)
{
    return letter(c) || digit(c) || c == '_';
}

/* Fold one ASCII letter to the canonical spelling; leave other bytes alone. */
static char uppercase(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - ('a' - 'A');
    return c;
}

/* Advance the parser's byte position over spaces and tabs, stopping at length. */
static void skip_space(struct parser *p)
{
    while (p->position < p->length && (p->line[p->position] == ' ' || p->line[p->position] == '\t'))
        p->position++;
}

/* Record the first diagnostic and convert the current byte offset to a column. */
static void record_error(struct parser *p, enum basic_error error)
{
    if (p->error == BASIC_OK) {
        p->error = error;
        p->error_column = p->position + 1;
    }
}

/* Read and uppercase one identifier into caller-owned name storage. On failure, record a syntax or length error. The input position advances only over identifier bytes. */
static bool read_name(struct parser *p, char name[static SPIFFOS_BASIC_NAME_MAX + 1])
{
    size_t start = p->position;
    if (start >= p->length || !letter(p->line[start])) {
        record_error(p, BASIC_SYNTAX);
        return false;
    }

    size_t i = 0;
    while (p->position < p->length && name_char(p->line[p->position]) && i < SPIFFOS_BASIC_NAME_MAX) {
        name[i++] = uppercase(p->line[p->position]);
        p->position++;
    }

    if (i == SPIFFOS_BASIC_NAME_MAX && p->position < p->length && name_char(p->line[p->position])) {
        record_error(p, BASIC_LONG_NAME);
        return false;
    }

    name[i] = '\0';
    return true;
}

/* Compare two NUL-terminated ASCII identifiers without regard to case. */
static bool same_name(const char *a, const char *b)
{
    while (*a && *b) {
        if (uppercase(*a) != uppercase(*b))
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

/* Find an existing variable in this session, or return SIZE_MAX; does not mutate session or take ownership. */
static size_t variable_index(const struct spiffos_basic_session *session, const char *name)
{
    for (size_t i = 0; i < session->variable_count; i++) {
        if (same_name(session->variables[i].name, name))
            return i;
    }
    return SIZE_MAX;
}

/* Return a signed operand's magnitude in an unsigned word, including
 * the full magnitude of INT64_MIN. Unsigned subtraction is modulo 2^64. */
static uint64_t integer_magnitude(int64_t value)
{
    return value < 0 ? UINT64_C(0) - (uint64_t)value : (uint64_t)value;
}

/* Calculate one signed 64-bit operation. Write out only on success.
 * Compare operands before using signed arithmetic, whose overflow is
 * undefined in C. Division in C11 already truncates toward zero. */
static enum basic_error binary_value(char operation, int64_t left,
                                     int64_t right, int64_t *out)
{
    switch (operation) {
    case '*': {
        bool negative = (left < 0) != (right < 0);
        uint64_t a = integer_magnitude(left);
        uint64_t b = integer_magnitude(right);
        uint64_t limit = (uint64_t)INT64_MAX + (uint64_t)negative;
        if (b != 0 && a > limit / b)
            return BASIC_OVERFLOW;

        uint64_t product = a * b;
        if (negative && product == (uint64_t)INT64_MAX + 1u)
            *out = INT64_MIN;
        else
            *out = negative ? -(int64_t)product : (int64_t)product;
        return BASIC_OK;
    }
    case '\\':
        if (right == 0)
            return BASIC_ZERO_DIVISOR;
        if (left == INT64_MIN && right == -1)
            return BASIC_OVERFLOW;
        *out = left / right;
        return BASIC_OK;
    case '+':
        if ((right > 0 && left > INT64_MAX - right) ||
            (right < 0 && left < INT64_MIN - right))
            return BASIC_OVERFLOW;
        *out = left + right;
        return BASIC_OK;
    case '-':
        if ((right < 0 && left > INT64_MAX + right) ||
            (right > 0 && left < INT64_MIN + right))
            return BASIC_OVERFLOW;
        *out = left - right;
        return BASIC_OK;
    default:
        return BASIC_SYNTAX;
    }
}

/* Parse a decimal token starting at the current position. In syntax-only
 * mode consume all digits and return zero. Otherwise check the magnitude
 * before each digit, then convert the signed result without overflow. */
static bool parse_decimal(struct parser *p, bool negative, int64_t *out)
{
    uint64_t magnitude = 0;
    const uint64_t limit = (uint64_t)INT64_MAX + (uint64_t)negative;
    while (p->position < p->length && digit(p->line[p->position])) {
        unsigned value = (unsigned)(p->line[p->position] - '0');
        if (p->evaluate) {
            if (magnitude > (limit - value) / 10u) {
                record_error(p, BASIC_OVERFLOW);
                return false;
            }
            magnitude = magnitude * 10u + value;
        }
        ++p->position;
    }
    if (negative && magnitude == (uint64_t)INT64_MAX + 1u)
        *out = INT64_MIN;
    else
        *out = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return true;
}

static bool parse_expression(struct parser *p, unsigned depth, int64_t *out);

/* Parse one number, variable, parenthesized expression, or unary sign.
 * A recursive call always consumes a nesting level. No session mutation.
 * An absent operand or unmatched close records a one-based column. */
static bool parse_primary(struct parser *p, unsigned depth, int64_t *out)
{
    skip_space(p);
    if (depth > SPIFFOS_BASIC_DEPTH_MAX) {
        record_error(p, BASIC_TOO_DEEP);
        return false;
    }
    if (p->position == p->length) {
        record_error(p, BASIC_SYNTAX);
        return false;
    }
    char token = p->line[p->position];
    if (token == '(') {
        ++p->position;
        if (!parse_expression(p, depth + 1u, out))
            return false;
        skip_space(p);
        if (p->position == p->length || p->line[p->position] != ')') {
            record_error(p, BASIC_SYNTAX);
            return false;
        }
        ++p->position;
        return true;
    }
    if (token == '+' || token == '-') {
        if (depth == SPIFFOS_BASIC_DEPTH_MAX) {
            record_error(p, BASIC_TOO_DEEP);
            return false;
        }
        ++p->position;
        skip_space(p);
        /* The negative decimal token can represent a magnitude one larger
         * than INT64_MAX. Parenthesized operands are ordinary values. */
        if (token == '-' && p->position < p->length &&
            digit(p->line[p->position]))
            return parse_decimal(p, true, out);
        if (!parse_primary(p, depth + 1u, out))
            return false;
        if (token == '-' && *out == INT64_MIN) {
            record_error(p, BASIC_OVERFLOW);
            return false;
        }
        if (token == '-')
            *out = -*out;
        return true;
    }
    if (digit(token))
        return parse_decimal(p, false, out);

    size_t start = p->position;
    char name[SPIFFOS_BASIC_NAME_MAX + 1];
    if (!read_name(p, name))
        return false;
    if (same_name(name, "PRINT")) {
        p->position = start;
        record_error(p, BASIC_SYNTAX);
        return false;
    }
    if (!p->evaluate) {
        *out = 0;
        return true;
    }
    size_t index = variable_index(p->session, name);
    if (index == SIZE_MAX) {
        for (size_t n = 0; ; ++n) {
            p->missing[n] = name[n];
            if (name[n] == '\0')
                break;
        }
        p->position = start;
        record_error(p, BASIC_UNDEFINED);
        return false;
    }
    *out = p->session->variables[index].value;
    return true;
}

/* Combine two parsed values for the evaluation pass. The syntax pass reads
 * the same input without arithmetic and its operand values remain zero. */
static bool combine(struct parser *p, char operation, int64_t right, int64_t *left)
{
    if (!p->evaluate)
        return true;
    enum basic_error error = binary_value(operation, *left, right, left);
    if (error != BASIC_OK)
        record_error(p, error);
    return error == BASIC_OK;
}

/* Parse a left-associative product or quotient at the current depth. */
static bool parse_term(struct parser *p, unsigned depth, int64_t *out)
{
    if (!parse_primary(p, depth, out))
        return false;
    while (p->position < p->length) {
        skip_space(p);
        if (p->position == p->length)
            break;
        char operation = p->line[p->position];
        if (operation != '*' && operation != '\\')
            break;
        ++p->position;
        int64_t right;
        if (!parse_primary(p, depth, &right) || !combine(p, operation, right, out))
            return false;
    }
    return true;
}

/* Parse a left-associative sum or difference. The caller handles trailing
 * syntax and owns result storage. Only parse_primary may increase depth. */
static bool parse_expression(struct parser *p, unsigned depth, int64_t *out)
{
    if (!parse_term(p, depth, out))
        return false;
    while (p->position < p->length) {
        skip_space(p);
        if (p->position == p->length)
            break;
        char operation = p->line[p->position];
        if (operation != '+' && operation != '-')
            break;
        ++p->position;
        int64_t right;
        if (!parse_term(p, depth, &right) || !combine(p, operation, right, out))
            return false;
    }
    return true;
}

/* Render a signed decimal value in caller-owned storage holding at least
 * 21 bytes. No I/O or allocation. The unsigned subtraction handles INT64_MIN
 * without attempting to negate it as a signed value. */
static void render_integer(char *out, int64_t value)
{
    uint64_t magnitude = integer_magnitude(value);
    if (value < 0)
        *out++ = '-';

    char reversed[20];
    size_t count = 0;
    do {
        reversed[count++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude != 0);
    while (count != 0)
        *out++ = reversed[--count];
    *out = '\0';
}

/* Render the parser's first diagnostic in the caller's 80-byte response.
 * The largest suffix is the 31-byte identifier; syntax columns are at most
 * 256. No I/O or state change. */
static void render_error(char *out, const struct parser *p)
{
    const char *message = "";
    switch (p->error) {
    case BASIC_SYNTAX: message = "ERROR: Syntax at column "; break;
    case BASIC_INVALID_BYTE: message = "ERROR: Invalid character"; break;
    case BASIC_OVERFLOW: message = "ERROR: Integer overflow"; break;
    case BASIC_ZERO_DIVISOR: message = "ERROR: Division by zero"; break;
    case BASIC_UNDEFINED: message = "ERROR: Undefined variable "; break;
    case BASIC_TOO_DEEP: message = "ERROR: Expression too deep"; break;
    case BASIC_LONG_NAME: message = "ERROR: Name too long"; break;
    case BASIC_LONG_LINE: message = "ERROR: Line too long"; break;
    case BASIC_TOO_MANY: message = "ERROR: Too many variables"; break;
    case BASIC_OK: message = ""; break;
    }
    while (*message != '\0')
        *out++ = *message++;
    if (p->error == BASIC_SYNTAX) {
        render_integer(out, (int64_t)p->error_column);
        return;
    }
    if (p->error == BASIC_UNDEFINED) {
        const char *name = p->missing;
        while (*name != '\0')
            *out++ = *name++;
    }
    *out = '\0';
}

/* Evaluate one complete input span with the caller's zero-initialized
 * session. Validate the syntax before attempting arithmetic or lookup.
 * Only successful assignments mutate state. See interactive.h for the
 * buffer lifetime, serialization and response contract. */
void spiffos_basic_execute(struct spiffos_basic_session *session, const char *line,
                          size_t length,
                          char response[static SPIFFOS_BASIC_RESPONSE_CAPACITY])
{
    struct parser parser = {.line = line, .length = length, .session = session};
    if (length > SPIFFOS_BASIC_LINE_MAX) {
        record_error(&parser, BASIC_LONG_LINE);
        goto failed;
    }
    for (size_t byte = 0; byte < length; ++byte) {
        char input = line[byte];
        if (input != '\t' && (input < ' ' || input > '~')) {
            parser.position = byte;
            record_error(&parser, BASIC_INVALID_BYTE);
            goto failed;
        }
    }

    response[0] = '\0';
    skip_space(&parser);
    if (parser.position == length)
        return;

    char name[SPIFFOS_BASIC_NAME_MAX + 1];
    if (!read_name(&parser, name))
        goto failed;
    bool printing = same_name(name, "PRINT");
    if (!printing) {
        skip_space(&parser);
        if (parser.position == length || line[parser.position] != '=') {
            record_error(&parser, BASIC_SYNTAX);
            goto failed;
        }
        ++parser.position;
    }

    size_t expression_start = parser.position;
    int64_t value;
    if (!parse_expression(&parser, 0, &value))
        goto failed;
    skip_space(&parser);
    if (parser.position != length) {
        record_error(&parser, BASIC_SYNTAX);
        goto failed;
    }

    parser.position = expression_start;
    parser.evaluate = true;
    if (!parse_expression(&parser, 0, &value))
        goto failed;
    if (printing) {
        render_integer(response, value);
        return;
    }

    size_t index = variable_index(session, name);
    if (index == SIZE_MAX) {
        if (session->variable_count == SPIFFOS_BASIC_VARIABLE_MAX) {
            record_error(&parser, BASIC_TOO_MANY);
            goto failed;
        }
        index = session->variable_count++;
        for (size_t byte = 0; ; ++byte) {
            session->variables[index].name[byte] = name[byte];
            if (name[byte] == '\0')
                break;
        }
    }
    session->variables[index].value = value;
    return;

failed:
    render_error(response, &parser);
}
