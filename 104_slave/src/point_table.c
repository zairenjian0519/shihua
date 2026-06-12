#include "point_table.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define IOA_YX_START 0x0001
#define IOA_YX_END 0x1000
#define IOA_YC_RTU_START 0x1001
#define IOA_YC_RTU_END 0x2000
#define IOA_YC_SENSOR_CONF_START 0x2001
#define IOA_YC_SENSOR_CONF_END 0x4000
#define IOA_YC_START 0x4001
#define IOA_YC_END 0x5000
#define IOA_YC_CUSTOM_START 0x5001
#define IOA_YC_CUSTOM_END 0x6000
#define IOA_YK_START 0x6001
#define IOA_YK_END 0x6100
#define IOA_YT_START 0x6201
#define IOA_YT_END 0x6400
#define IOA_DD_START 0x6401
#define IOA_DD_END 0x6600
#define SELECT_TIMEOUT_MS 60000

static uint64_t initial_time_ms(void)
{
    return 0;
}

static void set_cp56_time(struct sCP56Time2a* timestamp, uint64_t source_ms)
{
    if (!timestamp)
        return;

    if (source_ms == 0)
        memset(timestamp, 0, sizeof(*timestamp));
    else
        CP56Time2a_setFromMsTimestamp(timestamp, source_ms);
}

static bool rwlock_init(PointTableRwLock* lock)
{
    lock->mutex = Semaphore_create(1);
    lock->resource = Semaphore_create(1);
    lock->readers = 0;
    return lock->mutex != NULL && lock->resource != NULL;
}

static void rwlock_destroy(PointTableRwLock* lock)
{
    if (lock->mutex)
        Semaphore_destroy(lock->mutex);

    if (lock->resource)
        Semaphore_destroy(lock->resource);

    memset(lock, 0, sizeof(*lock));
}

static PointPartitionMeta make_meta(const char* name, PointType type, int start, int end)
{
    PointPartitionMeta meta;
    meta.name = name;
    meta.type = type;
    meta.range.start = start;
    meta.range.end = end;
    return meta;
}

static bool ioa_in_range(IoaRange range, int ioa)
{
    return ioa >= range.start && ioa <= range.end;
}

static size_t range_capacity(IoaRange range)
{
    if (range.end < range.start)
        return 0;

    return (size_t)(range.end - range.start + 1);
}

static void init_yc_point(YcPoint* point, int ioa, float value, YC_IECType type, bool periodic)
{
    point->ioa = (uint32_t)ioa;
    snprintf(point->name, sizeof(point->name), "YC_%05X", (unsigned)point->ioa);
    point->iec_type = type;
    point->raw_type = RAW_TYPE_FLOAT32;
    point->value = value;
    point->last_sent_value = value;
    point->quality = IEC60870_QUALITY_GOOD;
    point->deadband = 0.1f;
    point->scale = 1.0f;
    point->offset = 0.0f;
    point->full_scale = 1.0f;
    point->enable_spontaneous = true;
    point->enable_periodic = periodic;
    point->timestamp_ms = initial_time_ms();
}

static YcPoint* find_yc_in_partition(YcPoint* points, size_t count, int ioa)
{
    for (size_t i = 0; i < count; i++)
        if (points[i].ioa == (uint32_t)ioa)
            return &points[i];
    return NULL;
}

