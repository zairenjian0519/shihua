#include "history_store.h"

#include "log.h"

#include <mysql.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOE_DB_WRITE_MAX_RETRIES 3
#define SOE_DB_WRITE_RETRY_DELAY_MS 1000

static bool history_store_write_soe_locked(HistoryStore* store, const SoeRecord* record, int ca);
static void* history_store_worker_thread(void* parameter);

static bool valid_table_name(const char* name)
{
    if (!name || name[0] == '\0')
        return false;

    for (const char* p = name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_')
            return false;
    }

    return true;
}

void history_store_config_from_iec104(const Iec104Config* source, HistoryStoreConfig* dest)
{
    memset(dest, 0, sizeof(*dest));
    dest->enabled = source->mysql_enabled;
    snprintf(dest->host, sizeof(dest->host), "%s", source->mysql_host);
    dest->port = source->mysql_port;
    snprintf(dest->user, sizeof(dest->user), "%s", source->mysql_user);
    snprintf(dest->password, sizeof(dest->password), "%s", source->mysql_password);
    snprintf(dest->database, sizeof(dest->database), "%s", source->mysql_database);
    snprintf(dest->charset, sizeof(dest->charset), "%s", source->mysql_charset);
    dest->connect_timeout_ms = source->mysql_connect_timeout_ms;
    dest->ssl_verify_server_cert = source->mysql_ssl_verify_server_cert;
    dest->soe_enabled = source->history_soe_enabled;
    snprintf(dest->soe_table, sizeof(dest->soe_table), "%s", source->history_soe_table);
    dest->soe_max_records = source->history_soe_max_records;
    dest->query_max_records = source->history_query_max_records;
    dest->queue_capacity = source->history_db_queue_capacity;
}

static bool history_store_connect(HistoryStore* store)
{
    MYSQL* mysql;
    unsigned int timeout_s;
    my_bool reconnect = 1;
    my_bool ssl_verify_server_cert;

    if (!store->config.enabled)
        return false;

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

static bool exec_sql(HistoryStore* store, const char* sql)
{
    if (!history_store_connect(store))
        return false;

    if (mysql_real_query((MYSQL*)store->mysql, sql, (unsigned long)strlen(sql)) != 0) {
        LOG_ERROR("mysql", "query failed sql=%s error=%s", sql, mysql_error((MYSQL*)store->mysql));
        return false;
    }

    return true;
}

static void history_store_disconnect(HistoryStore* store)
{
    if (store->mysql)
        mysql_close((MYSQL*)store->mysql);
    store->mysql = NULL;
    store->connected = false;
    store->schema_ready = false;
}

static bool ensure_schema(HistoryStore* store)
{
    char sql[1024];

    if (store->schema_ready)
        return true;

    if (!store->config.soe_enabled)
        return false;

    if (!valid_table_name(store->config.soe_table)) {
        LOG_ERROR("mysql", "invalid soe table name: %s", store->config.soe_table);
        return false;
    }

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
        LOG_WARN("mysql", "history store starts without active database connection");

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

    if (store->mysql)
        mysql_close((MYSQL*)store->mysql);

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
    return store && store->config.enabled && store->config.soe_enabled;
}

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

static bool trim_soe_records(HistoryStore* store)
{
    char sql[512];

    if (store->config.soe_max_records <= 0)
        return true;

    snprintf(sql, sizeof(sql),
             "DELETE FROM %s WHERE id NOT IN ("
             "SELECT id FROM ("
             "SELECT id FROM %s ORDER BY timestamp_ms DESC, id DESC LIMIT %d"
             ") t"
             ")",
             store->config.soe_table, store->config.soe_table,
             store->config.soe_max_records);

    return exec_sql(store, sql);
}

bool history_store_append_soe(HistoryStore* store, const SoeRecord* record, int ca)
{
    if (!history_store_is_enabled(store) || !record || !store->queue_lock || !store->queue)
        return false;

    Semaphore_wait(store->queue_lock);

    if (store->queue_count >= store->queue_capacity) {
        store->queue_head = (store->queue_head + 1) % store->queue_capacity;
        store->queue_count--;
        LOG_WARN("mysql", "soe db queue full, drop oldest record");
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

    LOG_INFO("mysql", "history db worker started");

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
                LOG_ERROR("mysql", "drop soe after retries ioa=%d ts=%llu",
                          item.record.ioa,
                          (unsigned long long)item.record.timestamp_ms);
            }
        }
    }

    LOG_INFO("mysql", "history db worker stopped");
    return NULL;
}

static bool history_store_write_soe_locked(HistoryStore* store, const SoeRecord* record, int ca)
{
    char sql[512];
    MYSQL_STMT* stmt;
    MYSQL_BIND bind[6];
    uint64_t timestamp_ms;
    int ioa;
    int value;
    int quality;
    uint64_t sequence;
    int common_address;
    bool ok = false;
    bool disconnect_after_close = false;

    if (!history_store_is_enabled(store) || !record)
        return false;

    if (!ensure_schema(store))
        return false;

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

    timestamp_ms = record->timestamp_ms;
    ioa = record->ioa;
    value = record->value ? 1 : 0;
    quality = (int)record->quality;
    sequence = record->sequence;
    common_address = ca;

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
}

size_t history_store_query_soe(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                               SoeRecord* records, size_t max_records)
{
    char sql[512];
    MYSQL_STMT* stmt;
    MYSQL_BIND result[5];
    uint64_t begin_value = begin_ms;
    uint64_t end_value = end_ms;
    int limit_value;
    uint64_t timestamp_ms = 0;
    int ioa = 0;
    int value = 0;
    int quality = 0;
    uint64_t sequence = 0;
    size_t copied = 0;

    if (!history_store_is_enabled(store) || !records || max_records == 0)
        return 0;

    Semaphore_wait(store->db_lock);

    if (!ensure_schema(store))
    {
        Semaphore_post(store->db_lock);
        return 0;
    }

    if (store->config.query_max_records > 0 &&
        max_records > (size_t)store->config.query_max_records)
        max_records = (size_t)store->config.query_max_records;
    limit_value = (int)max_records;

    snprintf(sql, sizeof(sql),
             "SELECT timestamp_ms, ioa, value, quality, sequence_no "
             "FROM %s "
             "WHERE (? = 0 OR timestamp_ms >= ?) "
             "AND (? = 0 OR timestamp_ms <= ?) "
             "ORDER BY timestamp_ms ASC, id ASC "
             "LIMIT ?",
             store->config.soe_table);

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

    MYSQL_BIND params[5];
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
    Semaphore_post(store->db_lock);
    return copied;
}
