#ifndef IEC104_SLAVE_POINT_TABLE_H
#define IEC104_SLAVE_POINT_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cs101_information_objects.h"
#include "hal_thread.h"

typedef enum {
    POINT_TYPE_YX = 0,
    POINT_TYPE_YC = 1,
    POINT_TYPE_YK = 2,
    POINT_TYPE_YT = 3,
    POINT_TYPE_DD = 4,
    POINT_TYPE_YC_RTU = 5,
    POINT_TYPE_YC_SENSOR_CONF = 6,
    POINT_TYPE_YC_CUSTOM = 7
} PointType;

typedef struct {
    int start;
    int end;
} IoaRange;

typedef struct {
    const char* name;
    PointType type;
    IoaRange range;
} PointPartitionMeta;

typedef struct {
    Semaphore mutex;
    Semaphore resource;
    int readers;
} PointTableRwLock;

typedef struct {
    uint32_t ioa;
    char name[32];
    uint8_t type;      /* 0=single point, 1=double point */
    uint8_t value;     /* SIQ/DIQ state */
    uint8_t quality;
    uint8_t last_value;
    struct sCP56Time2a last_change_time;
    bool enable_soe;
    bool enable_spontaneous;
    uint64_t timestamp_ms;
} YX_Point;

typedef enum {
    YC_IEC_TYPE_NORMALIZED = M_ME_NA_1, /* M_ME_NA_1 */
    YC_IEC_TYPE_SCALED = M_ME_NB_1,     /* M_ME_NB_1 */
    YC_IEC_TYPE_FLOAT = M_ME_NC_1       /* M_ME_NC_1 */
} YC_IECType;

typedef enum {
    YK_IEC_TYPE_SINGLE = C_SC_NA_1,     /* C_SC_NA_1 */
    YK_IEC_TYPE_DOUBLE = C_DC_NA_1      /* C_DC_NA_1 */
} YK_IECType;

typedef enum {
    YT_IEC_TYPE_NORMALIZED = C_SE_NA_1, /* C_SE_NA_1 */
    YT_IEC_TYPE_SCALED = C_SE_NB_1,     /* C_SE_NB_1 */
    YT_IEC_TYPE_FLOAT = C_SE_NC_1       /* C_SE_NC_1 */
} YT_IECType;

typedef enum {
    DD_IEC_TYPE_COUNTER = M_IT_NA_1,    /* M_IT_NA_1 */
    DD_IEC_TYPE_COUNTER_CP56 = M_IT_TB_1/* M_IT_TB_1 */
} DD_IECType;

typedef enum {
    RAW_TYPE_BOOL,
    RAW_TYPE_UINT16,
    RAW_TYPE_INT16,
    RAW_TYPE_UINT32,
    RAW_TYPE_INT32,
    RAW_TYPE_FLOAT32,
    RAW_TYPE_FLOAT64
} RawDataType;

typedef struct {
    uint32_t ioa;
    char name[32];
    YC_IECType iec_type;
    RawDataType raw_type;
    float value;
    uint8_t quality;
    float deadband;
    float last_sent_value;
    float scale;
    float offset;
    float full_scale;
    bool enable_spontaneous;
    bool enable_periodic;
    uint64_t timestamp_ms;
} YC_Point;

typedef struct {
    uint32_t ioa;
    char name[32];
    YK_IECType iec_type;
    RawDataType raw_type;
    uint8_t state;
    uint8_t selected_value;
    uint8_t select_state;
    int select_timeout;
    uint64_t select_deadline_ms;
} YK_Point;

typedef struct {
    uint32_t ioa;
    char name[32];
    YT_IECType iec_type;
    RawDataType raw_type;
    float value;
    float selected_value;
    float min_value;
    float max_value;
    uint8_t quality;
    uint8_t select_state;
    int select_timeout;
    float scale;
    float offset;
    float full_scale;
    uint64_t select_deadline_ms;
} YT_Point;

