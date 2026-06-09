#include "history_store.h"

#include "log.h"

#ifdef HAVE_MARIADB
#include <mysql.h>
#endif

#ifdef HAVE_SQLITE
#include <sqlite3.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define SOE_DB_WRITE_MAX_RETRIES 3
#define SOE_DB_WRITE_RETRY_DELAY_MS 1000

static bool history_store_write_soe_locked(HistoryStore* store, const SoeRecord* record, int ca);
static void* history_store_worker_thread(void* parameter);
static size_t apply_query_limit(const HistoryStore* store, size_t max_records);

static bool backend_is_mysql(const HistoryStore* store)
{
    return store && strcmp(store->config.backend, "mysql") == 0;
}

static bool backend_is_sqlite(const HistoryStore* store)
{
    return store && strcmp(store->config.backend, "sqlite") == 0;
}

static const char* backend_tag(const HistoryStore* store)
{
    return backend_is_mysql(store) ? "mysql" : "sqlite";
}

static bool valid_identifier(const char* name)
{
    if (!name || name[0] == '\0')
        return false;

    for (const char* p = name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_')
            return false;
    }

    return true;
}

static bool valid_pragma_token(const char* value)
{
    if (!value || value[0] == '\0')
        return false;

    for (const char* p = value; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_')
            return false;
    }

    return true;
}

void history_store_config_from_iec104(const Iec104Config* source, HistoryStoreConfig* dest)
{
    memset(dest, 0, sizeof(*dest));
    snprintf(dest->backend, sizeof(dest->backend), "%s",
             source->history_storage_backend[0] ? source->history_storage_backend : "sqlite");

    snprintf(dest->host, sizeof(dest->host), "%s", source->mysql_host);
    dest->port = source->mysql_port;
    snprintf(dest->user, sizeof(dest->user), "%s", source->mysql_user);
    snprintf(dest->password, sizeof(dest->password), "%s", source->mysql_password);
    snprintf(dest->database, sizeof(dest->database), "%s", source->mysql_database);
    snprintf(dest->charset, sizeof(dest->charset), "%s", source->mysql_charset);
    dest->connect_timeout_ms = source->mysql_connect_timeout_ms;
    dest->ssl_verify_server_cert = source->mysql_ssl_verify_server_cert;

    dest->sqlite_enabled = source->sqlite_enabled;
    snprintf(dest->sqlite_database, sizeof(dest->sqlite_database), "%s", source->sqlite_database);
    dest->sqlite_busy_timeout_ms = source->sqlite_busy_timeout_ms;
    snprintf(dest->sqlite_journal_mode, sizeof(dest->sqlite_journal_mode), "%s",
             source->sqlite_journal_mode);
    snprintf(dest->sqlite_synchronous, sizeof(dest->sqlite_synchronous), "%s",
             source->sqlite_synchronous);

    if (strcmp(dest->backend, "mysql") == 0)
        dest->enabled = source->mysql_enabled;
    else if (strcmp(dest->backend, "sqlite") == 0)
        dest->enabled = source->sqlite_enabled;
    else
        dest->enabled = false;

    dest->soe_enabled = source->history_soe_enabled;
    snprintf(dest->soe_table, sizeof(dest->soe_table), "%s", source->history_soe_table);
    dest->soe_max_records = source->history_soe_max_records;
    dest->yx_enabled = source->history_yx_enabled;
    snprintf(dest->yx_table, sizeof(dest->yx_table), "%s", source->history_yx_table);
    dest->yc_enabled = source->history_yc_enabled;
    snprintf(dest->yc_table, sizeof(dest->yc_table), "%s", source->history_yc_table);
    dest->dd_enabled = source->history_dd_enabled;
    snprintf(dest->dd_table, sizeof(dest->dd_table), "%s", source->history_dd_table);
    dest->query_max_records = source->history_query_max_records;
    dest->queue_capacity = source->history_db_queue_capacity;
}

static bool make_dir_if_missing(const char* path)
{
#if defined(_WIN32)
    if (_mkdir(path) == 0 || errno == EEXIST)
        return true;
#else
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return true;
#endif
    return false;
}

static void ensure_parent_directory(const char* file_path)
{
    char path[260];
    size_t len;

    if (!file_path || !file_path[0])
        return;

    len = strlen(file_path);
    if (len >= sizeof(path))
        return;

    snprintf(path, sizeof(path), "%s", file_path);
    for (size_t i = 0; path[i]; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            char saved = path[i];
            path[i] = '\0';
            if (i > 0 && path[0] != '\0' && path[i - 1] != ':')
                make_dir_if_missing(path);
            path[i] = saved;
        }
    }
}

#ifdef HAVE_MARIADB
static bool mysql_connect_store(HistoryStore* store)
{
    MYSQL* mysql;
    unsigned int timeout_s;
    my_bool reconnect = 1;
    my_bool ssl_verify_server_cert;

    if (store->connected && store->mysql && mysql_ping((MYSQL*)store->mysql) == 0)
        return true;

    if (store->mysql) {
        mysql_close((MYSQL*)store->mysql);
        store->mysql = NULL;
        store->connected = false;
        store->schema_ready = false;
    }

    mysql = mysql_init(NULL);
    if (!mysql) {
        LOG_ERROR("mysql", "mysql_init failed");
        return false;
    }

    timeout_s = (unsigned int)((store->config.connect_timeout_ms + 999) / 1000);
    if (timeout_s == 0)
        timeout_s = 1;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_s);
    mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &timeout_s);
    mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &timeout_s);
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);
    ssl_verify_server_cert = store->config.ssl_verify_server_cert ? 1 : 0;
    mysql_options(mysql, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &ssl_verify_server_cert);

    if (mysql_real_connect(mysql, store->config.host, store->config.user,
                           store->config.password, store->config.database,
                           (unsigned int)store->config.port, NULL, 0) == NULL) {
        LOG_ERROR("mysql", "connect failed host=%s port=%d db=%s error=%s",
                  store->config.host, store->config.port, store->config.database,
                  mysql_error(mysql));
        mysql_close(mysql);
        return false;
    }

    if (store->config.charset[0] != '\0' &&
        mysql_set_character_set(mysql, store->config.charset) != 0) {
        LOG_WARN("mysql", "set charset %s failed: %s",
                 store->config.charset, mysql_error(mysql));
    }

    store->mysql = mysql;
    store->connected = true;
    LOG_INFO("mysql", "connected host=%s port=%d db=%s",
             store->config.host, store->config.port, store->config.database);
    return true;
}
#endif

#ifdef HAVE_SQLITE
static bool sqlite_exec(HistoryStore* store, const char* sql)
{
    char* error = NULL;
    int rc = sqlite3_exec((sqlite3*)store->sqlite, sql, NULL, NULL, &error);

    if (rc != SQLITE_OK) {
        LOG_ERROR("sqlite", "exec failed sql=%s error=%s", sql, error ? error : sqlite3_errmsg((sqlite3*)store->sqlite));
        sqlite3_free(error);
        return false;
    }

    return true;
}

