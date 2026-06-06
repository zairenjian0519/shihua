#include "active_upload.h"

#include "log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void clear_notify(ActiveUploadNotify* notify)
{
    if (notify)
        memset(notify, 0, sizeof(*notify));
}

bool active_upload_init(ActiveUploadArea* area, size_t yx_capacity,
                        size_t yc_capacity, size_t soe_capacity)
{
    memset(area, 0, sizeof(*area));
    area->yx_capacity = yx_capacity > 0 ? yx_capacity : 1;
    area->yc_capacity = yc_capacity > 0 ? yc_capacity : 1;
    area->soe_capacity = soe_capacity > 0 ? soe_capacity : 1;
    area->yx = (YxUploadEntry*)calloc(area->yx_capacity, sizeof(YxUploadEntry));
    area->yc = (YcUploadEntry*)calloc(area->yc_capacity, sizeof(YcUploadEntry));
    area->soe = (SoeUploadEntry*)calloc(area->soe_capacity, sizeof(SoeUploadEntry));
    area->lock = Semaphore_create(1);

    if (!area->yx || !area->yc || !area->soe || !area->lock) {
        active_upload_destroy(area);
        return false;
    }

    return true;
}

void active_upload_destroy(ActiveUploadArea* area)
{
    if (!area)
        return;

    if (area->lock)
        Semaphore_destroy(area->lock);

    free(area->yx);
    free(area->yc);
    free(area->soe);
    memset(area, 0, sizeof(*area));
}

uint64_t active_upload_get_version(ActiveUploadArea* area)
{
    uint64_t version;

    Semaphore_wait(area->lock);
    version = area->version;
    Semaphore_post(area->lock);

    return version;
}

void active_upload_clear(ActiveUploadArea* area)
{
    Semaphore_wait(area->lock);
    memset(area->yx, 0, area->yx_capacity * sizeof(YxUploadEntry));
    memset(area->yc, 0, area->yc_capacity * sizeof(YcUploadEntry));
    memset(area->soe, 0, area->soe_capacity * sizeof(SoeUploadEntry));
    area->version++;
    area->soe_sequence = 0;
    area->soe_next = 0;
    Semaphore_post(area->lock);
}

static bool put_yx_locked(ActiveUploadArea* area, const YxPoint* point)
{
    YxUploadEntry* empty = NULL;

    for (size_t i = 0; i < area->yx_capacity; i++) {
        if (area->yx[i].state == UPLOAD_STATE_PENDING && area->yx[i].ioa == point->ioa) {
            area->yx[i].value = point->value;
            area->yx[i].quality = point->quality;
            area->yx[i].timestamp_ms = point->timestamp_ms;
            area->yx[i].version = ++area->version;
            return true;
        }

        if (!empty && area->yx[i].state == UPLOAD_STATE_EMPTY)
            empty = &area->yx[i];
    }

    if (!empty)
        return false;

    empty->ioa = point->ioa;
    empty->value = point->value;
    empty->quality = point->quality;
    empty->timestamp_ms = point->timestamp_ms;
    empty->state = UPLOAD_STATE_PENDING;
    empty->version = ++area->version;
    return true;
}

static bool put_yc_locked(ActiveUploadArea* area, const YcPoint* point,
                          CS101_CauseOfTransmission cot)
{
    YcUploadEntry* empty = NULL;

    for (size_t i = 0; i < area->yc_capacity; i++) {
        if (area->yc[i].state == UPLOAD_STATE_PENDING && area->yc[i].ioa == point->ioa) {
            area->yc[i].value = point->value;
            area->yc[i].quality = point->quality;
            area->yc[i].iec_type = point->iec_type;
            area->yc[i].cot = cot;
            area->yc[i].timestamp_ms = point->timestamp_ms;
            area->yc[i].version = ++area->version;
            return true;
        }

        if (!empty && area->yc[i].state == UPLOAD_STATE_EMPTY)
            empty = &area->yc[i];
    }

    if (!empty)
        return false;

    empty->ioa = point->ioa;
    empty->value = point->value;
    empty->quality = point->quality;
    empty->iec_type = point->iec_type;
    empty->cot = cot;
    empty->timestamp_ms = point->timestamp_ms;
    empty->state = UPLOAD_STATE_PENDING;
    empty->version = ++area->version;
    return true;
}

