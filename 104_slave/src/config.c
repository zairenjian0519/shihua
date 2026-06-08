#include "config.h"

#include "log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_JSON_C
#if defined(__has_include)
#if __has_include(<json-c/json.h>)
#include <json-c/json.h>
#else
#include <json.h>
#endif
#else
#include <json.h>
#endif
#endif

static char* read_file(const char* path)
{
    FILE* fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    rewind(fp);

    char* data = (char*)calloc((size_t)size + 1, 1);
    if (!data) {
        fclose(fp);
        return NULL;
    }

    if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    return data;
}

static char* strip_json5_comments(const char* text)
{
    size_t len = strlen(text);
    char* out = (char*)calloc(len + 1, 1);
    if (!out)
        return NULL;

    bool in_string = false;
    bool escape = false;
    size_t j = 0;

    for (size_t i = 0; i < len; i++) {
        char c = text[i];

        if (in_string) {
            out[j++] = c;
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                in_string = false;
            continue;
        }

        if (c == '"') {
            in_string = true;
            out[j++] = c;
            continue;
        }

        if (c == '/' && text[i + 1] == '/') {
            i += 2;
            while (i < len && text[i] != '\n' && text[i] != '\r')
                i++;
            if (i < len)
                out[j++] = text[i];
            continue;
        }

        if (c == '/' && text[i + 1] == '*') {
            i += 2;
            while (i < len && !(text[i] == '*' && text[i + 1] == '/')) {
                if (text[i] == '\n' || text[i] == '\r')
                    out[j++] = text[i];
                i++;
            }
            if (i < len)
                i++;
            continue;
        }

        out[j++] = c;
    }

    out[j] = '\0';
    return out;
}

static bool is_json5_key_start(char c)
{
    return isalpha((unsigned char)c) || c == '_' || c == '$';
}

static bool is_json5_key_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '$' || c == '-';
}

static char* quote_json5_keys(const char* text)
{
    size_t len = strlen(text);
    char* out = (char*)calloc(len * 3 + 1, 1);
    if (!out)
        return NULL;

    char stack[128];
    bool expect_key[128];
    int depth = -1;
    bool in_string = false;
    bool escape = false;
    size_t j = 0;

    for (size_t i = 0; i < len; i++) {
        char c = text[i];

        if (in_string) {
            out[j++] = c;
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                in_string = false;
            continue;
        }

        if (c == '"') {
            in_string = true;
            out[j++] = c;
            continue;
        }

        if (c == '{') {
            if (depth < 127) {
                depth++;
                stack[depth] = 'o';
                expect_key[depth] = true;
            }
            out[j++] = c;
            continue;
        }

        if (c == '[') {
            if (depth < 127) {
                depth++;
                stack[depth] = 'a';
                expect_key[depth] = false;
            }
            out[j++] = c;
            continue;
        }

        if (c == '}' || c == ']') {
            if (depth >= 0)
                depth--;
            out[j++] = c;
            continue;
        }

        if (depth >= 0 && stack[depth] == 'o') {
            if (c == ',') {
                expect_key[depth] = true;
                out[j++] = c;
                continue;
            }

            if (c == ':') {
                expect_key[depth] = false;
                out[j++] = c;
                continue;
            }

            if (expect_key[depth] && is_json5_key_start(c)) {
                size_t start = i;
                size_t end = i + 1;

                while (end < len && is_json5_key_char(text[end]))
                    end++;

                size_t p = end;
                while (p < len && isspace((unsigned char)text[p]))
                    p++;

                if (p < len && text[p] == ':') {
                    out[j++] = '"';
                    memcpy(out + j, text + start, end - start);
                    j += end - start;
                    out[j++] = '"';
                    i = end - 1;
                    continue;
                }
            }
        }

        out[j++] = c;
    }

    out[j] = '\0';
    return out;
}