static bool sqlite_connect_store(HistoryStore* store)
{
    sqlite3* db = NULL;
    char sql[128];
    int rc;

    if (store->connected && store->sqlite)
        return true;

    if (store->sqlite) {
        sqlite3_close((sqlite3*)store->sqlite);
        store->sqlite = NULL;
        store->connected = false;
        store->schema_ready = false;
    }

    ensure_parent_directory(store->config.sqlite_database);
    rc = sqlite3_open_v2(store->config.sqlite_database, &db,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                         NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("sqlite", "open failed db=%s error=%s",
                  store->config.sqlite_database,
                  db ? sqlite3_errmsg(db) : "out of memory");
        if (db)
            sqlite3_close(db);
        return false;
    }

    store->sqlite = db;
    store->connected = true;

    if (store->config.sqlite_busy_timeout_ms > 0)
        sqlite3_busy_timeout(db, store->config.sqlite_busy_timeout_ms);

    if (valid_pragma_token(store->config.sqlite_journal_mode)) {
        snprintf(sql, sizeof(sql), "PRAGMA journal_mode=%s", store->config.sqlite_journal_mode);
        sqlite_exec(store, sql);
    }

    if (valid_pragma_token(store->config.sqlite_synchronous)) {
        snprintf(sql, sizeof(sql), "PRAGMA synchronous=%s", store->config.sqlite_synchronous);
        sqlite_exec(store, sql);
    }

    LOG_INFO("sqlite", "connected db=%s", store->config.sqlite_database);
    return true;
}
#endif

static bool history_store_connect(HistoryStore* store)
{
    if (!store->config.enabled)
        return false;

    if (backend_is_mysql(store)) {
#ifdef HAVE_MARIADB
        return mysql_connect_store(store);
#else
        LOG_ERROR("mysql", "mysql backend selected but MariaDB support is not built");
        return false;
#endif
    }

    if (backend_is_sqlite(store)) {
#ifdef HAVE_SQLITE
        return sqlite_connect_store(store);
#else
        LOG_ERROR("sqlite", "sqlite backend selected but SQLite support is not built");
        return false;
#endif
    }

    LOG_ERROR("history", "unknown history storage backend: %s", store->config.backend);
    return false;
}

static bool exec_sql(HistoryStore* store, const char* sql)
{
    if (!history_store_connect(store))
        return false;

    if (backend_is_mysql(store)) {
#ifdef HAVE_MARIADB
        if (mysql_real_query((MYSQL*)store->mysql, sql, (unsigned long)strlen(sql)) != 0) {
            LOG_ERROR("mysql", "query failed sql=%s error=%s", sql, mysql_error((MYSQL*)store->mysql));
            return false;
        }
        return true;
#else
        return false;
#endif
    }

    if (backend_is_sqlite(store)) {
#ifdef HAVE_SQLITE
        return sqlite_exec(store, sql);
#else
        return false;
#endif
    }

    return false;
}

static void history_store_disconnect(HistoryStore* store)
{
#ifdef HAVE_MARIADB
    if (store->mysql)
        mysql_close((MYSQL*)store->mysql);
#endif
#ifdef HAVE_SQLITE
    if (store->sqlite)
        sqlite3_close((sqlite3*)store->sqlite);
#endif
    store->mysql = NULL;
    store->sqlite = NULL;
    store->connected = false;
    store->schema_ready = false;
}

static bool ensure_schema(HistoryStore* store)
{
    char sql[1024];
    char index_time[128];
    char index_ioa_time[128];

    if (store->schema_ready)
        return true;

    if (!store->config.soe_enabled) {
        store->schema_ready = true;
        return true;
    }

    if (!valid_identifier(store->config.soe_table)) {
        LOG_ERROR(backend_tag(store), "invalid soe table name: %s", store->config.soe_table);
        return false;
    }

    if (backend_is_mysql(store)) {
        snprintf(sql, sizeof(sql),
                 "CREATE TABLE IF NOT EXISTS %s ("
                 "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
                 "timestamp_ms BIGINT UNSIGNED NOT NULL,"
                 "ioa INT UNSIGNED NOT NULL,"
                 "value TINYINT UNSIGNED NOT NULL,"
                 "quality TINYINT UNSIGNED NOT NULL,"
                 "sequence_no BIGINT UNSIGNED NOT NULL,"
                 "ca INT UNSIGNED DEFAULT 1,"
                 "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
                 "PRIMARY KEY (id),"
                 "KEY idx_soe_time (timestamp_ms),"
                 "KEY idx_soe_ioa_time (ioa, timestamp_ms)"
                 ")",
                 store->config.soe_table);
        if (!exec_sql(store, sql))
            return false;
    }
    else if (backend_is_sqlite(store)) {
        snprintf(sql, sizeof(sql),
                 "CREATE TABLE IF NOT EXISTS %s ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 "timestamp_ms INTEGER NOT NULL,"
                 "ioa INTEGER NOT NULL,"
                 "value INTEGER NOT NULL,"
                 "quality INTEGER NOT NULL,"
                 "sequence_no INTEGER NOT NULL,"
                 "ca INTEGER DEFAULT 1,"
                 "created_at TEXT DEFAULT CURRENT_TIMESTAMP"
                 ")",
                 store->config.soe_table);
        if (!exec_sql(store, sql))
            return false;

        snprintf(index_time, sizeof(index_time), "%s_idx_soe_time", store->config.soe_table);
        snprintf(index_ioa_time, sizeof(index_ioa_time), "%s_idx_soe_ioa_time", store->config.soe_table);
        snprintf(sql, sizeof(sql),
                 "CREATE INDEX IF NOT EXISTS %s ON %s (timestamp_ms)",
                 index_time, store->config.soe_table);
        if (!exec_sql(store, sql))
            return false;
        snprintf(sql, sizeof(sql),
                 "CREATE INDEX IF NOT EXISTS %s ON %s (ioa, timestamp_ms)",
                 index_ioa_time, store->config.soe_table);
        if (!exec_sql(store, sql))
            return false;
    }
    else {
        return false;
    }

    store->schema_ready = true;
    return true;
}

bool history_store_init(HistoryStore* store, const HistoryStoreConfig* config)
{
    memset(store, 0, sizeof(*store));
    store->config = *config;

    if (store->config.soe_max_records <= 0)
        store->config.soe_max_records = 10000;
    if (store->config.query_max_records <= 0)
        store->config.query_max_records = 1000;
    if (store->config.queue_capacity <= 0)
        store->config.queue_capacity = 4096;
    store->queue_capacity = store->config.queue_capacity;

    if (!store->config.enabled)
        return true;

    store->db_lock = Semaphore_create(1);
    store->queue_lock = Semaphore_create(1);
    store->queue_items = Semaphore_create(0);
    store->queue = (HistorySoeQueueItem*)calloc((size_t)store->queue_capacity,
                                                sizeof(HistorySoeQueueItem));
    if (!store->db_lock || !store->queue_lock || !store->queue_items || !store->queue) {
        history_store_destroy(store);
        return false;
    }

    if (!history_store_connect(store) || !ensure_schema(store))
        LOG_WARN(backend_tag(store), "history store starts without active database connection");

    store->worker_running = true;
    store->worker_thread = Thread_create(history_store_worker_thread, store, false);
    if (!store->worker_thread) {
        store->worker_running = false;
        return false;
    }
    Thread_start(store->worker_thread);
    return true;
}

