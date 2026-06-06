#ifndef IEC104_SLAVE_CONFIG_H
#define IEC104_SLAVE_CONFIG_H

#include <stdbool.h>

typedef struct {
    bool enabled;
    char local_ip[64];
    int local_port;
    int common_address;
    int max_open_connections;
    int low_priority_queue_size;
    int high_priority_queue_size;
    int k;
    int w;
    int t0_seconds;
    int t1_seconds;
    int t2_seconds;
    int t3_seconds;
    bool scan_enabled;
    bool active_upload_enabled;
    bool periodic_enabled;
    int periodic_interval_ms;
    int scan_interval_ms;
    bool raw_message_log;
    bool log_enabled;
    char log_level[16];
    char log_file_name[256];
    int log_file_count;
    int log_max_file_size_bytes;
    bool log_append_start_timestamp;
    bool diag_enabled;
    char diag_bind_ip[64];
    int diag_port;
    bool diag_writable;
    bool diag_allow_clear;
} Iec104Config;

void config_init_defaults(Iec104Config* config);
bool config_load_file(const char* path, Iec104Config* config);

#endif
