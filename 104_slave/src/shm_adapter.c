#include "shm_adapter.h"

#include "log.h"

#include <string.h>

bool shm_adapter_init(ShmAdapter* adapter)
{
    memset(adapter, 0, sizeof(*adapter));
    return true;
}

void shm_adapter_destroy(ShmAdapter* adapter)
{
    if (!adapter)
        return;

    shm_adapter_close(adapter);
    memset(adapter, 0, sizeof(*adapter));
}

bool shm_adapter_open(ShmAdapter* adapter, const char* name)
{
    (void)name;
    adapter->connected = true;
    LOG_INFO("shm", "shared memory adapter opened in stub mode");
    return true;
}

void shm_adapter_close(ShmAdapter* adapter)
{
    if (!adapter)
        return;

    adapter->connected = false;
}

bool shm_adapter_read_inputs(ShmAdapter* adapter, PointTable* table, uint64_t now_ms)
{
    (void)adapter;
    point_table_simulate_scan(table, now_ms);
    return true;
}

bool shm_adapter_write_yk(ShmAdapter* adapter, int ioa, bool state, uint64_t timestamp_ms)
{
    (void)timestamp_ms;

    if (!adapter || !adapter->connected)
        return false;

    adapter->last_output_version++;
    LOG_INFO("shm", "stub write yk ioa=%d state=%d", ioa, state ? 1 : 0);
    return true;
}

bool shm_adapter_write_yt(ShmAdapter* adapter, int ioa, float value, uint64_t timestamp_ms)
{
    (void)timestamp_ms;

    if (!adapter || !adapter->connected)
        return false;

    adapter->last_output_version++;
    LOG_INFO("shm", "stub write yt ioa=%d value=%.3f", ioa, value);
    return true;
}

void shm_adapter_poll(ShmAdapter* adapter, PointTable* table, uint64_t now_ms)
{
    if (!adapter || !adapter->connected)
        return;

    if (shm_adapter_read_inputs(adapter, table, now_ms))
        adapter->last_input_version++;
}