static bool put_soe_locked(ActiveUploadArea* area, const YxPoint* point)
{
    SoeUploadEntry* entry = &area->soe[area->soe_next];

    entry->ioa = point->ioa;
    entry->value = point->value;
    entry->quality = point->quality;
    entry->timestamp_ms = point->timestamp_ms;
    entry->sequence = ++area->soe_sequence;
    entry->version = ++area->version;
    entry->state = UPLOAD_STATE_PENDING;

    area->soe_next = (area->soe_next + 1) % area->soe_capacity;
    return true;
}

bool active_upload_put_yx(ActiveUploadArea* area, const YxPoint* point, bool with_soe)
{
    bool ok;

    Semaphore_wait(area->lock);
    ok = put_yx_locked(area, point);
    if (ok && with_soe)
        ok = put_soe_locked(area, point);
    Semaphore_post(area->lock);

    return ok;
}

bool active_upload_put_yc(ActiveUploadArea* area, const YcPoint* point,
                          CS101_CauseOfTransmission cot)
{
    bool ok;

    Semaphore_wait(area->lock);
    ok = put_yc_locked(area, point, cot);
    Semaphore_post(area->lock);

    return ok;
}

static bool publish_yx_change(ActiveUploadArea* area, const YxPoint* point,
                              bool* has_yx, bool* has_soe)
{
    bool ok;

    ok = active_upload_put_yx(area, point, point->enable_soe);

    if (ok) {
        *has_yx = true;
        if (point->enable_soe)
            *has_soe = true;
    }

    return ok;
}

static bool publish_yc_change(ActiveUploadArea* area, const YcPoint* point,
                              CS101_CauseOfTransmission cot, bool* has_yc)
{
    bool ok;

    ok = active_upload_put_yc(area, point, cot);

    if (ok)
        *has_yc = true;

    return ok;
}

bool active_upload_scan_point_table(ActiveUploadArea* area, PointTable* table,
                                    uint64_t now_ms, bool force_periodic,
                                    ActiveUploadNotify* notify)
{
    PointTableSnapshot snapshot;
    bool has_yx = false;
    bool has_yc = false;
    bool has_soe = false;

    clear_notify(notify);

    if (!point_table_snapshot_create(table, &snapshot)) {
        LOG_ERROR("upload", "failed to create point table snapshot for active upload");
        return false;
    }

    for (size_t i = 0; i < snapshot.yx_count; i++) {
        YxPoint* point = &snapshot.yx[i];

        if (!point->enable_spontaneous || point->value == point->last_value)
            continue;

        if (point->timestamp_ms == 0)
            point->timestamp_ms = now_ms;

        if (publish_yx_change(area, point, &has_yx, &has_soe))
            point_table_mark_yx_reported(table, point->ioa, point->value);
        else
            LOG_WARN("upload", "yx active upload area full ioa=%d", point->ioa);
    }

    for (size_t i = 0; i < snapshot.yc_count; i++) {
        YcPoint* point = &snapshot.yc[i];
        float delta = fabsf(point->value - point->last_sent_value);
        CS101_CauseOfTransmission cot = CS101_COT_PERIODIC;
        bool need_upload = false;

        if (point->enable_spontaneous && delta >= point->deadband) {
            cot = CS101_COT_SPONTANEOUS;
            need_upload = true;
        }
        else if (force_periodic && point->enable_periodic) {
            cot = CS101_COT_PERIODIC;
            need_upload = true;
        }

        if (!need_upload)
            continue;

        if (point->timestamp_ms == 0)
            point->timestamp_ms = now_ms;

        if (publish_yc_change(area, point, cot, &has_yc))
            point_table_mark_yc_reported(table, point->ioa, point->value);
        else
            LOG_WARN("upload", "yc active upload area full ioa=%d", point->ioa);
    }

    point_table_snapshot_destroy(&snapshot);

    if (notify) {
        notify->has_yx = has_yx;
        notify->has_yc = has_yc;
        notify->has_soe = has_soe;
        notify->upload_version = active_upload_get_version(area);
    }

    return has_yx || has_yc || has_soe;
}