static char* remove_trailing_commas(const char* text)
{
    size_t len = strlen(text);
    char* out = (char*)calloc(len + 1, 1);
    if (!out)
        return NULL;

    bool in_string = false;
    bool escape = false;
    size_t j = 0;

    for (size_t i = 0; i < len; i++) {
        char c = text[i];

        if (in_string) {
            out[j++] = c;
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                in_string = false;
            continue;
        }

        if (c == '"') {
            in_string = true;
            out[j++] = c;
            continue;
        }

        if (c == ',') {
            size_t p = i + 1;
            while (p < len && isspace((unsigned char)text[p]))
                p++;

            if (p < len && (text[p] == '}' || text[p] == ']'))
                continue;
        }

        out[j++] = c;
    }

    out[j] = '\0';
    return out;
}

static char* preprocess_json5(const char* text)
{
    char* no_comments = strip_json5_comments(text);
    if (!no_comments)
        return NULL;

    char* quoted_keys = quote_json5_keys(no_comments);
    free(no_comments);
    if (!quoted_keys)
        return NULL;

    char* strict_json = remove_trailing_commas(quoted_keys);
    free(quoted_keys);
    return strict_json;
}

static const char* find_key(const char* text, const char* key)
{
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "%s:", key);
    return strstr(text, pattern);
}

static bool parse_bool_key(const char* text, const char* key, bool current)
{
    const char* p = find_key(text, key);
    if (!p)
        return current;

    p = strchr(p, ':');
    if (!p)
        return current;

    p++;
    while (*p && isspace((unsigned char)*p))
        p++;

    if (strncmp(p, "true", 4) == 0)
        return true;
    if (strncmp(p, "false", 5) == 0)
        return false;

    return current;
}

static int parse_int_key(const char* text, const char* key, int current)
{
    const char* p = find_key(text, key);
    if (!p)
        return current;

    p = strchr(p, ':');
    if (!p)
        return current;

    return (int)strtol(p + 1, NULL, 10);
}

static void parse_string_key(const char* text, const char* key, char* dest, size_t dest_size)
{
    const char* p = find_key(text, key);
    if (!p)
        return;

    p = strchr(p, ':');
    if (!p)
        return;

    p++;
    while (*p && *p != '"')
        p++;

    if (*p != '"')
        return;

    p++;
    const char* end = strchr(p, '"');
    if (!end)
        return;

    size_t len = (size_t)(end - p);
    if (len >= dest_size)
        len = dest_size - 1;

    memcpy(dest, p, len);
    dest[len] = '\0';
}

#ifdef HAVE_JSON_C
static json_object* json_get_obj(json_object* parent, const char* key)
{
    json_object* child = NULL;
    if (!parent)
        return NULL;

    if (!json_object_object_get_ex(parent, key, &child))
        return NULL;

    return child;
}

static void json_read_bool(json_object* parent, const char* key, bool* dest)
{
    json_object* value = json_get_obj(parent, key);
    if (value && json_object_is_type(value, json_type_boolean))
        *dest = json_object_get_boolean(value);
}

static void json_read_int(json_object* parent, const char* key, int* dest)
{
    json_object* value = json_get_obj(parent, key);
    if (value && json_object_is_type(value, json_type_int))
        *dest = json_object_get_int(value);
}

static void json_read_string(json_object* parent, const char* key, char* dest, size_t dest_size)
{
    json_object* value = json_get_obj(parent, key);
    if (!value || !json_object_is_type(value, json_type_string))
        return;

    const char* text = json_object_get_string(value);
    if (!text)
        return;

    snprintf(dest, dest_size, "%s", text);
}