bool point_table_init_demo(PointTable* table)
{
    memset(table, 0, sizeof(*table));

    if (!rwlock_init(&table->lock))
        return false;

    table->yx_meta = make_meta("yx_table", POINT_TYPE_YX, IOA_YX_START, IOA_YX_END);
    table->yc_meta = make_meta("yc_table", POINT_TYPE_YC, IOA_YC_START, IOA_YC_END);
    table->yk_meta = make_meta("yk_table", POINT_TYPE_YK, IOA_YK_START, IOA_YK_END);
    table->yt_meta = make_meta("yt_table", POINT_TYPE_YT, IOA_YT_START, IOA_YT_END);
    table->dd_meta = make_meta("dd_table", POINT_TYPE_DD, IOA_DD_START, IOA_DD_END);
    table->yc_rtu_meta = make_meta("yc_rtu_table", POINT_TYPE_YC_RTU, IOA_YC_RTU_START, IOA_YC_RTU_END);
    table->yc_sensor_conf_meta = make_meta("yc_sensor_conf_table", POINT_TYPE_YC_SENSOR_CONF,
                                           IOA_YC_SENSOR_CONF_START, IOA_YC_SENSOR_CONF_END);
    table->yc_custom_meta = make_meta("yc_custom_table", POINT_TYPE_YC_CUSTOM,
                                      IOA_YC_CUSTOM_START, IOA_YC_CUSTOM_END);

    table->yx_count = range_capacity(table->yx_meta.range);
    table->yc_count = range_capacity(table->yc_meta.range);
    table->yk_count = range_capacity(table->yk_meta.range);
    table->yt_count = range_capacity(table->yt_meta.range);
    table->dd_count = range_capacity(table->dd_meta.range);
    table->yc_rtu_count = range_capacity(table->yc_rtu_meta.range);
    table->yc_sensor_conf_count = range_capacity(table->yc_sensor_conf_meta.range);
    table->yc_custom_count = range_capacity(table->yc_custom_meta.range);

    table->yx = (YxPoint*)calloc(table->yx_count, sizeof(YxPoint));
    table->yc = (YcPoint*)calloc(table->yc_count, sizeof(YcPoint));
    table->yk = (YkPoint*)calloc(table->yk_count, sizeof(YkPoint));
    table->yt = (YtPoint*)calloc(table->yt_count, sizeof(YtPoint));
    table->dd = (DdPoint*)calloc(table->dd_count, sizeof(DdPoint));
    table->yc_rtu = (YcPoint*)calloc(table->yc_rtu_count, sizeof(YcPoint));
    table->yc_sensor_conf = (YcPoint*)calloc(table->yc_sensor_conf_count, sizeof(YcPoint));
    table->yc_custom = (YcPoint*)calloc(table->yc_custom_count, sizeof(YcPoint));

    if (!table->yx || !table->yc || !table->yk || !table->yt || !table->dd ||
        !table->yc_rtu || !table->yc_sensor_conf || !table->yc_custom) {
        point_table_destroy(table);
        return false;
    }

    for (size_t i = 0; i < table->yx_count; i++) {
        table->yx[i].ioa = (uint32_t)(IOA_YX_START + (int)i);
        snprintf(table->yx[i].name, sizeof(table->yx[i].name), "YX_%05X", (unsigned)table->yx[i].ioa);
        table->yx[i].type = 0;
        table->yx[i].value = (uint8_t)((i % 2) != 0);
        table->yx[i].last_value = table->yx[i].value;
        table->yx[i].quality = IEC60870_QUALITY_GOOD;
        set_cp56_time(&table->yx[i].last_change_time, initial_time_ms());
        table->yx[i].enable_soe = true;
        table->yx[i].enable_spontaneous = true;
        table->yx[i].timestamp_ms = initial_time_ms();
    }

    for (size_t i = 0; i < table->yc_count; i++) {
        init_yc_point(&table->yc[i], IOA_YC_START + (int)i, 10.0f + (float)i,
                      YC_IEC_TYPE_SCALED, true);
    }

    for (size_t i = 0; i < table->yc_rtu_count; i++) {
        init_yc_point(&table->yc_rtu[i], IOA_YC_RTU_START + (int)i, 100.0f + (float)i,
                      YC_IEC_TYPE_SCALED, false);
    }

    for (size_t i = 0; i < table->yc_sensor_conf_count; i++) {
        init_yc_point(&table->yc_sensor_conf[i], IOA_YC_SENSOR_CONF_START + (int)i,
                      200.0f + (float)i, YC_IEC_TYPE_SCALED, false);
    }

    for (size_t i = 0; i < table->yc_custom_count; i++) {
        init_yc_point(&table->yc_custom[i], IOA_YC_CUSTOM_START + (int)i,
                      500.0f + (float)i, YC_IEC_TYPE_SCALED, false);
    }

    for (size_t i = 0; i < table->yk_count; i++) {
        table->yk[i].ioa = (uint32_t)(IOA_YK_START + (int)i);
        snprintf(table->yk[i].name, sizeof(table->yk[i].name), "YK_%05X", (unsigned)table->yk[i].ioa);
        table->yk[i].iec_type = YK_IEC_TYPE_SINGLE;
        table->yk[i].raw_type = RAW_TYPE_BOOL;
        table->yk[i].state = 0;
        table->yk[i].selected_value = 0;
        table->yk[i].select_state = 0;
        table->yk[i].select_timeout = SELECT_TIMEOUT_MS;
    }

    for (size_t i = 0; i < table->yt_count; i++) {
        table->yt[i].ioa = (uint32_t)(IOA_YT_START + (int)i);
        snprintf(table->yt[i].name, sizeof(table->yt[i].name), "YT_%05X", (unsigned)table->yt[i].ioa);
        table->yt[i].iec_type = YT_IEC_TYPE_SCALED;
        table->yt[i].raw_type = RAW_TYPE_INT16;
        table->yt[i].value = 0.0f;
        table->yt[i].selected_value = 0.0f;
        table->yt[i].min_value = -10000.0f;
        table->yt[i].max_value = 10000.0f;
        table->yt[i].quality = IEC60870_QUALITY_GOOD;
        table->yt[i].select_state = 0;
        table->yt[i].select_timeout = SELECT_TIMEOUT_MS;
        table->yt[i].scale = 1.0f;
        table->yt[i].offset = 0.0f;
        table->yt[i].full_scale = 1.0f;
    }

    for (size_t i = 0; i < table->dd_count; i++) {
        table->dd[i].ioa = (uint32_t)(IOA_DD_START + (int)i);
        snprintf(table->dd[i].name, sizeof(table->dd[i].name), "DD_%05X", (unsigned)table->dd[i].ioa);
        table->dd[i].iec_type = DD_IEC_TYPE_COUNTER_CP56;
        table->dd[i].raw_type = RAW_TYPE_INT32;
        table->dd[i].value = 1000 + (int32_t)(100 * i);
        table->dd[i].frozen_value = table->dd[i].value;
        table->dd[i].quality = IEC60870_QUALITY_GOOD;
        table->dd[i].seq = 0;
        set_cp56_time(&table->dd[i].freeze_time, initial_time_ms());
        table->dd[i].last_sent_value = table->dd[i].value;
        table->dd[i].enable_periodic = true;
        table->dd[i].timestamp_ms = initial_time_ms();
    }

    return true;
}

