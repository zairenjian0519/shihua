#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

static LogLevel g_log_level = LOG_LEVEL_INFO;
static int g_file_enabled = 0;
static char g_file_name[256] = {0};
static char g_base_path[256] = {0};
static int g_file_count = 2;
static int g_max_file_size = 10485760;
static int g_current_index = 0;
static FILE* g_file = NULL;

static const char* level_name(LogLevel level)
{
    switch (level) {
    case LOG_LEVEL_TRACE: return "trace";
    case LOG_LEVEL_DEBUG: return "debug";
    case LOG_LEVEL_INFO: return "info";
    case LOG_LEVEL_WARN: return "warn";
    case LOG_LEVEL_ERROR: return "error";
    default: return "unknown";
    }
}

LogLevel log_level_from_string(const char* text, LogLevel fallback)
{
    if (!text)
        return fallback;

    if (strcmp(text, "trace") == 0)
        return LOG_LEVEL_TRACE;
    if (strcmp(text, "debug") == 0)
        return LOG_LEVEL_DEBUG;
    if (strcmp(text, "info") == 0)
        return LOG_LEVEL_INFO;
    if (strcmp(text, "warn") == 0)
        return LOG_LEVEL_WARN;
    if (strcmp(text, "error") == 0)
        return LOG_LEVEL_ERROR;

    return fallback;
}

static void make_parent_dirs(const char* file_name)
{
    char path[256];
    size_t len;

    snprintf(path, sizeof(path), "%s", file_name);
    len = strlen(path);

    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            char saved = path[i];
            path[i] = '\0';
            if (strlen(path) > 0)
                MKDIR(path);
            path[i] = saved;
        }
    }
}

static void split_file_name(const char* file_name, char* stem, size_t stem_size,
                            char* ext, size_t ext_size)
{
    const char* dot = strrchr(file_name, '.');
    const char* slash1 = strrchr(file_name, '/');
    const char* slash2 = strrchr(file_name, '\\');
    const char* slash = slash1 > slash2 ? slash1 : slash2;

    if (dot && (!slash || dot > slash)) {
        size_t stem_len = (size_t)(dot - file_name);
        if (stem_len >= stem_size)
            stem_len = stem_size - 1;
        memcpy(stem, file_name, stem_len);
        stem[stem_len] = '\0';
        snprintf(ext, ext_size, "%s", dot);
    }
    else {
        snprintf(stem, stem_size, "%s", file_name);
        snprintf(ext, ext_size, "%s", ".log");
    }
}

static void build_file_name(int index, char* out, size_t out_size)
{
    char stem[256];
    char ext[64];

    split_file_name(g_base_path, stem, sizeof(stem), ext, sizeof(ext));
    snprintf(out, out_size, "%s_%d%s", stem, index, ext);
}

static int current_file_too_large(void)
{
    long size;

    if (!g_file)
        return 0;

    if (fflush(g_file) != 0)
        return 0;

    size = ftell(g_file);
    return size >= g_max_file_size;
}

static void open_log_file(void)
{
    if (!g_file_enabled)
        return;

    build_file_name(g_current_index, g_file_name, sizeof(g_file_name));
    make_parent_dirs(g_file_name);

    g_file = fopen(g_file_name, "a");
    if (!g_file) {
        fprintf(stderr, "failed to open log file %s\n", g_file_name);
        g_file_enabled = 0;
    }
}

static void rotate_if_needed(void)
{
    if (!g_file_enabled || !g_file || !current_file_too_large())
        return;

    fclose(g_file);
    g_file = NULL;

    g_current_index = (g_current_index + 1) % g_file_count;
    build_file_name(g_current_index, g_file_name, sizeof(g_file_name));
    g_file = fopen(g_file_name, "w");
    if (!g_file) {
        fprintf(stderr, "failed to rotate log file %s\n", g_file_name);
        g_file_enabled = 0;
    }
}

void log_init(LogLevel level)
{
    g_log_level = level;
}

void log_init_config(const LogConfig* config)
{
    char timestamp[32] = "";
    char stem[256];
    char ext[64];
    time_t now;
    struct tm* tm_now;

    log_close();

    if (!config) {
        log_init(LOG_LEVEL_INFO);
        return;
    }

    g_log_level = config->level;
    g_file_enabled = config->enabled;
    g_file_count = config->file_count > 0 ? config->file_count : 2;
    g_max_file_size = config->max_file_size_bytes > 0 ? config->max_file_size_bytes : 10485760;

    if (!g_file_enabled)
        return;

    if (config->append_start_timestamp) {
        now = time(NULL);
        tm_now = localtime(&now);
        if (tm_now)
            strftime(timestamp, sizeof(timestamp), "_%Y%m%d_%H%M%S", tm_now);
    }

    split_file_name(config->file_name, stem, sizeof(stem), ext, sizeof(ext));
    snprintf(g_base_path, sizeof(g_base_path), "%s%s%s", stem, timestamp, ext);
    open_log_file();
}

void log_close(void)
{
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
}

void log_vwrite(LogLevel level, const char* module, const char* format, va_list args)
{
    time_t now;
    struct tm* tm_now;
    char time_buf[32];
    va_list stdout_args;
    va_list file_args;

    if (level < g_log_level)
        return;

    now = time(NULL);
    tm_now = localtime(&now);

    if (tm_now)
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_now);
    else
        snprintf(time_buf, sizeof(time_buf), "unknown-time");

    va_copy(stdout_args, args);
    fprintf(stdout, "%s %-5s [%s] ", time_buf, level_name(level), module);
    vfprintf(stdout, format, stdout_args);
    fputc('\n', stdout);
    fflush(stdout);
    va_end(stdout_args);

    if (g_file_enabled && g_file) {
        va_copy(file_args, args);
        fprintf(g_file, "%s %-5s [%s] ", time_buf, level_name(level), module);
        vfprintf(g_file, format, file_args);
        fputc('\n', g_file);
        fflush(g_file);
        va_end(file_args);
        rotate_if_needed();
    }
}

void log_write(LogLevel level, const char* module, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    log_vwrite(level, module, format, args);
    va_end(args);
}
