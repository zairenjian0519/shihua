#ifndef IEC104_SLAVE_CLIENT_CONTEXT_H
#define IEC104_SLAVE_CLIENT_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "cs104_slave.h"
#include "hal_thread.h"
#include "point_table.h"

typedef enum {
    MSG_TOTAL_CALL = 0,
    MSG_COUNTER_CALL = 1,
    MSG_READ_POINT = 2,
    MSG_CLOCK_SYNC = 3,
    MSG_REMOTE_CONTROL = 4,
    MSG_REMOTE_ADJUST = 5,
    MSG_CUSTOM_CALL = 6,
    MSG_HISTORY_CALL = 7,
    MSG_ACTIVE_UPLOAD = 8,
    MSG_CONNECTION_CLOSED = 9
} Iec104MsgType;

#define IEC104_MAX_SETPOINTS_PER_ASDU 40

typedef struct {
    uint32_t ioa;
    float value;
    uint8_t select;
    int qualifier;
} Iec104SetpointItem;

typedef struct {
    Iec104MsgType type;
    int ca;
    uint32_t ioa;
    uint8_t type_id;
    uint8_t cot;
    uint8_t qoi;
    uint8_t qcc;
    uint8_t oa;
    uint64_t request_id;
    uint64_t upload_version;
    uint8_t has_yx;
    uint8_t has_yc;
    uint8_t has_soe;
    uint8_t command_select;
    uint8_t command_state;
    int command_qualifier;
    float setpoint_value;
    uint8_t setpoint_count;
    Iec104SetpointItem setpoints[IEC104_MAX_SETPOINTS_PER_ASDU];
    CP56Time2a begin_time;
    CP56Time2a end_time;
    uint8_t payload[256];
    uint16_t payload_len;
} Iec104WorkMsg;

typedef struct ClientContext ClientContext;

typedef bool (*ClientWorkHandler)(void* owner, ClientContext* client, const Iec104WorkMsg* msg);

struct ClientContext {
    IMasterConnection connection;
    Thread worker_thread;
    Semaphore queue_lock;
    Semaphore queue_items;
    Iec104WorkMsg* queue;
    int queue_capacity;
    int queue_head;
    int queue_tail;
    int queue_count;
    volatile bool worker_running;
    bool connected;
    bool started;
    uint64_t last_send_time_ms;
    uint64_t last_uploaded_version;
    ClientWorkHandler handler;
    void* owner;
};

bool client_context_init(ClientContext* ctx, int queue_capacity,
                         ClientWorkHandler handler, void* owner);
void client_context_destroy(ClientContext* ctx);

bool client_context_start(ClientContext* ctx);
void client_context_stop(ClientContext* ctx);

void client_context_bind_connection(ClientContext* ctx, IMasterConnection connection);
void client_context_close_connection(ClientContext* ctx);

bool client_context_post(ClientContext* ctx, const Iec104WorkMsg* msg, bool high_priority);
bool client_context_is_active(ClientContext* ctx);
void client_context_wait_send_interval(ClientContext* ctx, uint64_t interval_ms);

#endif
