/*
 * Minimal freestanding stdlib — conversions, LCG random, abs helpers.
 */
#include "stdlib.h"
#include "ctype.h"

/* --- atoi / atol ------------------------------------------------------- */
int atoi(const char* s)
{
    return (int)atol(s);
}

long atol(const char* s)
{
    long val = 0;
    int neg = 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '+' || *s == '-') { if (*s == '-') neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

/* --- itoa / utoa ------------------------------------------------------- */
static void reverse(char* s, int n)
{
    for (int i = 0, j = n - 1; i < j; i++, j--) {
        char t = s[i]; s[i] = s[j]; s[j] = t;
    }
}

char* utoa(uint32_t value, char* str, int base)
{
    static const char digits[] = "0123456789abcdef";
    int n = 0;
    if (base < 2 || base > 16) { str[0] = '\0'; return str; }
    if (value == 0) { str[n++] = '0'; str[n] = '\0'; return str; }
    while (value > 0) {
        str[n++] = digits[value % (uint32_t)base];
        value /= (uint32_t)base;
    }
    reverse(str, n);
    str[n] = '\0';
    return str;
}

char* itoa(int value, char* str, int base)
{
    if (base == 10 && value < 0) {
        str[0] = '-';
        utoa((uint32_t)(-(long)value), str + 1, base);
        return str;
    }
    return utoa((uint32_t)value, str, base);
}

/* --- strtol / strtoul -------------------------------------------------- */
static int digit_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

unsigned long strtoul(const char* s, char** endptr, int base)
{
    unsigned long val = 0;
    const char* start = s;
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '+' || *s == '-') { if (*s == '-') neg = 1; s++; }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    } else if (base == 0 && s[0] == '0') {
        s++; base = 8;
    } else if (base == 0) {
        base = 10;
    }
    int d;
    while ((d = digit_val(*s)) >= 0 && d < base) {
        val = val * (unsigned long)base + (unsigned long)d;
        s++;
    }
    if (endptr != NULL) *endptr = (char*)(s == start ? start : s);
    return neg ? (unsigned long)(-(long)val) : val;
}

long strtol(const char* s, char** endptr, int base)
{
    return (long)strtoul(s, endptr, base);
}

int atoi_base(const char* s, int base)
{
    return (int)strtol(s, NULL, base);
}

/* --- pseudo-random (LCG) ----------------------------------------------- */
static unsigned long long rand_state = 1;

int rand(void)
{
    rand_state = rand_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((rand_state >> 33) & (unsigned long long)RAND_MAX);
}

void srand(unsigned int seed)
{
    rand_state = seed ? (unsigned long long)seed : 1ULL;
}

/* --- abs helpers ------------------------------------------------------- */
int abs(int n)          { return n < 0 ? -n : n; }
long labs(long n)       { return n < 0 ? -n : n; }
