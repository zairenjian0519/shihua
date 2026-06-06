#include "custom_asdu.h"

#include "hal_time.h"
#include "log.h"

#include <string.h>

static void set_timestamp(CP56Time2a timestamp, uint64_t source_ms)
{
    uint64_t value = source_ms;
    if (value == 0)
        value = Hal_getTimeInMs();

    CP56Time2a_setFromMsTimestamp(timestamp, value);
}

static void append_ioa_le(uint8_t* buffer, size_t* offset, uint32_t ioa)
{
    buffer[(*offset)++] = (uint8_t)(ioa & 0xff);
    buffer[(*offset)++] = (uint8_t)((ioa >> 8) & 0xff);
    buffer[(*offset)++] = (uint8_t)((ioa >> 16) & 0xff);
}

bool custom_asdu_is_call_type(TypeID type)
{
    return type >= CUSTOM_TYPE_MEASURE_TOTAL_CALL && type <= CUSTOM_TYPE_RESERVED_SENSOR_CALL;
}

bool custom_asdu_is_history_return_type(TypeID type)
{
    return type == CUSTOM_TYPE_HISTORY_YC ||
           type == CUSTOM_TYPE_HISTORY_YX ||
           type == CUSTOM_TYPE_HISTORY_DD ||
           type == CUSTOM_TYPE_HISTORY_DYNAGRAM;
}

bool custom_asdu_is_history_call_type(TypeID type)
{
    return type == CUSTOM_TYPE_HISTORY_CALL || type == CUSTOM_TYPE_HISTORY_SOE_CALL;
}

bool custom_asdu_send_private(IMasterConnection connection, CS101_AppLayerParameters params,
                              TypeID type, CS101_CauseOfTransmission cot,
                              int oa, int ca, uint32_t ioa,
                              const uint8_t* payload, size_t payload_len,
                              bool negative)
{
    CS101_ASDU asdu = CS101_ASDU_create(params, false, cot, oa, ca, false, negative);
    uint8_t data[230];
    size_t offset = 0;
    bool ok;

    CS101_ASDU_setTypeID(asdu, type);
    append_ioa_le(data, &offset, ioa);

    if (payload && payload_len > 0) {
        size_t max_payload = sizeof(data) - offset;
        if (payload_len > max_payload)
            payload_len = max_payload;
        memcpy(data + offset, payload, payload_len);
        offset += payload_len;
    }

    CS101_ASDU_addPayload(asdu, data, (int)offset);
    ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool custom_asdu_send_ack(IMasterConnection connection, CS101_AppLayerParameters params,
                          TypeID request_type, int oa, int ca, uint32_t ioa)
{
    uint8_t payload[1] = {0};
    return custom_asdu_send_private(connection, params, request_type,
                                    CS101_COT_ACTIVATION_CON, oa, ca, ioa,
                                    payload, sizeof(payload), false);
}

bool custom_asdu_send_finish(IMasterConnection connection, CS101_AppLayerParameters params,
                             TypeID request_type, int oa, int ca, uint32_t ioa)
{
    uint8_t payload[1] = {0};
    return custom_asdu_send_private(connection, params, request_type,
                                    CS101_COT_ACTIVATION_TERMINATION, oa, ca, ioa,
                                    payload, sizeof(payload), false);
}

bool custom_asdu_send_history_yc(IMasterConnection connection, CS101_AppLayerParameters params,
                                 int oa, int ca, const YcPoint* point)
{
    CS101_ASDU asdu = CS101_ASDU_create(params, false, CS101_COT_REQUEST, oa, ca, false, false);
    InformationObject io = NULL;
    struct sCP56Time2a timestamp;
    bool ok;

    CS101_ASDU_setTypeID(asdu, CUSTOM_TYPE_HISTORY_YC);
    set_timestamp(&timestamp, point->timestamp_ms);

    if (point->iec_type == YC_IEC_TYPE_NORMALIZED) {
        io = (InformationObject)MeasuredValueNormalizedWithCP56Time2a_create(
            NULL, point->ioa, point->value, point->quality, &timestamp);
    }
    else if (point->iec_type == YC_IEC_TYPE_SCALED) {
        io = (InformationObject)MeasuredValueScaledWithCP56Time2a_create(
            NULL, point->ioa, (int)point->value, point->quality, &timestamp);
    }
    else {
        io = (InformationObject)MeasuredValueShortWithCP56Time2a_create(
            NULL, point->ioa, point->value, point->quality, &timestamp);
    }

    CS101_ASDU_addInformationObject(asdu, io);
    InformationObject_destroy(io);
    ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool custom_asdu_send_history_yx(IMasterConnection connection, CS101_AppLayerParameters params,
                                 int oa, int ca, const SoeRecord* record)
{
    CS101_ASDU asdu = CS101_ASDU_create(params, false, CS101_COT_REQUEST, oa, ca, false, false);
    InformationObject io;
    struct sCP56Time2a timestamp;
    bool ok;

    CS101_ASDU_setTypeID(asdu, CUSTOM_TYPE_HISTORY_YX);
    set_timestamp(&timestamp, record->timestamp_ms);
    io = (InformationObject)SinglePointWithCP56Time2a_create(NULL, record->ioa,
                                                             record->value,
                                                             record->quality,
                                                             &timestamp);
    CS101_ASDU_addInformationObject(asdu, io);
    InformationObject_destroy(io);
    ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool custom_asdu_send_history_dd(IMasterConnection connection, CS101_AppLayerParameters params,
                                 int oa, int ca, const DdPoint* point)
{
    CS101_ASDU asdu = CS101_ASDU_create(params, false, CS101_COT_REQUEST, oa, ca, false, false);
    BinaryCounterReading bcr;
    InformationObject io;
    struct sCP56Time2a timestamp;
    bool ok;

    CS101_ASDU_setTypeID(asdu, CUSTOM_TYPE_HISTORY_DD);
    set_timestamp(&timestamp, point->timestamp_ms);
    bcr = BinaryCounterReading_create(NULL, point->value, point->seq,
                                      false, false,
                                      (point->quality & IEC60870_QUALITY_INVALID) != 0);
    io = (InformationObject)IntegratedTotalsWithCP56Time2a_create(NULL, point->ioa, bcr, &timestamp);
    CS101_ASDU_addInformationObject(asdu, io);
    InformationObject_destroy(io);
    BinaryCounterReading_destroy(bcr);
    ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool custom_asdu_send_history_dynagram(IMasterConnection connection, CS101_AppLayerParameters params,
                                       int oa, int ca, uint32_t ioa,
                                       const uint8_t* payload, size_t payload_len)
{
    return custom_asdu_send_private(connection, params, CUSTOM_TYPE_HISTORY_DYNAGRAM,
                                    CS101_COT_REQUEST, oa, ca, ioa,
                                    payload, payload_len, false);
}
