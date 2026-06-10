#include "iec104_server.h"

#include "active_upload.h"
#include "asdu_builder.h"
#include "client_context.h"
#include "command_handler.h"
#include "custom_asdu.h"
#include "diag_server.h"
#include "hal_thread.h"
#include "hal_time.h"
#include "history_store.h"
#include "log.h"
#include "report_scheduler.h"
#include "shm_adapter.h"

#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <time.h>
#endif

#define CLIENT_QUEUE_CAPACITY 128
#define BUSINESS_SEND_INTERVAL_MS 50
#define GI_SEND_INTERVAL_MS 50
#define GI_MAX_YX_PER_FRAME 80
#define ACTIVE_MAX_YX_PER_FRAME 60
#define ACTIVE_MAX_SOE_PER_FRAME 20
#define GI_MAX_YC_SCALED_PER_FRAME 80
#define GI_MAX_YC_SHORT_PER_FRAME 48
#define ACTIVE_MAX_YC_TIME_SCALED_PER_FRAME 18
#define ACTIVE_MAX_YC_TIME_SHORT_PER_FRAME 16
#define CI_MAX_DD_PER_FRAME 48
#define CI_MAX_DD_WITH_TIME_PER_FRAME 20
#define TOTAL_CALL_DD_SEND_INTERVAL_MS 200
#define DYNAGRAM_MAX_WORDS_PER_FRAME 116
#define HISTORY_SOE_MAX_RECORDS_PER_FRAME 20
#define DYNAGRAM_IOA_START 0x5401
#define DYNAGRAM_IOA_END 0x5800
#define ELEC_DYNAGRAM_IOA_START 0x5801
#define ELEC_DYNAGRAM_IOA_END 0x5B26
#define WELLHEAD_PRESSURE_DYNAGRAM_IOA_START 0x5B27
#define WELLHEAD_PRESSURE_DYNAGRAM_IOA_END 0x5CB6
#define RTU_PARAM_MAX_WORDS_PER_FRAME 116
#define RTU_PARAM_IOA_START 0x1001
#define RTU_PARAM_IOA_END 0x103E
#define SENSOR_PARAM_MAX_WORDS_PER_FRAME 116
#define SENSOR_PARAM_IOA_START 0x2001
#define SENSOR_PARAM_IOA_END 0x4000
#define CUSTOM_YC_WORD_MAX_PER_FRAME 116
#define METER_TRUCK_IOA_START 0x5001
#define METER_TRUCK_IOA_END 0x5100
#define INJECTION_IOA_START 0x5101
#define INJECTION_IOA_END 0x5200
#define HARMONIC_IOA_START 0x5201
#define HARMONIC_IOA_END 0x5400
#define ACTIVE_POWER_IOA_START 0x5CB7
#define ACTIVE_POWER_IOA_END 0x5FD6
#define RESERVED_SENSOR_IOA_START 0x4200
#define RESERVED_SENSOR_IOA_END 0x42AA

typedef struct {
    YxPoint yx_records[256];
    YcPoint yc_records[256];
    DdPoint dd_records[256];
    uint64_t yx_ids[256];
    uint64_t yc_ids[256];
    uint64_t dd_ids[256];
    uint64_t begin_ms;
    uint64_t end_ms;
    size_t yx_count;
    size_t yc_count;
    size_t dd_count;
} HistoryDataQueryResult;

struct Iec104Server {
    Iec104Config config;
    PointTable* table;
    CS104_Slave slave;
    CS101_AppLayerParameters al_params;
    Semaphore lock;
    ClientContext* clients;
    int client_count;
    ActiveUploadArea active_upload;
    SoeHistory soe_history;
    HistoryStore history_store;
    ShmAdapter shm;
    DiagServer diag;
    uint64_t last_soe_history_version;
    Thread shm_thread;
    Thread report_thread;
    volatile bool worker_running;
};

static volatile sig_atomic_t g_running = 1;
static uint64_t g_request_id = 0;

static void send_custom_dynagram(Iec104Server* server, ClientContext* client, const Iec104WorkMsg* msg);
static void send_custom_elec_dynagram(Iec104Server* server, ClientContext* client, const Iec104WorkMsg* msg);
static void send_custom_harmonic(Iec104Server* server, ClientContext* client, const Iec104WorkMsg* msg);
static void append_ioa_le(uint8_t* buffer, size_t* offset, uint32_t ioa);
static void append_word_le(uint8_t* buffer, size_t* offset, uint16_t value);
static void append_cp56_time(uint8_t* buffer, size_t* offset, uint64_t timestamp_ms);
static size_t history_data_total_count(const HistoryDataQueryResult* result);

static uint64_t msg_time_to_ms(CP56Time2a time)
{
    if (!time)
        return 0;

    for (int i = 0; i < 7; i++) {
        if (time->encodedValue[i] != 0)
            return CP56Time2a_toMsTimestamp(time);
    }

    return 0;
}

static bool set_system_time_ms(uint64_t timestamp_ms)
{
#if defined(_WIN32)
    (void)timestamp_ms;
    LOG_ERROR("iec104", "clock sync cannot set system time on Windows build");
    return false;
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(timestamp_ms / 1000);
    ts.tv_nsec = (long)((timestamp_ms % 1000) * 1000000ULL);

    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        LOG_ERROR("iec104", "clock sync failed to set system time errno=%d", errno);
        return false;
    }

    return true;
#endif
}

void iec104_server_request_stop(void)
{
    g_running = 0;
}

static uint64_t next_request_id(void)
{
    return ++g_request_id;
}

static size_t history_data_total_count(const HistoryDataQueryResult* result)
{
    if (!result)
        return 0;

    return result->yx_count + result->yc_count + result->dd_count;
}

static const char* custom_type_name(uint8_t type_id)
{
    switch (type_id) {
    case CUSTOM_TYPE_MEASURE_TOTAL_CALL:
        return "measure-total";
    case CUSTOM_TYPE_DYNAGRAM_CALL:
        return "dynagram";
    case CUSTOM_TYPE_ELEC_DYNAGRAM_CALL:
        return "elec-dynagram";
    case CUSTOM_TYPE_HISTORY_CALL:
        return "history";
    case CUSTOM_TYPE_RTU_PARAM_CALL:
        return "rtu-param";
    case CUSTOM_TYPE_SENSOR_PARAM_CALL:
        return "sensor-param";
    case CUSTOM_TYPE_HARMONIC_CALL:
        return "harmonic";
    case CUSTOM_TYPE_METER_TRUCK_CALL:
        return "meter-truck";
    case CUSTOM_TYPE_INJECTION_CALL:
        return "injection";
    case CUSTOM_TYPE_ALL_DYNAGRAM_CALL:
        return "all-dynagram";
    case CUSTOM_TYPE_ACTIVE_POWER_CALL:
        return "active-power";
    case CUSTOM_TYPE_WELLHEAD_PRESSURE_CALL:
        return "wellhead-pressure";
    case CUSTOM_TYPE_HISTORY_SOE_CALL:
        return "history-soe";
    case CUSTOM_TYPE_RESERVED_SENSOR_CALL:
        return "reserved-sensor";
    default:
        return "unknown";
    }
}

static void log_batch_send_result(const char* tag, const char* kind, uint64_t request_id,
                                  CS101_CauseOfTransmission cot, uint32_t start_ioa,
                                  uint32_t end_ioa, size_t count, bool ok)
{
    const char* result = ok ? "sent" : "send failed";

    if (request_id != 0) {
        if (ok) {
            LOG_INFO(tag, "%s %s request=%llu cot=0x%02x start_ioa=0x%04x end_ioa=0x%04x count=%zu",
                     kind, result, (unsigned long long)request_id, (unsigned)cot,
                     start_ioa, end_ioa, count);
        }
        else {
            LOG_WARN(tag, "%s %s request=%llu cot=0x%02x start_ioa=0x%04x end_ioa=0x%04x count=%zu",
                     kind, result, (unsigned long long)request_id, (unsigned)cot,
                     start_ioa, end_ioa, count);
        }
    }
    else {
        if (ok) {
            LOG_INFO(tag, "%s %s cot=0x%02x start_ioa=0x%04x end_ioa=0x%04x count=%zu",
                     kind, result, (unsigned)cot, start_ioa, end_ioa, count);
        }
        else {
            LOG_WARN(tag, "%s %s cot=0x%02x start_ioa=0x%04x end_ioa=0x%04x count=%zu",
                     kind, result, (unsigned)cot, start_ioa, end_ioa, count);
        }
    }
}

static ClientContext* get_client_for_connection(Iec104Server* server, IMasterConnection connection)
{
    for (int i = 0; i < server->client_count; i++)
        if (server->clients[i].connection == connection)
            return &server->clients[i];

    return NULL;
}

static ClientContext* get_free_client(Iec104Server* server)
{
    for (int i = 0; i < server->client_count; i++)
        if (!server->clients[i].connected)
            return &server->clients[i];

    return NULL;
}

static void post_active_upload_to_clients(Iec104Server* server, const ActiveUploadNotify* notify)
{
    Iec104WorkMsg msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_ACTIVE_UPLOAD;
    msg.ca = server->config.common_address;
    msg.has_yx = notify && notify->has_yx ? 1 : 0;
    msg.has_yc = notify && notify->has_yc ? 1 : 0;
    msg.has_soe = notify && notify->has_soe ? 1 : 0;
    msg.upload_version = notify ? notify->upload_version : active_upload_get_version(&server->active_upload);

    for (int i = 0; i < server->client_count; i++)
        if (client_context_is_active(&server->clients[i]))
            client_context_post(&server->clients[i], &msg, false);
}

static bool diag_notify_upload(void* owner, uint64_t upload_version)
{
    Iec104Server* server = (Iec104Server*)owner;
    ActiveUploadNotify notify;

    memset(&notify, 0, sizeof(notify));
    notify.upload_version = upload_version;
    notify.has_yx = true;
    notify.has_yc = true;
    notify.has_soe = true;
    post_active_upload_to_clients(server, &notify);
    return true;
}

static void raw_message_handler(void* parameter, IMasterConnection connection,
                                uint8_t* msg, int msg_size, bool sent)
{
    (void)parameter;
    (void)connection;

    printf("%s:", sent ? "SEND" : "RCVD");
    for (int i = 0; i < msg_size; i++)
        printf(" %02x", msg[i]);
    printf("\n");
}

static bool clock_sync_handler(void* parameter, IMasterConnection connection,
                               CS101_ASDU asdu, CP56Time2a new_time)
{
    (void)connection;
    (void)asdu;

    Iec104Server* server = (Iec104Server*)parameter;
    uint64_t timestamp = CP56Time2a_toMsTimestamp(new_time);
    LOG_INFO("iec104", "clock sync received timestamp_ms=%llu", (unsigned long long)timestamp);

    (void)server;
    if (!set_system_time_ms(timestamp))
        return false;

    CP56Time2a_setFromMsTimestamp(new_time, timestamp);
    LOG_INFO("iec104", "clock sync applied timestamp_ms=%llu", (unsigned long long)timestamp);
    return true;
}

static bool send_counter_activation_termination(Iec104Server* server, IMasterConnection connection,
                                                int oa, uint8_t qcc)
{
    CS101_ASDU term = CS101_ASDU_create(server->al_params, false,
                                        CS101_COT_ACTIVATION_TERMINATION,
                                        oa, server->config.common_address, false, false);
    InformationObject io = (InformationObject)CounterInterrogationCommand_create(NULL, 0, qcc);
    CS101_ASDU_addInformationObject(term, io);
    InformationObject_destroy(io);
    bool ok = IMasterConnection_sendASDU(connection, term);
    CS101_ASDU_destroy(term);
    return ok;
}