void history_store_destroy(HistoryStore* store)
{
    if (!store)
        return;

    if (store->worker_thread) {
        store->worker_running = false;
        Semaphore_post(store->queue_items);
        Thread_destroy(store->worker_thread);
        store->worker_thread = NULL;
    }

    history_store_disconnect(store);

    if (store->db_lock)
        Semaphore_destroy(store->db_lock);
    if (store->queue_lock)
        Semaphore_destroy(store->queue_lock);
    if (store->queue_items)
        Semaphore_destroy(store->queue_items);
    free(store->queue);

    memset(store, 0, sizeof(*store));
}

bool history_store_is_enabled(const HistoryStore* store)
{
    return store && store->config.enabled;
}

#ifdef HAVE_MARIADB
static bool bind_u64(MYSQL_BIND* bind, uint64_t* value)
{
    memset(bind, 0, sizeof(*bind));
    bind->buffer_type = MYSQL_TYPE_LONGLONG;
    bind->buffer = value;
    bind->is_unsigned = 1;
    return true;
}

static bool bind_i32(MYSQL_BIND* bind, int* value)
{
    memset(bind, 0, sizeof(*bind));
    bind->buffer_type = MYSQL_TYPE_LONG;
    bind->buffer = value;
    return true;
}

static bool bind_float(MYSQL_BIND* bind, float* value)
{
    memset(bind, 0, sizeof(*bind));
    bind->buffer_type = MYSQL_TYPE_FLOAT;
    bind->buffer = value;
    return true;
}
#endif

static size_t apply_query_limit(const HistoryStore* store, size_t max_records)
{
    if (store->config.query_max_records > 0 &&
        max_records > (size_t)store->config.query_max_records)
        return (size_t)store->config.query_max_records;

    return max_records;
}

static bool trim_soe_records(HistoryStore* store)
{
    char sql[512];

    if (store->config.soe_max_records <= 0)
        return true;

    if (backend_is_mysql(store)) {
        snprintf(sql, sizeof(sql),
                 "DELETE FROM %s WHERE id NOT IN ("
                 "SELECT id FROM ("
                 "SELECT id FROM %s ORDER BY timestamp_ms DESC, id DESC LIMIT %d"
                 ") t"
                 ")",
                 store->config.soe_table, store->config.soe_table,
                 store->config.soe_max_records);
    }
    else {
        snprintf(sql, sizeof(sql),
                 "DELETE FROM %s WHERE id NOT IN ("
                 "SELECT id FROM %s ORDER BY timestamp_ms DESC, id DESC LIMIT %d"
                 ")",
                 store->config.soe_table, store->config.soe_table,
                 store->config.soe_max_records);
    }

    return exec_sql(store, sql);
}

bool history_store_append_soe(HistoryStore* store, const SoeRecord* record, int ca)
{
    if (!history_store_is_enabled(store) || !store->config.soe_enabled ||
        !record || !store->queue_lock || !store->queue)
        return false;

    Semaphore_wait(store->queue_lock);

    if (store->queue_count >= store->queue_capacity) {
        store->queue_head = (store->queue_head + 1) % store->queue_capacity;
        store->queue_count--;
        LOG_WARN(backend_tag(store), "soe db queue full, drop oldest record");
    }

    store->queue[store->queue_tail].record = *record;
    store->queue[store->queue_tail].ca = ca;
    store->queue_tail = (store->queue_tail + 1) % store->queue_capacity;
    store->queue_count++;

    Semaphore_post(store->queue_lock);
    Semaphore_post(store->queue_items);
    return true;
}

static bool history_store_take_soe(HistoryStore* store, HistorySoeQueueItem* item)
{
    bool ok = false;

    Semaphore_wait(store->queue_lock);
    if (store->queue_count > 0) {
        *item = store->queue[store->queue_head];
        store->queue_head = (store->queue_head + 1) % store->queue_capacity;
        store->queue_count--;
        ok = true;
    }
    Semaphore_post(store->queue_lock);

    return ok;
}

static void* history_store_worker_thread(void* parameter)
{
    HistoryStore* store = (HistoryStore*)parameter;

    LOG_INFO(backend_tag(store), "history db worker started backend=%s", store->config.backend);

    while (store->worker_running) {
        HistorySoeQueueItem item;

        Semaphore_wait(store->queue_items);
        if (!store->worker_running)
            break;

        while (history_store_take_soe(store, &item)) {
            bool ok = false;

            for (int retry = 0; retry <= SOE_DB_WRITE_MAX_RETRIES && store->worker_running; retry++) {
                Semaphore_wait(store->db_lock);
                ok = history_store_write_soe_locked(store, &item.record, item.ca);
                Semaphore_post(store->db_lock);

                if (ok)
                    break;

                if (retry < SOE_DB_WRITE_MAX_RETRIES)
                    Thread_sleep(SOE_DB_WRITE_RETRY_DELAY_MS);
            }

            if (!ok) {
                LOG_ERROR(backend_tag(store), "drop soe after retries ioa=%d ts=%llu",
                          item.record.ioa,
                          (unsigned long long)item.record.timestamp_ms);
            }
        }
    }

    LOG_INFO(backend_tag(store), "history db worker stopped");
    return NULL;
}

