#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MKDIR(path) _mkdir(path)
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

static LogLevel g_log_level = LOG_LEVEL_INFO;
static int g_file_enabled = 0;
static char g_file_name[256] = {0};
static char g_base_path[256] = {0};
static char g_log_dir[256] = {0};
static char g_log_prefix[128] = {0};
static char g_log_ext[64] = {0};
static int g_file_count = 2;
static int g_max_file_size = 10485760;
static int g_append_timestamp = 1;
static int g_create_sequence = 0;
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

static void split_log_path(const char* file_name)
{
    char stem[256];
    const char* slash1;
    const char* slash2;
    const char* slash;
    const char* base;

    split_file_name(file_name, stem, sizeof(stem), g_log_ext, sizeof(g_log_ext));

    slash1 = strrchr(stem, '/');
    slash2 = strrchr(stem, '\\');
    slash = slash1 > slash2 ? slash1 : slash2;
    if (slash) {
        size_t dir_len = (size_t)(slash - stem);
        if (dir_len >= sizeof(g_log_dir))
            dir_len = sizeof(g_log_dir) - 1;
        memcpy(g_log_dir, stem, dir_len);
        g_log_dir[dir_len] = '\0';
        base = slash + 1;
    }
    else {
        snprintf(g_log_dir, sizeof(g_log_dir), ".");
        base = stem;
    }

    snprintf(g_log_prefix, sizeof(g_log_prefix), "%s", base);
}

static int file_name_matches_log_pattern(const char* name)
{
    size_t prefix_len = strlen(g_log_prefix);
    size_t ext_len = strlen(g_log_ext);
    size_t name_len;

    if (!name)
        return 0;

    name_len = strlen(name);
    if (name_len <= prefix_len + ext_len)
        return 0;
    if (strncmp(name, g_log_prefix, prefix_len) != 0)
        return 0;
    if (strcmp(name + name_len - ext_len, g_log_ext) != 0)
        return 0;
    if (name[prefix_len] != '_')
        return 0;

    return 1;
}

static void join_log_path(const char* name, char* out, size_t out_size)
{
    if (strcmp(g_log_dir, ".") == 0)
        snprintf(out, out_size, "%s", name);
    else
        snprintf(out, out_size, "%s/%s", g_log_dir, name);
}

static void make_timestamp(char* timestamp, size_t timestamp_size)
{
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);

    if (tm_now)
        strftime(timestamp, timestamp_size, "%Y%m%d_%H%M%S", tm_now);
    else
        snprintf(timestamp, timestamp_size, "unknown_time");
}

static void build_new_log_file_name(char* out, size_t out_size)
{
    char timestamp[32];
    char name[256];

    if (g_append_timestamp) {
        make_timestamp(timestamp, sizeof(timestamp));
        snprintf(name, sizeof(name), "%s_%s_%03d%s",
                 g_log_prefix, timestamp, g_create_sequence++, g_log_ext);
    }
    else {
        snprintf(name, sizeof(name), "%s%s", g_log_prefix, g_log_ext);
    }

    join_log_path(name, out, out_size);
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

typedef struct {
    char path[256];
    time_t mtime;
} LogFileEntry;

static int compare_log_file_entry(const void* left, const void* right)
{
    const LogFileEntry* a = (const LogFileEntry*)left;
    const LogFileEntry* b = (const LogFileEntry*)right;

    if (a->mtime < b->mtime)
        return -1;
    if (a->mtime > b->mtime)
        return 1;
    return strcmp(a->path, b->path);
}

static void collect_log_file(LogFileEntry* entries, size_t* count, size_t max_count,
                             const char* name, time_t mtime)
{
    char path[256];

    if (!file_name_matches_log_pattern(name) || *count >= max_count)
        return;

    join_log_path(name, path, sizeof(path));
    if (strcmp(path, g_file_name) == 0)
        return;

    snprintf(entries[*count].path, sizeof(entries[*count].path), "%s", path);
    entries[*count].mtime = mtime;
    (*count)++;
}

static void cleanup_old_log_files(void)
{
    LogFileEntry entries[512];
    size_t count = 0;
    size_t keep_old;

    if (g_file_count <= 0)
        return;

#ifdef _WIN32
    {
        char pattern[256];
        intptr_t handle;
        struct _finddata_t data;

        if (strcmp(g_log_dir, ".") == 0)
            snprintf(pattern, sizeof(pattern), "%s_*%s", g_log_prefix, g_log_ext);
        else
            snprintf(pattern, sizeof(pattern), "%s/%s_*%s", g_log_dir, g_log_prefix, g_log_ext);

        handle = _findfirst(pattern, &data);
        if (handle != -1) {
            do {
                collect_log_file(entries, &count, sizeof(entries) / sizeof(entries[0]),
                                 data.name, data.time_write);
            } while (_findnext(handle, &data) == 0);
            _findclose(handle);
        }
    }
#else
    {
        DIR* dir = opendir(g_log_dir);
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                char path[256];
                struct stat st;
                join_log_path(entry->d_name, path, sizeof(path));
                if (stat(path, &st) == 0)
                    collect_log_file(entries, &count, sizeof(entries) / sizeof(entries[0]),
                                     entry->d_name, st.st_mtime);
            }
            closedir(dir);
        }
    }
#endif

    if (g_file_count <= 1)
        keep_old = 0;
    else
        keep_old = (size_t)g_file_count - 1;

    if (count <= keep_old)
        return;

    qsort(entries, count, sizeof(entries[0]), compare_log_file_entry);
    for (size_t i = 0; i < count - keep_old; i++)
        remove(entries[i].path);
}

static void open_log_file(void)
{
    if (!g_file_enabled)
        return;

    build_new_log_file_name(g_file_name, sizeof(g_file_name));
    make_parent_dirs(g_file_name);
    cleanup_old_log_files();

    g_file = fopen(g_file_name, "w");
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

    build_new_log_file_name(g_file_name, sizeof(g_file_name));
    cleanup_old_log_files();
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
    log_close();

    if (!config) {
        log_init(LOG_LEVEL_INFO);
        return;
    }

    g_log_level = config->level;
    g_file_enabled = config->enabled;
    g_file_count = config->file_count > 0 ? config->file_count : 2;
    g_max_file_size = config->max_file_size_bytes > 0 ? config->max_file_size_bytes : 10485760;
    g_append_timestamp = config->append_start_timestamp;
    g_create_sequence = 0;

    if (!g_file_enabled)
        return;

    snprintf(g_base_path, sizeof(g_base_path), "%s", config->file_name);
    split_log_path(g_base_path);
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