static void send_remote_control_response(Iec104Server* server, ClientContext* client,
                                         const Iec104WorkMsg* msg,
                                         CS101_CauseOfTransmission cot,
                                         bool negative)
{
    CS101_ASDU asdu = CS101_ASDU_create(server->al_params, false, cot,
                                       msg->oa, server->config.common_address, false, negative);
    InformationObject io = NULL;

    if (msg->type_id == C_DC_NA_1) {
        io = (InformationObject)DoubleCommand_create(NULL, (int)msg->ioa,
                                                     msg->command_state,
                                                     msg->command_select != 0,
                                                     msg->command_qualifier);
    }
    else {
        io = (InformationObject)SingleCommand_create(NULL, (int)msg->ioa,
                                                     msg->command_state != 0,
                                                     msg->command_select != 0,
                                                     msg->command_qualifier);
    }

    CS101_ASDU_addInformationObject(asdu, io);
    InformationObject_destroy(io);
    IMasterConnection_sendASDU(client->connection, asdu);
    CS101_ASDU_destroy(asdu);
}

static bool remote_control_output_state(const YkPoint* point, uint8_t command_state,
                                        bool* output_state)
{
    if (point->iec_type == YK_IEC_TYPE_DOUBLE) {
        if (command_state == IEC60870_DOUBLE_POINT_OFF) {
            *output_state = false;
            return true;
        }

        if (command_state == IEC60870_DOUBLE_POINT_ON) {
            *output_state = true;
            return true;
        }

        return false;
    }

    *output_state = command_state != 0;
    return true;
}

static void clear_yk_selection(YkPoint* point)
{
    if (!point)
        return;

    point->select_state = 0;
    point->selected_value = 0;
    point->select_deadline_ms = 0;
}

static bool remote_control_type_matches(const YkPoint* point, uint8_t type_id)
{
    return (point->iec_type == YK_IEC_TYPE_SINGLE && type_id == C_SC_NA_1) ||
           (point->iec_type == YK_IEC_TYPE_DOUBLE && type_id == C_DC_NA_1);
}

static void clear_yt_selection(YtPoint* point)
{
    if (!point)
        return;

    point->select_state = 0;
    point->selected_value = 0.0f;
    point->select_deadline_ms = 0;
}

static bool remote_adjust_type_matches(const YtPoint* point, uint8_t type_id)
{
    return (point->iec_type == YT_IEC_TYPE_NORMALIZED && type_id == C_SE_NA_1) ||
           (point->iec_type == YT_IEC_TYPE_SCALED && type_id == C_SE_NB_1) ||
           (point->iec_type == YT_IEC_TYPE_FLOAT && type_id == C_SE_NC_1);
}

static void send_setpoint_response(Iec104Server* server, ClientContext* client,
                                   const Iec104WorkMsg* msg,
                                   CS101_CauseOfTransmission cot,
                                   bool negative)
{
    CS101_ASDU asdu = CS101_ASDU_create(server->al_params, false, cot,
                                       msg->oa, server->config.common_address, false, negative);
    size_t count = msg->setpoint_count > 0 ? msg->setpoint_count : 1;

    for (size_t i = 0; i < count; i++) {
        uint32_t ioa = msg->setpoint_count > 0 ? msg->setpoints[i].ioa : msg->ioa;
        float value = msg->setpoint_count > 0 ? msg->setpoints[i].value : msg->setpoint_value;
        bool select = msg->setpoint_count > 0 ? (msg->setpoints[i].select != 0) :
                                                (msg->command_select != 0);
        int qualifier = msg->setpoint_count > 0 ? msg->setpoints[i].qualifier :
                                                   msg->command_qualifier;
        InformationObject io = NULL;

        if (msg->type_id == C_SE_NA_1) {
            io = (InformationObject)SetpointCommandNormalized_create(NULL, (int)ioa,
                                                                    value, select, qualifier);
        }
        else if (msg->type_id == C_SE_NB_1) {
            io = (InformationObject)SetpointCommandScaled_create(NULL, (int)ioa,
                                                                (int)value, select, qualifier);
        }
        else {
            io = (InformationObject)SetpointCommandShort_create(NULL, (int)ioa,
                                                               value, select, qualifier);
        }

        if (io) {
            CS101_ASDU_addInformationObject(asdu, io);
            InformationObject_destroy(io);
        }
    }

    IMasterConnection_sendASDU(client->connection, asdu);
    CS101_ASDU_destroy(asdu);
}

static void handle_remote_control_work(Iec104Server* server, ClientContext* client,
                                       const Iec104WorkMsg* msg)
{
    bool rejected = false;
    CS101_CauseOfTransmission response_cot = CS101_COT_ACTIVATION_TERMINATION;
    uint64_t now = Hal_getMonotonicTimeInMs();

    point_table_write_lock(server->table);
    YkPoint* point = point_table_find_yk(server->table, (int)msg->ioa);

    /* 1. 基础校验：点号必须存在，且单点/双点命令类型必须与点表一致。 */
    if (!point) {
        rejected = true;
        LOG_WARN("control", "reject yk ioa=%u point not found", msg->ioa);
    }
    else if (!remote_control_type_matches(point, msg->type_id)) {
        rejected = true;
        LOG_WARN("control", "reject yk ioa=%u type mismatch point_type=%u msg_type=%u",
                 msg->ioa, (unsigned)point->iec_type, (unsigned)msg->type_id);
    }

    /* 2. 主站撤销：清除选择态，并按协议返回遥控结束。 */
    else if (msg->cot == CS101_COT_DEACTIVATION) {
        clear_yk_selection(point);
        LOG_INFO("control", "cancel yk ioa=%u", msg->ioa);
    }

    /* 3. 主站选择：保存预置值和超时截止时间，并返回遥控反校。 */
    else if (msg->command_select) {
        point->select_state = 1;
        point->selected_value = msg->command_state;
        point->select_deadline_ms = now + (uint64_t)point->select_timeout;
        response_cot = CS101_COT_ACTIVATION_CON;
        LOG_INFO("control", "preset yk ioa=%u value=%u timeout_ms=%d",
                 msg->ioa, msg->command_state, point->select_timeout);
    }

    /* 4. 主站执行：必须先选择、未超时、执行值与预置值一致。 */
    else {
        if (!point->select_state) {
            rejected = true;
            LOG_WARN("control", "reject yk execute without preset ioa=%u value=%u",
                     msg->ioa, msg->command_state);
        }
        else if (now > point->select_deadline_ms) {
            clear_yk_selection(point);
            rejected = true;
            LOG_WARN("control", "reject yk execute timeout ioa=%u value=%u",
                     msg->ioa, msg->command_state);
        }
        else if (point->selected_value != msg->command_state) {
            uint8_t preset_value = point->selected_value;
            clear_yk_selection(point);
            rejected = true;
            LOG_WARN("control", "reject yk execute mismatch ioa=%u preset=%u execute=%u",
                     msg->ioa, preset_value, msg->command_state);
        }
        else {
            /* 5. 输出执行：遥控不再映射遥信点，只按命令值写硬件/共享内存。 */
            bool output_state = false;

            if (!remote_control_output_state(point, msg->command_state, &output_state)) {
                rejected = true;
                LOG_WARN("control", "reject yk ioa=%u invalid command value=%u",
                         msg->ioa, msg->command_state);
            }
            else if (!shm_adapter_write_yk(&server->shm, (int)msg->ioa, output_state, now)) {
                rejected = true;
                LOG_WARN("control", "yk hardware output failed ioa=%u value=%u",
                         msg->ioa, msg->command_state);
            }
            else {
                point->state = msg->command_state;
                clear_yk_selection(point);
            }

            if (!rejected)
                LOG_INFO("control", "execute yk ioa=%u value=%u output=%d",
                         msg->ioa, msg->command_state, output_state ? 1 : 0);
        }

        if (rejected)
            clear_yk_selection(point);
    }

    point_table_write_unlock(server->table);

    /* 7. 响应主站：选择返回0x07反校，其余执行/撤销/拒绝均返回0x0A结束。 */
    if (rejected)
        response_cot = CS101_COT_ACTIVATION_TERMINATION;

    send_remote_control_response(server, client, msg, response_cot, false);
}

static void handle_remote_adjust_work(Iec104Server* server, ClientContext* client,
                                      const Iec104WorkMsg* msg)
{
    bool rejected = false;
    CS101_CauseOfTransmission response_cot = CS101_COT_ACTIVATION_TERMINATION;
    uint64_t now = Hal_getMonotonicTimeInMs();
    size_t count = msg->setpoint_count > 0 ? msg->setpoint_count : 1;

    point_table_write_lock(server->table);

    for (size_t i = 0; i < count; i++) {
        uint32_t ioa = msg->setpoint_count > 0 ? msg->setpoints[i].ioa : msg->ioa;
        float value = msg->setpoint_count > 0 ? msg->setpoints[i].value : msg->setpoint_value;
        YtPoint* point = point_table_find_yt(server->table, (int)ioa);

        /* 1. 整帧预校验：任一点不满足条件，则整帧拒绝，不做部分选择/执行。 */
        if (!point) {
            rejected = true;
            LOG_WARN("control", "reject yt ioa=%u point not found", ioa);
            continue;
        }
        else if (!remote_adjust_type_matches(point, msg->type_id)) {
            rejected = true;
            LOG_WARN("control", "reject yt ioa=%u type mismatch point_type=%u msg_type=%u",
                     ioa, (unsigned)point->iec_type, (unsigned)msg->type_id);
            continue;
        }

        if (msg->cot == CS101_COT_DEACTIVATION)
            continue;

        if (msg->command_select) {
            if (value < point->min_value || value > point->max_value) {
                rejected = true;
                LOG_WARN("control", "reject yt select out of range ioa=%u value=%.3f min=%.3f max=%.3f",
                         ioa, value, point->min_value, point->max_value);
            }
            continue;
        }

        if (!point->select_state) {
            rejected = true;
            LOG_WARN("control", "reject yt execute without preset ioa=%u value=%.3f",
                     ioa, value);
        }
        else if (now > point->select_deadline_ms) {
            rejected = true;
            LOG_WARN("control", "reject yt execute timeout ioa=%u value=%.3f",
                     ioa, value);
        }
        else if (point->selected_value != value) {
            float preset_value = point->selected_value;
            rejected = true;
            LOG_WARN("control", "reject yt execute mismatch ioa=%u preset=%.3f execute=%.3f",
                     ioa, preset_value, value);
        }
        else if (value < point->min_value || value > point->max_value) {
            rejected = true;
            LOG_WARN("control", "reject yt execute out of range ioa=%u value=%.3f min=%.3f max=%.3f",
                     ioa, value, point->min_value, point->max_value);
        }
    }

    /* 2. 整帧拒绝时清除涉及点的选择态；通过时再统一撤销/选择/执行。 */
    if (rejected) {
        for (size_t i = 0; i < count; i++) {
            uint32_t ioa = msg->setpoint_count > 0 ? msg->setpoints[i].ioa : msg->ioa;
            YtPoint* point = point_table_find_yt(server->table, (int)ioa);
            clear_yt_selection(point);
        }
    }
    else {
        for (size_t i = 0; i < count; i++) {
            uint32_t ioa = msg->setpoint_count > 0 ? msg->setpoints[i].ioa : msg->ioa;
            float value = msg->setpoint_count > 0 ? msg->setpoints[i].value : msg->setpoint_value;
            YtPoint* point = point_table_find_yt(server->table, (int)ioa);

            if (msg->cot == CS101_COT_DEACTIVATION) {
                clear_yt_selection(point);
                LOG_INFO("control", "cancel yt ioa=%u", ioa);
            }
            else if (msg->command_select) {
                point->select_state = 1;
                point->selected_value = value;
                point->select_deadline_ms = now + (uint64_t)point->select_timeout;
                LOG_INFO("control", "select yt ioa=%u value=%.3f timeout_ms=%d",
                         ioa, value, point->select_timeout);
            }
            else if (!shm_adapter_write_yt(&server->shm, (int)ioa, value, now)) {
                rejected = true;
                clear_yt_selection(point);
                LOG_WARN("control", "yt hardware output failed ioa=%u value=%.3f",
                         ioa, value);
            }
            else {
                point->value = value;
                clear_yt_selection(point);
                LOG_INFO("control", "execute yt ioa=%u value=%.3f", ioa, value);
            }
        }
    }

    point_table_write_unlock(server->table);

    /* 3. 一帧多点响应：选择全通过返回0x07，其余执行/撤销/拒绝返回0x0A。 */
    if (rejected)
        response_cot = CS101_COT_ACTIVATION_TERMINATION;
    else if (msg->command_select && msg->cot == CS101_COT_ACTIVATION)
        response_cot = CS101_COT_ACTIVATION_CON;

    send_setpoint_response(server, client, msg, response_cot, false);
}