static bool history_store_write_soe_locked(HistoryStore* store, const SoeRecord* record, int ca)
{
    char sql[512];

    if (!history_store_is_enabled(store) || !store->config.soe_enabled || !record)
        return false;

    if (!ensure_schema(store))
        return false;

    if (backend_is_mysql(store)) {
#ifdef HAVE_MARIADB
        MYSQL_STMT* stmt;
        MYSQL_BIND bind[6];
        uint64_t timestamp_ms = record->timestamp_ms;
        int ioa = record->ioa;
        int value = record->value ? 1 : 0;
        int quality = (int)record->quality;
        uint64_t sequence = record->sequence;
        int common_address = ca;
        bool ok = false;
        bool disconnect_after_close = false;

        snprintf(sql, sizeof(sql),
                 "INSERT INTO %s "
                 "(timestamp_ms, ioa, value, quality, sequence_no, ca) "
                 "VALUES (?, ?, ?, ?, ?, ?)",
                 store->config.soe_table);

        stmt = mysql_stmt_init((MYSQL*)store->mysql);
        if (!stmt)
            return false;

        if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
            LOG_ERROR("mysql", "prepare insert soe failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            history_store_disconnect(store);
            return false;
        }

        bind_u64(&bind[0], &timestamp_ms);
        bind_i32(&bind[1], &ioa);
        bind_i32(&bind[2], &value);
        bind_i32(&bind[3], &quality);
        bind_u64(&bind[4], &sequence);
        bind_i32(&bind[5], &common_address);

        if (mysql_stmt_bind_param(stmt, bind) == 0 &&
            mysql_stmt_execute(stmt) == 0) {
            ok = true;
        }
        else {
            LOG_ERROR("mysql", "insert soe failed ioa=%d error=%s", record->ioa, mysql_stmt_error(stmt));
            disconnect_after_close = true;
        }

        mysql_stmt_close(stmt);
        if (disconnect_after_close)
            history_store_disconnect(store);

        if (ok)
            trim_soe_records(store);

        return ok;
#else
        return false;
#endif
    }

    if (backend_is_sqlite(store)) {
#ifdef HAVE_SQLITE
        sqlite3_stmt* stmt;
        bool ok = false;

        snprintf(sql, sizeof(sql),
                 "INSERT INTO %s "
                 "(timestamp_ms, ioa, value, quality, sequence_no, ca) "
                 "VALUES (?, ?, ?, ?, ?, ?)",
                 store->config.soe_table);

        if (sqlite3_prepare_v2((sqlite3*)store->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
            LOG_ERROR("sqlite", "prepare insert soe failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
            history_store_disconnect(store);
            return false;
        }

        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)record->timestamp_ms);
        sqlite3_bind_int(stmt, 2, record->ioa);
        sqlite3_bind_int(stmt, 3, record->value ? 1 : 0);
        sqlite3_bind_int(stmt, 4, (int)record->quality);
        sqlite3_bind_int64(stmt, 5, (sqlite3_int64)record->sequence);
        sqlite3_bind_int(stmt, 6, ca);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            ok = true;
        }
        else {
            LOG_ERROR("sqlite", "insert soe failed ioa=%d error=%s",
                      record->ioa, sqlite3_errmsg((sqlite3*)store->sqlite));
        }

        sqlite3_finalize(stmt);
        if (ok)
            trim_soe_records(store);
        return ok;
#else
        return false;
#endif
    }

    return false;
}

size_t history_store_query_soe(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                               SoeRecord* records, size_t max_records)
{
    char sql[512];
    size_t copied = 0;

    if (!history_store_is_enabled(store) || !store->config.soe_enabled ||
        !records || max_records == 0)
        return 0;

    Semaphore_wait(store->db_lock);

    if (!history_store_connect(store) || !ensure_schema(store)) {
        Semaphore_post(store->db_lock);
        return 0;
    }

    max_records = apply_query_limit(store, max_records);

    snprintf(sql, sizeof(sql),
             "SELECT timestamp_ms, ioa, value, quality, sequence_no "
             "FROM %s "
             "WHERE (? = 0 OR timestamp_ms >= ?) "
             "AND (? = 0 OR timestamp_ms <= ?) "
             "ORDER BY timestamp_ms ASC, id ASC "
             "LIMIT ?",
             store->config.soe_table);

    if (backend_is_mysql(store)) {
#ifdef HAVE_MARIADB
        MYSQL_STMT* stmt;
        MYSQL_BIND params[5];
        MYSQL_BIND result[5];
        uint64_t begin_value = begin_ms;
        uint64_t end_value = end_ms;
        int limit_value = (int)max_records;
        uint64_t timestamp_ms = 0;
        int ioa = 0;
        int value = 0;
        int quality = 0;
        uint64_t sequence = 0;

        stmt = mysql_stmt_init((MYSQL*)store->mysql);
        if (!stmt) {
            Semaphore_post(store->db_lock);
            return 0;
        }

        if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
            LOG_ERROR("mysql", "prepare query soe failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&params[0], &begin_value);
        bind_u64(&params[1], &begin_value);
        bind_u64(&params[2], &end_value);
        bind_u64(&params[3], &end_value);
        bind_i32(&params[4], &limit_value);

        if (mysql_stmt_bind_param(stmt, params) != 0 ||
            mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("mysql", "query soe execute failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&result[0], &timestamp_ms);
        bind_i32(&result[1], &ioa);
        bind_i32(&result[2], &value);
        bind_i32(&result[3], &quality);
        bind_u64(&result[4], &sequence);

        if (mysql_stmt_bind_result(stmt, result) != 0) {
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        while (copied < max_records) {
            int rc = mysql_stmt_fetch(stmt);
            if (rc == MYSQL_NO_DATA)
                break;
            if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
                LOG_ERROR("mysql", "query soe fetch failed: %s", mysql_stmt_error(stmt));
                break;
            }

            records[copied].timestamp_ms = timestamp_ms;
            records[copied].ioa = ioa;
            records[copied].value = value != 0;
            records[copied].quality = (QualityDescriptor)quality;
            records[copied].sequence = sequence;
            copied++;
        }

        mysql_stmt_close(stmt);
#endif
    }
    else if (backend_is_sqlite(store)) {
#ifdef HAVE_SQLITE
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2((sqlite3*)store->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
            LOG_ERROR("sqlite", "prepare query soe failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
            Semaphore_post(store->db_lock);
            return 0;
        }

        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)end_ms);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)end_ms);
        sqlite3_bind_int(stmt, 5, (int)max_records);

        while (copied < max_records) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE)
                break;
            if (rc != SQLITE_ROW) {
                LOG_ERROR("sqlite", "query soe fetch failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
                break;
            }

            records[copied].timestamp_ms = (uint64_t)sqlite3_column_int64(stmt, 0);
            records[copied].ioa = sqlite3_column_int(stmt, 1);
            records[copied].value = sqlite3_column_int(stmt, 2) != 0;
            records[copied].quality = (QualityDescriptor)sqlite3_column_int(stmt, 3);
            records[copied].sequence = (uint64_t)sqlite3_column_int64(stmt, 4);
            copied++;
        }

        sqlite3_finalize(stmt);
#endif
    }

    Semaphore_post(store->db_lock);
    return copied;
}