static bool load_with_json_c(const char* path, const char* source_text, Iec104Config* config)
{
    bool loaded = false;
    char* strict_json = preprocess_json5(source_text);

    if (!strict_json) {
        LOG_ERROR("config", "JSON5 preprocessing failed for %s", path);
        return false;
    }

    enum json_tokener_error error = json_tokener_success;
    json_object* root = json_tokener_parse_verbose(strict_json, &error);

    if (!root) {
        LOG_ERROR("config", "json-c parse failed for %s: %s",
                  path, json_tokener_error_desc(error));
        free(strict_json);
        return false;
    }

    json_object* iec104 = json_get_obj(root, "iec104");
    json_object* server = json_get_obj(iec104, "server");
    json_object* apci = json_get_obj(iec104, "apci");
    json_object* report = json_get_obj(iec104, "report");
    json_object* log = json_get_obj(iec104, "log");
    json_object* diag = json_get_obj(iec104, "diag");
    json_object* mysql = json_get_obj(iec104, "mysql");
    json_object* history_storage = json_get_obj(iec104, "history_storage");
    json_object* history_soe = json_get_obj(history_storage, "soe");
    json_object* history_query = json_get_obj(history_storage, "query");
    json_object* history_db_queue = json_get_obj(history_storage, "db_queue");
    if (!diag)
        diag = json_get_obj(iec104, "diagnostic");
    if (!diag)
        diag = json_get_obj(root, "diagnostic");
    json_object* point_table = json_get_obj(root, "point_table");
    json_object* address_plan = json_get_obj(point_table, "address_plan");

    json_read_bool(server, "enabled", &config->enabled);
    json_read_string(server, "local_ip", config->local_ip, sizeof(config->local_ip));
    json_read_int(server, "local_port", &config->local_port);
    json_read_int(server, "common_address", &config->common_address);
    json_read_int(server, "max_open_connections", &config->max_open_connections);
    json_read_int(server, "low_priority_queue_size", &config->low_priority_queue_size);
    json_read_int(server, "high_priority_queue_size", &config->high_priority_queue_size);
    json_read_bool(server, "raw_message_log", &config->raw_message_log);

    if (config->common_address == 1)
        json_read_int(address_plan, "common_address", &config->common_address);

    json_read_int(apci, "k", &config->k);
    json_read_int(apci, "w", &config->w);
    json_read_int(apci, "t0_seconds", &config->t0_seconds);
    json_read_int(apci, "t1_seconds", &config->t1_seconds);
    json_read_int(apci, "t2_seconds", &config->t2_seconds);
    json_read_int(apci, "t3_seconds", &config->t3_seconds);

    json_read_bool(report, "scan_enabled", &config->scan_enabled);
    json_read_bool(report, "active_upload_enabled", &config->active_upload_enabled);
    json_read_bool(report, "periodic_enabled", &config->periodic_enabled);
    json_read_int(report, "periodic_interval_ms", &config->periodic_interval_ms);
    json_read_int(report, "scan_interval_ms", &config->scan_interval_ms);

    json_read_bool(log, "enabled", &config->log_enabled);
    json_read_string(log, "level", config->log_level, sizeof(config->log_level));
    json_read_string(log, "file_name", config->log_file_name, sizeof(config->log_file_name));
    json_read_int(log, "file_count", &config->log_file_count);
    json_read_int(log, "max_file_size_bytes", &config->log_max_file_size_bytes);
    json_read_bool(log, "append_start_timestamp", &config->log_append_start_timestamp);

    json_read_bool(diag, "enabled", &config->diag_enabled);
    json_read_string(diag, "bind_ip", config->diag_bind_ip, sizeof(config->diag_bind_ip));
    json_read_int(diag, "port", &config->diag_port);
    json_read_bool(diag, "writable", &config->diag_writable);
    json_read_bool(diag, "allow_clear", &config->diag_allow_clear);

    json_read_bool(mysql, "enabled", &config->mysql_enabled);
    json_read_string(mysql, "host", config->mysql_host, sizeof(config->mysql_host));
    json_read_int(mysql, "port", &config->mysql_port);
    json_read_string(mysql, "user", config->mysql_user, sizeof(config->mysql_user));
    json_read_string(mysql, "password", config->mysql_password, sizeof(config->mysql_password));
    json_read_string(mysql, "database", config->mysql_database, sizeof(config->mysql_database));
    json_read_string(mysql, "charset", config->mysql_charset, sizeof(config->mysql_charset));
    json_read_int(mysql, "connect_timeout_ms", &config->mysql_connect_timeout_ms);
    json_read_bool(mysql, "ssl_verify_server_cert", &config->mysql_ssl_verify_server_cert);

    json_read_bool(history_soe, "enabled", &config->history_soe_enabled);
    json_read_string(history_soe, "table", config->history_soe_table, sizeof(config->history_soe_table));
    json_read_int(history_soe, "max_records", &config->history_soe_max_records);
    json_read_int(history_query, "max_records_per_call", &config->history_query_max_records);
    json_read_int(history_db_queue, "capacity", &config->history_db_queue_capacity);

    loaded = true;

    json_object_put(root);
    free(strict_json);
    return loaded;
}
#endif