static void send_total_call(Iec104Server* server, ClientContext* client, const Iec104WorkMsg* msg)
{
    IMasterConnection connection = client->connection;

    if (!client_context_is_active(client))
        return;

    PointTableSnapshot snapshot;
    if (!point_table_snapshot_create(server->table, &snapshot)) {
        LOG_ERROR("client", "failed to create total call snapshot");
        return;
    }

    for (size_t i = 0; i < snapshot.yx_count && client_context_is_active(client);) {
        size_t batch = 1;
        while (i + batch < snapshot.yx_count &&
               batch < GI_MAX_YX_PER_FRAME &&
               snapshot.yx[i + batch].ioa == snapshot.yx[i].ioa + (int)batch)
            batch++;

        client_context_wait_send_interval(client, GI_SEND_INTERVAL_MS);
        bool ok = asdu_send_yx_batch(connection, server->al_params, msg->oa, server->config.common_address,
                                     CS101_COT_INTERROGATED_BY_STATION, &snapshot.yx[i], batch);
        log_batch_send_result("client", "total call yx", msg->request_id,
                              CS101_COT_INTERROGATED_BY_STATION,
                              snapshot.yx[i].ioa, snapshot.yx[i + batch - 1].ioa,
                              batch, ok);
        i += batch;
    }

    YcPoint* yc_partitions[] = {
        snapshot.yc,
        snapshot.yc_rtu,
        snapshot.yc_sensor_conf,
        snapshot.yc_custom
    };
    size_t yc_counts[] = {
        snapshot.yc_count,
        snapshot.yc_rtu_count,
        snapshot.yc_sensor_conf_count,
        snapshot.yc_custom_count
    };

    for (size_t p = 0; p < sizeof(yc_partitions) / sizeof(yc_partitions[0]); p++) {
        YcPoint* points = yc_partitions[p];
        size_t count = yc_counts[p];

        for (size_t i = 0; i < count && client_context_is_active(client);) {
            size_t max_batch = (points[i].iec_type == YC_IEC_TYPE_SCALED) ?
                               GI_MAX_YC_SCALED_PER_FRAME : GI_MAX_YC_SHORT_PER_FRAME;
            size_t batch = 1;

            while (i + batch < count &&
                   batch < max_batch &&
                   points[i + batch].iec_type == points[i].iec_type &&
                   points[i + batch].ioa == points[i].ioa + (int)batch)
                batch++;

            client_context_wait_send_interval(client, GI_SEND_INTERVAL_MS);
            bool ok = asdu_send_yc_batch(connection, server->al_params, msg->oa, server->config.common_address,
                                         CS101_COT_INTERROGATED_BY_STATION, &points[i], batch);
            log_batch_send_result("client", "total call yc", msg->request_id,
                                  CS101_COT_INTERROGATED_BY_STATION,
                                  points[i].ioa, points[i + batch - 1].ioa,
                                  batch, ok);
            i += batch;
        }
    }

      point_table_snapshot_destroy(&snapshot);

      Iec104WorkMsg custom_msg = *msg;
      custom_msg.type = MSG_CUSTOM_CALL;
      custom_msg.type_id = CUSTOM_TYPE_DYNAGRAM_CALL;
      send_custom_dynagram(server, client, &custom_msg);

      custom_msg.type_id = CUSTOM_TYPE_ELEC_DYNAGRAM_CALL;
      send_custom_elec_dynagram(server, client, &custom_msg);

      custom_msg.type_id = CUSTOM_TYPE_HARMONIC_CALL;
      send_custom_harmonic(server, client, &custom_msg);

      PointTableSnapshot dd_snapshot;
      if (point_table_snapshot_create(server->table, &dd_snapshot)) {
          size_t sent_count = 0;
          uint32_t dd_start_ioa = dd_snapshot.dd_count > 0 ? dd_snapshot.dd[0].ioa : 0;
          uint32_t dd_end_ioa = dd_snapshot.dd_count > 0 ?
                                dd_snapshot.dd[dd_snapshot.dd_count - 1].ioa : 0;

          LOG_INFO("client", "total call dd start request=%llu start_ioa=0x%04x end_ioa=0x%04x total=%zu interval_ms=%d",
                   (unsigned long long)msg->request_id, dd_start_ioa, dd_end_ioa,
                   dd_snapshot.dd_count,
                   TOTAL_CALL_DD_SEND_INTERVAL_MS);

          for (size_t i = 0; i < dd_snapshot.dd_count && client_context_is_active(client);) {
              size_t batch = 1;

              while (i + batch < dd_snapshot.dd_count &&
                     batch < CI_MAX_DD_PER_FRAME &&
                     dd_snapshot.dd[i + batch].ioa == dd_snapshot.dd[i].ioa + (int)batch)
                  batch++;

              uint32_t start_ioa = dd_snapshot.dd[i].ioa;
              uint32_t end_ioa = dd_snapshot.dd[i + batch - 1].ioa;

              client_context_wait_send_interval(client, TOTAL_CALL_DD_SEND_INTERVAL_MS);
              bool ok = asdu_send_dd_batch(connection, server->al_params, msg->oa,
                                           server->config.common_address, CS101_COT_REQUEST,
                                           &dd_snapshot.dd[i], batch, false);
              log_batch_send_result("client", "total call dd", msg->request_id,
                                    CS101_COT_REQUEST, start_ioa, end_ioa, batch, ok);
              if (!ok) {
                  LOG_WARN("client", "total call dd send failed request=%llu start_ioa=0x%04x end_ioa=0x%04x batch=%zu sent=%zu/%zu",
                           (unsigned long long)msg->request_id, start_ioa, end_ioa,
                           batch, sent_count, dd_snapshot.dd_count);
              }
              else {
                  sent_count += batch;
                  LOG_INFO("client", "total call dd sent request=%llu start_ioa=0x%04x end_ioa=0x%04x batch=%zu sent=%zu/%zu",
                           (unsigned long long)msg->request_id, start_ioa, end_ioa,
                           batch, sent_count, dd_snapshot.dd_count);
              }

              i += batch;
          }

          LOG_INFO("client", "total call dd finished request=%llu start_ioa=0x%04x end_ioa=0x%04x sent=%zu/%zu active=%d",
                   (unsigned long long)msg->request_id, dd_start_ioa, dd_end_ioa, sent_count,
                   dd_snapshot.dd_count, client_context_is_active(client) ? 1 : 0);

          point_table_snapshot_destroy(&dd_snapshot);
      }
      else {
          LOG_ERROR("client", "failed to create total call counter snapshot");
      }

      if (client_context_is_active(client)) {
        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        asdu_send_interrogation_termination(connection, server->al_params,
                                            msg->oa, server->config.common_address, msg->qoi);
    }

    LOG_INFO("client", "total call finished request=%llu", (unsigned long long)msg->request_id);
}

static void send_counter_call(Iec104Server* server, ClientContext* client, const Iec104WorkMsg* msg)
{
    IMasterConnection connection = client->connection;
    uint8_t frz = msg->qcc & 0xc0;
    bool with_timestamp = frz != IEC60870_QCC_FRZ_READ;

    if (!client_context_is_active(client))
        return;

    if (frz == IEC60870_QCC_FRZ_FREEZE_WITHOUT_RESET ||
        frz == IEC60870_QCC_FRZ_FREEZE_WITH_RESET ||
        frz == IEC60870_QCC_FRZ_COUNTER_RESET) {
        point_table_write_lock(server->table);
        for (size_t i = 0; i < server->table->dd_count; i++) {
            server->table->dd[i].timestamp_ms = Hal_getTimeInMs();
            server->table->dd[i].seq++;
            CP56Time2a_setFromMsTimestamp(&server->table->dd[i].freeze_time,
                                          server->table->dd[i].timestamp_ms);
            if (frz == IEC60870_QCC_FRZ_FREEZE_WITH_RESET ||
                frz == IEC60870_QCC_FRZ_COUNTER_RESET)
                server->table->dd[i].value = 0;
        }
        point_table_write_unlock(server->table);
    }

    PointTableSnapshot snapshot;
    if (!point_table_snapshot_create(server->table, &snapshot)) {
        LOG_ERROR("client", "failed to create counter call snapshot");
        return;
    }

    for (size_t i = 0; i < snapshot.dd_count && client_context_is_active(client);) {
        size_t max_batch = with_timestamp ? CI_MAX_DD_WITH_TIME_PER_FRAME : CI_MAX_DD_PER_FRAME;
        size_t batch = 1;

        while (i + batch < snapshot.dd_count &&
               batch < max_batch &&
               snapshot.dd[i + batch].ioa == snapshot.dd[i].ioa + (int)batch)
            batch++;

        client_context_wait_send_interval(client, GI_SEND_INTERVAL_MS);
        bool ok = asdu_send_dd_batch(connection, server->al_params, msg->oa, server->config.common_address,
                                     CS101_COT_REQUEST,
                                     &snapshot.dd[i], batch, with_timestamp);
        log_batch_send_result("client", "counter call dd", msg->request_id,
                              CS101_COT_REQUEST,
                              snapshot.dd[i].ioa, snapshot.dd[i + batch - 1].ioa,
                              batch, ok);
        i += batch;
    }

    point_table_snapshot_destroy(&snapshot);

    if (client_context_is_active(client)) {
        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        send_counter_activation_termination(server, connection, msg->oa, msg->qcc);
    }

    LOG_INFO("client", "counter call finished request=%llu", (unsigned long long)msg->request_id);
}