void point_table_destroy(PointTable* table)
{
    if (!table)
        return;

    free(table->yx);
    free(table->yc);
    free(table->yk);
    free(table->yt);
    free(table->dd);
    free(table->yc_rtu);
    free(table->yc_sensor_conf);
    free(table->yc_custom);
    rwlock_destroy(&table->lock);
    memset(table, 0, sizeof(*table));
}

void point_table_read_lock(PointTable* table)
{
    Semaphore_wait(table->lock.mutex);
    table->lock.readers++;
    if (table->lock.readers == 1)
        Semaphore_wait(table->lock.resource);
    Semaphore_post(table->lock.mutex);
}

void point_table_read_unlock(PointTable* table)
{
    Semaphore_wait(table->lock.mutex);
    table->lock.readers--;
    if (table->lock.readers == 0)
        Semaphore_post(table->lock.resource);
    Semaphore_post(table->lock.mutex);
}

void point_table_write_lock(PointTable* table)
{
    Semaphore_wait(table->lock.resource);
}

void point_table_write_unlock(PointTable* table)
{
    Semaphore_post(table->lock.resource);
}

static bool copy_points(void** dest, const void* src, size_t count, size_t element_size)
{
    *dest = NULL;

    if (count == 0)
        return true;

    *dest = calloc(count, element_size);
    if (!*dest)
        return false;

    memcpy(*dest, src, count * element_size);
    return true;
}

