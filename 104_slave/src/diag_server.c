#include "diag_server.h"

#include "hal_time.h"
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET diag_socket_t;
#define DIAG_INVALID_SOCKET INVALID_SOCKET
#define diag_close_socket closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int diag_socket_t;
#define DIAG_INVALID_SOCKET (-1)
#define diag_close_socket close
#endif

#define DIAG_REQUEST_MAX 1024
#define DIAG_RESPONSE_MAX 4096

static bool socket_init_once(void)
{
#ifdef _WIN32
    static bool initialized = false;
    WSADATA data;

    if (initialized)
        return true;

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return false;

    initialized = true;
#endif
    return true;
}

static void socket_set_reuseaddr(diag_socket_t fd)
{
    int value = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&value, sizeof(value));
}

static void set_response(char* response, size_t response_size, const char* format, ...)
{
    va_list args;

    if (!response || response_size == 0)
        return;

    va_start(args, format);
    vsnprintf(response, response_size, format, args);
    va_end(args);
}

static bool parse_int_arg(const char* text, const char* key, int* value)
{
    const char* p = strstr(text, key);
    if (!p)
        return false;

    p += strlen(key);
    while (*p == ' ' || *p == '=')
        p++;

    *value = (int)strtol(p, NULL, 0);
    return true;
}

static bool parse_float_arg(const char* text, const char* key, float* value)
{
    const char* p = strstr(text, key);
    if (!p)
        return false;

    p += strlen(key);
    while (*p == ' ' || *p == '=')
        p++;

    *value = (float)strtod(p, NULL);
    return true;
}

static bool parse_uint8_arg(const char* text, const char* key, uint8_t* value)
{
    int int_value;

    if (!parse_int_arg(text, key, &int_value))
        return false;

    if (int_value < 0)
        int_value = 0;
    if (int_value > 255)
        int_value = 255;

    *value = (uint8_t)int_value;
    return true;
}

static bool parse_bool_arg(const char* text, const char* key, bool* value)
{
    int int_value;

    if (!parse_int_arg(text, key, &int_value))
        return false;

    *value = int_value != 0;
    return true;
}

static bool command_has_flag(const char* text, const char* flag)
{
    return strstr(text, flag) != NULL;
}

static bool ensure_writable(DiagServer* diag, char* response, size_t response_size)
{
    if (diag->config.writable)
        return true;

    set_response(response, response_size, "{\"result\":\"error\",\"code\":\"READ_ONLY\"}");
    return false;
}

static bool handle_get(DiagServer* diag, const char* command, char* response, size_t response_size)
{
    int ioa = 0;

    if (!parse_int_arg(command, "ioa", &ioa)) {
        set_response(response, response_size, "{\"result\":\"error\",\"code\":\"MISSING_IOA\"}");
        return false;
    }

    point_table_read_lock(diag->table);

    YxPoint* yx = point_table_find_yx(diag->table, ioa);
    if (yx) {
        set_response(response, response_size,
                     "{\"result\":\"ok\",\"type\":\"yx\",\"ioa\":%d,\"value\":%d,\"quality\":%u}",
                     ioa, yx->value ? 1 : 0, yx->quality);
        point_table_read_unlock(diag->table);
        return true;
    }

    YcPoint* yc = point_table_find_yc(diag->table, ioa);
    if (yc) {
        set_response(response, response_size,
                     "{\"result\":\"ok\",\"type\":\"yc\",\"ioa\":%d,\"value\":%.3f,\"quality\":%u}",
                     ioa, yc->value, yc->quality);
        point_table_read_unlock(diag->table);
        return true;
    }

    DdPoint* dd = point_table_find_dd(diag->table, ioa);
    if (dd) {
        set_response(response, response_size,
                     "{\"result\":\"ok\",\"type\":\"dd\",\"ioa\":%d,\"value\":%d,\"quality\":%u,\"seq\":%u}",
                     ioa, dd->value, dd->quality, dd->seq);
        point_table_read_unlock(diag->table);
        return true;
    }

    YkPoint* yk = point_table_find_yk(diag->table, ioa);
    if (yk) {
        set_response(response, response_size,
                     "{\"result\":\"ok\",\"type\":\"yk\",\"ioa\":%d,\"state\":%d,\"selected\":%d}",
                     ioa, yk->state ? 1 : 0, yk->select_state ? 1 : 0);
        point_table_read_unlock(diag->table);
        return true;
    }

    YtPoint* yt = point_table_find_yt(diag->table, ioa);
    if (yt) {
        set_response(response, response_size,
                     "{\"result\":\"ok\",\"type\":\"yt\",\"ioa\":%d,\"value\":%.3f,\"selected\":%d}",
                     ioa, yt->value, yt->select_state ? 1 : 0);
        point_table_read_unlock(diag->table);
        return true;
    }

    point_table_read_unlock(diag->table);
    set_response(response, response_size, "{\"result\":\"error\",\"code\":\"IOA_NOT_FOUND\"}");
    return false;
}