static void send_active_upload(Iec104Server* server, ClientContext* client, const Iec104WorkMsg* msg)
{
    IMasterConnection connection = client->connection;

    if (!client_context_is_active(client))
        return;

    if (msg->upload_version != 0 && msg->upload_version <= client->last_uploaded_version)
        return;

    ActiveUploadSnapshot snapshot;
    if (!active_upload_snapshot_create(&server->active_upload, client->last_uploaded_version, &snapshot)) {
        LOG_ERROR("client", "failed to create active upload snapshot");
        return;
    }

	for (size_t i = 0; i < snapshot.yx_count && client_context_is_active(client);) {
        YxPoint points[ACTIVE_MAX_YX_PER_FRAME];
        size_t batch = 0;

        while (i + batch < snapshot.yx_count && batch < ACTIVE_MAX_YX_PER_FRAME) {
            memset(&points[batch], 0, sizeof(points[batch]));
            points[batch].ioa = snapshot.yx[i + batch].ioa;
            points[batch].value = snapshot.yx[i + batch].value;
            points[batch].quality = snapshot.yx[i + batch].quality;
            points[batch].timestamp_ms = snapshot.yx[i + batch].timestamp_ms;
            batch++;
        }

        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        bool ok = asdu_send_yx_batch_non_sequence(connection, server->al_params, 0,
                                                  server->config.common_address,
                                                  CS101_COT_SPONTANEOUS, points, batch);
        log_batch_send_result("client", "active yx", 0, CS101_COT_SPONTANEOUS,
                              points[0].ioa, points[batch - 1].ioa, batch, ok);
        i += batch;
    }
	

    for (size_t i = 0; i < snapshot.soe_count && client_context_is_active(client);) {
        YxPoint points[ACTIVE_MAX_SOE_PER_FRAME];
        size_t batch = 0;

        while (i + batch < snapshot.soe_count && batch < ACTIVE_MAX_SOE_PER_FRAME) {
            memset(&points[batch], 0, sizeof(points[batch]));
            points[batch].ioa = snapshot.soe[i + batch].ioa;
            points[batch].value = snapshot.soe[i + batch].value;
            points[batch].quality = snapshot.soe[i + batch].quality;
            points[batch].timestamp_ms = snapshot.soe[i + batch].timestamp_ms;
            batch++;
        }

        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        bool ok = asdu_send_yx_time_batch(connection, server->al_params, 0,
                                          server->config.common_address,
                                          CS101_COT_SPONTANEOUS, points, batch);
        log_batch_send_result("client", "active soe", 0, CS101_COT_SPONTANEOUS,
                              points[0].ioa, points[batch - 1].ioa, batch, ok);
        i += batch;
    }

    
    for (size_t i = 0; i < snapshot.yc_count && client_context_is_active(client);) {
        CS101_CauseOfTransmission cot = snapshot.yc[i].cot;
        YC_IECType iec_type = snapshot.yc[i].iec_type;
        size_t max_batch = (iec_type == YC_IEC_TYPE_FLOAT) ?
                           ACTIVE_MAX_YC_TIME_SHORT_PER_FRAME : ACTIVE_MAX_YC_TIME_SCALED_PER_FRAME;
        YcPoint points[GI_MAX_YC_SCALED_PER_FRAME];
        size_t batch = 0;

        while (i + batch < snapshot.yc_count &&
               batch < max_batch &&
               snapshot.yc[i + batch].cot == cot &&
               snapshot.yc[i + batch].iec_type == iec_type) {
            memset(&points[batch], 0, sizeof(points[batch]));
            points[batch].ioa = snapshot.yc[i + batch].ioa;
            points[batch].value = snapshot.yc[i + batch].value;
            points[batch].quality = snapshot.yc[i + batch].quality;
            points[batch].iec_type = snapshot.yc[i + batch].iec_type;
            points[batch].timestamp_ms = snapshot.yc[i + batch].timestamp_ms;
            batch++;
        }

        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        bool ok = asdu_send_yc_time_batch_non_sequence(connection, server->al_params, 0,
                                                       server->config.common_address,
                                                       cot, points, batch);
        log_batch_send_result("client", "active yc", 0, cot,
                              points[0].ioa, points[batch - 1].ioa, batch, ok);
        i += batch;
    }

    client->last_uploaded_version = snapshot.version;
    active_upload_snapshot_destroy(&snapshot);
}