size_t history_store_query_yx(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                              YxPoint* records, size_t max_records)
{
    char sql[512];
    size_t copied = 0;

    if (!history_store_is_enabled(store) || !store->config.yx_enabled ||
        !records || max_records == 0)
        return 0;

    if (!valid_identifier(store->config.yx_table)) {
        LOG_ERROR(backend_tag(store), "invalid history yx table name: %s", store->config.yx_table);
        return 0;
    }

    Semaphore_wait(store->db_lock);
    if (!history_store_connect(store)) {
        Semaphore_post(store->db_lock);
        return 0;
    }

    max_records = apply_query_limit(store, max_records);
    snprintf(sql, sizeof(sql),
             "SELECT timestamp_ms, ioa, value, quality "
             "FROM %s "
             "WHERE (? = 0 OR timestamp_ms >= ?) "
             "AND (? = 0 OR timestamp_ms <= ?) "
             "ORDER BY timestamp_ms ASC, id ASC "
             "LIMIT ?",
             store->config.yx_table);

    if (backend_is_mysql(store)) {
#ifdef HAVE_MARIADB
        MYSQL_STMT* stmt;
        MYSQL_BIND params[5];
        MYSQL_BIND result[4];
        uint64_t begin_value = begin_ms;
        uint64_t end_value = end_ms;
        int limit_value = (int)max_records;
        uint64_t timestamp_ms = 0;
        int ioa = 0;
        int value = 0;
        int quality = 0;

        stmt = mysql_stmt_init((MYSQL*)store->mysql);
        if (!stmt) {
            Semaphore_post(store->db_lock);
            return 0;
        }

        if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
            LOG_ERROR("mysql", "prepare query history yx failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&params[0], &begin_value);
        bind_u64(&params[1], &begin_value);
        bind_u64(&params[2], &end_value);
        bind_u64(&params[3], &end_value);
        bind_i32(&params[4], &limit_value);

        if (mysql_stmt_bind_param(stmt, params) != 0 ||
            mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("mysql", "query history yx execute failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&result[0], &timestamp_ms);
        bind_i32(&result[1], &ioa);
        bind_i32(&result[2], &value);
        bind_i32(&result[3], &quality);

        if (mysql_stmt_bind_result(stmt, result) == 0) {
            while (copied < max_records) {
                int rc = mysql_stmt_fetch(stmt);
                if (rc == MYSQL_NO_DATA)
                    break;
                if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
                    LOG_ERROR("mysql", "query history yx fetch failed: %s", mysql_stmt_error(stmt));
                    break;
                }

                memset(&records[copied], 0, sizeof(records[copied]));
                records[copied].timestamp_ms = timestamp_ms;
                records[copied].ioa = (uint32_t)ioa;
                records[copied].value = (uint8_t)(value != 0 ? 1 : 0);
                records[copied].quality = (uint8_t)quality;
                copied++;
            }
        }
        mysql_stmt_close(stmt);
#endif
    }
    else if (backend_is_sqlite(store)) {
#ifdef HAVE_SQLITE
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2((sqlite3*)store->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
            LOG_ERROR("sqlite", "prepare query history yx failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
            Semaphore_post(store->db_lock);
            return 0;
        }

        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)end_ms);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)end_ms);
        sqlite3_bind_int(stmt, 5, (int)max_records);

        while (copied < max_records) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE)
                break;
            if (rc != SQLITE_ROW) {
                LOG_ERROR("sqlite", "query history yx fetch failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
                break;
            }

            memset(&records[copied], 0, sizeof(records[copied]));
            records[copied].timestamp_ms = (uint64_t)sqlite3_column_int64(stmt, 0);
            records[copied].ioa = (uint32_t)sqlite3_column_int(stmt, 1);
            records[copied].value = (uint8_t)(sqlite3_column_int(stmt, 2) != 0 ? 1 : 0);
            records[copied].quality = (uint8_t)sqlite3_column_int(stmt, 3);
            copied++;
        }

        sqlite3_finalize(stmt);
#endif
    }

    Semaphore_post(store->db_lock);
    return copied;
}

size_t history_store_query_yx_page(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                                   uint64_t last_timestamp_ms, uint64_t last_id,
                                   YxPoint* records, uint64_t* record_ids,
                                   size_t max_records)
{
    char sql[640];
    size_t copied = 0;

    if (!history_store_is_enabled(store) || !store->config.yx_enabled ||
        !records || !record_ids || max_records == 0)
        return 0;

    if (!valid_identifier(store->config.yx_table)) {
        LOG_ERROR(backend_tag(store), "invalid history yx table name: %s", store->config.yx_table);
        return 0;
    }

    Semaphore_wait(store->db_lock);
    if (!history_store_connect(store)) {
        Semaphore_post(store->db_lock);
        return 0;
    }

    max_records = apply_query_limit(store, max_records);
    snprintf(sql, sizeof(sql),
             "SELECT id, timestamp_ms, ioa, value, quality "
             "FROM %s "
             "WHERE (? = 0 OR timestamp_ms >= ?) "
             "AND (? = 0 OR timestamp_ms <= ?) "
             "AND (timestamp_ms > ? OR (timestamp_ms = ? AND id > ?)) "
             "ORDER BY timestamp_ms ASC, id ASC "
             "LIMIT ?",
             store->config.yx_table);

    if (backend_is_mysql(store)) {
#ifdef HAVE_MARIADB
        MYSQL_STMT* stmt;
        MYSQL_BIND params[8];
        MYSQL_BIND result[5];
        uint64_t begin_value = begin_ms;
        uint64_t end_value = end_ms;
        uint64_t cursor_ts = last_timestamp_ms;
        uint64_t cursor_id = last_id;
        int limit_value = (int)max_records;
        uint64_t id = 0;
        uint64_t timestamp_ms = 0;
        int ioa = 0;
        int value = 0;
        int quality = 0;

        stmt = mysql_stmt_init((MYSQL*)store->mysql);
        if (!stmt) {
            Semaphore_post(store->db_lock);
            return 0;
        }

        if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
            LOG_ERROR("mysql", "prepare query history yx page failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&params[0], &begin_value);
        bind_u64(&params[1], &begin_value);
        bind_u64(&params[2], &end_value);
        bind_u64(&params[3], &end_value);
        bind_u64(&params[4], &cursor_ts);
        bind_u64(&params[5], &cursor_ts);
        bind_u64(&params[6], &cursor_id);
        bind_i32(&params[7], &limit_value);

        if (mysql_stmt_bind_param(stmt, params) != 0 ||
            mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("mysql", "query history yx page execute failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&result[0], &id);
        bind_u64(&result[1], &timestamp_ms);
        bind_i32(&result[2], &ioa);
        bind_i32(&result[3], &value);
        bind_i32(&result[4], &quality);

        if (mysql_stmt_bind_result(stmt, result) == 0) {
            while (copied < max_records) {
                int rc = mysql_stmt_fetch(stmt);
                if (rc == MYSQL_NO_DATA)
                    break;
                if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
                    LOG_ERROR("mysql", "query history yx page fetch failed: %s", mysql_stmt_error(stmt));
                    break;
                }

                memset(&records[copied], 0, sizeof(records[copied]));
                record_ids[copied] = id;
                records[copied].timestamp_ms = timestamp_ms;
                records[copied].ioa = (uint32_t)ioa;
                records[copied].value = (uint8_t)(value != 0 ? 1 : 0);
                records[copied].quality = (uint8_t)quality;
                copied++;
            }
        }
        mysql_stmt_close(stmt);
#endif
    }
    else if (backend_is_sqlite(store)) {
#ifdef HAVE_SQLITE
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2((sqlite3*)store->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
            LOG_ERROR("sqlite", "prepare query history yx page failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
            Semaphore_post(store->db_lock);
            return 0;
        }

        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)end_ms);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)end_ms);
        sqlite3_bind_int64(stmt, 5, (sqlite3_int64)last_timestamp_ms);
        sqlite3_bind_int64(stmt, 6, (sqlite3_int64)last_timestamp_ms);
        sqlite3_bind_int64(stmt, 7, (sqlite3_int64)last_id);
        sqlite3_bind_int(stmt, 8, (int)max_records);

        while (copied < max_records) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE)
                break;
            if (rc != SQLITE_ROW) {
                LOG_ERROR("sqlite", "query history yx page fetch failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
                break;
            }

            memset(&records[copied], 0, sizeof(records[copied]));
            record_ids[copied] = (uint64_t)sqlite3_column_int64(stmt, 0);
            records[copied].timestamp_ms = (uint64_t)sqlite3_column_int64(stmt, 1);
            records[copied].ioa = (uint32_t)sqlite3_column_int(stmt, 2);
            records[copied].value = (uint8_t)(sqlite3_column_int(stmt, 3) != 0 ? 1 : 0);
            records[copied].quality = (uint8_t)sqlite3_column_int(stmt, 4);
            copied++;
        }

        sqlite3_finalize(stmt);