bool point_table_snapshot_create(PointTable* table, PointTableSnapshot* snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));

    point_table_read_lock(table);

    snapshot->yx_meta = table->yx_meta;
    snapshot->yc_meta = table->yc_meta;
    snapshot->yk_meta = table->yk_meta;
    snapshot->yt_meta = table->yt_meta;
    snapshot->dd_meta = table->dd_meta;
    snapshot->yc_rtu_meta = table->yc_rtu_meta;
    snapshot->yc_sensor_conf_meta = table->yc_sensor_conf_meta;
    snapshot->yc_custom_meta = table->yc_custom_meta;

    snapshot->yx_count = table->yx_count;
    snapshot->yc_count = table->yc_count;
    snapshot->yk_count = table->yk_count;
    snapshot->yt_count = table->yt_count;
    snapshot->dd_count = table->dd_count;
    snapshot->yc_rtu_count = table->yc_rtu_count;
    snapshot->yc_sensor_conf_count = table->yc_sensor_conf_count;
    snapshot->yc_custom_count = table->yc_custom_count;

    bool ok =
        copy_points((void**)&snapshot->yx, table->yx, snapshot->yx_count, sizeof(YxPoint)) &&
        copy_points((void**)&snapshot->yc, table->yc, snapshot->yc_count, sizeof(YcPoint)) &&
        copy_points((void**)&snapshot->yk, table->yk, snapshot->yk_count, sizeof(YkPoint)) &&
        copy_points((void**)&snapshot->yt, table->yt, snapshot->yt_count, sizeof(YtPoint)) &&
        copy_points((void**)&snapshot->dd, table->dd, snapshot->dd_count, sizeof(DdPoint)) &&
        copy_points((void**)&snapshot->yc_rtu, table->yc_rtu, snapshot->yc_rtu_count, sizeof(YcPoint)) &&
        copy_points((void**)&snapshot->yc_sensor_conf, table->yc_sensor_conf,
                    snapshot->yc_sensor_conf_count, sizeof(YcPoint)) &&
        copy_points((void**)&snapshot->yc_custom, table->yc_custom,
                    snapshot->yc_custom_count, sizeof(YcPoint));

    point_table_read_unlock(table);

    if (!ok)
        point_table_snapshot_destroy(snapshot);

    return ok;
}

void point_table_snapshot_destroy(PointTableSnapshot* snapshot)
{
    if (!snapshot)
        return;

    free(snapshot->yx);
    free(snapshot->yc);
    free(snapshot->yk);
    free(snapshot->yt);
    free(snapshot->dd);
    free(snapshot->yc_rtu);
    free(snapshot->yc_sensor_conf);
    free(snapshot->yc_custom);
    memset(snapshot, 0, sizeof(*snapshot));
}

const PointPartitionMeta* point_table_get_partition(const PointTable* table, int ioa)
{
    if (ioa_in_range(table->yx_meta.range, ioa))
        return &table->yx_meta;
    if (ioa_in_range(table->yc_rtu_meta.range, ioa))
        return &table->yc_rtu_meta;
    if (ioa_in_range(table->yc_sensor_conf_meta.range, ioa))
        return &table->yc_sensor_conf_meta;
    if (ioa_in_range(table->yc_meta.range, ioa))
        return &table->yc_meta;
    if (ioa_in_range(table->yc_custom_meta.range, ioa))
        return &table->yc_custom_meta;
    if (ioa_in_range(table->yk_meta.range, ioa))
        return &table->yk_meta;
    if (ioa_in_range(table->yt_meta.range, ioa))
        return &table->yt_meta;
    if (ioa_in_range(table->dd_meta.range, ioa))
        return &table->dd_meta;

    return NULL;
}

YxPoint* point_table_find_yx(PointTable* table, int ioa)
{
    for (size_t i = 0; i < table->yx_count; i++)
        if (table->yx[i].ioa == (uint32_t)ioa)
            return &table->yx[i];
    return NULL;
}

YcPoint* point_table_find_yc(PointTable* table, int ioa)
{
    if (ioa_in_range(table->yc_meta.range, ioa))
        return find_yc_in_partition(table->yc, table->yc_count, ioa);

    if (ioa_in_range(table->yc_rtu_meta.range, ioa))
        return find_yc_in_partition(table->yc_rtu, table->yc_rtu_count, ioa);

    if (ioa_in_range(table->yc_sensor_conf_meta.range, ioa))
        return find_yc_in_partition(table->yc_sensor_conf, table->yc_sensor_conf_count, ioa);

    if (ioa_in_range(table->yc_custom_meta.range, ioa))
        return find_yc_in_partition(table->yc_custom, table->yc_custom_count, ioa);

    return NULL;
}

YkPoint* point_table_find_yk(PointTable* table, int ioa)
{
    for (size_t i = 0; i < table->yk_count; i++)
        if (table->yk[i].ioa == (uint32_t)ioa)
            return &table->yk[i];
    return NULL;
}