static void send_custom_measure_total(Iec104Server* server, ClientContext* client,
                                      const Iec104WorkMsg* msg)
{
    PointTableSnapshot snapshot;
    IMasterConnection connection = client->connection;

    LOG_INFO("custom", "start %s type=0x%02x request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             (unsigned long long)msg->request_id);

    if (!point_table_snapshot_create(server->table, &snapshot)) {
        LOG_ERROR("client", "failed to create custom measure total snapshot");
        return;
    }

    for (size_t i = 0; i < snapshot.yx_count && client_context_is_active(client);) {
        size_t batch = 1;
        while (i + batch < snapshot.yx_count &&
               batch < GI_MAX_YX_PER_FRAME &&
               snapshot.yx[i + batch].ioa == snapshot.yx[i].ioa + (int)batch)
            batch++;

        client_context_wait_send_interval(client, GI_SEND_INTERVAL_MS);
        bool ok = asdu_send_yx_batch(connection, server->al_params, msg->oa,
                                     server->config.common_address,
                                     CS101_COT_REQUEST, &snapshot.yx[i], batch);
        log_batch_send_result("custom", "measure total yx", msg->request_id,
                              CS101_COT_REQUEST,
                              snapshot.yx[i].ioa, snapshot.yx[i + batch - 1].ioa,
                              batch, ok);
        i += batch;
    }

    YcPoint* yc_partitions[] = {
        snapshot.yc,
        snapshot.yc_rtu,
        snapshot.yc_sensor_conf,
        snapshot.yc_custom
    };
    size_t yc_counts[] = {
        snapshot.yc_count,
        snapshot.yc_rtu_count,
        snapshot.yc_sensor_conf_count,
        snapshot.yc_custom_count
    };

    for (size_t p = 0; p < sizeof(yc_partitions) / sizeof(yc_partitions[0]); p++) {
        YcPoint* points = yc_partitions[p];
        size_t count = yc_counts[p];

        for (size_t i = 0; i < count && client_context_is_active(client);) {
            size_t batch = 1;
            while (i + batch < count &&
                   batch < GI_MAX_YC_SCALED_PER_FRAME &&
                   points[i + batch].ioa == points[i].ioa + (int)batch)
                batch++;

            YcPoint scaled_points[GI_MAX_YC_SCALED_PER_FRAME];
            memcpy(scaled_points, &points[i], batch * sizeof(YcPoint));
            for (size_t j = 0; j < batch; j++)
                scaled_points[j].iec_type = YC_IEC_TYPE_SCALED;

            client_context_wait_send_interval(client, GI_SEND_INTERVAL_MS);
            bool ok = asdu_send_yc_batch(connection, server->al_params, msg->oa,
                                         server->config.common_address,
                                         CS101_COT_REQUEST, scaled_points, batch);
            log_batch_send_result("custom", "measure total yc", msg->request_id,
                                  CS101_COT_REQUEST,
                                  scaled_points[0].ioa, scaled_points[batch - 1].ioa,
                                  batch, ok);
            i += batch;
        }
    }

    point_table_snapshot_destroy(&snapshot);
    LOG_INFO("custom", "data sent %s type=0x%02x request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             (unsigned long long)msg->request_id);
}

static void send_history_soe(Iec104Server* server, ClientContext* client, const Iec104WorkMsg* msg)
{
    SoeRecord records[256];
    uint64_t begin_ms = msg_time_to_ms((CP56Time2a)&msg->begin_time);
    uint64_t end_ms = msg_time_to_ms((CP56Time2a)&msg->end_time);
    size_t max_records = sizeof(records) / sizeof(records[0]);
    size_t count = 0;

    if (server->config.history_query_max_records > 0 &&
        max_records > (size_t)server->config.history_query_max_records)
        max_records = (size_t)server->config.history_query_max_records;

    if (history_store_is_enabled(&server->history_store) &&
        server->config.history_soe_enabled) {
        count = history_store_query_soe(&server->history_store, begin_ms, end_ms,
                                        records, max_records);
    }
    else {
        count = soe_history_query(&server->soe_history, begin_ms, end_ms, records, max_records);
    }

    LOG_INFO("custom", "start %s type=0x%02x begin=%llu end=%llu records=%zu request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             (unsigned long long)begin_ms, (unsigned long long)end_ms, count,
             (unsigned long long)msg->request_id);

    for (size_t i = 0; i < count && client_context_is_active(client);) {
        size_t batch = count - i;
        if (batch > HISTORY_SOE_MAX_RECORDS_PER_FRAME)
            batch = HISTORY_SOE_MAX_RECORDS_PER_FRAME;

        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        uint8_t payload[3 + HISTORY_SOE_MAX_RECORDS_PER_FRAME * (2 * 2 + 7)];
        size_t offset = 0;
        CS101_ASDU asdu;
        bool ok;

        append_ioa_le(payload, &offset, msg->ioa);
        for (size_t j = 0; j < batch; j++) {
            const SoeRecord* record = &records[i + j];
            uint16_t status_word = (uint16_t)((record->value ? 1u : 0u) |
                                             (((uint16_t)record->quality & 0xffu) << 8));
            append_word_le(payload, &offset, (uint16_t)(record->ioa & 0xffff));
            append_word_le(payload, &offset, status_word);
            append_cp56_time(payload, &offset, record->timestamp_ms);
        }

        asdu = CS101_ASDU_create(server->al_params, true, CS101_COT_REQUEST,
                                 msg->oa, server->config.common_address, false, false);
        CS101_ASDU_setTypeID(asdu, (TypeID)msg->type_id);
        CS101_ASDU_setNumberOfElements(asdu, (int)batch);
        CS101_ASDU_addPayload(asdu, payload, (int)offset);
        ok = IMasterConnection_sendASDU(client->connection, asdu);
        CS101_ASDU_destroy(asdu);

        LOG_DEBUG("custom", "history soe frame type=0x%02x ioa=0x%06x records=%zu ok=%d request=%llu",
                  msg->type_id, msg->ioa, batch, ok ? 1 : 0,
                  (unsigned long long)msg->request_id);
        i += batch;
    }

    LOG_INFO("custom", "data sent %s type=0x%02x records=%zu request=%llu",
             custom_type_name(msg->type_id), msg->type_id, count,
             (unsigned long long)msg->request_id);
}

static void query_history_data(Iec104Server* server, const Iec104WorkMsg* msg,
                               HistoryDataQueryResult* result)
{
    size_t max_records;

    memset(result, 0, sizeof(*result));
    result->begin_ms = msg_time_to_ms((CP56Time2a)&msg->begin_time);
    result->end_ms = msg_time_to_ms((CP56Time2a)&msg->end_time);
    max_records = sizeof(result->yx_records) / sizeof(result->yx_records[0]);

    if (history_store_is_enabled(&server->history_store)) {
        result->yx_count = history_store_query_yx_page(&server->history_store,
                                                       result->begin_ms, result->end_ms,
                                                       0, 0,
                                                       result->yx_records, result->yx_ids,
                                                       max_records);
        result->yc_count = history_store_query_yc_page(&server->history_store,
                                                       result->begin_ms, result->end_ms,
                                                       0, 0,
                                                       result->yc_records, result->yc_ids,
                                                       max_records);
        result->dd_count = history_store_query_dd_page(&server->history_store,
                                                       result->begin_ms, result->end_ms,
                                                       0, 0,
                                                       result->dd_records, result->dd_ids,
                                                       max_records);
    }
}

static void send_history_data_records(Iec104Server* server, ClientContext* client,
                                      const Iec104WorkMsg* msg,
                                      const HistoryDataQueryResult* result)
{
    YxPoint yx_records[256];
    YcPoint yc_records[256];
    DdPoint dd_records[256];
    uint64_t yx_ids[256];
    uint64_t yc_ids[256];
    uint64_t dd_ids[256];
    size_t max_records = sizeof(yx_records) / sizeof(yx_records[0]);
    size_t yx_total = 0;
    size_t yc_total = 0;
    size_t dd_total = 0;
    size_t yx_count = result->yx_count;
    size_t yc_count = result->yc_count;
    size_t dd_count = result->dd_count;
    uint64_t last_yx_ts = 0;
    uint64_t last_yx_id = 0;
    uint64_t last_yc_ts = 0;
    uint64_t last_yc_id = 0;
    uint64_t last_dd_ts = 0;
    uint64_t last_dd_id = 0;

    memcpy(yx_records, result->yx_records, result->yx_count * sizeof(yx_records[0]));
    memcpy(yc_records, result->yc_records, result->yc_count * sizeof(yc_records[0]));
    memcpy(dd_records, result->dd_records, result->dd_count * sizeof(dd_records[0]));
    memcpy(yx_ids, result->yx_ids, result->yx_count * sizeof(yx_ids[0]));
    memcpy(yc_ids, result->yc_ids, result->yc_count * sizeof(yc_ids[0]));
    memcpy(dd_ids, result->dd_ids, result->dd_count * sizeof(dd_ids[0]));

    LOG_INFO("custom", "start %s type=0x%02x begin=%llu end=%llu yx=%zu yc=%zu dd=%zu request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             (unsigned long long)result->begin_ms, (unsigned long long)result->end_ms,
             result->yx_count, result->yc_count, result->dd_count,
             (unsigned long long)msg->request_id);

    while (yx_count > 0 && client_context_is_active(client)) {
        for (size_t i = 0; i < yx_count && client_context_is_active(client); i++) {
            SoeRecord record;

            memset(&record, 0, sizeof(record));
            record.ioa = (int)yx_records[i].ioa;
            record.value = yx_records[i].value != 0;
            record.quality = (QualityDescriptor)yx_records[i].quality;
            record.timestamp_ms = yx_records[i].timestamp_ms;
            client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
            custom_asdu_send_history_yx(client->connection, server->al_params,
                                        msg->oa, server->config.common_address,
                                        &record);
        }

        yx_total += yx_count;
        last_yx_ts = yx_records[yx_count - 1].timestamp_ms;
        last_yx_id = yx_ids[yx_count - 1];
        if (yx_count < max_records)
            break;

        yx_count = history_store_query_yx_page(&server->history_store,
                                               result->begin_ms, result->end_ms,
                                               last_yx_ts, last_yx_id,
                                               yx_records, yx_ids, max_records);
    }

    while (yc_count > 0 && client_context_is_active(client)) {
        for (size_t i = 0; i < yc_count && client_context_is_active(client); i++) {
            client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
            custom_asdu_send_history_yc(client->connection, server->al_params,
                                        msg->oa, server->config.common_address,
                                        &yc_records[i]);
        }

        yc_total += yc_count;
        last_yc_ts = yc_records[yc_count - 1].timestamp_ms;
        last_yc_id = yc_ids[yc_count - 1];
        if (yc_count < max_records)
            break;

        yc_count = history_store_query_yc_page(&server->history_store,
                                               result->begin_ms, result->end_ms,
                                               last_yc_ts, last_yc_id,
                                               yc_records, yc_ids, max_records);
    }

    while (dd_count > 0 && client_context_is_active(client)) {
        for (size_t i = 0; i < dd_count && client_context_is_active(client); i++) {
            client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
            custom_asdu_send_history_dd(client->connection, server->al_params,
                                        msg->oa, server->config.common_address,
                                        &dd_records[i]);
        }

        dd_total += dd_count;
        last_dd_ts = dd_records[dd_count - 1].timestamp_ms;
        last_dd_id = dd_ids[dd_count - 1];
        if (dd_count < max_records)
            break;

        dd_count = history_store_query_dd_page(&server->history_store,
                                               result->begin_ms, result->end_ms,
                                               last_dd_ts, last_dd_id,
                                               dd_records, dd_ids, max_records);
    }

    LOG_INFO("custom", "data sent %s type=0x%02x yx=%zu yc=%zu dd=%zu request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             yx_total, yc_total, dd_total,
             (unsigned long long)msg->request_id);
}

static void send_custom_placeholder(Iec104Server* server, ClientContext* client,
                                    const Iec104WorkMsg* msg)
{
    uint8_t payload[4];

    LOG_WARN("custom", "placeholder %s type=0x%02x ioa=0x%06x request=%llu",
             custom_type_name(msg->type_id), msg->type_id, msg->ioa,
             (unsigned long long)msg->request_id);

    payload[0] = msg->type_id;
    payload[1] = 0;
    payload[2] = 0;
    payload[3] = 0;

    client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
    custom_asdu_send_private(client->connection, server->al_params, msg->type_id,
                             CS101_COT_REQUEST, msg->oa,
                             server->config.common_address, msg->ioa,
                             payload, sizeof(payload), false);
}

static void append_ioa_le(uint8_t* buffer, size_t* offset, uint32_t ioa)
{
    buffer[(*offset)++] = (uint8_t)(ioa & 0xff);
    buffer[(*offset)++] = (uint8_t)((ioa >> 8) & 0xff);
    buffer[(*offset)++] = (uint8_t)((ioa >> 16) & 0xff);
}

static void append_word_le(uint8_t* buffer, size_t* offset, uint16_t value)
{
    buffer[(*offset)++] = (uint8_t)(value & 0xff);
    buffer[(*offset)++] = (uint8_t)((value >> 8) & 0xff);
}

static void append_cp56_time(uint8_t* buffer, size_t* offset, uint64_t timestamp_ms)
{
    struct sCP56Time2a timestamp;

    if (timestamp_ms == 0)
        timestamp_ms = Hal_getTimeInMs();

    CP56Time2a_setFromMsTimestamp(&timestamp, timestamp_ms);
    memcpy(buffer + *offset, timestamp.encodedValue, sizeof(timestamp.encodedValue));
    *offset += sizeof(timestamp.encodedValue);
}

static bool send_custom_dynagram_frame(Iec104Server* server, IMasterConnection connection,
                                       const Iec104WorkMsg* msg, const YcPoint* points,
                                       size_t count)
{
    uint8_t payload[3 + DYNAGRAM_MAX_WORDS_PER_FRAME * 2 + 7];
    size_t offset = 0;
    CS101_ASDU asdu;
    bool ok;

    if (!points || count == 0)
        return true;

    if (count > DYNAGRAM_MAX_WORDS_PER_FRAME)
        count = DYNAGRAM_MAX_WORDS_PER_FRAME;

    append_ioa_le(payload, &offset, points[0].ioa);
	
    for (size_t i = 0; i < count; i++)
        append_word_le(payload, &offset, (uint16_t)((int)points[i].value & 0xffff));
	
    append_cp56_time(payload, &offset, points[0].timestamp_ms);

    asdu = CS101_ASDU_create(server->al_params, true, CS101_COT_REQUEST,
                             msg->oa, server->config.common_address, false, false);
    CS101_ASDU_setTypeID(asdu, (TypeID)msg->type_id);
    CS101_ASDU_setNumberOfElements(asdu, (int)count);
    CS101_ASDU_addPayload(asdu, payload, (int)offset);
    ok = IMasterConnection_sendASDU(connection, asdu);
    CS101_ASDU_destroy(asdu);

    LOG_DEBUG("custom", "dynagram frame %s type=0x%02x start_ioa=0x%06x words=%zu time_ms=%llu ok=%d",
              custom_type_name(msg->type_id), msg->type_id, points[0].ioa, count,
              (unsigned long long)(points[0].timestamp_ms), ok ? 1 : 0);

    return ok;
}

static void send_custom_dynagram_range(Iec104Server* server, ClientContext* client,
                                       const Iec104WorkMsg* msg,
                                       const PointTableSnapshot* snapshot,
                                       int start_ioa, int end_ioa)
{
    IMasterConnection connection = client->connection;

    for (size_t i = 0; i < snapshot->yc_custom_count && client_context_is_active(client);) {
        YcPoint* point = &snapshot->yc_custom[i];

        if ((int)point->ioa < start_ioa || (int)point->ioa > end_ioa) {
            i++;
            continue;
        }

        size_t batch = 1;
        while (i + batch < snapshot->yc_custom_count &&
               batch < DYNAGRAM_MAX_WORDS_PER_FRAME &&
               (int)snapshot->yc_custom[i + batch].ioa <= end_ioa &&
               snapshot->yc_custom[i + batch].ioa == point->ioa + (uint32_t)batch)
            batch++;

        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        send_custom_dynagram_frame(server, connection, msg, point, batch);
        i += batch;
    }
}

static void send_custom_dynagram_ranges(Iec104Server* server, ClientContext* client,
                                        const Iec104WorkMsg* msg,
                                        const IoaRange* ranges, size_t range_count)
{
    PointTableSnapshot snapshot;

    LOG_INFO("custom", "start %s type=0x%02x ranges=%zu request=%llu",
             custom_type_name(msg->type_id), msg->type_id, range_count,
             (unsigned long long)msg->request_id);

    if (!point_table_snapshot_create(server->table, &snapshot)) {
        LOG_ERROR("client", "failed to create dynagram snapshot type=0x%02x", msg->type_id);
        return;
    }

    for (size_t i = 0; i < range_count && client_context_is_active(client); i++)
        send_custom_dynagram_range(server, client, msg, &snapshot,
                                   ranges[i].start, ranges[i].end);

    point_table_snapshot_destroy(&snapshot);
    LOG_INFO("custom", "data sent %s type=0x%02x request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             (unsigned long long)msg->request_id);
}

static void send_custom_dynagram(Iec104Server* server, ClientContext* client,
                                 const Iec104WorkMsg* msg)
{
    IoaRange ranges[] = {
        {DYNAGRAM_IOA_START, DYNAGRAM_IOA_END}
    };

    send_custom_dynagram_ranges(server, client, msg, ranges,
                                sizeof(ranges) / sizeof(ranges[0]));
}

static void send_custom_elec_dynagram(Iec104Server* server, ClientContext* client,
                                      const Iec104WorkMsg* msg)
{
    IoaRange ranges[] = {
        {ELEC_DYNAGRAM_IOA_START, ELEC_DYNAGRAM_IOA_END}
    };

    send_custom_dynagram_ranges(server, client, msg, ranges,
                                sizeof(ranges) / sizeof(ranges[0]));
}

static void send_custom_rtu_param(Iec104Server* server, ClientContext* client,
                                  const Iec104WorkMsg* msg)
{
    PointTableSnapshot snapshot;
    IMasterConnection connection = client->connection;

    LOG_INFO("custom", "start %s type=0x%02x range=0x%04x-0x%04x request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             RTU_PARAM_IOA_START, RTU_PARAM_IOA_END,
             (unsigned long long)msg->request_id);

    if (!point_table_snapshot_create(server->table, &snapshot)) {
        LOG_ERROR("client", "failed to create rtu param snapshot type=0x%02x", msg->type_id);
        return;
    }

    for (size_t i = 0; i < snapshot.yc_rtu_count && client_context_is_active(client);) {
        YcPoint* point = &snapshot.yc_rtu[i];

        if ((int)point->ioa < RTU_PARAM_IOA_START || (int)point->ioa > RTU_PARAM_IOA_END) {
            i++;
            continue;
        }

        size_t batch = 1;
        while (i + batch < snapshot.yc_rtu_count &&
               batch < RTU_PARAM_MAX_WORDS_PER_FRAME &&
               (int)snapshot.yc_rtu[i + batch].ioa <= RTU_PARAM_IOA_END &&
               snapshot.yc_rtu[i + batch].ioa == point->ioa + (uint32_t)batch)
            batch++;

        uint8_t payload[3 + RTU_PARAM_MAX_WORDS_PER_FRAME * 2];
        size_t offset = 0;
        CS101_ASDU asdu;
        append_ioa_le(payload, &offset, point->ioa);
        for (size_t j = 0; j < batch; j++)
            append_word_le(payload, &offset, (uint16_t)((int)snapshot.yc_rtu[i + j].value & 0xffff));

        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        asdu = CS101_ASDU_create(server->al_params, true, CS101_COT_REQUEST,
                                 msg->oa, server->config.common_address, false, false);
        CS101_ASDU_setTypeID(asdu, (TypeID)msg->type_id);
        CS101_ASDU_setNumberOfElements(asdu, (int)batch);
        CS101_ASDU_addPayload(asdu, payload, (int)offset);
        IMasterConnection_sendASDU(connection, asdu);
        CS101_ASDU_destroy(asdu);
        i += batch;
    }

    point_table_snapshot_destroy(&snapshot);
    LOG_INFO("custom", "data sent %s type=0x%02x range=0x%04x-0x%04x request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             RTU_PARAM_IOA_START, RTU_PARAM_IOA_END,
             (unsigned long long)msg->request_id);
}

static void send_custom_sensor_param(Iec104Server* server, ClientContext* client,
                                     const Iec104WorkMsg* msg)
{
    PointTableSnapshot snapshot;
    IMasterConnection connection = client->connection;

    LOG_INFO("custom", "start %s type=0x%02x range=0x%04x-0x%04x request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             SENSOR_PARAM_IOA_START, SENSOR_PARAM_IOA_END,
             (unsigned long long)msg->request_id);

    if (!point_table_snapshot_create(server->table, &snapshot)) {
        LOG_ERROR("client", "failed to create sensor param snapshot type=0x%02x", msg->type_id);
        return;
    }

    for (size_t i = 0; i < snapshot.yc_sensor_conf_count && client_context_is_active(client);) {
        YcPoint* point = &snapshot.yc_sensor_conf[i];

        if ((int)point->ioa < SENSOR_PARAM_IOA_START || (int)point->ioa > SENSOR_PARAM_IOA_END) {
            i++;
            continue;
        }

        size_t batch = 1;
        while (i + batch < snapshot.yc_sensor_conf_count &&
               batch < SENSOR_PARAM_MAX_WORDS_PER_FRAME &&
               (int)snapshot.yc_sensor_conf[i + batch].ioa <= SENSOR_PARAM_IOA_END &&
               snapshot.yc_sensor_conf[i + batch].ioa == point->ioa + (uint32_t)batch)
            batch++;

        uint8_t payload[3 + SENSOR_PARAM_MAX_WORDS_PER_FRAME * 2];
        size_t offset = 0;
        CS101_ASDU asdu;
        append_ioa_le(payload, &offset, point->ioa);
        for (size_t j = 0; j < batch; j++)
            append_word_le(payload, &offset, (uint16_t)((int)snapshot.yc_sensor_conf[i + j].value & 0xffff));

        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        asdu = CS101_ASDU_create(server->al_params, true, CS101_COT_REQUEST,
                                 msg->oa, server->config.common_address, false, false);
        CS101_ASDU_setTypeID(asdu, (TypeID)msg->type_id);
        CS101_ASDU_setNumberOfElements(asdu, (int)batch);
        CS101_ASDU_addPayload(asdu, payload, (int)offset);
        IMasterConnection_sendASDU(connection, asdu);
        CS101_ASDU_destroy(asdu);
        i += batch;
    }

    point_table_snapshot_destroy(&snapshot);
    LOG_INFO("custom", "data sent %s type=0x%02x range=0x%04x-0x%04x request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             SENSOR_PARAM_IOA_START, SENSOR_PARAM_IOA_END,
             (unsigned long long)msg->request_id);
}

static void send_custom_yc_word_points(Iec104Server* server, ClientContext* client,
                                       const Iec104WorkMsg* msg,
                                       const YcPoint* points, size_t count,
                                       int start_ioa, int end_ioa,
                                       const char* label)
{
    IMasterConnection connection = client->connection;

    LOG_INFO("custom", "start %s type=0x%02x range=0x%04x-0x%04x request=%llu",
             label, msg->type_id, start_ioa, end_ioa,
             (unsigned long long)msg->request_id);

    for (size_t i = 0; i < count && client_context_is_active(client);) {
        const YcPoint* point = &points[i];

        if ((int)point->ioa < start_ioa || (int)point->ioa > end_ioa) {
            i++;
            continue;
        }

        size_t batch = 1;
        while (i + batch < count &&
               batch < CUSTOM_YC_WORD_MAX_PER_FRAME &&
               (int)points[i + batch].ioa <= end_ioa &&
               points[i + batch].ioa == point->ioa + (uint32_t)batch)
            batch++;

        uint8_t payload[3 + CUSTOM_YC_WORD_MAX_PER_FRAME * 2];
        size_t offset = 0;
        CS101_ASDU asdu;
        append_ioa_le(payload, &offset, point->ioa);
        for (size_t j = 0; j < batch; j++)
            append_word_le(payload, &offset, (uint16_t)((int)points[i + j].value & 0xffff));

        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        asdu = CS101_ASDU_create(server->al_params, true, CS101_COT_REQUEST,
                                 msg->oa, server->config.common_address, false, false);
        CS101_ASDU_setTypeID(asdu, (TypeID)msg->type_id);
        CS101_ASDU_setNumberOfElements(asdu, (int)batch);
        CS101_ASDU_addPayload(asdu, payload, (int)offset);
        IMasterConnection_sendASDU(connection, asdu);
        CS101_ASDU_destroy(asdu);
        i += batch;
    }

    LOG_INFO("custom", "data sent %s type=0x%02x range=0x%04x-0x%04x request=%llu",
             label, msg->type_id, start_ioa, end_ioa,
             (unsigned long long)msg->request_id);
}

static void send_custom_yc_custom_word_range(Iec104Server* server, ClientContext* client,
                                             const Iec104WorkMsg* msg,
                                             int start_ioa, int end_ioa,
                                             const char* label)
{
    PointTableSnapshot snapshot;

    if (!point_table_snapshot_create(server->table, &snapshot)) {
        LOG_ERROR("client", "failed to create %s snapshot type=0x%02x", label, msg->type_id);
        return;
    }

    send_custom_yc_word_points(server, client, msg, snapshot.yc_custom, snapshot.yc_custom_count,
                               start_ioa, end_ioa, label);

    point_table_snapshot_destroy(&snapshot);
}

static void send_custom_harmonic(Iec104Server* server, ClientContext* client,
                                 const Iec104WorkMsg* msg)
{
    send_custom_yc_custom_word_range(server, client, msg,
                                     HARMONIC_IOA_START, HARMONIC_IOA_END,
                                     custom_type_name(msg->type_id));
}

static void send_custom_meter_truck(Iec104Server* server, ClientContext* client,
                                    const Iec104WorkMsg* msg)
{
    send_custom_yc_custom_word_range(server, client, msg,
                                     METER_TRUCK_IOA_START, METER_TRUCK_IOA_END,
                                     custom_type_name(msg->type_id));
}

static void send_custom_injection(Iec104Server* server, ClientContext* client,
                                  const Iec104WorkMsg* msg)
{
    send_custom_yc_custom_word_range(server, client, msg,
                                     INJECTION_IOA_START, INJECTION_IOA_END,
                                     custom_type_name(msg->type_id));
}

static void send_custom_all_dynagram(Iec104Server* server, ClientContext* client,
                                     const Iec104WorkMsg* msg)
{
    Iec104WorkMsg dynagram_msg = *msg;
    Iec104WorkMsg elec_dynagram_msg = *msg;
    Iec104WorkMsg wellhead_msg = *msg;
    IoaRange dynagram_ranges[] = {
        {DYNAGRAM_IOA_START, DYNAGRAM_IOA_END}
    };
    IoaRange elec_dynagram_ranges[] = {
        {ELEC_DYNAGRAM_IOA_START, ELEC_DYNAGRAM_IOA_END}
    };

    dynagram_msg.type_id = CUSTOM_TYPE_DYNAGRAM_CALL;
    send_custom_dynagram_ranges(server, client, &dynagram_msg, dynagram_ranges,
                                sizeof(dynagram_ranges) / sizeof(dynagram_ranges[0]));

    elec_dynagram_msg.type_id = CUSTOM_TYPE_ELEC_DYNAGRAM_CALL;
    send_custom_dynagram_ranges(server, client, &elec_dynagram_msg, elec_dynagram_ranges,
                                sizeof(elec_dynagram_ranges) / sizeof(elec_dynagram_ranges[0]));

    wellhead_msg.type_id = CUSTOM_TYPE_WELLHEAD_PRESSURE_CALL;
    send_custom_yc_custom_word_range(server, client, &wellhead_msg,
                                     WELLHEAD_PRESSURE_DYNAGRAM_IOA_START,
                                     WELLHEAD_PRESSURE_DYNAGRAM_IOA_END,
                                     custom_type_name(wellhead_msg.type_id));
}

static void send_custom_active_power(Iec104Server* server, ClientContext* client,
                                     const Iec104WorkMsg* msg)
{
    send_custom_yc_custom_word_range(server, client, msg,
                                     ACTIVE_POWER_IOA_START, ACTIVE_POWER_IOA_END,
                                     custom_type_name(msg->type_id));
}

static void send_custom_wellhead_pressure(Iec104Server* server, ClientContext* client,
                                          const Iec104WorkMsg* msg)
{
    send_custom_yc_custom_word_range(server, client, msg,
                                     WELLHEAD_PRESSURE_DYNAGRAM_IOA_START,
                                     WELLHEAD_PRESSURE_DYNAGRAM_IOA_END,
                                     custom_type_name(msg->type_id));
}

static void send_custom_reserved_sensor(Iec104Server* server, ClientContext* client,
                                        const Iec104WorkMsg* msg)
{
    PointTableSnapshot snapshot;
    const char* label = custom_type_name(msg->type_id);

    if (!point_table_snapshot_create(server->table, &snapshot)) {
        LOG_ERROR("client", "failed to create %s snapshot type=0x%02x", label, msg->type_id);
        return;
    }

    send_custom_yc_word_points(server, client, msg, snapshot.yc, snapshot.yc_count,
                               RESERVED_SENSOR_IOA_START, RESERVED_SENSOR_IOA_END,
                               label);

    point_table_snapshot_destroy(&snapshot);
}

static void handle_custom_call_work(Iec104Server* server, ClientContext* client,
                                    const Iec104WorkMsg* msg)
{
    HistoryDataQueryResult history_data;
    bool has_history_data = false;

    if (!client_context_is_active(client))
        return;

    LOG_INFO("custom", "handle start %s type=0x%02x cot=0x%02x ca=%d ioa=0x%06x request=%llu",
             custom_type_name(msg->type_id), msg->type_id, msg->cot, msg->ca, msg->ioa,
             (unsigned long long)msg->request_id);

    memset(&history_data, 0, sizeof(history_data));
    if (msg->type_id == CUSTOM_TYPE_HISTORY_CALL) {
        query_history_data(server, msg, &history_data);
        has_history_data = history_data_total_count(&history_data) > 0;
        if (!has_history_data) {
            LOG_INFO("custom", "no history data type=0x%02x begin=%llu end=%llu request=%llu",
                     msg->type_id,
                     (unsigned long long)history_data.begin_ms,
                     (unsigned long long)history_data.end_ms,
                     (unsigned long long)msg->request_id);
            goto finish_only;
        }
    }

    client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
    custom_asdu_send_ack(client->connection, server->al_params, msg->type_id,
                         msg->oa, server->config.common_address, msg->ioa);
    LOG_INFO("custom", "ack sent %s type=0x%02x request=%llu",
             custom_type_name(msg->type_id), msg->type_id,
             (unsigned long long)msg->request_id);

    switch (msg->type_id) {
    case CUSTOM_TYPE_MEASURE_TOTAL_CALL:
        send_custom_measure_total(server, client, msg);
        break;

    case CUSTOM_TYPE_DYNAGRAM_CALL:
        send_custom_dynagram(server, client, msg);
        break;

    case CUSTOM_TYPE_ELEC_DYNAGRAM_CALL:
        send_custom_elec_dynagram(server, client, msg);
        break;

    case CUSTOM_TYPE_HISTORY_CALL:
        send_history_data_records(server, client, msg, &history_data);
        break;

    case CUSTOM_TYPE_RTU_PARAM_CALL:
        send_custom_rtu_param(server, client, msg);
        break;

    case CUSTOM_TYPE_SENSOR_PARAM_CALL:
        send_custom_sensor_param(server, client, msg);
        break;

    case CUSTOM_TYPE_HARMONIC_CALL:
        send_custom_harmonic(server, client, msg);
        break;

    case CUSTOM_TYPE_METER_TRUCK_CALL:
        send_custom_meter_truck(server, client, msg);
        break;

    case CUSTOM_TYPE_INJECTION_CALL:
        send_custom_injection(server, client, msg);
        break;

    case CUSTOM_TYPE_ALL_DYNAGRAM_CALL:
        send_custom_all_dynagram(server, client, msg);
        break;

    case CUSTOM_TYPE_ACTIVE_POWER_CALL:
        send_custom_active_power(server, client, msg);
        break;

    case CUSTOM_TYPE_WELLHEAD_PRESSURE_CALL:
        send_custom_wellhead_pressure(server, client, msg);
        break;

    case CUSTOM_TYPE_HISTORY_SOE_CALL:
        send_history_soe(server, client, msg);
        break;

    case CUSTOM_TYPE_RESERVED_SENSOR_CALL:
        send_custom_reserved_sensor(server, client, msg);
        break;

    default:
        send_custom_placeholder(server, client, msg);
        break;
    }

finish_only:
    if (client_context_is_active(client)) {
        client_context_wait_send_interval(client, BUSINESS_SEND_INTERVAL_MS);
        custom_asdu_send_finish(client->connection, server->al_params, msg->type_id,
                                msg->oa, server->config.common_address, msg->ioa);
        LOG_INFO("custom", "finish sent %s type=0x%02x request=%llu",
                 custom_type_name(msg->type_id), msg->type_id,
                 (unsigned long long)msg->request_id);
    }
}

static bool client_work_handler(void* owner, ClientContext* client, const Iec104WorkMsg* msg)
{
    Iec104Server* server = (Iec104Server*)owner;

    switch (msg->type) {
    case MSG_TOTAL_CALL:
        send_total_call(server, client, msg);
        return true;

    case MSG_COUNTER_CALL:
        send_counter_call(server, client, msg);
        return true;

    case MSG_ACTIVE_UPLOAD:
        send_active_upload(server, client, msg);
        return true;

    case MSG_REMOTE_CONTROL:
        handle_remote_control_work(server, client, msg);
        return true;

    case MSG_REMOTE_ADJUST:
        handle_remote_adjust_work(server, client, msg);
        return true;

    case MSG_CUSTOM_CALL:
        handle_custom_call_work(server, client, msg);
        return true;

    case MSG_HISTORY_CALL:
        handle_custom_call_work(server, client, msg);
        return true;

    default:
        return false;
    }
}

static void* shm_refresh_thread(void* parameter)
{
    Iec104Server* server = (Iec104Server*)parameter;

    LOG_INFO("shm", "refresh thread started interval_ms=%d", server->config.scan_interval_ms);

    while (server->worker_running) {
        uint64_t now = Hal_getMonotonicTimeInMs();
        shm_adapter_poll(&server->shm, server->table, now);
        Thread_sleep(server->config.scan_interval_ms);
    }

    LOG_INFO("shm", "refresh thread stopped");
    return NULL;
}

static void* report_scheduler_thread(void* parameter)
{
    Iec104Server* server = (Iec104Server*)parameter;
    uint64_t last_periodic_ms = 0;

    LOG_INFO("report", "scheduler thread started scan_ms=%d periodic_enabled=%d periodic_ms=%d",
             server->config.scan_interval_ms, server->config.periodic_enabled ? 1 : 0,
             server->config.periodic_interval_ms);

    while (server->worker_running) {
        uint64_t now = Hal_getMonotonicTimeInMs();
        bool force_periodic = false;
        ActiveUploadNotify notify;

        if (server->config.periodic_enabled &&
            (last_periodic_ms == 0 ||
             now - last_periodic_ms >= (uint64_t)server->config.periodic_interval_ms)) {
            force_periodic = true;
            last_periodic_ms = now;
        }

        if (active_upload_scan_point_table(&server->active_upload, server->table,
                                           now, force_periodic, &notify)) {
            if (notify.has_soe) {
                ActiveUploadSnapshot snapshot;
                if (active_upload_snapshot_create(&server->active_upload,
                                                  server->last_soe_history_version, &snapshot)) {
                    for (size_t i = 0; i < snapshot.soe_count; i++)
                        soe_history_append(&server->soe_history, snapshot.soe[i].ioa,
                                           snapshot.soe[i].value, snapshot.soe[i].quality,
                                           snapshot.soe[i].timestamp_ms);
                    for (size_t i = 0; i < snapshot.soe_count; i++) {
                        SoeRecord record;
                        memset(&record, 0, sizeof(record));
                        record.ioa = snapshot.soe[i].ioa;
                        record.value = snapshot.soe[i].value;
                        record.quality = snapshot.soe[i].quality;
                        record.timestamp_ms = snapshot.soe[i].timestamp_ms;
                        record.sequence = snapshot.soe[i].sequence;
                        history_store_append_soe(&server->history_store, &record,
                                                 server->config.common_address);
                    }
                    server->last_soe_history_version = snapshot.version;
                    active_upload_snapshot_destroy(&snapshot);
                }
            }

            post_active_upload_to_clients(server, &notify);
        }

        Thread_sleep(server->config.scan_interval_ms);
    }

    LOG_INFO("report", "scheduler thread stopped");
    return NULL;
}

static bool interrogation_handler(void* parameter, IMasterConnection connection,
                                  CS101_ASDU asdu, uint8_t qoi)
{
    Iec104Server* server = (Iec104Server*)parameter;
    int ca = CS101_ASDU_getCA(asdu);
    ClientContext* client = get_client_for_connection(server, connection);

    LOG_INFO("iec104", "interrogation ca=%d qoi=%u", ca, qoi);

    if (ca != server->config.common_address) {
        CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_CA);
        CS101_ASDU_setNegative(asdu, true);
        IMasterConnection_sendASDU(connection, asdu);
        return true;
    }

    if (!client) {
        IMasterConnection_sendACT_CON(connection, asdu, true);
        return true;
    }

    if (qoi != IEC60870_QOI_STATION) {
        IMasterConnection_sendACT_CON(connection, asdu, true);
        return true;
    }

    Iec104WorkMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_TOTAL_CALL;
    msg.ca = ca;
    msg.qoi = qoi;
    msg.oa = (uint8_t)CS101_ASDU_getOA(asdu);
    msg.request_id = next_request_id();

    IMasterConnection_sendACT_CON(connection, asdu, false);

    if (!client_context_post(client, &msg, true))
        IMasterConnection_sendACT_CON(connection, asdu, true);

    return true;
}

static bool counter_interrogation_handler(void* parameter, IMasterConnection connection,
                                          CS101_ASDU asdu, QualifierOfCIC qcc)
{
    Iec104Server* server = (Iec104Server*)parameter;
    int ca = CS101_ASDU_getCA(asdu);
    int oa = CS101_ASDU_getOA(asdu);
    ClientContext* client = get_client_for_connection(server, connection);

    if (ca != server->config.common_address) {
        CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_CA);
        CS101_ASDU_setNegative(asdu, true);
        IMasterConnection_sendASDU(connection, asdu);
        return true;
    }

    if (!client) {
        IMasterConnection_sendACT_CON(connection, asdu, true);
        return true;
    }

    LOG_INFO("iec104", "counter interrogation qcc=%u", qcc);
    Iec104WorkMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_COUNTER_CALL;
    msg.ca = ca;
    msg.oa = (uint8_t)oa;
    msg.qcc = qcc;
    msg.request_id = next_request_id();

    IMasterConnection_sendACT_CON(connection, asdu, false);

    if (!client_context_post(client, &msg, true))
        IMasterConnection_sendACT_CON(connection, asdu, true);

    return true;
}

static bool asdu_handler(void* parameter, IMasterConnection connection, CS101_ASDU asdu)
{
    Iec104Server* server = (Iec104Server*)parameter;
    ClientContext* client = get_client_for_connection(server, connection);

    if (CS101_ASDU_getCA(asdu) != server->config.common_address) {
        CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_CA);
        CS101_ASDU_setNegative(asdu, true);
        IMasterConnection_sendASDU(connection, asdu);
        return true;
    }

    if (!client)
        return false;

    TypeID type = CS101_ASDU_getTypeID(asdu);

    if (type == C_SC_NA_1 || type == C_DC_NA_1 ||
        type == C_SE_NA_1 || type == C_SE_NB_1 || type == C_SE_NC_1) {
        CS101_CauseOfTransmission cot = CS101_ASDU_getCOT(asdu);
        int number_of_elements = CS101_ASDU_getNumberOfElements(asdu);
        if (cot != CS101_COT_ACTIVATION && cot != CS101_COT_DEACTIVATION) {
            CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_COT);
            CS101_ASDU_setNegative(asdu, true);
            IMasterConnection_sendASDU(connection, asdu);
            return true;
        }

        if (number_of_elements <= 0 ||
            number_of_elements > IEC104_MAX_SETPOINTS_PER_ASDU) {
            CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_IOA);
            CS101_ASDU_setNegative(asdu, true);
            IMasterConnection_sendASDU(connection, asdu);
            return true;
        }

        Iec104WorkMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = (type == C_SC_NA_1 || type == C_DC_NA_1) ?
                   MSG_REMOTE_CONTROL : MSG_REMOTE_ADJUST;
        msg.ca = CS101_ASDU_getCA(asdu);
        msg.type_id = (uint8_t)type;
        msg.cot = (uint8_t)cot;
        msg.oa = (uint8_t)CS101_ASDU_getOA(asdu);
        msg.request_id = next_request_id();

        if (type == C_SC_NA_1 || type == C_DC_NA_1) {
            InformationObject io = CS101_ASDU_getElement(asdu, 0);
            if (!io) {
                CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_IOA);
                CS101_ASDU_setNegative(asdu, true);
                IMasterConnection_sendASDU(connection, asdu);
                return true;
            }

            msg.ioa = (uint32_t)InformationObject_getObjectAddress(io);

            if (type == C_SC_NA_1) {
                SingleCommand command = (SingleCommand)io;
                msg.command_select = SingleCommand_isSelect(command) ? 1 : 0;
                msg.command_state = SingleCommand_getState(command) ? 1 : 0;
                msg.command_qualifier = SingleCommand_getQU(command);
            }
            else {
                DoubleCommand command = (DoubleCommand)io;
                msg.command_select = DoubleCommand_isSelect(command) ? 1 : 0;
                msg.command_state = (uint8_t)DoubleCommand_getState(command);
                msg.command_qualifier = DoubleCommand_getQU(command);
            }

            InformationObject_destroy(io);
        }
        else {
            msg.setpoint_count = (uint8_t)number_of_elements;

            for (int i = 0; i < number_of_elements; i++) {
                InformationObject io = CS101_ASDU_getElement(asdu, i);
                if (!io) {
                    CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_IOA);
                    CS101_ASDU_setNegative(asdu, true);
                    IMasterConnection_sendASDU(connection, asdu);
                    return true;
                }

                Iec104SetpointItem* item = &msg.setpoints[i];
                item->ioa = (uint32_t)InformationObject_getObjectAddress(io);

                if (type == C_SE_NA_1) {
                    SetpointCommandNormalized command = (SetpointCommandNormalized)io;
                    item->select = SetpointCommandNormalized_isSelect(command) ? 1 : 0;
                    item->value = SetpointCommandNormalized_getValue(command);
                    item->qualifier = SetpointCommandNormalized_getQL(command);
                }
                else if (type == C_SE_NB_1) {
                    SetpointCommandScaled command = (SetpointCommandScaled)io;
                    item->select = SetpointCommandScaled_isSelect(command) ? 1 : 0;
                    item->value = (float)SetpointCommandScaled_getValue(command);
                    item->qualifier = SetpointCommandScaled_getQL(command);
                }
                else {
                    SetpointCommandShort command = (SetpointCommandShort)io;
                    item->select = SetpointCommandShort_isSelect(command) ? 1 : 0;
                    item->value = SetpointCommandShort_getValue(command);
                    item->qualifier = SetpointCommandShort_getQL(command);
                }

                InformationObject_destroy(io);
            }

            msg.ioa = msg.setpoints[0].ioa;
            msg.command_select = msg.setpoints[0].select;
            msg.setpoint_value = msg.setpoints[0].value;
            msg.command_qualifier = msg.setpoints[0].qualifier;

            for (int i = 1; i < number_of_elements; i++) {
                if (msg.setpoints[i].select != msg.command_select) {
                    CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_COT);
                    CS101_ASDU_setNegative(asdu, true);
                    IMasterConnection_sendASDU(connection, asdu);
                    return true;
                }
            }
        }

        if (!client_context_post(client, &msg, true))
            return false;

        return true;
    }

    if (custom_asdu_is_call_type(type)) {
        if (CS101_ASDU_getCOT(asdu) != CS101_COT_ACTIVATION) {
            CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_COT);
            CS101_ASDU_setNegative(asdu, true);
            IMasterConnection_sendASDU(connection, asdu);
            return true;
        }

        Iec104WorkMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = custom_asdu_is_history_call_type(type) ? MSG_HISTORY_CALL : MSG_CUSTOM_CALL;
        msg.ca = CS101_ASDU_getCA(asdu);
        msg.type_id = (uint8_t)type;
        msg.cot = (uint8_t)CS101_ASDU_getCOT(asdu);
        msg.oa = (uint8_t)CS101_ASDU_getOA(asdu);
        msg.request_id = next_request_id();

        uint8_t* payload = CS101_ASDU_getPayload(asdu);
        int payload_size = CS101_ASDU_getPayloadSize(asdu);
        if (payload && payload_size > 0) {
            if (payload_size >= 3) {
                msg.ioa = (uint32_t)payload[0] |
                          ((uint32_t)payload[1] << 8) |
                          ((uint32_t)payload[2] << 16);
            }

            if (payload_size > (int)sizeof(msg.payload))
                payload_size = (int)sizeof(msg.payload);
            memcpy(msg.payload, payload, (size_t)payload_size);
            msg.payload_len = (uint16_t)payload_size;

            if (msg.type == MSG_HISTORY_CALL && payload_size >= 17) {
                memcpy(msg.begin_time.encodedValue, &payload[3], 7);
                memcpy(msg.end_time.encodedValue, &payload[10], 7);
            }
        }

        LOG_INFO("custom", "received %s type=0x%02x cot=0x%02x ca=%d oa=%u ioa=0x%06x payload_len=%u request=%llu",
                 custom_type_name(msg.type_id), msg.type_id, msg.cot, msg.ca, msg.oa, msg.ioa,
                 msg.payload_len, (unsigned long long)msg.request_id);

        if (!client_context_post(client, &msg, true))
            return false;

        return true;
    }

    if (custom_asdu_is_history_return_type(type)) {
        LOG_WARN("iec104", "received slave return history TypeID=0x%02x from master", type);
        CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_TYPE_ID);
        CS101_ASDU_setNegative(asdu, true);
        IMasterConnection_sendASDU(connection, asdu);
        return true;
    }

    LOG_WARN("iec104", "unsupported TypeID=%d", CS101_ASDU_getTypeID(asdu));
    CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_TYPE_ID);
    CS101_ASDU_setNegative(asdu, true);
    IMasterConnection_sendASDU(connection, asdu);
    return true;
}