static bool notify_upload(DiagServer* diag)
{
    if (!diag->notify_upload)
        return false;

    return diag->notify_upload(diag->owner, active_upload_get_version(diag->active_upload));
}

static bool handle_set_yx(DiagServer* diag, const char* command, char* response, size_t response_size)
{
    int ioa = 0;
    bool value = false;
    uint8_t quality = IEC60870_QUALITY_GOOD;
    uint64_t now = Hal_getTimeInMs();

    if (!ensure_writable(diag, response, response_size))
        return false;

    if (!parse_int_arg(command, "ioa", &ioa) || !parse_bool_arg(command, "value", &value)) {
        set_response(response, response_size, "{\"result\":\"error\",\"code\":\"MISSING_ARGUMENT\"}");
        return false;
    }

    if (!point_table_set_yx(diag->table, ioa, value, now)) {
        set_response(response, response_size, "{\"result\":\"error\",\"code\":\"IOA_NOT_FOUND\"}");
        return false;
    }

    point_table_read_lock(diag->table);
    YxPoint* point = point_table_find_yx(diag->table, ioa);
    YxPoint copy = point ? *point : (YxPoint){0};
    point_table_read_unlock(diag->table);

    if (copy.ioa != 0) {
        if (parse_uint8_arg(command, "quality", &quality)) {
            point_table_write_lock(diag->table);
            point = point_table_find_yx(diag->table, ioa);
            if (point)
                point->quality = quality;
            point_table_write_unlock(diag->table);
        }
    }

    set_response(response, response_size, "{\"result\":\"ok\",\"message\":\"YX updated\"}");
    return true;
}

static bool handle_set_yc(DiagServer* diag, const char* command, char* response, size_t response_size)
{
    int ioa = 0;
    float value = 0.0f;
    uint8_t quality = IEC60870_QUALITY_GOOD;
    uint64_t now = Hal_getTimeInMs();

    if (!ensure_writable(diag, response, response_size))
        return false;

    if (!parse_int_arg(command, "ioa", &ioa) || !parse_float_arg(command, "value", &value)) {
        set_response(response, response_size, "{\"result\":\"error\",\"code\":\"MISSING_ARGUMENT\"}");
        return false;
    }

    if (!point_table_set_yc(diag->table, ioa, value, now)) {
        set_response(response, response_size, "{\"result\":\"error\",\"code\":\"IOA_NOT_FOUND\"}");
        return false;
    }

    point_table_read_lock(diag->table);
    YcPoint* point = point_table_find_yc(diag->table, ioa);
    YcPoint copy = point ? *point : (YcPoint){0};
    point_table_read_unlock(diag->table);

    if (copy.ioa != 0) {
        if (parse_uint8_arg(command, "quality", &quality)) {
            point_table_write_lock(diag->table);
            point = point_table_find_yc(diag->table, ioa);
            if (point)
                point->quality = quality;
            copy.quality = quality;
            point_table_write_unlock(diag->table);
        }
        active_upload_put_yc(diag->active_upload, &copy, CS101_COT_SPONTANEOUS);
        point_table_mark_yc_reported(diag->table, copy.ioa, copy.value);
        notify_upload(diag);
    }

    set_response(response, response_size, "{\"result\":\"ok\",\"message\":\"YC updated\"}");
    return true;
}

