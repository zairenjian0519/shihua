#ifndef IEC104_SLAVE_ACTIVE_UPLOAD_H
#define IEC104_SLAVE_ACTIVE_UPLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cs101_information_objects.h"
#include "hal_thread.h"
#include "point_table.h"

typedef enum {
    UPLOAD_STATE_EMPTY = 0,
    UPLOAD_STATE_PENDING = 1
} UploadState;

typedef struct {
    int ioa;
    bool value;
    QualityDescriptor quality;
    uint64_t timestamp_ms;
    uint64_t version;
    UploadState state;
} YxUploadEntry;

typedef struct {
    int ioa;
    float value;
    QualityDescriptor quality;
    YC_IECType iec_type;
    CS101_CauseOfTransmission cot;
    uint64_t timestamp_ms;
    uint64_t version;
    UploadState state;
} YcUploadEntry;

typedef struct {
    int ioa;
    bool value;
    QualityDescriptor quality;
    uint64_t timestamp_ms;
    uint64_t sequence;
    uint64_t version;
    UploadState state;
} SoeUploadEntry;

typedef struct {
    uint64_t upload_version;
    bool has_yx;
    bool has_yc;
    bool has_soe;
} ActiveUploadNotify;

typedef struct {
    Semaphore lock;
    uint64_t version;
    uint64_t soe_sequence;
    size_t yx_capacity;
    size_t yc_capacity;
    size_t soe_capacity;
    size_t soe_next;
    YxUploadEntry* yx;
    YcUploadEntry* yc;
    SoeUploadEntry* soe;
} ActiveUploadArea;

typedef struct {
    uint64_t version;
    YxUploadEntry* yx;
    size_t yx_count;
    YcUploadEntry* yc;
    size_t yc_count;
    SoeUploadEntry* soe;
    size_t soe_count;
} ActiveUploadSnapshot;

bool active_upload_init(ActiveUploadArea* area, size_t yx_capacity,
                        size_t yc_capacity, size_t soe_capacity);
void active_upload_destroy(ActiveUploadArea* area);
uint64_t active_upload_get_version(ActiveUploadArea* area);
void active_upload_clear(ActiveUploadArea* area);

bool active_upload_scan_point_table(ActiveUploadArea* area, PointTable* table,
                                    uint64_t now_ms, bool force_periodic,
                                    ActiveUploadNotify* notify);
bool active_upload_put_yx(ActiveUploadArea* area, const YxPoint* point, bool with_soe);
bool active_upload_put_yc(ActiveUploadArea* area, const YcPoint* point,
                          CS101_CauseOfTransmission cot);

bool active_upload_snapshot_create(ActiveUploadArea* area, uint64_t after_version,
                                   ActiveUploadSnapshot* snapshot);
void active_upload_snapshot_destroy(ActiveUploadSnapshot* snapshot);

#endif
