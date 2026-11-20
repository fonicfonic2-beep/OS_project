/*
 * apps/calculator.c — integer expression calculator.
 *
 * Recursive-descent parser:
 *   expr   = term (('+' | '-') term)*
 *   term   = factor (('*' | '/') factor)*
 *   factor = number | '(' expr ')' | '-' factor
 * Arithmetic is done in long. Division by zero and syntax errors are
 * reported. Type 'q' or an empty line to quit.
 */
#include "apps.h"
#include "types.h"
#include "console.h"
#include "i18n.h"
#include "keyboard.h"
#include "stdio.h"
#include "stdlib.h"
#include "ctype.h"

#define CALC_LINE_MAX 256

static const char* g_pos;
static int         g_error; /* 0=ok, 1=syntax, 2=divide by zero */

static void skip_spaces(void)
{
    while (*g_pos == ' ' || *g_pos == '\t') {
        g_pos++;
    }
}

static long parse_expr(void);

static long parse_factor(void)
{
    long value;

    skip_spaces();

    if (*g_pos == '-') {
        g_pos++;
        return -parse_factor();
    }
    if (*g_pos == '+') {
        g_pos++;
        return parse_factor();
    }
    if (*g_pos == '(') {
        g_pos++;
        value = parse_expr();
        skip_spaces();
        if (*g_pos == ')') {
            g_pos++;
        } else {
            g_error = 1; /* missing closing parenthesis */
        }
        return value;
    }
    if (isdigit((unsigned char)*g_pos)) {
        char* end = NULL;
        value = strtol(g_pos, &end, 10);
        if (end == g_pos) {
            g_error = 1;
            return 0;
        }
        g_pos = end;
        return value;
    }
    g_error = 1; /* unexpected character */
    return 0;
}

static long parse_term(void)
{
    long left = parse_factor();
    skip_spaces();

    while (*g_pos == '*' || *g_pos == '/') {
        char op = *g_pos;
        long right;
        g_pos++;
        right = parse_factor();
        if (op == '*') {
            left = left * right;
        } else {
            if (right == 0) {
                g_error = 2;
                return 0;
            }
            left = left / right;
        }
        skip_spaces();
    }
    return left;
}

static long parse_expr(void)
{
    long left = parse_term();
    skip_spaces();

    while (*g_pos == '+' || *g_pos == '-') {
        char op = *g_pos;
        long right;
        g_pos++;
        right = parse_term();
        if (op == '+') {
            left = left + right;
        } else {
            left = left - right;
        }
        skip_spaces();
    }
    return left;
}

static int calc_read_line(char* buf, size_t max)
{
    size_t pos = 0;
    int c;

    for (;;) {
        c = console_getchar();
        if (c == 0) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            buf[pos] = '\0';
            console_putc('\n');
            return (int)pos;
        }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                console_putc('\b');
            }
            continue;
        }
        if (pos < max - 1) {
            buf[pos++] = (char)c;
            console_putc((char)c);
        }
    }
}

void app_calculator(void)
{
    char line[CALC_LINE_MAX];

    console_printf("%s\n", tr("calc_quit_hint"));

    for (;;) {
        long result;

        console_printf("%s", tr("calc_prompt"));
        calc_read_line(line, sizeof(line));

        if (line[0] == '\0' || line[0] == 'q' || line[0] == 'Q') {
            break;
        }

        g_pos = line;
        g_error = 0;
        result = parse_expr();
        skip_spaces();

        if (g_error == 2) {
            console_printf("%s\n", tr("calc_div0"));
        } else if (g_error != 0 || *g_pos != '\0') {
            console_printf("%s\n", tr("calc_syntax"));
        } else {
            console_printf("= %ld\n", result);
        }
    }
}