static bool connection_request_handler(void* parameter, const char* ip_address)
{
    (void)parameter;
    LOG_INFO("client", "connection request from %s", ip_address);
    return true;
}

static void connection_event_handler(void* parameter, IMasterConnection connection,
                                     CS104_PeerConnectionEvent event)
{
    Iec104Server* server = (Iec104Server*)parameter;

    if (event == CS104_CON_EVENT_CONNECTION_OPENED) {
        ClientContext* client = get_free_client(server);
        if (client) {
            client_context_bind_connection(client, connection);
            LOG_INFO("client", "connection opened %p", connection);
        }
        else {
            LOG_WARN("client", "no free client context for connection %p", connection);
        }
    }
    else if (event == CS104_CON_EVENT_CONNECTION_CLOSED) {
        ClientContext* client = get_client_for_connection(server, connection);
        if (client)
            client_context_close_connection(client);
        LOG_INFO("client", "connection closed %p", connection);
    }
    else if (event == CS104_CON_EVENT_ACTIVATED) {
        ClientContext* client = get_client_for_connection(server, connection);
        if (client)
            client->started = true;
        LOG_INFO("client", "connection activated %p", connection);
    }
    else if (event == CS104_CON_EVENT_DEACTIVATED) {
        ClientContext* client = get_client_for_connection(server, connection);
        if (client)
            client->started = false;
        LOG_INFO("client", "connection deactivated %p", connection);
    }
}

