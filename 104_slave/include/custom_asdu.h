#ifndef IEC104_SLAVE_CUSTOM_ASDU_H
#define IEC104_SLAVE_CUSTOM_ASDU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cs104_slave.h"
#include "point_table.h"
#include "soe_history.h"

#define CUSTOM_TYPE_MEASURE_TOTAL_CALL 0x88   /* 总召唤测量值 */
#define CUSTOM_TYPE_DYNAGRAM_CALL 0x89        /* 召唤示功图数据 */
#define CUSTOM_TYPE_ELEC_DYNAGRAM_CALL 0x8A   /* 召唤电功图数据 */
#define CUSTOM_TYPE_HISTORY_CALL 0x8B         /* 召唤历史数据 */
#define CUSTOM_TYPE_RTU_PARAM_CALL 0x8C       /* 召唤 RTU 参数 */
#define CUSTOM_TYPE_SENSOR_PARAM_CALL 0x8D    /* 召唤传感器参数 */
#define CUSTOM_TYPE_HARMONIC_CALL 0x8E        /* 召唤谐波数据 */
#define CUSTOM_TYPE_METER_TRUCK_CALL 0x8F     /* 召唤计量车数据 */
#define CUSTOM_TYPE_INJECTION_CALL 0x90       /* 召唤注采阶段数据 */
#define CUSTOM_TYPE_ALL_DYNAGRAM_CALL 0x91    /* 召唤所有功图数据 */
#define CUSTOM_TYPE_ACTIVE_POWER_CALL 0x92    /* 召唤起井有功功率 */
#define CUSTOM_TYPE_WELLHEAD_PRESSURE_CALL 0x93 /* 召唤井口回压数据 */
#define CUSTOM_TYPE_HISTORY_SOE_CALL 0x94     /* 召唤存储历史 SOE 数据 */
#define CUSTOM_TYPE_RESERVED_SENSOR_CALL 0x95 /* 召唤预留传感器数据 */

#define CUSTOM_TYPE_HISTORY_YC 0x4D
#define CUSTOM_TYPE_HISTORY_YX 0x4F
#define CUSTOM_TYPE_HISTORY_DD 0x5C
#define CUSTOM_TYPE_HISTORY_DYNAGRAM 0x5F

bool custom_asdu_is_call_type(TypeID type);
bool custom_asdu_is_history_return_type(TypeID type);
bool custom_asdu_is_history_call_type(TypeID type);

bool custom_asdu_send_private(IMasterConnection connection, CS101_AppLayerParameters params,
                              TypeID type, CS101_CauseOfTransmission cot,
                              int oa, int ca, uint32_t ioa,
                              const uint8_t* payload, size_t payload_len,
                              bool negative);
bool custom_asdu_send_ack(IMasterConnection connection, CS101_AppLayerParameters params,
                          TypeID request_type, int oa, int ca, uint32_t ioa);
bool custom_asdu_send_finish(IMasterConnection connection, CS101_AppLayerParameters params,
                             TypeID request_type, int oa, int ca, uint32_t ioa);

bool custom_asdu_send_history_yc(IMasterConnection connection, CS101_AppLayerParameters params,
                                 int oa, int ca, const YcPoint* point);
bool custom_asdu_send_history_yx(IMasterConnection connection, CS101_AppLayerParameters params,
                                 int oa, int ca, const SoeRecord* record);
bool custom_asdu_send_history_dd(IMasterConnection connection, CS101_AppLayerParameters params,
                                 int oa, int ca, const DdPoint* point);
bool custom_asdu_send_history_dynagram(IMasterConnection connection, CS101_AppLayerParameters params,
                                       int oa, int ca, uint32_t ioa,
                                       const uint8_t* payload, size_t payload_len);

#endif
