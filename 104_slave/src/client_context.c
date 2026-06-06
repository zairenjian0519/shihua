#include "client_context.h"

#include "hal_time.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

static bool queue_push_locked(ClientContext* ctx, const Iec104WorkMsg* msg, bool high_priority)
{
    if (ctx->queue_count >= ctx->queue_capacity) {
        if (msg->type == MSG_ACTIVE_UPLOAD) {
            for (int i = 0; i < ctx->queue_count; i++) {
                int index = (ctx->queue_head + i) % ctx->queue_capacity;
                if (ctx->queue[index].type == MSG_ACTIVE_UPLOAD) {
                    ctx->queue[index] = *msg;
                    return true;
                }
            }
        }

        return false;
    }

    if (high_priority) {
        ctx->queue_head = (ctx->queue_head - 1 + ctx->queue_capacity) % ctx->queue_capacity;
        ctx->queue[ctx->queue_head] = *msg;
    }
    else {
        ctx->queue[ctx->queue_tail] = *msg;
        ctx->queue_tail = (ctx->queue_tail + 1) % ctx->queue_capacity;
    }

    ctx->queue_count++;
    return true;
}

static bool queue_pop(ClientContext* ctx, Iec104WorkMsg* msg)
{
    Semaphore_wait(ctx->queue_items);
    Semaphore_wait(ctx->queue_lock);

    if (ctx->queue_count == 0) {
        Semaphore_post(ctx->queue_lock);
        return false;
    }

    *msg = ctx->queue[ctx->queue_head];
    ctx->queue_head = (ctx->queue_head + 1) % ctx->queue_capacity;
    ctx->queue_count--;

    Semaphore_post(ctx->queue_lock);
    return true;
}

static void* client_worker_thread(void* parameter)
{
    ClientContext* ctx = (ClientContext*)parameter;

    LOG_INFO("client", "worker thread started");

    while (ctx->worker_running) {
        Iec104WorkMsg msg;

        if (!queue_pop(ctx, &msg))
            continue;

        if (msg.type == MSG_CONNECTION_CLOSED) {
            ctx->connected = false;
            continue;
        }

        if (ctx->handler)
            ctx->handler(ctx->owner, ctx, &msg);
    }

    LOG_INFO("client", "worker thread stopped");
    return NULL;
}

bool client_context_init(ClientContext* ctx, int queue_capacity,
                         ClientWorkHandler handler, void* owner)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->queue_capacity = queue_capacity;
    ctx->queue = (Iec104WorkMsg*)calloc((size_t)queue_capacity, sizeof(Iec104WorkMsg));
    ctx->queue_lock = Semaphore_create(1);
    ctx->queue_items = Semaphore_create(0);
    ctx->handler = handler;
    ctx->owner = owner;

    return ctx->queue && ctx->queue_lock && ctx->queue_items;
}

void client_context_destroy(ClientContext* ctx)
{
    if (!ctx)
        return;

    client_context_stop(ctx);

    if (ctx->queue_lock)
        Semaphore_destroy(ctx->queue_lock);

    if (ctx->queue_items)
        Semaphore_destroy(ctx->queue_items);

    free(ctx->queue);
    memset(ctx, 0, sizeof(*ctx));
}

bool client_context_start(ClientContext* ctx)
{
    ctx->worker_running = true;
    ctx->worker_thread = Thread_create(client_worker_thread, ctx, false);
    if (!ctx->worker_thread) {
        ctx->worker_running = false;
        return false;
    }

    Thread_start(ctx->worker_thread);
    return true;
}

void client_context_stop(ClientContext* ctx)
{
    if (!ctx || !ctx->worker_thread)
        return;

    ctx->worker_running = false;
    Semaphore_post(ctx->queue_items);
    Thread_destroy(ctx->worker_thread);
    ctx->worker_thread = NULL;
}

void client_context_bind_connection(ClientContext* ctx, IMasterConnection connection)
{
    Semaphore_wait(ctx->queue_lock);
    ctx->connection = connection;
    ctx->connected = true;
    ctx->started = false;
    ctx->queue_head = 0;
    ctx->queue_tail = 0;
    ctx->queue_count = 0;
    ctx->last_send_time_ms = 0;
    ctx->last_uploaded_version = 0;
    Semaphore_post(ctx->queue_lock);
}

void client_context_close_connection(ClientContext* ctx)
{
    if (!ctx)
        return;

    Semaphore_wait(ctx->queue_lock);
    ctx->connection = NULL;
    ctx->connected = false;
    ctx->started = false;
    ctx->queue_head = 0;
    ctx->queue_tail = 0;
    ctx->queue_count = 0;
    ctx->last_send_time_ms = 0;
    ctx->last_uploaded_version = 0;
    Semaphore_post(ctx->queue_lock);

    Semaphore_post(ctx->queue_items);
}

bool client_context_post(ClientContext* ctx, const Iec104WorkMsg* msg, bool high_priority)
{
    bool ok = false;

    if (!ctx || !ctx->worker_thread)
        return false;

    Semaphore_wait(ctx->queue_lock);
    ok = queue_push_locked(ctx, msg, high_priority);
    Semaphore_post(ctx->queue_lock);

    if (ok)
        Semaphore_post(ctx->queue_items);

    return ok;
}

bool client_context_is_active(ClientContext* ctx)
{
    return ctx && ctx->connected && ctx->connection != NULL;
}

void client_context_wait_send_interval(ClientContext* ctx, uint64_t interval_ms)
{
    uint64_t now = Hal_getMonotonicTimeInMs();

    if (ctx->last_send_time_ms != 0 && now < ctx->last_send_time_ms + interval_ms) {
        uint64_t wait_ms = ctx->last_send_time_ms + interval_ms - now;
        if (wait_ms > 0 && wait_ms < 1000)
            Thread_sleep((int)wait_ms);
    }

    ctx->last_send_time_ms = Hal_getMonotonicTimeInMs();
}
