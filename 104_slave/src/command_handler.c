#include "command_handler.h"

#include "hal_time.h"
#include "log.h"

static void send_command_response(IMasterConnection connection, CS101_ASDU asdu,
                                  CS101_CauseOfTransmission cot, bool negative)
{
    CS101_ASDU_setCOT(asdu, cot);
    CS101_ASDU_setNegative(asdu, negative);
    IMasterConnection_sendASDU(connection, asdu);
}

static bool handle_control_command(PointTable* table, IMasterConnection connection, CS101_ASDU asdu)
{
    TypeID type = CS101_ASDU_getTypeID(asdu);

    if (CS101_ASDU_getCOT(asdu) != CS101_COT_ACTIVATION &&
        CS101_ASDU_getCOT(asdu) != CS101_COT_DEACTIVATION) {
        send_command_response(connection, asdu, CS101_COT_UNKNOWN_COT, true);
        return true;
    }

    InformationObject io = CS101_ASDU_getElement(asdu, 0);
    if (!io) {
        send_command_response(connection, asdu, CS101_COT_UNKNOWN_IOA, true);
        return true;
    }

    int ioa = InformationObject_getObjectAddress(io);
    point_table_write_lock(table);

    YkPoint* point = point_table_find_yk(table, ioa);
    if (!point) {
        point_table_write_unlock(table);
        InformationObject_destroy(io);
        send_command_response(connection, asdu, CS101_COT_UNKNOWN_IOA, true);
        return true;
    }

    bool select = false;
    uint8_t value = 0;
    uint64_t now = Hal_getMonotonicTimeInMs();
    bool negative = false;

    if (type == C_DC_NA_1) {
        DoubleCommand command = (DoubleCommand)io;
        select = DoubleCommand_isSelect(command);
        value = (uint8_t)DoubleCommand_getState(command);
    }
    else {
        SingleCommand command = (SingleCommand)io;
        select = SingleCommand_isSelect(command);
        value = (uint8_t)(SingleCommand_getState(command) ? 1 : 0);
    }

    if ((point->iec_type == YK_IEC_TYPE_SINGLE && type != C_SC_NA_1) ||
        (point->iec_type == YK_IEC_TYPE_DOUBLE && type != C_DC_NA_1)) {
        negative = true;
    }
    else if (CS101_ASDU_getCOT(asdu) == CS101_COT_DEACTIVATION) {
        point->select_state = 0;
        point->selected_value = 0;
        point->select_deadline_ms = 0;
        LOG_INFO("control", "cancel yk ioa=%d", ioa);
    }
    else if (select) {
        point->select_state = 1;
        point->selected_value = value;
        point->select_deadline_ms = now + (uint64_t)point->select_timeout;
        LOG_INFO("control", "preset yk ioa=%d value=%u", ioa, value);
    }
    else if (!point->select_state || now > point->select_deadline_ms ||
             point->selected_value != value) {
        point->select_state = 0;
        point->selected_value = 0;
        point->select_deadline_ms = 0;
        negative = true;
    }
    else {
        point->state = value;
        point->select_state = 0;
        point->selected_value = 0;
        point->select_deadline_ms = 0;
        LOG_INFO("control", "execute yk ioa=%d value=%u", ioa, value);
    }

    point_table_write_unlock(table);
    InformationObject_destroy(io);
    send_command_response(connection, asdu,
                          CS101_ASDU_getCOT(asdu) == CS101_COT_DEACTIVATION ?
                          CS101_COT_DEACTIVATION_CON : CS101_COT_ACTIVATION_CON,
                          negative);
    return true;
}

static bool handle_setpoint_command(PointTable* table, IMasterConnection connection, CS101_ASDU asdu)
{
    TypeID type = CS101_ASDU_getTypeID(asdu);

    if (CS101_ASDU_getCOT(asdu) != CS101_COT_ACTIVATION) {
        send_command_response(connection, asdu, CS101_COT_UNKNOWN_COT, true);
        return true;
    }

    InformationObject io = CS101_ASDU_getElement(asdu, 0);
    if (!io) {
        send_command_response(connection, asdu, CS101_COT_UNKNOWN_IOA, true);
        return true;
    }

    int ioa = InformationObject_getObjectAddress(io);
    bool select = false;
    float value = 0.0f;

    if (type == C_SE_NA_1) {
        SetpointCommandNormalized command = (SetpointCommandNormalized)io;
        select = SetpointCommandNormalized_isSelect(command);
        value = SetpointCommandNormalized_getValue(command);
    }
    else if (type == C_SE_NB_1) {
        SetpointCommandScaled command = (SetpointCommandScaled)io;
        select = SetpointCommandScaled_isSelect(command);
        value = (float)SetpointCommandScaled_getValue(command);
    }
    else {
        SetpointCommandShort command = (SetpointCommandShort)io;
        select = SetpointCommandShort_isSelect(command);
        value = SetpointCommandShort_getValue(command);
    }

    uint64_t now = Hal_getMonotonicTimeInMs();
    point_table_write_lock(table);

    YtPoint* point = point_table_find_yt(table, ioa);
    if (!point) {
        point_table_write_unlock(table);
        InformationObject_destroy(io);
        send_command_response(connection, asdu, CS101_COT_UNKNOWN_IOA, true);
        return true;
    }

    if (value < point->min_value || value > point->max_value) {
        point_table_write_unlock(table);
        InformationObject_destroy(io);
        send_command_response(connection, asdu, CS101_COT_ACTIVATION_CON, true);
        return true;
    }

    if (select) {
        point->select_state = 1;
        point->selected_value = value;
        point->select_deadline_ms = now + (uint64_t)point->select_timeout;
        LOG_INFO("control", "select yt ioa=%d value=%.3f", ioa, value);
    }
    else {
        if (point->select_state && now > point->select_deadline_ms) {
            point->select_state = 0;
            point_table_write_unlock(table);
            InformationObject_destroy(io);
            send_command_response(connection, asdu, CS101_COT_ACTIVATION_CON, true);
            return true;
        }

        point->value = value;
        point->select_state = 0;
        LOG_INFO("control", "execute yt ioa=%d value=%.3f", ioa, value);
    }

    point_table_write_unlock(table);
    InformationObject_destroy(io);
    send_command_response(connection, asdu, CS101_COT_ACTIVATION_CON, false);
    return true;
}

bool command_handle_asdu(PointTable* table, IMasterConnection connection, CS101_ASDU asdu)
{
    TypeID type = CS101_ASDU_getTypeID(asdu);

    if (type == C_SC_NA_1 || type == C_DC_NA_1)
        return handle_control_command(table, connection, asdu);

    if (type == C_SE_NA_1 || type == C_SE_NB_1 || type == C_SE_NC_1)
        return handle_setpoint_command(table, connection, asdu);

    return false;
}