#endif
    }

    Semaphore_post(store->db_lock);
    return copied;
}

size_t history_store_query_yc(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                              YcPoint* records, size_t max_records)
{
    char sql[512];
    size_t copied = 0;

    if (!history_store_is_enabled(store) || !store->config.yc_enabled ||
        !records || max_records == 0)
        return 0;

    if (!valid_identifier(store->config.yc_table)) {
        LOG_ERROR(backend_tag(store), "invalid history yc table name: %s", store->config.yc_table);
        return 0;
    }

    Semaphore_wait(store->db_lock);
    if (!history_store_connect(store)) {
        Semaphore_post(store->db_lock);
        return 0;
    }

    max_records = apply_query_limit(store, max_records);
    snprintf(sql, sizeof(sql),
             "SELECT timestamp_ms, ioa, value, quality, iec_type "
             "FROM %s "
             "WHERE (? = 0 OR timestamp_ms >= ?) "
             "AND (? = 0 OR timestamp_ms <= ?) "
             "ORDER BY timestamp_ms ASC, id ASC "
             "LIMIT ?",
             store->config.yc_table);

    if (backend_is_mysql(store)) {
#ifdef HAVE_MARIADB
        MYSQL_STMT* stmt;
        MYSQL_BIND params[5];
        MYSQL_BIND result[5];
        uint64_t begin_value = begin_ms;
        uint64_t end_value = end_ms;
        int limit_value = (int)max_records;
        uint64_t timestamp_ms = 0;
        int ioa = 0;
        float value = 0.0f;
        int quality = 0;
        int iec_type = YC_IEC_TYPE_FLOAT;

        stmt = mysql_stmt_init((MYSQL*)store->mysql);
        if (!stmt) {
            Semaphore_post(store->db_lock);
            return 0;
        }

        if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
            LOG_ERROR("mysql", "prepare query history yc failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&params[0], &begin_value);
        bind_u64(&params[1], &begin_value);
        bind_u64(&params[2], &end_value);
        bind_u64(&params[3], &end_value);
        bind_i32(&params[4], &limit_value);

        if (mysql_stmt_bind_param(stmt, params) != 0 ||
            mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("mysql", "query history yc execute failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&result[0], &timestamp_ms);
        bind_i32(&result[1], &ioa);
        bind_float(&result[2], &value);
        bind_i32(&result[3], &quality);
        bind_i32(&result[4], &iec_type);

        if (mysql_stmt_bind_result(stmt, result) == 0) {
            while (copied < max_records) {
                int rc = mysql_stmt_fetch(stmt);
                if (rc == MYSQL_NO_DATA)
                    break;
                if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
                    LOG_ERROR("mysql", "query history yc fetch failed: %s", mysql_stmt_error(stmt));
                    break;
                }

                memset(&records[copied], 0, sizeof(records[copied]));
                records[copied].timestamp_ms = timestamp_ms;
                records[copied].ioa = (uint32_t)ioa;
                records[copied].value = value;
                records[copied].quality = (uint8_t)quality;
                records[copied].iec_type = (YC_IECType)iec_type;
                copied++;
            }
        }
        mysql_stmt_close(stmt);
#endif
    }
    else if (backend_is_sqlite(store)) {
#ifdef HAVE_SQLITE
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2((sqlite3*)store->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
            LOG_ERROR("sqlite", "prepare query history yc failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
            Semaphore_post(store->db_lock);
            return 0;
        }

        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)end_ms);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)end_ms);
        sqlite3_bind_int(stmt, 5, (int)max_records);

        while (copied < max_records) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE)
                break;
            if (rc != SQLITE_ROW) {
                LOG_ERROR("sqlite", "query history yc fetch failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
                break;
            }

            memset(&records[copied], 0, sizeof(records[copied]));
            records[copied].timestamp_ms = (uint64_t)sqlite3_column_int64(stmt, 0);
            records[copied].ioa = (uint32_t)sqlite3_column_int(stmt, 1);
            records[copied].value = (float)sqlite3_column_double(stmt, 2);
            records[copied].quality = (uint8_t)sqlite3_column_int(stmt, 3);
            records[copied].iec_type = (YC_IECType)sqlite3_column_int(stmt, 4);
            copied++;
        }

        sqlite3_finalize(stmt);
#endif
    }

    Semaphore_post(store->db_lock);
    return copied;
}