typedef struct {
    uint32_t ioa;
    char name[32];
    DD_IECType iec_type;
    RawDataType raw_type;
    int32_t value;
    uint8_t quality;
    uint8_t seq;
    struct sCP56Time2a freeze_time;
    int32_t last_sent_value;
    bool enable_periodic;
    uint64_t timestamp_ms;
} DD_Point;

typedef YX_Point YxPoint;
typedef YC_Point YcPoint;
typedef YK_Point YkPoint;
typedef YT_Point YtPoint;
typedef DD_Point DdPoint;

typedef struct {
    PointTableRwLock lock;

    PointPartitionMeta yx_meta;
    YxPoint* yx;
    size_t yx_count;

    PointPartitionMeta yc_meta;
    YcPoint* yc;
    size_t yc_count;

    PointPartitionMeta yk_meta;
    YkPoint* yk;
    size_t yk_count;

    PointPartitionMeta yt_meta;
    YtPoint* yt;
    size_t yt_count;

    PointPartitionMeta dd_meta;
    DdPoint* dd;
    size_t dd_count;

    PointPartitionMeta yc_rtu_meta;
    YcPoint* yc_rtu;
    size_t yc_rtu_count;

    PointPartitionMeta yc_sensor_conf_meta;
    YcPoint* yc_sensor_conf;
    size_t yc_sensor_conf_count;

    PointPartitionMeta yc_custom_meta;
    YcPoint* yc_custom;
    size_t yc_custom_count;
} PointTable;

typedef struct {
    PointPartitionMeta yx_meta;
    YxPoint* yx;
    size_t yx_count;

    PointPartitionMeta yc_meta;
    YcPoint* yc;
    size_t yc_count;

    PointPartitionMeta yk_meta;
    YkPoint* yk;
    size_t yk_count;

    PointPartitionMeta yt_meta;
    YtPoint* yt;
    size_t yt_count;

    PointPartitionMeta dd_meta;
    DdPoint* dd;
    size_t dd_count;

    PointPartitionMeta yc_rtu_meta;
    YcPoint* yc_rtu;
    size_t yc_rtu_count;

    PointPartitionMeta yc_sensor_conf_meta;
    YcPoint* yc_sensor_conf;
    size_t yc_sensor_conf_count;

    PointPartitionMeta yc_custom_meta;
    YcPoint* yc_custom;
    size_t yc_custom_count;
} PointTableSnapshot;

bool point_table_init_demo(PointTable* table);
void point_table_destroy(PointTable* table);

void point_table_read_lock(PointTable* table);
void point_table_read_unlock(PointTable* table);
void point_table_write_lock(PointTable* table);
void point_table_write_unlock(PointTable* table);

bool point_table_snapshot_create(PointTable* table, PointTableSnapshot* snapshot);
void point_table_snapshot_destroy(PointTableSnapshot* snapshot);

const PointPartitionMeta* point_table_get_partition(const PointTable* table, int ioa);

YxPoint* point_table_find_yx(PointTable* table, int ioa);
YcPoint* point_table_find_yc(PointTable* table, int ioa);
YkPoint* point_table_find_yk(PointTable* table, int ioa);
YtPoint* point_table_find_yt(PointTable* table, int ioa);
DdPoint* point_table_find_dd(PointTable* table, int ioa);

bool point_table_set_yx(PointTable* table, int ioa, bool value, uint64_t timestamp_ms);
bool point_table_set_yc(PointTable* table, int ioa, float value, uint64_t timestamp_ms);
bool point_table_set_yk(PointTable* table, int ioa, bool state);
bool point_table_set_yt(PointTable* table, int ioa, float value);
bool point_table_mark_yx_reported(PointTable* table, int ioa, bool value);
bool point_table_mark_yc_reported(PointTable* table, int ioa, float value);

void point_table_simulate_scan(PointTable* table, uint64_t now_ms);

#endif
