#ifndef _LIBC_STDIO_H
#define _LIBC_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF      (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ   512

typedef struct FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *file);
size_t fread(void *buf, size_t size, size_t count, FILE *file);
size_t fwrite(const void *buf, size_t size, size_t count, FILE *file);
int    fseek(FILE *file, long offset, int whence);
long   ftell(FILE *file);
int    feof(FILE *file);
int    fflush(FILE *file);
int    fgetc(FILE *file);
char  *fgets(char *buf, int size, FILE *file);
int    fputc(int c, FILE *file);
int    fputs(const char *s, FILE *file);
int    putc(int c, FILE *file);
int    putchar(int c);
int    puts(const char *s);
int    remove(const char *path);
int    rename(const char *old_path, const char *new_path);

int printf(const char *format, ...);
int fprintf(FILE *file, const char *format, ...);
int sprintf(char *buf, const char *format, ...);
int snprintf(char *buf, size_t size, const char *format, ...);
int vprintf(const char *format, va_list args);
int vfprintf(FILE *file, const char *format, va_list args);
int vsprintf(char *buf, const char *format, va_list args);
int vsnprintf(char *buf, size_t size, const char *format, va_list args);
int sscanf(const char *str, const char *format, ...);

#endif
