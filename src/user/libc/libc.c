#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <nosleep.h>

#define HEAP_END 0x3F00000u

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
    if (size > HEAP_END)
        return NULL;
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
    if (heap_top >= HEAP_END || size > HEAP_END - heap_top ||
        HEAP_HEADER_SIZE > HEAP_END - heap_top - size)
        return NULL;

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
    if (count != 0 && size > SIZE_MAX / count)
        return NULL;
    size_t total = count * size;
    void *ptr = malloc(total);
    if (ptr != NULL)
        memset(ptr, 0, total);
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

void exit(int code) {
    sys_exit(code);
}

void abort(void) {
    sys_puts("abort\n", 6, 0xC);
    sys_exit(134);
}
