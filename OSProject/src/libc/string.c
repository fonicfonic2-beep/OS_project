/*
 * Minimal freestanding string / memory routines.
 * strdup / strndup allocate from the kernel heap (kmalloc).
 */
#include "string.h"
#include "heap.h"

/* --- memory ------------------------------------------------------------ */
void* memcpy(void* dst, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void* memset(void* dst, int c, size_t n)
{
    uint8_t* d = (uint8_t*)dst;
    uint8_t v = (uint8_t)c;
    for (size_t i = 0; i < n; i++) d[i] = v;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n)
{
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

void* memchr(const void* s, int c, size_t n)
{
    const uint8_t* p = (const uint8_t*)s;
    uint8_t v = (uint8_t)c;
    for (size_t i = 0; i < n; i++)
        if (p[i] == v) return (void*)(p + i);
    return NULL;
}

/* --- string length ----------------------------------------------------- */
size_t strlen(const char* s)
{
    size_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

size_t strnlen(const char* s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n] != '\0') n++;
    return n;
}

/* --- copy / concat ----------------------------------------------------- */
char* strcpy(char* dst, const char* src)
{
    char* d = dst;
    while ((*d++ = *src++) != '\0') {}
    return dst;
}

char* strncpy(char* dst, const char* src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char* strcat(char* dst, const char* src)
{
    char* d = dst + strlen(dst);
    while ((*d++ = *src++) != '\0') {}
    return dst;
}

char* strncat(char* dst, const char* src, size_t n)
{
    char* d = dst + strlen(dst);
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) d[i] = src[i];
    d[i] = '\0';
    return dst;
}

/* --- compare ----------------------------------------------------------- */
int strcmp(const char* a, const char* b)
{
    while (*a != '\0' && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n)
{
    if (n == 0) return 0;
    while (n > 1 && *a != '\0' && *a == *b) { a++; b++; n--; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* --- search ------------------------------------------------------------ */
char* strchr(const char* s, int c)
{
    char v = (char)c;
    for (;; s++) {
        if (*s == v) return (char*)s;
        if (*s == '\0') return NULL;
    }
}

char* strrchr(const char* s, int c)
{
    char v = (char)c;
    const char* last = NULL;
    for (;; s++) {
        if (*s == v) last = s;
        if (*s == '\0') return (char*)last;
    }
}

char* strstr(const char* haystack, const char* needle)
{
    if (*needle == '\0') return (char*)haystack;
    size_t nlen = strlen(needle);
    for (; *haystack != '\0'; haystack++) {
        if (strncmp(haystack, needle, nlen) == 0)
            return (char*)haystack;
    }
    return NULL;
}

size_t strspn(const char* s, const char* accept)
{
    size_t n = 0;
    for (; s[n] != '\0'; n++)
        if (strchr(accept, s[n]) == NULL) break;
    return n;
}

size_t strcspn(const char* s, const char* reject)
{
    size_t n = 0;
    for (; s[n] != '\0'; n++)
        if (strchr(reject, s[n]) != NULL) break;
    return n;
}

/* --- strtok (reentrant via static save pointer) ------------------------ */
static char* strtok_save = NULL;

char* strtok(char* str, const char* delim)
{
    if (str == NULL) {
        str = strtok_save;
        if (str == NULL) return NULL;
    }
    /* skip leading delimiters */
    str += strspn(str, delim);
    if (*str == '\0') { strtok_save = NULL; return NULL; }
    char* token = str;
    str += strcspn(str, delim);
    if (*str != '\0') { *str = '\0'; strtok_save = str + 1; }
    else              { strtok_save = NULL; }
    return token;
}

/* --- duplication (kernel heap) ----------------------------------------- */
char* strdup(const char* s)
{
    size_t n = strlen(s) + 1;
    char* p = (char*)kmalloc(n);
    if (p == NULL) return NULL;
    memcpy(p, s, n);
    return p;
}

char* strndup(const char* s, size_t n)
{
    size_t len = strnlen(s, n);
    char* p = (char*)kmalloc(len + 1);
    if (p == NULL) return NULL;
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}