bool iec104_server_init(Iec104Server** server_out, const Iec104Config* config, PointTable* table)
{
    Iec104Server* server = (Iec104Server*)calloc(1, sizeof(Iec104Server));
    if (!server)
        return false;

    server->config = *config;
    server->table = table;
    server->client_count = config->max_open_connections;
    if (server->client_count < 1)
        server->client_count = 1;
    server->clients = (ClientContext*)calloc((size_t)server->client_count, sizeof(ClientContext));
    server->lock = Semaphore_create(1);
    server->slave = CS104_Slave_create(config->low_priority_queue_size,
                                       config->high_priority_queue_size);

    size_t upload_yx_capacity = table->yx_count;
    size_t upload_yc_capacity = table->yc_count + table->yc_rtu_count +
                                table->yc_sensor_conf_count + table->yc_custom_count;
    size_t upload_soe_capacity = table->yx_count > 4096 ? table->yx_count : 4096;
    HistoryStoreConfig history_config;
    history_store_config_from_iec104(config, &history_config);

    if (!server->clients || !server->lock || !server->slave ||
        !active_upload_init(&server->active_upload, upload_yx_capacity,
                            upload_yc_capacity, upload_soe_capacity) ||
        !soe_history_init(&server->soe_history) ||
        !history_store_init(&server->history_store, &history_config) ||
        !shm_adapter_init(&server->shm)) {
        iec104_server_destroy(server);
        return false;
    }

    for (int i = 0; i < server->client_count; i++) {
        if (!client_context_init(&server->clients[i], CLIENT_QUEUE_CAPACITY,
                                 client_work_handler, server)) {
            iec104_server_destroy(server);
            return false;
        }
    }

    DiagConfig diag_config;
    memset(&diag_config, 0, sizeof(diag_config));
    diag_config.enabled = config->diag_enabled;
    diag_config.writable = config->diag_writable;
    diag_config.allow_clear = config->diag_allow_clear;
    snprintf(diag_config.bind_ip, sizeof(diag_config.bind_ip), "%s", config->diag_bind_ip);
    diag_config.port = config->diag_port;
    diag_server_init(&server->diag, &diag_config, table, &server->active_upload,
                     &server->soe_history, diag_notify_upload, server);

    CS104_Slave_setLocalAddress(server->slave, config->local_ip);
    CS104_Slave_setLocalPort(server->slave, config->local_port);
    CS104_Slave_setMaxOpenConnections(server->slave, server->client_count);
    CS104_Slave_setServerMode(server->slave, CS104_MODE_SINGLE_REDUNDANCY_GROUP);

    server->al_params = CS104_Slave_getAppLayerParameters(server->slave);

    CS104_APCIParameters apci = CS104_Slave_getConnectionParameters(server->slave);
    apci->k = config->k;
    apci->w = config->w;
    apci->t0 = config->t0_seconds;
    apci->t1 = config->t1_seconds;
    apci->t2 = config->t2_seconds;
    apci->t3 = config->t3_seconds;

    CS104_Slave_setClockSyncHandler(server->slave, clock_sync_handler, server);
    CS104_Slave_setInterrogationHandler(server->slave, interrogation_handler, server);
    CS104_Slave_setCounterInterrogationHandler(server->slave, counter_interrogation_handler, server);
    CS104_Slave_setASDUHandler(server->slave, asdu_handler, server);
    CS104_Slave_setConnectionRequestHandler(server->slave, connection_request_handler, server);
    CS104_Slave_setConnectionEventHandler(server->slave, connection_event_handler, server);

    if (config->raw_message_log)
        CS104_Slave_setRawMessageHandler(server->slave, raw_message_handler, server);

    *server_out = server;
    return true;
}