size_t history_store_query_yc_page(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                                   uint64_t last_timestamp_ms, uint64_t last_id,
                                   YcPoint* records, uint64_t* record_ids,
                                   size_t max_records)
{
    char sql[640];
    size_t copied = 0;

    if (!history_store_is_enabled(store) || !store->config.yc_enabled ||
        !records || !record_ids || max_records == 0)
        return 0;

    if (!valid_identifier(store->config.yc_table)) {
        LOG_ERROR(backend_tag(store), "invalid history yc table name: %s", store->config.yc_table);
        return 0;
    }

    Semaphore_wait(store->db_lock);
    if (!history_store_connect(store)) {
        Semaphore_post(store->db_lock);
        return 0;
    }

    max_records = apply_query_limit(store, max_records);
    snprintf(sql, sizeof(sql),
             "SELECT id, timestamp_ms, ioa, value, quality, iec_type "
             "FROM %s "
             "WHERE (? = 0 OR timestamp_ms >= ?) "
             "AND (? = 0 OR timestamp_ms <= ?) "
             "AND (timestamp_ms > ? OR (timestamp_ms = ? AND id > ?)) "
             "ORDER BY timestamp_ms ASC, id ASC "
             "LIMIT ?",
             store->config.yc_table);

    if (backend_is_mysql(store)) {
#ifdef HAVE_MARIADB
        MYSQL_STMT* stmt;
        MYSQL_BIND params[8];
        MYSQL_BIND result[6];
        uint64_t begin_value = begin_ms;
        uint64_t end_value = end_ms;
        uint64_t cursor_ts = last_timestamp_ms;
        uint64_t cursor_id = last_id;
        int limit_value = (int)max_records;
        uint64_t id = 0;
        uint64_t timestamp_ms = 0;
        int ioa = 0;
        float value = 0.0f;
        int quality = 0;
        int iec_type = YC_IEC_TYPE_FLOAT;

        stmt = mysql_stmt_init((MYSQL*)store->mysql);
        if (!stmt) {
            Semaphore_post(store->db_lock);
            return 0;
        }

        if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
            LOG_ERROR("mysql", "prepare query history yc page failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&params[0], &begin_value);
        bind_u64(&params[1], &begin_value);
        bind_u64(&params[2], &end_value);
        bind_u64(&params[3], &end_value);
        bind_u64(&params[4], &cursor_ts);
        bind_u64(&params[5], &cursor_ts);
        bind_u64(&params[6], &cursor_id);
        bind_i32(&params[7], &limit_value);

        if (mysql_stmt_bind_param(stmt, params) != 0 ||
            mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("mysql", "query history yc page execute failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&result[0], &id);
        bind_u64(&result[1], &timestamp_ms);
        bind_i32(&result[2], &ioa);
        bind_float(&result[3], &value);
        bind_i32(&result[4], &quality);
        bind_i32(&result[5], &iec_type);

        if (mysql_stmt_bind_result(stmt, result) == 0) {
            while (copied < max_records) {
                int rc = mysql_stmt_fetch(stmt);
                if (rc == MYSQL_NO_DATA)
                    break;
                if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
                    LOG_ERROR("mysql", "query history yc page fetch failed: %s", mysql_stmt_error(stmt));
                    break;
                }

                memset(&records[copied], 0, sizeof(records[copied]));
                record_ids[copied] = id;
                records[copied].timestamp_ms = timestamp_ms;
                records[copied].ioa = (uint32_t)ioa;
                records[copied].value = value;
                records[copied].quality = (uint8_t)quality;
                records[copied].iec_type = (YC_IECType)iec_type;
                copied++;
            }
        }
        mysql_stmt_close(stmt);
#endif
    }
    else if (backend_is_sqlite(store)) {
#ifdef HAVE_SQLITE
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2((sqlite3*)store->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
            LOG_ERROR("sqlite", "prepare query history yc page failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
            Semaphore_post(store->db_lock);
            return 0;
        }

        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)end_ms);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)end_ms);
        sqlite3_bind_int64(stmt, 5, (sqlite3_int64)last_timestamp_ms);
        sqlite3_bind_int64(stmt, 6, (sqlite3_int64)last_timestamp_ms);
        sqlite3_bind_int64(stmt, 7, (sqlite3_int64)last_id);
        sqlite3_bind_int(stmt, 8, (int)max_records);

        while (copied < max_records) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE)
                break;
            if (rc != SQLITE_ROW) {
                LOG_ERROR("sqlite", "query history yc page fetch failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
                break;
            }

            memset(&records[copied], 0, sizeof(records[copied]));
            record_ids[copied] = (uint64_t)sqlite3_column_int64(stmt, 0);
            records[copied].timestamp_ms = (uint64_t)sqlite3_column_int64(stmt, 1);
            records[copied].ioa = (uint32_t)sqlite3_column_int(stmt, 2);
            records[copied].value = (float)sqlite3_column_double(stmt, 3);
            records[copied].quality = (uint8_t)sqlite3_column_int(stmt, 4);
            records[copied].iec_type = (YC_IECType)sqlite3_column_int(stmt, 5);
            copied++;
        }

        sqlite3_finalize(stmt);
#endif
    }

    Semaphore_post(store->db_lock);
    return copied;
}

size_t history_store_query_dd(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                              DdPoint* records, size_t max_records)
{
    char sql[512];
    size_t copied = 0;

    if (!history_store_is_enabled(store) || !store->config.dd_enabled ||
        !records || max_records == 0)
        return 0;

    if (!valid_identifier(store->config.dd_table)) {
        LOG_ERROR(backend_tag(store), "invalid history dd table name: %s", store->config.dd_table);
        return 0;
    }

    Semaphore_wait(store->db_lock);
    if (!history_store_connect(store)) {
        Semaphore_post(store->db_lock);
        return 0;
    }

    max_records = apply_query_limit(store, max_records);
    snprintf(sql, sizeof(sql),
             "SELECT timestamp_ms, ioa, value, quality, seq "
             "FROM %s "
             "WHERE (? = 0 OR timestamp_ms >= ?) "
             "AND (? = 0 OR timestamp_ms <= ?) "
             "ORDER BY timestamp_ms ASC, id ASC "
             "LIMIT ?",
             store->config.dd_table);

    if (backend_is_mysql(store)) {
#ifdef HAVE_MARIADB
        MYSQL_STMT* stmt;
        MYSQL_BIND params[5];
        MYSQL_BIND result[5];
        uint64_t begin_value = begin_ms;
        uint64_t end_value = end_ms;
        int limit_value = (int)max_records;
        uint64_t timestamp_ms = 0;
        int ioa = 0;
        int value = 0;
        int quality = 0;
        int seq = 0;

        stmt = mysql_stmt_init((MYSQL*)store->mysql);
        if (!stmt) {
            Semaphore_post(store->db_lock);
            return 0;
        }

        if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
            LOG_ERROR("mysql", "prepare query history dd failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&params[0], &begin_value);
        bind_u64(&params[1], &begin_value);
        bind_u64(&params[2], &end_value);
        bind_u64(&params[3], &end_value);
        bind_i32(&params[4], &limit_value);

        if (mysql_stmt_bind_param(stmt, params) != 0 ||
            mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("mysql", "query history dd execute failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&result[0], &timestamp_ms);
        bind_i32(&result[1], &ioa);
        bind_i32(&result[2], &value);
        bind_i32(&result[3], &quality);
        bind_i32(&result[4], &seq);

        if (mysql_stmt_bind_result(stmt, result) == 0) {
            while (copied < max_records) {
                int rc = mysql_stmt_fetch(stmt);
                if (rc == MYSQL_NO_DATA)
                    break;
                if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
                    LOG_ERROR("mysql", "query history dd fetch failed: %s", mysql_stmt_error(stmt));
                    break;
                }

                memset(&records[copied], 0, sizeof(records[copied]));
                records[copied].timestamp_ms = timestamp_ms;
                records[copied].ioa = (uint32_t)ioa;
                records[copied].value = (int32_t)value;
                records[copied].quality = (uint8_t)quality;
                records[copied].seq = (uint8_t)seq;
                records[copied].iec_type = DD_IEC_TYPE_COUNTER_CP56;
                copied++;
            }
        }
        mysql_stmt_close(stmt);
#endif
    }
    else if (backend_is_sqlite(store)) {
#ifdef HAVE_SQLITE
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2((sqlite3*)store->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
            LOG_ERROR("sqlite", "prepare query history dd failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
            Semaphore_post(store->db_lock);
            return 0;
        }

        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)end_ms);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)end_ms);
        sqlite3_bind_int(stmt, 5, (int)max_records);

        while (copied < max_records) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE)
                break;
            if (rc != SQLITE_ROW) {
                LOG_ERROR("sqlite", "query history dd fetch failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
                break;
            }

            memset(&records[copied], 0, sizeof(records[copied]));
            records[copied].timestamp_ms = (uint64_t)sqlite3_column_int64(stmt, 0);
            records[copied].ioa = (uint32_t)sqlite3_column_int(stmt, 1);
            records[copied].value = (int32_t)sqlite3_column_int(stmt, 2);
            records[copied].quality = (uint8_t)sqlite3_column_int(stmt, 3);
            records[copied].seq = (uint8_t)sqlite3_column_int(stmt, 4);
            records[copied].iec_type = DD_IEC_TYPE_COUNTER_CP56;
            copied++;
        }

        sqlite3_finalize(stmt);
