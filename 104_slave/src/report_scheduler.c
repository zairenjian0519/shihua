#include "report_scheduler.h"

#include "asdu_builder.h"

void report_scheduler_send_periodic(CS104_Slave slave, CS101_AppLayerParameters params,
                                    PointTable* table, int oa, int ca, uint64_t now_ms)
{
    (void)now_ms;

    PointTableSnapshot snapshot;
    if (!point_table_snapshot_create(table, &snapshot))
        return;

    for (size_t i = 0; i < snapshot.yc_count; i++) {
        if (snapshot.yc[i].enable_periodic)
            asdu_enqueue_periodic_yc(slave, params, oa, ca, &snapshot.yc[i]);
    }

    point_table_snapshot_destroy(&snapshot);
}
