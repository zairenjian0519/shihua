#include "soe_history.h"

#include <string.h>

bool soe_history_init(SoeHistory* history)
{
    memset(history, 0, sizeof(*history));
    history->lock = Semaphore_create(1);
    if (!history->lock)
        return false;

    history->head = 0;
    history->count = 0;
    history->next_sequence = 0;
    return true;
}

void soe_history_destroy(SoeHistory* history)
{
    if (!history)
        return;

    if (history->lock)
        Semaphore_destroy(history->lock);

    memset(history, 0, sizeof(*history));
}

void soe_history_clear(SoeHistory* history)
{
    Semaphore_wait(history->lock);
    history->head = 0;
    history->count = 0;
    history->next_sequence = 0;
    memset(history->records, 0, sizeof(history->records));
    Semaphore_post(history->lock);
}

void soe_history_append(SoeHistory* history, int ioa, bool value,
                        QualityDescriptor quality, uint64_t timestamp_ms)
{
    if (!history || !history->lock)
        return;

    Semaphore_wait(history->lock);

    history->records[history->head].ioa = ioa;
    history->records[history->head].value = value;
    history->records[history->head].quality = quality;
    history->records[history->head].timestamp_ms = timestamp_ms;
    history->records[history->head].sequence = ++history->next_sequence;
    history->head = (history->head + 1) % SOE_HISTORY_MAX_RECORDS;

    if (history->count < SOE_HISTORY_MAX_RECORDS)
        history->count++;

    Semaphore_post(history->lock);
}

size_t soe_history_query(SoeHistory* history, uint64_t begin_ms, uint64_t end_ms,
                         SoeRecord* records, size_t max_records)
{
    size_t copied = 0;

    if (!history || !history->lock || !records || max_records == 0)
        return 0;

    Semaphore_wait(history->lock);

    size_t start = (history->head + SOE_HISTORY_MAX_RECORDS - history->count) % SOE_HISTORY_MAX_RECORDS;

    for (size_t i = 0; i < history->count && copied < max_records; i++) {
        size_t index = (start + i) % SOE_HISTORY_MAX_RECORDS;
        SoeRecord* record = &history->records[index];

        if ((begin_ms == 0 || record->timestamp_ms >= begin_ms) &&
            (end_ms == 0 || record->timestamp_ms <= end_ms))
            records[copied++] = *record;
    }

    Semaphore_post(history->lock);
    return copied;
}

size_t soe_history_count(SoeHistory* history)
{
    size_t count;

    if (!history || !history->lock)
        return 0;

    Semaphore_wait(history->lock);
    count = history->count;
    Semaphore_post(history->lock);
    return count;
}
