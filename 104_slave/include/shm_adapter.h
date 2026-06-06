#ifndef IEC104_SLAVE_SHM_ADAPTER_H
#define IEC104_SLAVE_SHM_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#include "point_table.h"

typedef struct {
    bool connected;
    uint64_t last_input_version;
    uint64_t last_output_version;
    void* user;
} ShmAdapter;

typedef struct {
    int ioa;
    uint8_t quality;
    uint64_t timestamp_ms;
    union {
        bool yx;
        float yc;
        int32_t dd;
    } value;
} ShmInputPoint;

typedef struct {
    int ioa;
    uint64_t timestamp_ms;
    union {
        bool yk;
        float yt;
    } value;
} ShmOutputCommand;

bool shm_adapter_init(ShmAdapter* adapter);
void shm_adapter_destroy(ShmAdapter* adapter);
bool shm_adapter_open(ShmAdapter* adapter, const char* name);
void shm_adapter_close(ShmAdapter* adapter);
bool shm_adapter_read_inputs(ShmAdapter* adapter, PointTable* table, uint64_t now_ms);
bool shm_adapter_write_yk(ShmAdapter* adapter, int ioa, bool state, uint64_t timestamp_ms);
bool shm_adapter_write_yt(ShmAdapter* adapter, int ioa, float value, uint64_t timestamp_ms);
void shm_adapter_poll(ShmAdapter* adapter, PointTable* table, uint64_t now_ms);

#endif
