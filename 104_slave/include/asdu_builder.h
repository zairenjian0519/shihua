#ifndef IEC104_SLAVE_ASDU_BUILDER_H
#define IEC104_SLAVE_ASDU_BUILDER_H

#include <stdbool.h>

#include "cs104_slave.h"
#include "point_table.h"

bool asdu_send_yx(IMasterConnection connection, CS101_AppLayerParameters params,
                  int oa, int ca, CS101_CauseOfTransmission cot, const YxPoint* point,
                  bool with_timestamp);
bool asdu_send_yx_batch(IMasterConnection connection, CS101_AppLayerParameters params,
                        int oa, int ca, CS101_CauseOfTransmission cot,
                        const YxPoint* points, size_t count);
bool asdu_send_yx_batch_non_sequence(IMasterConnection connection, CS101_AppLayerParameters params,
                                     int oa, int ca, CS101_CauseOfTransmission cot,
                                     const YxPoint* points, size_t count);
bool asdu_send_yx_time_batch(IMasterConnection connection, CS101_AppLayerParameters params,
                             int oa, int ca, CS101_CauseOfTransmission cot,
                             const YxPoint* points, size_t count);
bool asdu_send_yc(IMasterConnection connection, CS101_AppLayerParameters params,
                  int oa, int ca, CS101_CauseOfTransmission cot, const YcPoint* point);
bool asdu_send_yc_batch(IMasterConnection connection, CS101_AppLayerParameters params,
                        int oa, int ca, CS101_CauseOfTransmission cot,
                        const YcPoint* points, size_t count);
bool asdu_send_yc_batch_non_sequence(IMasterConnection connection, CS101_AppLayerParameters params,
                                     int oa, int ca, CS101_CauseOfTransmission cot,
                                     const YcPoint* points, size_t count);
bool asdu_send_dd(IMasterConnection connection, CS101_AppLayerParameters params,
                  int oa, int ca, CS101_CauseOfTransmission cot, const DdPoint* point,
                  bool with_timestamp);
bool asdu_send_dd_batch(IMasterConnection connection, CS101_AppLayerParameters params,
                        int oa, int ca, CS101_CauseOfTransmission cot,
                        const DdPoint* points, size_t count, bool with_timestamp);
bool asdu_send_interrogation_termination(IMasterConnection connection, CS101_AppLayerParameters params,
                                         int oa, int ca, uint8_t qoi);
bool asdu_enqueue_periodic_yc(CS104_Slave slave, CS101_AppLayerParameters params,
                              int oa, int ca, const YcPoint* point);

#endif