bool iec104_server_start(Iec104Server* server)
{
    if (!server->config.enabled) {
        LOG_WARN("iec104", "server disabled by config");
        return false;
    }

    CS104_Slave_start(server->slave);

    if (!CS104_Slave_isRunning(server->slave)) {
        LOG_ERROR("iec104", "start failed on %s:%d", server->config.local_ip, server->config.local_port);
        return false;
    }

    LOG_INFO("iec104", "server started on %s:%d", server->config.local_ip, server->config.local_port);

    server->worker_running = true;
    for (int i = 0; i < server->client_count; i++) {
        if (!client_context_start(&server->clients[i])) {
            LOG_ERROR("iec104", "failed to create client worker thread index=%d", i);
            server->worker_running = false;
            for (int j = 0; j < i; j++)
                client_context_stop(&server->clients[j]);
            CS104_Slave_stop(server->slave);
            return false;
        }
    }

    shm_adapter_open(&server->shm, "iec104_slave");
    if (!diag_server_start(&server->diag)) {
        LOG_ERROR("iec104", "failed to start diagnostic server");
        server->worker_running = false;
        for (int i = 0; i < server->client_count; i++)
            client_context_stop(&server->clients[i]);
        CS104_Slave_stop(server->slave);
        return false;
    }

    if (server->config.scan_enabled)
        server->shm_thread = Thread_create(shm_refresh_thread, server, false);
    else
        LOG_INFO("shm", "refresh thread disabled by config");

    if (server->config.active_upload_enabled)
        server->report_thread = Thread_create(report_scheduler_thread, server, false);
    else
        LOG_INFO("report", "active upload scheduler disabled by config");

    if ((server->config.scan_enabled && !server->shm_thread) ||
        (server->config.active_upload_enabled && !server->report_thread)) {
        LOG_ERROR("iec104", "failed to create worker threads");
        server->worker_running = false;
        for (int i = 0; i < server->client_count; i++)
            client_context_stop(&server->clients[i]);
        if (server->shm_thread) {
            Thread_destroy(server->shm_thread);
            server->shm_thread = NULL;
        }
        if (server->report_thread) {
            Thread_destroy(server->report_thread);
            server->report_thread = NULL;
        }
        CS104_Slave_stop(server->slave);
        return false;
    }

    if (server->shm_thread)
        Thread_start(server->shm_thread);
    if (server->report_thread)
        Thread_start(server->report_thread);
    return true;
}

void iec104_server_run(Iec104Server* server)
{
    while (g_running) {
        (void)server;
        Thread_sleep(10);
    }
}

void iec104_server_stop(Iec104Server* server)
{
    if (!server || !server->slave)
        return;

    LOG_INFO("iec104", "stopping server");
    server->worker_running = false;
    diag_server_stop(&server->diag);
    for (int i = 0; i < server->client_count; i++)
        client_context_stop(&server->clients[i]);

    if (server->shm_thread) {
        Thread_destroy(server->shm_thread);
        server->shm_thread = NULL;
    }

    if (server->report_thread) {
        Thread_destroy(server->report_thread);
        server->report_thread = NULL;
    }

    CS104_Slave_stop(server->slave);
    shm_adapter_close(&server->shm);
}

void iec104_server_destroy(Iec104Server* server)
{
    if (!server)
        return;

    if (server->slave)
        CS104_Slave_destroy(server->slave);

    for (int i = 0; i < server->client_count; i++)
        client_context_destroy(&server->clients[i]);
    free(server->clients);

    diag_server_destroy(&server->diag);
    shm_adapter_destroy(&server->shm);
    history_store_destroy(&server->history_store);
    soe_history_destroy(&server->soe_history);
    active_upload_destroy(&server->active_upload);

    if (server->lock)
        Semaphore_destroy(server->lock);

    free(server);
}