YtPoint* point_table_find_yt(PointTable* table, int ioa)
{
    for (size_t i = 0; i < table->yt_count; i++)
        if (table->yt[i].ioa == (uint32_t)ioa)
            return &table->yt[i];
    return NULL;
}

DdPoint* point_table_find_dd(PointTable* table, int ioa)
{
    for (size_t i = 0; i < table->dd_count; i++)
        if (table->dd[i].ioa == (uint32_t)ioa)
            return &table->dd[i];
    return NULL;
}

bool point_table_set_yx(PointTable* table, int ioa, bool value, uint64_t timestamp_ms)
{
    point_table_write_lock(table);

    YxPoint* point = point_table_find_yx(table, ioa);
    if (!point) {
        point_table_write_unlock(table);
        return false;
    }

    point->last_value = point->value;
    point->value = (uint8_t)(value ? 1 : 0);
    point->timestamp_ms = timestamp_ms;
    if (point->last_value != point->value)
        set_cp56_time(&point->last_change_time, timestamp_ms);
    point_table_write_unlock(table);
    return true;
}

bool point_table_set_yc(PointTable* table, int ioa, float value, uint64_t timestamp_ms)
{
    point_table_write_lock(table);

    YcPoint* point = point_table_find_yc(table, ioa);
    if (!point) {
        point_table_write_unlock(table);
        return false;
    }

    point->value = value;
    point->timestamp_ms = timestamp_ms;
    point_table_write_unlock(table);
    return true;
}

bool point_table_set_yk(PointTable* table, int ioa, bool state)
{
    point_table_write_lock(table);

    YkPoint* point = point_table_find_yk(table, ioa);
    if (!point) {
        point_table_write_unlock(table);
        return false;
    }

    point->state = (uint8_t)(state ? 1 : 0);
    point_table_write_unlock(table);
    return true;
}

bool point_table_set_yt(PointTable* table, int ioa, float value)
{
    point_table_write_lock(table);

    YtPoint* point = point_table_find_yt(table, ioa);
    if (!point) {
        point_table_write_unlock(table);
        return false;
    }

    if (value < point->min_value || value > point->max_value) {
        point_table_write_unlock(table);
        return false;
    }

    point->value = value;
    point_table_write_unlock(table);
    return true;
}

bool point_table_mark_yx_reported(PointTable* table, int ioa, bool value)
{
    point_table_write_lock(table);

    YxPoint* point = point_table_find_yx(table, ioa);
    if (!point) {
        point_table_write_unlock(table);
        return false;
    }

    point->last_value = (uint8_t)(value ? 1 : 0);
    point_table_write_unlock(table);
    return true;
}

bool point_table_mark_yc_reported(PointTable* table, int ioa, float value)
{
    point_table_write_lock(table);

    YcPoint* point = point_table_find_yc(table, ioa);
    if (!point) {
        point_table_write_unlock(table);
        return false;
    }

    point->last_sent_value = value;
    point_table_write_unlock(table);
    return true;
}

void point_table_simulate_scan(PointTable* table, uint64_t now_ms)
{
    point_table_write_lock(table);

    if (table->yx_count > 0) {
        uint8_t value = (uint8_t)(((now_ms / 5000) % 2) != 0);
        if (table->yx[0].value != value) {
            table->yx[0].last_value = table->yx[0].value;
            table->yx[0].value = value;
            table->yx[0].timestamp_ms = now_ms;
            set_cp56_time(&table->yx[0].last_change_time, now_ms);
        }
    }

    if (table->yc_count > 0) {
        float delta = (float)((now_ms / 1000) % 10) * 0.01f;
        table->yc[0].value = 10.0f + delta;
        table->yc[0].timestamp_ms = now_ms;
    }

    if (table->yc_custom_count > 0) {
        float delta = (float)((now_ms / 1000) % 20) * 0.05f;
        table->yc_custom[0].value = 500.0f + delta;
        table->yc_custom[0].timestamp_ms = now_ms;
    }

    if (table->dd_count > 0 && now_ms % 5000 < 1000) {
        table->dd[0].value += 1;
        table->dd[0].seq++;
        table->dd[0].timestamp_ms = now_ms;
        set_cp56_time(&table->dd[0].freeze_time, now_ms);
    }

    point_table_write_unlock(table);

    (void)fabsf(0.0f);
}
