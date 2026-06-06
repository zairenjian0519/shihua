#ifndef IEC104_SLAVE_REPORT_SCHEDULER_H
#define IEC104_SLAVE_REPORT_SCHEDULER_H

#include <stdint.h>

#include "cs104_slave.h"
#include "point_table.h"

void report_scheduler_send_periodic(CS104_Slave slave, CS101_AppLayerParameters params,
                                    PointTable* table, int oa, int ca, uint64_t now_ms);

#endif
