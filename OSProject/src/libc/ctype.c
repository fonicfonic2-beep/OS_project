/*
 * Minimal freestanding ctype — ASCII only.
 * All functions accept an int that is either EOF (-1) or representable
 * as unsigned char; anything else returns false / is passed through.
 */
#include "ctype.h"

int isalpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}

int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }

int isprint(int c) { return c >= 0x20 && c <= 0x7E; }

int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int iscntrl(int c)
{
    return (c >= 0 && c < 0x20) || c == 0x7F;
}

int toupper(int c)
{
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

int tolower(int c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}
