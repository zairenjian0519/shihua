#ifndef IEC104_SLAVE_LOG_H
#define IEC104_SLAVE_LOG_H

#include <stdarg.h>

typedef enum {
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_WARN = 3,
    LOG_LEVEL_ERROR = 4
} LogLevel;

typedef struct {
    int enabled;
    LogLevel level;
    char file_name[256];
    int file_count;
    int max_file_size_bytes;
    int append_start_timestamp;
} LogConfig;

LogLevel log_level_from_string(const char* text, LogLevel fallback);
void log_init(LogLevel level);
void log_init_config(const LogConfig* config);
void log_close(void);
void log_write(LogLevel level, const char* module, const char* format, ...);
void log_vwrite(LogLevel level, const char* module, const char* format, va_list args);

#define LOG_TRACE(module, ...) log_write(LOG_LEVEL_TRACE, module, __VA_ARGS__)
#define LOG_DEBUG(module, ...) log_write(LOG_LEVEL_DEBUG, module, __VA_ARGS__)
#define LOG_INFO(module, ...)  log_write(LOG_LEVEL_INFO, module, __VA_ARGS__)
#define LOG_WARN(module, ...)  log_write(LOG_LEVEL_WARN, module, __VA_ARGS__)
#define LOG_ERROR(module, ...) log_write(LOG_LEVEL_ERROR, module, __VA_ARGS__)

#endif
