#ifndef IEC104_SLAVE_HISTORY_STORE_H
#define IEC104_SLAVE_HISTORY_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "hal_thread.h"
#include "point_table.h"
#include "soe_history.h"

typedef struct {
    bool enabled;
    char backend[16];
    char host[64];
    int port;
    char user[64];
    char password[128];
    char database[64];
    char charset[32];
    int connect_timeout_ms;
    bool ssl_verify_server_cert;
    bool sqlite_enabled;
    char sqlite_database[260];
    int sqlite_busy_timeout_ms;
    char sqlite_journal_mode[16];
    char sqlite_synchronous[16];
    bool soe_enabled;
    char soe_table[64];
    int soe_max_records;
    bool yx_enabled;
    char yx_table[64];
    bool yc_enabled;
    char yc_table[64];
    bool dd_enabled;
    char dd_table[64];
    int query_max_records;
    int queue_capacity;
} HistoryStoreConfig;

typedef struct {
    SoeRecord record;
    int ca;
} HistorySoeQueueItem;

typedef struct {
    HistoryStoreConfig config;
    void* mysql;
    void* sqlite;
    bool connected;
    bool schema_ready;
    Semaphore db_lock;
    Semaphore queue_lock;
    Semaphore queue_items;
    HistorySoeQueueItem* queue;
    int queue_capacity;
    int queue_head;
    int queue_tail;
    int queue_count;
    volatile bool worker_running;
    Thread worker_thread;
} HistoryStore;

void history_store_config_from_iec104(const Iec104Config* source, HistoryStoreConfig* dest);
bool history_store_init(HistoryStore* store, const HistoryStoreConfig* config);
void history_store_destroy(HistoryStore* store);
bool history_store_is_enabled(const HistoryStore* store);
bool history_store_append_soe(HistoryStore* store, const SoeRecord* record, int ca);
size_t history_store_query_soe(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                               SoeRecord* records, size_t max_records);
size_t history_store_query_yx(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                              YxPoint* records, size_t max_records);
size_t history_store_query_yc(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                              YcPoint* records, size_t max_records);
size_t history_store_query_dd(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                              DdPoint* records, size_t max_records);
size_t history_store_query_yx_page(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                                   uint64_t last_timestamp_ms, uint64_t last_id,
                                   YxPoint* records, uint64_t* record_ids,
                                   size_t max_records);
size_t history_store_query_yc_page(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                                   uint64_t last_timestamp_ms, uint64_t last_id,
                                   YcPoint* records, uint64_t* record_ids,
                                   size_t max_records);
size_t history_store_query_dd_page(HistoryStore* store, uint64_t begin_ms, uint64_t end_ms,
                                   uint64_t last_timestamp_ms, uint64_t last_id,
                                   DdPoint* records, uint64_t* record_ids,
                                   size_t max_records);

#endif