static size_t count_yx_after(ActiveUploadArea* area, uint64_t after_version)
{
    size_t count = 0;

    for (size_t i = 0; i < area->yx_capacity; i++)
        if (area->yx[i].state == UPLOAD_STATE_PENDING && area->yx[i].version > after_version)
            count++;

    return count;
}

static size_t count_yc_after(ActiveUploadArea* area, uint64_t after_version)
{
    size_t count = 0;

    for (size_t i = 0; i < area->yc_capacity; i++)
        if (area->yc[i].state == UPLOAD_STATE_PENDING && area->yc[i].version > after_version)
            count++;

    return count;
}

static size_t count_soe_after(ActiveUploadArea* area, uint64_t after_version)
{
    size_t count = 0;

    for (size_t i = 0; i < area->soe_capacity; i++)
        if (area->soe[i].state == UPLOAD_STATE_PENDING && area->soe[i].version > after_version)
            count++;

    return count;
}

bool active_upload_snapshot_create(ActiveUploadArea* area, uint64_t after_version,
                                   ActiveUploadSnapshot* snapshot)
{
    size_t yx_index = 0;
    size_t yc_index = 0;
    size_t soe_index = 0;

    memset(snapshot, 0, sizeof(*snapshot));

    Semaphore_wait(area->lock);

    snapshot->version = area->version;
    snapshot->yx_count = count_yx_after(area, after_version);
    snapshot->yc_count = count_yc_after(area, after_version);
    snapshot->soe_count = count_soe_after(area, after_version);

    if (snapshot->yx_count > 0) {
        snapshot->yx = (YxUploadEntry*)calloc(snapshot->yx_count, sizeof(YxUploadEntry));
        if (!snapshot->yx)
            goto fail;
    }

    if (snapshot->yc_count > 0) {
        snapshot->yc = (YcUploadEntry*)calloc(snapshot->yc_count, sizeof(YcUploadEntry));
        if (!snapshot->yc)
            goto fail;
    }

    if (snapshot->soe_count > 0) {
        snapshot->soe = (SoeUploadEntry*)calloc(snapshot->soe_count, sizeof(SoeUploadEntry));
        if (!snapshot->soe)
            goto fail;
    }

    for (size_t i = 0; i < area->yx_capacity; i++)
        if (area->yx[i].state == UPLOAD_STATE_PENDING && area->yx[i].version > after_version)
            snapshot->yx[yx_index++] = area->yx[i];

    for (size_t i = 0; i < area->yc_capacity; i++)
        if (area->yc[i].state == UPLOAD_STATE_PENDING && area->yc[i].version > after_version)
            snapshot->yc[yc_index++] = area->yc[i];

    for (size_t i = 0; i < area->soe_capacity; i++)
        if (area->soe[i].state == UPLOAD_STATE_PENDING && area->soe[i].version > after_version)
            snapshot->soe[soe_index++] = area->soe[i];

    Semaphore_post(area->lock);
    return true;

fail:
    Semaphore_post(area->lock);
    active_upload_snapshot_destroy(snapshot);
    return false;
}

void active_upload_snapshot_destroy(ActiveUploadSnapshot* snapshot)
{
    if (!snapshot)
        return;

    free(snapshot->yx);
    free(snapshot->yc);
    free(snapshot->soe);
    memset(snapshot, 0, sizeof(*snapshot));
}
