#ifndef IEC104_SLAVE_DIAG_SERVER_H
#define IEC104_SLAVE_DIAG_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "active_upload.h"
#include "client_context.h"
#include "hal_thread.h"
#include "point_table.h"
#include "soe_history.h"

typedef bool (*DiagNotifyUpload)(void* owner, uint64_t upload_version);

typedef struct {
    bool enabled;
    bool writable;
    bool allow_clear;
    char bind_ip[64];
    int port;
} DiagConfig;

typedef struct {
    DiagConfig config;
    PointTable* table;
    ActiveUploadArea* active_upload;
    SoeHistory* soe_history;
    DiagNotifyUpload notify_upload;
    void* owner;
    Thread thread;
    volatile bool running;
    int listen_fd;
} DiagServer;

bool diag_server_init(DiagServer* diag, const DiagConfig* config,
                      PointTable* table, ActiveUploadArea* active_upload,
                      SoeHistory* soe_history, DiagNotifyUpload notify_upload,
                      void* owner);
void diag_server_destroy(DiagServer* diag);
bool diag_server_start(DiagServer* diag);
void diag_server_stop(DiagServer* diag);
bool diag_server_execute(DiagServer* diag, const char* command,
                         char* response, size_t response_size);

#endif