static bool handle_set_dd(DiagServer* diag, const char* command, char* response, size_t response_size)
{
    int ioa = 0;
    int value = 0;
    uint8_t quality = IEC60870_QUALITY_GOOD;

    if (!ensure_writable(diag, response, response_size))
        return false;

    if (!parse_int_arg(command, "ioa", &ioa) || !parse_int_arg(command, "value", &value)) {
        set_response(response, response_size, "{\"result\":\"error\",\"code\":\"MISSING_ARGUMENT\"}");
        return false;
    }

    point_table_write_lock(diag->table);
    DdPoint* point = point_table_find_dd(diag->table, ioa);
    if (!point) {
        point_table_write_unlock(diag->table);
        set_response(response, response_size, "{\"result\":\"error\",\"code\":\"IOA_NOT_FOUND\"}");
        return false;
    }

    point->value = value;
    if (parse_uint8_arg(command, "quality", &quality))
        point->quality = quality;
    point->seq++;
    point->timestamp_ms = Hal_getTimeInMs();
    CP56Time2a_setFromMsTimestamp(&point->freeze_time, point->timestamp_ms);
    point_table_write_unlock(diag->table);

    set_response(response, response_size, "{\"result\":\"ok\",\"message\":\"DD updated\"}");
    return true;
}

static bool handle_soe(DiagServer* diag, const char* command, char* response, size_t response_size)
{
    if (strstr(command, "add")) {
        int ioa = 0;
        bool value = false;
        uint8_t quality = IEC60870_QUALITY_GOOD;
        uint64_t now = Hal_getTimeInMs();
        YxPoint copy;
        bool found = false;

        if (!ensure_writable(diag, response, response_size))
            return false;

        if (!parse_int_arg(command, "ioa", &ioa) || !parse_bool_arg(command, "value", &value)) {
            set_response(response, response_size, "{\"result\":\"error\",\"code\":\"MISSING_ARGUMENT\"}");
            return false;
        }

        parse_uint8_arg(command, "quality", &quality);

        point_table_read_lock(diag->table);
        YxPoint* point = point_table_find_yx(diag->table, ioa);
        if (point) {
            copy = *point;
            found = true;
        }
        point_table_read_unlock(diag->table);

        if (!found) {
            set_response(response, response_size, "{\"result\":\"error\",\"code\":\"IOA_NOT_FOUND\"}");
            return false;
        }

        copy.value = value;
        copy.quality = quality;
        copy.timestamp_ms = now;
        active_upload_put_yx(diag->active_upload, &copy, true);
        soe_history_append(diag->soe_history, copy.ioa, copy.value, copy.quality, copy.timestamp_ms);
        notify_upload(diag);
        set_response(response, response_size, "{\"result\":\"ok\",\"message\":\"SOE added\"}");
        return true;
    }

    if (strstr(command, "clear")) {
        if (!diag->config.allow_clear) {
            set_response(response, response_size, "{\"result\":\"error\",\"code\":\"CLEAR_DISABLED\"}");
            return false;
        }

        soe_history_clear(diag->soe_history);
        set_response(response, response_size, "{\"result\":\"ok\",\"message\":\"SOE cleared\"}");
        return true;
    }

    SoeRecord records[16];
    size_t count = soe_history_query(diag->soe_history, 0, 0, records,
                                     sizeof(records) / sizeof(records[0]));
    size_t offset = 0;
    offset += (size_t)snprintf(response + offset, response_size - offset,
                               "{\"result\":\"ok\",\"count\":%u,\"records\":[",
                               (unsigned)soe_history_count(diag->soe_history));
    for (size_t i = 0; i < count && offset < response_size; i++) {
        offset += (size_t)snprintf(response + offset, response_size - offset,
                                   "%s{\"seq\":%llu,\"ioa\":%d,\"value\":%d,\"quality\":%u,\"time\":%llu}",
                                   i == 0 ? "" : ",",
                                   (unsigned long long)records[i].sequence,
                                   records[i].ioa, records[i].value ? 1 : 0,
                                   records[i].quality,
                                   (unsigned long long)records[i].timestamp_ms);
    }
    snprintf(response + offset, response_size - offset, "]}");
    return true;
}

