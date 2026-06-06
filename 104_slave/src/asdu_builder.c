#include "asdu_builder.h"

#include "log.h"

#include "hal_time.h"

static void set_timestamp(CP56Time2a timestamp, uint64_t source_ms)
{
    uint64_t value = source_ms;
    if (value == 0)
        value = Hal_getTimeInMs();
    CP56Time2a_setFromMsTimestamp(timestamp, value);
}

bool asdu_send_yx(IMasterConnection connection, CS101_AppLayerParameters params,
                  int oa, int ca, CS101_CauseOfTransmission cot, const YxPoint* point,
                  bool with_timestamp)
{
    CS101_ASDU asdu = CS101_ASDU_create(params, true, cot, oa, ca, false, false);
    InformationObject io = NULL;

    if (with_timestamp) {
        struct sCP56Time2a timestamp;
        set_timestamp(&timestamp, point->timestamp_ms);
        io = (InformationObject)SinglePointWithCP56Time2a_create(NULL, point->ioa, point->value,
                                                                 point->quality, &timestamp);
    }
    else {
        io = (InformationObject)SinglePointInformation_create(NULL, point->ioa, point->value,
                                                             point->quality);
    }

    CS101_ASDU_addInformationObject(asdu, io);
    InformationObject_destroy(io);

    bool ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool asdu_send_yx_batch(IMasterConnection connection, CS101_AppLayerParameters params,
                        int oa, int ca, CS101_CauseOfTransmission cot,
                        const YxPoint* points, size_t count)
{
    if (!points || count == 0)
        return true;

    CS101_ASDU asdu = CS101_ASDU_create(params, true, cot, oa, ca, false, false);

    for (size_t i = 0; i < count; i++) {
        InformationObject io = (InformationObject)SinglePointInformation_create(NULL,
                                                                                points[i].ioa,
                                                                                points[i].value,
                                                                                points[i].quality);
        CS101_ASDU_addInformationObject(asdu, io);
        InformationObject_destroy(io);
    }

    bool ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool asdu_send_yx_batch_non_sequence(IMasterConnection connection, CS101_AppLayerParameters params,
                                     int oa, int ca, CS101_CauseOfTransmission cot,
                                     const YxPoint* points, size_t count)
{
    if (!points || count == 0)
        return true;

    CS101_ASDU asdu = CS101_ASDU_create(params, false, cot, oa, ca, false, false);

    for (size_t i = 0; i < count; i++) {
        InformationObject io = (InformationObject)SinglePointInformation_create(NULL,
                                                                                points[i].ioa,
                                                                                points[i].value,
                                                                                points[i].quality);
        CS101_ASDU_addInformationObject(asdu, io);
        InformationObject_destroy(io);
    }

    bool ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool asdu_send_yx_time_batch(IMasterConnection connection, CS101_AppLayerParameters params,
                             int oa, int ca, CS101_CauseOfTransmission cot,
                             const YxPoint* points, size_t count)
{
    if (!points || count == 0)
        return true;

    CS101_ASDU asdu = CS101_ASDU_create(params, false, cot, oa, ca, false, false);

    for (size_t i = 0; i < count; i++) {
        struct sCP56Time2a timestamp;
        set_timestamp(&timestamp, points[i].timestamp_ms);
        InformationObject io = (InformationObject)SinglePointWithCP56Time2a_create(NULL,
                                                                                   points[i].ioa,
                                                                                   points[i].value,
                                                                                   points[i].quality,
                                                                                   &timestamp);
        CS101_ASDU_addInformationObject(asdu, io);
        InformationObject_destroy(io);
    }

    bool ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool asdu_send_yc(IMasterConnection connection, CS101_AppLayerParameters params,
                  int oa, int ca, CS101_CauseOfTransmission cot, const YcPoint* point)
{
    CS101_ASDU asdu = CS101_ASDU_create(params, false, cot, oa, ca, false, false);
    InformationObject io = NULL;

    if (point->iec_type == YC_IEC_TYPE_NORMALIZED) {
        io = (InformationObject)MeasuredValueNormalized_create(NULL, point->ioa, point->value, point->quality);
    }
    else if (point->iec_type == YC_IEC_TYPE_SCALED) {
        io = (InformationObject)MeasuredValueScaled_create(NULL, point->ioa, (int)point->value, point->quality);
    }
    else {
        io = (InformationObject)MeasuredValueShort_create(NULL, point->ioa, point->value, point->quality);
    }

    CS101_ASDU_addInformationObject(asdu, io);
    InformationObject_destroy(io);

    bool ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool asdu_send_yc_batch(IMasterConnection connection, CS101_AppLayerParameters params,
                        int oa, int ca, CS101_CauseOfTransmission cot,
                        const YcPoint* points, size_t count)
{
    if (!points || count == 0)
        return true;

    CS101_ASDU asdu = CS101_ASDU_create(params, true, cot, oa, ca, false, false);

    for (size_t i = 0; i < count; i++) {
        InformationObject io = NULL;

        if (points[i].iec_type == YC_IEC_TYPE_NORMALIZED) {
            io = (InformationObject)MeasuredValueNormalized_create(NULL, points[i].ioa,
                                                                   points[i].value,
                                                                   points[i].quality);
        }
        else if (points[i].iec_type == YC_IEC_TYPE_SCALED) {
            io = (InformationObject)MeasuredValueScaled_create(NULL, points[i].ioa,
                                                               (int)points[i].value,
                                                               points[i].quality);
        }
        else {
            io = (InformationObject)MeasuredValueShort_create(NULL, points[i].ioa,
                                                              points[i].value,
                                                              points[i].quality);
        }

        CS101_ASDU_addInformationObject(asdu, io);
        InformationObject_destroy(io);
    }

    bool ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool asdu_send_yc_batch_non_sequence(IMasterConnection connection, CS101_AppLayerParameters params,
                                     int oa, int ca, CS101_CauseOfTransmission cot,
                                     const YcPoint* points, size_t count)
{
    if (!points || count == 0)
        return true;

    CS101_ASDU asdu = CS101_ASDU_create(params, false, cot, oa, ca, false, false);

    for (size_t i = 0; i < count; i++) {
        InformationObject io = NULL;

        if (points[i].iec_type == YC_IEC_TYPE_NORMALIZED) {
            io = (InformationObject)MeasuredValueNormalized_create(NULL, points[i].ioa,
                                                                   points[i].value,
                                                                   points[i].quality);
        }
        else if (points[i].iec_type == YC_IEC_TYPE_SCALED) {
            io = (InformationObject)MeasuredValueScaled_create(NULL, points[i].ioa,
                                                               (int)points[i].value,
                                                               points[i].quality);
        }
        else {
            io = (InformationObject)MeasuredValueShort_create(NULL, points[i].ioa,
                                                              points[i].value,
                                                              points[i].quality);
        }

        CS101_ASDU_addInformationObject(asdu, io);
        InformationObject_destroy(io);
    }

    bool ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool asdu_send_dd(IMasterConnection connection, CS101_AppLayerParameters params,
                  int oa, int ca, CS101_CauseOfTransmission cot, const DdPoint* point,
                  bool with_timestamp)
{
    CS101_ASDU asdu = CS101_ASDU_create(params, false, cot, oa, ca, false, false);
    BinaryCounterReading bcr = BinaryCounterReading_create(NULL, point->value, point->seq,
                                                           false, false,
                                                           (point->quality & IEC60870_QUALITY_INVALID) != 0);
    InformationObject io = NULL;

    if (with_timestamp) {
        struct sCP56Time2a timestamp;
        set_timestamp(&timestamp, point->timestamp_ms);
        io = (InformationObject)IntegratedTotalsWithCP56Time2a_create(NULL, point->ioa, bcr, &timestamp);
    }
    else {
        io = (InformationObject)IntegratedTotals_create(NULL, point->ioa, bcr);
    }

    CS101_ASDU_addInformationObject(asdu, io);
    InformationObject_destroy(io);
    BinaryCounterReading_destroy(bcr);

    bool ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool asdu_send_dd_batch(IMasterConnection connection, CS101_AppLayerParameters params,
                        int oa, int ca, CS101_CauseOfTransmission cot,
                        const DdPoint* points, size_t count, bool with_timestamp)
{
    if (!points || count == 0)
        return true;

    CS101_ASDU asdu = CS101_ASDU_create(params, true, cot, oa, ca, false, false);

    for (size_t i = 0; i < count; i++) {
        BinaryCounterReading bcr = BinaryCounterReading_create(NULL, points[i].value,
                                                               points[i].seq,
                                                               false, false,
                                                               (points[i].quality & IEC60870_QUALITY_INVALID) != 0);
        InformationObject io = NULL;

        if (with_timestamp) {
            struct sCP56Time2a timestamp;
            set_timestamp(&timestamp, points[i].timestamp_ms);
            io = (InformationObject)IntegratedTotalsWithCP56Time2a_create(NULL, points[i].ioa,
                                                                          bcr, &timestamp);
        }
        else {
            io = (InformationObject)IntegratedTotals_create(NULL, points[i].ioa, bcr);
        }

        CS101_ASDU_addInformationObject(asdu, io);
        InformationObject_destroy(io);
        BinaryCounterReading_destroy(bcr);
    }

    bool ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool asdu_send_interrogation_termination(IMasterConnection connection, CS101_AppLayerParameters params,
                                         int oa, int ca, uint8_t qoi)
{
    CS101_ASDU asdu = CS101_ASDU_create(params, false, CS101_COT_ACTIVATION_TERMINATION,
                                       oa, ca, false, false);
    InformationObject io = (InformationObject)InterrogationCommand_create(NULL, 0, qoi);

    CS101_ASDU_addInformationObject(asdu, io);
    InformationObject_destroy(io);

    bool ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);
    return ok;
}

bool asdu_enqueue_periodic_yc(CS104_Slave slave, CS101_AppLayerParameters params,
                              int oa, int ca, const YcPoint* point)
{
    CS101_ASDU asdu = CS101_ASDU_create(params, false, CS101_COT_PERIODIC, oa, ca, false, false);
    InformationObject io = NULL;

    if (point->iec_type == YC_IEC_TYPE_NORMALIZED)
        io = (InformationObject)MeasuredValueNormalized_create(NULL, point->ioa, point->value, point->quality);
    else if (point->iec_type == YC_IEC_TYPE_SCALED)
        io = (InformationObject)MeasuredValueScaled_create(NULL, point->ioa, (int)point->value, point->quality);
    else
        io = (InformationObject)MeasuredValueShort_create(NULL, point->ioa, point->value, point->quality);

    CS101_ASDU_addInformationObject(asdu, io);
    InformationObject_destroy(io);
    CS104_Slave_enqueueASDU(slave, asdu);
    CS101_ASDU_destroy(asdu);
    return true;
}
