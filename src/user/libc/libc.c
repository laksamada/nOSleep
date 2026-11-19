#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <nosleep.h>

#define HEAP_END       0x3F00000u
#define ATEXIT_MAX     16

int errno;

void *memset(void *s, int c, size_t n) {
    uint8_t *dest = s;
    while (n--)
        *dest++ = (uint8_t) c;
    return s;
}

void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    void *start = dest;
    __asm__ volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(n) : /* <Empty> */ : "memory");
    return start;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    if (d < s)
        return memcpy(dest, src, n);
    while (n--)
        d[n] = s[n];
    return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p = s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (uint8_t) c)
            return (void *) (p + i);
    return NULL;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0')
        len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *start = dest;
    while ((*dest++ = *src++) != '\0')
        ;
    return start;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

char *strcat(char *dest, const char *src) {
    strcpy(dest + strlen(dest), src);
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *end = dest + strlen(dest);
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++)
        end[i] = src[i];
    end[i] = '\0';
    return dest;
}

int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (uint8_t) *a - (uint8_t) *b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0')
            return (uint8_t) a[i] - (uint8_t) b[i];
    }
    return 0;
}

int strcasecmp(const char *a, const char *b) {
    while (*a != '\0' && tolower(*a) == tolower(*b)) {
        a++;
        b++;
    }
    return tolower((uint8_t) *a) - tolower((uint8_t) *b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int x = tolower((uint8_t) a[i]), y = tolower((uint8_t) b[i]);
        if (x != y || x == '\0')
            return x - y;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    for (; *s != '\0'; s++)
        if (*s == (char) c)
            return (char *) s;
    return c == '\0' ? (char *) s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *found = NULL;
    for (; *s != '\0'; s++)
        if (*s == (char) c)
            found = s;
    return c == '\0' ? (char *) s : (char *) found;
}

char *strstr(const char *haystack, const char *needle) {
    size_t len = strlen(needle);
    for (; *haystack != '\0'; haystack++)
        if (strncmp(haystack, needle, len) == 0)
            return (char *) haystack;
    return len == 0 ? (char *) haystack : NULL;
}

char *strdup(const char *s) {
    char *copy = malloc(strlen(s) + 1);
    if (copy != NULL)
        strcpy(copy, s);
    return copy;
}

char *strerror(int errnum) {
    (void) errnum;
    return "error";
}

int isspace(int c)  { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isdigit(int c)  { return c >= '0' && c <= '9'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
int islower(int c)  { return c >= 'a' && c <= 'z'; }
int isalpha(int c)  { return isupper(c) || islower(c); }
int isalnum(int c)  { return isalpha(c) || isdigit(c); }
int isprint(int c)  { return c >= 32 && c < 127; }
int iscntrl(int c)  { return (c >= 0 && c < 32) || c == 127; }
int ispunct(int c)  { return isprint(c) && !isalnum(c) && c != ' '; }
int toupper(int c)  { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c)  { return isupper(c) ? c - 'A' + 'a' : c; }

struct HeapBlock {
    size_t            size;
    struct HeapBlock *next;
    bool              free;
};

#define HEAP_HEADER_SIZE ((sizeof(struct HeapBlock) + 15) & ~15u)

extern char _linker_user_program_end[];

static struct HeapBlock *heap_first, *heap_last;
static uintptr_t heap_top;

static size_t align16(size_t n) {
    return (n + 15) & ~15u;
}

static void heap_merge_next(struct HeapBlock *block) {
    while (block->next != NULL && block->next->free) {
        if (heap_last == block->next)
            heap_last = block;
        block->size += HEAP_HEADER_SIZE + block->next->size;
        block->next = block->next->next;
    }
}

static void heap_split(struct HeapBlock *block, size_t size) {
    if (block->size < size + HEAP_HEADER_SIZE + 16)
        return;
    struct HeapBlock *rest = (struct HeapBlock *) ((char *) block + HEAP_HEADER_SIZE + size);
    rest->size = block->size - size - HEAP_HEADER_SIZE;
    rest->next = block->next;
    rest->free = true;
    block->next = rest;
    block->size = size;
    if (heap_last == block)
        heap_last = rest;
}

void *malloc(size_t size) {
    size = align16(size == 0 ? 1 : size);

    for (struct HeapBlock *block = heap_first; block != NULL; block = block->next) {
        if (!block->free)
            continue;
        heap_merge_next(block);
        if (block->size >= size) {
            heap_split(block, size);
            block->free = false;
            return (char *) block + HEAP_HEADER_SIZE;
        }
    }

    if (heap_top == 0)
        heap_top = align16((uintptr_t) _linker_user_program_end);
    if (heap_top + HEAP_HEADER_SIZE + size > HEAP_END) {
        errno = ENOMEM;
        return NULL;
    }

    struct HeapBlock *block = (struct HeapBlock *) heap_top;
    block->size = size;
    block->next = NULL;
    block->free = false;
    if (heap_last != NULL)
        heap_last->next = block;
    else
        heap_first = block;
    heap_last = block;
    heap_top += HEAP_HEADER_SIZE + size;
    return (char *) block + HEAP_HEADER_SIZE;
}

void free(void *ptr) {
    if (ptr != NULL)
        ((struct HeapBlock *) ((char *) ptr - HEAP_HEADER_SIZE))->free = true;
}

void *calloc(size_t count, size_t size) {
    void *ptr = malloc(count * size);
    if (ptr != NULL)
        memset(ptr, 0, count * size);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (ptr == NULL)
        return malloc(size);
    struct HeapBlock *block = (struct HeapBlock *) ((char *) ptr - HEAP_HEADER_SIZE);
    if (block->size >= size)
        return ptr;
    void *bigger = malloc(size);
    if (bigger != NULL) {
        memcpy(bigger, ptr, block->size);
        free(ptr);
    }
    return bigger;
}

static void (*atexit_functions[ATEXIT_MAX])(void);
static int atexit_count;

int atexit(void (*function)(void)) {
    if (atexit_count >= ATEXIT_MAX)
        return -1;
    atexit_functions[atexit_count++] = function;
    return 0;
}

void exit(int code) {
    while (atexit_count > 0)
        atexit_functions[--atexit_count]();
    sys_exit(code);
}

void abort(void) {
    sys_puts("abort\n", 6, 0xC);
    sys_exit(134);
}

int abs(int value) {
    return value < 0 ? -value : value;
}

long labs(long value) {
    return value < 0 ? -value : value;
}

double fabs(double x) {
    return x < 0 ? -x : x;
}

float fabsf(float x) {
    return x < 0 ? -x : x;
}

static int digit_value(char c) {
    if (isdigit(c))
        return c - '0';
    if (isalpha(c))
        return tolower(c) - 'a' + 10;
    return 99;
}

unsigned long strtoul(const char *s, char **end, int base) {
    while (isspace(*s))
        s++;
    bool negative = *s == '-';
    if (*s == '-' || *s == '+')
        s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = s[0] == '0' ? 8 : 10;
    }

    unsigned long value = 0;
    while (digit_value(*s) < base)
        value = value * base + digit_value(*s++);
    if (end != NULL)
        *end = (char *) s;
    return negative ? -value : value;
}

long strtol(const char *s, char **end, int base) {
    return (long) strtoul(s, end, base);
}

int atoi(const char *s) {
    return (int) strtol(s, NULL, 10);
}

double strtod(const char *s, char **end) {
    while (isspace(*s))
        s++;
    double sign = 1;
    if (*s == '-' || *s == '+')
        sign = *s++ == '-' ? -1 : 1;

    double value = 0;
    while (isdigit(*s))
        value = value * 10 + (*s++ - '0');
    if (*s == '.') {
        double scale = 0.1;
        for (s++; isdigit(*s); s++, scale /= 10)
            value += (*s - '0') * scale;
    }
    if (end != NULL)
        *end = (char *) s;
    return sign * value;
}

double atof(const char *s) {
    return strtod(s, NULL);
}

char *getenv(const char *name) {
    (void) name;
    return NULL;
}

int system(const char *command) {
    (void) command;
    return -1;
}

int unlink(const char *path) {
    (void) path;
    errno = ENOSYS;
    return -1;
}

int mkdir(const char *path, mode_t mode) {
    (void) path;
    (void) mode;
    errno = ENOSYS;
    return -1;
}