#endif
    }

    Semaphore_post(store->db_lock);
    return copied;
}

size_t history_store_query_dd_page(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                                   uint64_t last_timestamp_ms, uint64_t last_id,
                                   DdPoint* records, uint64_t* record_ids,
                                   size_t max_records)
{
    char sql[640];
    size_t copied = 0;

    if (!history_store_is_enabled(store) || !store->config.dd_enabled ||
        !records || !record_ids || max_records == 0)
        return 0;

    if (!valid_identifier(store->config.dd_table)) {
        LOG_ERROR(backend_tag(store), "invalid history dd table name: %s", store->config.dd_table);
        return 0;
    }

    Semaphore_wait(store->db_lock);
    if (!history_store_connect(store)) {
        Semaphore_post(store->db_lock);
        return 0;
    }

    max_records = apply_query_limit(store, max_records);
    snprintf(sql, sizeof(sql),
             "SELECT id, timestamp_ms, ioa, value, quality, seq "
             "FROM %s "
             "WHERE (? = 0 OR timestamp_ms >= ?) "
             "AND (? = 0 OR timestamp_ms <= ?) "
             "AND (timestamp_ms > ? OR (timestamp_ms = ? AND id > ?)) "
             "ORDER BY timestamp_ms ASC, id ASC "
             "LIMIT ?",
             store->config.dd_table);

    if (backend_is_mysql(store)) {
#ifdef HAVE_MARIADB
        MYSQL_STMT* stmt;
        MYSQL_BIND params[8];
        MYSQL_BIND result[6];
        uint64_t begin_value = begin_ms;
        uint64_t end_value = end_ms;
        uint64_t cursor_ts = last_timestamp_ms;
        uint64_t cursor_id = last_id;
        int limit_value = (int)max_records;
        uint64_t id = 0;
        uint64_t timestamp_ms = 0;
        int ioa = 0;
        int value = 0;
        int quality = 0;
        int seq = 0;

        stmt = mysql_stmt_init((MYSQL*)store->mysql);
        if (!stmt) {
            Semaphore_post(store->db_lock);
            return 0;
        }

        if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
            LOG_ERROR("mysql", "prepare query history dd page failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&params[0], &begin_value);
        bind_u64(&params[1], &begin_value);
        bind_u64(&params[2], &end_value);
        bind_u64(&params[3], &end_value);
        bind_u64(&params[4], &cursor_ts);
        bind_u64(&params[5], &cursor_ts);
        bind_u64(&params[6], &cursor_id);
        bind_i32(&params[7], &limit_value);

        if (mysql_stmt_bind_param(stmt, params) != 0 ||
            mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("mysql", "query history dd page execute failed: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            Semaphore_post(store->db_lock);
            return 0;
        }

        bind_u64(&result[0], &id);
        bind_u64(&result[1], &timestamp_ms);
        bind_i32(&result[2], &ioa);
        bind_i32(&result[3], &value);
        bind_i32(&result[4], &quality);
        bind_i32(&result[5], &seq);

        if (mysql_stmt_bind_result(stmt, result) == 0) {
            while (copied < max_records) {
                int rc = mysql_stmt_fetch(stmt);
                if (rc == MYSQL_NO_DATA)
                    break;
                if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
                    LOG_ERROR("mysql", "query history dd page fetch failed: %s", mysql_stmt_error(stmt));
                    break;
                }

                memset(&records[copied], 0, sizeof(records[copied]));
                record_ids[copied] = id;
                records[copied].timestamp_ms = timestamp_ms;
                records[copied].ioa = (uint32_t)ioa;
                records[copied].value = (int32_t)value;
                records[copied].quality = (uint8_t)quality;
                records[copied].seq = (uint8_t)seq;
                records[copied].iec_type = DD_IEC_TYPE_COUNTER_CP56;
                copied++;
            }
        }
        mysql_stmt_close(stmt);
#endif
    }
    else if (backend_is_sqlite(store)) {
#ifdef HAVE_SQLITE
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2((sqlite3*)store->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
            LOG_ERROR("sqlite", "prepare query history dd page failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
            Semaphore_post(store->db_lock);
            return 0;
        }

        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)begin_ms);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)end_ms);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)end_ms);
        sqlite3_bind_int64(stmt, 5, (sqlite3_int64)last_timestamp_ms);
        sqlite3_bind_int64(stmt, 6, (sqlite3_int64)last_timestamp_ms);
        sqlite3_bind_int64(stmt, 7, (sqlite3_int64)last_id);
        sqlite3_bind_int(stmt, 8, (int)max_records);

        while (copied < max_records) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE)
                break;
            if (rc != SQLITE_ROW) {
                LOG_ERROR("sqlite", "query history dd page fetch failed: %s", sqlite3_errmsg((sqlite3*)store->sqlite));
                break;
            }

            memset(&records[copied], 0, sizeof(records[copied]));
            record_ids[copied] = (uint64_t)sqlite3_column_int64(stmt, 0);
            records[copied].timestamp_ms = (uint64_t)sqlite3_column_int64(stmt, 1);
            records[copied].ioa = (uint32_t)sqlite3_column_int(stmt, 2);
            records[copied].value = (int32_t)sqlite3_column_int(stmt, 3);
            records[copied].quality = (uint8_t)sqlite3_column_int(stmt, 4);
            records[copied].seq = (uint8_t)sqlite3_column_int(stmt, 5);
            records[copied].iec_type = DD_IEC_TYPE_COUNTER_CP56;
            copied++;
        }

        sqlite3_finalize(stmt);
#endif
    }

    Semaphore_post(store->db_lock);
    return copied;
}