void config_init_defaults(Iec104Config* config)
{
    memset(config, 0, sizeof(*config));
    config->enabled = true;
    snprintf(config->local_ip, sizeof(config->local_ip), "0.0.0.0");
    config->local_port = 2404;
    config->common_address = 1;
    config->max_open_connections = 1;
    config->low_priority_queue_size = 64;
    config->high_priority_queue_size = 32;
    config->k = 12;
    config->w = 8;
    config->t0_seconds = 10;
    config->t1_seconds = 15;
    config->t2_seconds = 10;
    config->t3_seconds = 20;
    config->scan_enabled = true;
    config->active_upload_enabled = true;
    config->periodic_enabled = true;
    config->periodic_interval_ms = 2000;
    config->scan_interval_ms = 1000;
    config->raw_message_log = false;
    config->log_enabled = true;
    snprintf(config->log_level, sizeof(config->log_level), "info");
    snprintf(config->log_file_name, sizeof(config->log_file_name), "logs/iec104_slave.log");
    config->log_file_count = 2;
    config->log_max_file_size_bytes = 10485760;
    config->log_append_start_timestamp = true;
    config->diag_enabled = false;
    snprintf(config->diag_bind_ip, sizeof(config->diag_bind_ip), "127.0.0.1");
    config->diag_port = 24040;
    config->diag_writable = true;
    config->diag_allow_clear = false;
    config->mysql_enabled = false;
    snprintf(config->mysql_host, sizeof(config->mysql_host), "127.0.0.1");
    config->mysql_port = 3306;
    snprintf(config->mysql_user, sizeof(config->mysql_user), "iec104");
    snprintf(config->mysql_password, sizeof(config->mysql_password), "");
    snprintf(config->mysql_database, sizeof(config->mysql_database), "iec104_history");
    snprintf(config->mysql_charset, sizeof(config->mysql_charset), "utf8mb4");
    config->mysql_connect_timeout_ms = 3000;
    config->mysql_ssl_verify_server_cert = false;
    config->history_soe_enabled = true;
    snprintf(config->history_soe_table, sizeof(config->history_soe_table), "iec104_soe_history");
    config->history_soe_max_records = 10000;
    config->history_query_max_records = 1000;
    config->history_db_queue_capacity = 4096;
}