static bool handle_active_upload(DiagServer* diag, const char* command,
                                 char* response, size_t response_size)
{
    if (strstr(command, "clear")) {
        if (!diag->config.allow_clear) {
            set_response(response, response_size, "{\"result\":\"error\",\"code\":\"CLEAR_DISABLED\"}");
            return false;
        }

        active_upload_clear(diag->active_upload);
        set_response(response, response_size, "{\"result\":\"ok\",\"message\":\"active upload cleared\"}");
        return true;
    }

    if (strstr(command, "notify")) {
        bool ok = notify_upload(diag);
        set_response(response, response_size, "{\"result\":\"%s\",\"message\":\"notify\"}", ok ? "ok" : "error");
        return ok;
    }

    ActiveUploadSnapshot snapshot;
    if (!active_upload_snapshot_create(diag->active_upload, 0, &snapshot)) {
        set_response(response, response_size, "{\"result\":\"error\",\"code\":\"SNAPSHOT_FAILED\"}");
        return false;
    }

    if (command_has_flag(command, "--type soe")) {
        size_t offset = 0;
        offset += (size_t)snprintf(response + offset, response_size - offset,
                                   "{\"result\":\"ok\",\"version\":%llu,\"soe\":[",
                                   (unsigned long long)snapshot.version);
        for (size_t i = 0; i < snapshot.soe_count && i < 16 && offset < response_size; i++) {
            offset += (size_t)snprintf(response + offset, response_size - offset,
                                       "%s{\"seq\":%llu,\"ioa\":%d,\"value\":%d,\"quality\":%u}",
                                       i == 0 ? "" : ",",
                                       (unsigned long long)snapshot.soe[i].sequence,
                                       snapshot.soe[i].ioa,
                                       snapshot.soe[i].value ? 1 : 0,
                                       snapshot.soe[i].quality);
        }
        snprintf(response + offset, response_size - offset, "]}");
    }
    else {
        set_response(response, response_size,
                     "{\"result\":\"ok\",\"version\":%llu,\"yx\":%u,\"yc\":%u,\"soe\":%u}",
                     (unsigned long long)snapshot.version,
                     (unsigned)snapshot.yx_count, (unsigned)snapshot.yc_count,
                     (unsigned)snapshot.soe_count);
    }
    active_upload_snapshot_destroy(&snapshot);
    return true;
}

bool diag_server_init(DiagServer* diag, const DiagConfig* config,
                      PointTable* table, ActiveUploadArea* active_upload,
                      SoeHistory* soe_history, DiagNotifyUpload notify_upload,
                      void* owner)
{
    memset(diag, 0, sizeof(*diag));

    if (config)
        diag->config = *config;

    diag->table = table;
    diag->active_upload = active_upload;
    diag->soe_history = soe_history;
    diag->notify_upload = notify_upload;
    diag->owner = owner;
    diag->listen_fd = (int)DIAG_INVALID_SOCKET;
    return true;
}

void diag_server_destroy(DiagServer* diag)
{
    if (!diag)
        return;

    diag_server_stop(diag);
    memset(diag, 0, sizeof(*diag));
}

static void trim_line(char* text)
{
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' ||
                       text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[len - 1] = '\0';
        len--;
    }
}

static void handle_client_socket(DiagServer* diag, diag_socket_t client_fd)
{
    char request[DIAG_REQUEST_MAX];
    char response[DIAG_RESPONSE_MAX];
    int received;

    memset(request, 0, sizeof(request));
    memset(response, 0, sizeof(response));

    received = (int)recv(client_fd, request, sizeof(request) - 1, 0);
    if (received <= 0)
        return;

    request[received] = '\0';
    trim_line(request);

    diag_server_execute(diag, request, response, sizeof(response));
    strncat(response, "\n", sizeof(response) - strlen(response) - 1);
    send(client_fd, response, (int)strlen(response), 0);
}

