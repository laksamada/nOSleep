#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nosleep.h>

#define CONSOLE_COLOR   0x07
#define ERROR_COLOR     0x0C
#define PRINT_BUFFER    1024

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

struct FILE {
    uint32_t color;
};

static FILE console_in  = {.color = CONSOLE_COLOR};
static FILE console_out = {.color = CONSOLE_COLOR};
static FILE console_err = {.color = ERROR_COLOR};

FILE *stdin  = &console_in;
FILE *stdout = &console_out;
FILE *stderr = &console_err;

size_t fwrite(const void *buf, size_t size, size_t count, FILE *file) {
    sys_puts(buf, size * count, file->color);
    return count;
}

int fflush(FILE *file) {
    (void) file;
    return 0;
}

int fputc(int c, FILE *file) {
    char ch = c;
    return fwrite(&ch, 1, 1, file) == 1 ? c : EOF;
}

int fputs(const char *s, FILE *file) {
    fwrite(s, 1, strlen(s), file);
    return 0;
}

int putc(int c, FILE *file) {
    return fputc(c, file);
}

int putchar(int c) {
    return fputc(c, stdout);
}

int puts(const char *s) {
    fputs(s, stdout);
    return putchar('\n');
}

struct Output {
    char  *buf;
    size_t size;
    size_t len;
};

static void emit(struct Output *out, char c) {
    if (out->len + 1 < out->size)
        out->buf[out->len] = c;
    out->len++;
}

static void emit_padding(struct Output *out, char c, int count) {
    while (count-- > 0)
        emit(out, c);
}

struct Spec {
    bool left;
    bool zero;
    bool plus;
    bool space;
    int  width;
    int  precision;
};

static void emit_field(struct Output *out, const struct Spec *spec, const char *prefix, const char *digits, int length) {
    int prefix_length = strlen(prefix);
    int zeros = spec->precision > length ? spec->precision - length : 0;
    int padding = spec->width - prefix_length - zeros - length;
    if (spec->zero && !spec->left && spec->precision < 0) {
        zeros += padding > 0 ? padding : 0;
        padding = 0;
    }
    if (!spec->left)
        emit_padding(out, ' ', padding);
    for (int i = 0; i < prefix_length; i++)
        emit(out, prefix[i]);
    emit_padding(out, '0', zeros);
    for (int i = 0; i < length; i++)
        emit(out, digits[i]);
    if (spec->left)
        emit_padding(out, ' ', padding);
}

static int unsigned_to_text(unsigned long long value, unsigned base, bool upper, char *text) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char reversed[24];
    int length = 0;
    do {
        reversed[length++] = digits[value % base];
        value /= base;
    } while (value != 0);
    for (int i = 0; i < length; i++)
        text[i] = reversed[length - 1 - i];
    return length;
}

int vsnprintf(char *buf, size_t size, const char *format, va_list args) {
    struct Output out = {.buf = buf, .size = size, .len = 0};

    for (const char *p = format; *p != '\0'; p++) {
        if (*p != '%') {
            emit(&out, *p);
            continue;
        }

        struct Spec spec = {.precision = -1};
        for (p++;; p++) {
            if (*p == '-')
                spec.left = true;
            else if (*p == '0')
                spec.zero = true;
            else if (*p == '+')
                spec.plus = true;
            else if (*p == ' ')
                spec.space = true;
            else if (*p != '#')
                break;
        }
        if (*p == '*') {
            spec.width = va_arg(args, int);
            p++;
        }
        while (is_digit(*p))
            spec.width = spec.width * 10 + (*p++ - '0');
        if (*p == '.') {
            spec.precision = 0;
            if (*++p == '*') {
                spec.precision = va_arg(args, int);
                p++;
            }
            while (is_digit(*p))
                spec.precision = spec.precision * 10 + (*p++ - '0');
        }

        int longs = 0;
        while (*p == 'l' || *p == 'h' || *p == 'z' || *p == 'j' || *p == 't')
            longs += *p++ == 'l';

        char text[24];
        switch (*p) {
            case 'd':
            case 'i': {
                long long value = longs >= 2 ? va_arg(args, long long) : va_arg(args, int);
                const char *prefix = value < 0 ? "-" : (spec.plus ? "+" : (spec.space ? " " : ""));
                unsigned long long magnitude = value < 0 ? -(unsigned long long) value : (unsigned long long) value;
                emit_field(&out, &spec, prefix, text, unsigned_to_text(magnitude, 10, false, text));
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'o': {
                unsigned long long value = longs >= 2 ? va_arg(args, unsigned long long) : va_arg(args, unsigned int);
                unsigned base = *p == 'u' ? 10 : (*p == 'o' ? 8 : 16);
                emit_field(&out, &spec, "", text, unsigned_to_text(value, base, *p == 'X', text));
                break;
            }
            case 'p': {
                uintptr_t value = (uintptr_t) va_arg(args, void *);
                emit_field(&out, &spec, "0x", text, unsigned_to_text(value, 16, false, text));
                break;
            }
            case 'c': {
                char c = (char) va_arg(args, int);
                spec.precision = -1;
                emit_field(&out, &spec, "", &c, 1);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                if (s == NULL)
                    s = "(null)";
                int length = strlen(s);
                if (spec.precision >= 0 && spec.precision < length)
                    length = spec.precision;
                spec.precision = -1;
                spec.zero = false;
                emit_field(&out, &spec, "", s, length);
                break;
            }
            case '%':
                emit(&out, '%');
                break;
            default:
                p--;
                break;
        }
    }

    if (size > 0)
        buf[out.len < size ? out.len : size - 1] = '\0';
    return out.len;
}

int vsprintf(char *buf, const char *format, va_list args) {
    return vsnprintf(buf, SIZE_MAX, format, args);
}

int snprintf(char *buf, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buf, size, format, args);
    va_end(args);
    return result;
}

int sprintf(char *buf, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buf, SIZE_MAX, format, args);
    va_end(args);
    return result;
}

int vfprintf(FILE *file, const char *format, va_list args) {
    char text[PRINT_BUFFER];
    int length = vsnprintf(text, sizeof(text), format, args);
    fwrite(text, 1, length < PRINT_BUFFER ? length : PRINT_BUFFER - 1, file);
    return length;
}

int vprintf(const char *format, va_list args) {
    return vfprintf(stdout, format, args);
}

int fprintf(FILE *file, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vfprintf(file, format, args);
    va_end(args);
    return result;
}

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vfprintf(stdout, format, args);
    va_end(args);
    return result;
}