bool config_load_file(const char* path, Iec104Config* config)
{
    config_init_defaults(config);

    char* text = read_file(path);
    if (!text) {
        LOG_WARN("config", "cannot read %s, using built-in defaults", path);
        return false;
    }

#ifdef HAVE_JSON_C
    if (!load_with_json_c(path, text, config)) {
        LOG_WARN("config", "falling back to legacy key scanner for %s", path);
    }
    else {
        free(text);
        LOG_INFO("config", "loaded by json-c after JSON5 preprocessing");
        LOG_INFO("config", "listen %s:%d ca=%d max_open_connections=%d",
                 config->local_ip, config->local_port, config->common_address, config->max_open_connections);
        return true;
    }
#else
    LOG_WARN("config", "json-c is not enabled, using legacy key scanner for %s", path);
#endif

    config->enabled = parse_bool_key(text, "enabled", config->enabled);
    parse_string_key(text, "local_ip", config->local_ip, sizeof(config->local_ip));
    config->local_port = parse_int_key(text, "local_port", config->local_port);
    config->common_address = parse_int_key(text, "common_address", config->common_address);
    config->max_open_connections = parse_int_key(text, "max_open_connections", config->max_open_connections);
    config->k = parse_int_key(text, "k", config->k);
    config->w = parse_int_key(text, "w", config->w);
    config->t0_seconds = parse_int_key(text, "t0_seconds", config->t0_seconds);
    config->t1_seconds = parse_int_key(text, "t1_seconds", config->t1_seconds);
    config->t2_seconds = parse_int_key(text, "t2_seconds", config->t2_seconds);
    config->t3_seconds = parse_int_key(text, "t3_seconds", config->t3_seconds);
    config->scan_enabled = parse_bool_key(text, "scan_enabled", config->scan_enabled);
    config->active_upload_enabled = parse_bool_key(text, "active_upload_enabled", config->active_upload_enabled);
    config->periodic_enabled = parse_bool_key(text, "periodic_enabled", config->periodic_enabled);
    config->periodic_interval_ms = parse_int_key(text, "periodic_interval_ms", config->periodic_interval_ms);
    config->scan_interval_ms = parse_int_key(text, "scan_interval_ms", config->scan_interval_ms);
    config->raw_message_log = parse_bool_key(text, "raw_message_log", config->raw_message_log);
    config->log_enabled = parse_bool_key(text, "enabled", config->log_enabled);
    parse_string_key(text, "level", config->log_level, sizeof(config->log_level));
    parse_string_key(text, "file_name", config->log_file_name, sizeof(config->log_file_name));
    config->log_file_count = parse_int_key(text, "file_count", config->log_file_count);
    config->log_max_file_size_bytes = parse_int_key(text, "max_file_size_bytes", config->log_max_file_size_bytes);
    config->log_append_start_timestamp =
        parse_bool_key(text, "append_start_timestamp", config->log_append_start_timestamp);
    config->diag_enabled = parse_bool_key(text, "diag_enabled", config->diag_enabled);
    if (!config->diag_enabled)
        config->diag_enabled = parse_bool_key(text, "diagnostic_enabled", config->diag_enabled);
    parse_string_key(text, "bind_ip", config->diag_bind_ip, sizeof(config->diag_bind_ip));
    config->diag_port = parse_int_key(text, "diag_port", config->diag_port);
    config->diag_writable = parse_bool_key(text, "writable", config->diag_writable);
    config->diag_allow_clear = parse_bool_key(text, "allow_clear", config->diag_allow_clear);
    config->mysql_enabled = parse_bool_key(text, "mysql_enabled", config->mysql_enabled);
    parse_string_key(text, "mysql_host", config->mysql_host, sizeof(config->mysql_host));
    config->mysql_port = parse_int_key(text, "mysql_port", config->mysql_port);
    parse_string_key(text, "mysql_user", config->mysql_user, sizeof(config->mysql_user));
    parse_string_key(text, "mysql_password", config->mysql_password, sizeof(config->mysql_password));
    parse_string_key(text, "mysql_database", config->mysql_database, sizeof(config->mysql_database));
    parse_string_key(text, "mysql_charset", config->mysql_charset, sizeof(config->mysql_charset));
    config->mysql_connect_timeout_ms =
        parse_int_key(text, "mysql_connect_timeout_ms", config->mysql_connect_timeout_ms);
    config->mysql_ssl_verify_server_cert =
        parse_bool_key(text, "mysql_ssl_verify_server_cert", config->mysql_ssl_verify_server_cert);
    config->history_soe_enabled = parse_bool_key(text, "history_soe_enabled", config->history_soe_enabled);
    parse_string_key(text, "history_soe_table", config->history_soe_table, sizeof(config->history_soe_table));
    config->history_soe_max_records =
        parse_int_key(text, "history_soe_max_records", config->history_soe_max_records);
    config->history_query_max_records =
        parse_int_key(text, "history_query_max_records", config->history_query_max_records);
    config->history_db_queue_capacity =
        parse_int_key(text, "history_db_queue_capacity", config->history_db_queue_capacity);

    free(text);

    LOG_INFO("config", "listen %s:%d ca=%d max_open_connections=%d",
             config->local_ip, config->local_port, config->common_address, config->max_open_connections);
    return true;
}