static void* diag_server_thread(void* parameter)
{
    DiagServer* diag = (DiagServer*)parameter;
    diag_socket_t listen_fd;
    struct sockaddr_in addr;

    if (!socket_init_once()) {
        LOG_ERROR("diag", "socket init failed");
        return NULL;
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == DIAG_INVALID_SOCKET) {
        LOG_ERROR("diag", "socket create failed");
        return NULL;
    }

    socket_set_reuseaddr(listen_fd);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)diag->config.port);
    if (inet_pton(AF_INET, diag->config.bind_ip, &addr.sin_addr) != 1)
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        LOG_ERROR("diag", "bind failed %s:%d", diag->config.bind_ip, diag->config.port);
        diag_close_socket(listen_fd);
        return NULL;
    }

    if (listen(listen_fd, 8) != 0) {
        LOG_ERROR("diag", "listen failed %s:%d", diag->config.bind_ip, diag->config.port);
        diag_close_socket(listen_fd);
        return NULL;
    }

    diag->listen_fd = (int)listen_fd;
    LOG_INFO("diag", "diagnostic server listening on %s:%d",
             diag->config.bind_ip, diag->config.port);

    while (diag->running) {
        diag_socket_t client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd == DIAG_INVALID_SOCKET) {
            if (diag->running)
                Thread_sleep(50);
            continue;
        }

        handle_client_socket(diag, client_fd);
        diag_close_socket(client_fd);
    }

    diag_close_socket(listen_fd);
    diag->listen_fd = (int)DIAG_INVALID_SOCKET;
    LOG_INFO("diag", "diagnostic server stopped");
    return NULL;
}

bool diag_server_start(DiagServer* diag)
{
    if (!diag || !diag->config.enabled)
        return true;

    if (diag->thread)
        return true;

    diag->running = true;
    diag->thread = Thread_create(diag_server_thread, diag, false);
    if (!diag->thread) {
        diag->running = false;
        return false;
    }

    Thread_start(diag->thread);
    return true;
}

void diag_server_stop(DiagServer* diag)
{
    if (!diag || !diag->thread)
        return;

    diag->running = false;
    if (diag->listen_fd != (int)DIAG_INVALID_SOCKET) {
        diag_close_socket((diag_socket_t)diag->listen_fd);
        diag->listen_fd = (int)DIAG_INVALID_SOCKET;
    }

    Thread_destroy(diag->thread);
    diag->thread = NULL;
}

bool diag_server_execute(DiagServer* diag, const char* command,
                         char* response, size_t response_size)
{
    if (!diag || !command) {
        set_response(response, response_size, "{\"result\":\"error\",\"code\":\"BAD_REQUEST\"}");
        return false;
    }

    if (strncmp(command, "status", 6) == 0) {
        set_response(response, response_size,
                     "{\"result\":\"ok\",\"soe_count\":%u,\"upload_version\":%llu}",
                     (unsigned)soe_history_count(diag->soe_history),
                     (unsigned long long)active_upload_get_version(diag->active_upload));
        return true;
    }

    if (strncmp(command, "get", 3) == 0)
        return handle_get(diag, command, response, response_size);
    if (strncmp(command, "set-yx", 6) == 0)
        return handle_set_yx(diag, command, response, response_size);
    if (strncmp(command, "set-yc", 6) == 0)
        return handle_set_yc(diag, command, response, response_size);
    if (strncmp(command, "set-dd", 6) == 0)
        return handle_set_dd(diag, command, response, response_size);
    if (strncmp(command, "soe", 3) == 0)
        return handle_soe(diag, command, response, response_size);
    if (strncmp(command, "active-upload", 13) == 0)
        return handle_active_upload(diag, command, response, response_size);

    set_response(response, response_size, "{\"result\":\"error\",\"code\":\"UNKNOWN_COMMAND\"}");
    return false;
}
