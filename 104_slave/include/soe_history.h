#ifndef IEC104_SLAVE_SOE_HISTORY_H
#define IEC104_SLAVE_SOE_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cs101_information_objects.h"
#include "hal_thread.h"

#define SOE_HISTORY_MAX_RECORDS 4096

typedef struct {
    int ioa;
    bool value;
    QualityDescriptor quality;
    uint64_t timestamp_ms;
    uint64_t sequence;
} SoeRecord;

typedef struct {
    Semaphore lock;
    SoeRecord records[SOE_HISTORY_MAX_RECORDS];
    size_t head;
    size_t count;
    uint64_t next_sequence;
} SoeHistory;

bool soe_history_init(SoeHistory* history);
void soe_history_destroy(SoeHistory* history);
void soe_history_clear(SoeHistory* history);
void soe_history_append(SoeHistory* history, int ioa, bool value,
                        QualityDescriptor quality, uint64_t timestamp_ms);
size_t soe_history_query(SoeHistory* history, uint64_t begin_ms, uint64_t end_ms,
                         SoeRecord* records, size_t max_records);
size_t soe_history_count(SoeHistory* history);

#endif
