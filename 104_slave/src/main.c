#include "config.h"
#include "iec104_server.h"
#include "log.h"
#include "point_table.h"

#include "hal_thread.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sigint_handler(int signal_id)
{
    (void)signal_id;
    iec104_server_request_stop();
}

static const char* parse_config_path(int argc, char** argv)
{
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0)
            return argv[i + 1];
    }

    return "104config.json5";
}

int main(int argc, char** argv)
{
    signal(SIGINT, sigint_handler);
#ifdef SIGTERM
    signal(SIGTERM, sigint_handler);
#endif

    log_init(LOG_LEVEL_INFO);

    const char* config_path = parse_config_path(argc, argv);
    Iec104Config config;
    config_load_file(config_path, &config);

    LogConfig log_config;
    memset(&log_config, 0, sizeof(log_config));
    log_config.enabled = config.log_enabled ? 1 : 0;
    log_config.level = log_level_from_string(config.log_level, LOG_LEVEL_INFO);
    snprintf(log_config.file_name, sizeof(log_config.file_name), "%s", config.log_file_name);
    log_config.file_count = config.log_file_count;
    log_config.max_file_size_bytes = config.log_max_file_size_bytes;
    log_config.append_start_timestamp = config.log_append_start_timestamp ? 1 : 0;
    log_init_config(&log_config);

    PointTable table;
    if (!point_table_init_demo(&table)) {
        LOG_ERROR("main", "failed to initialize point table");
        return EXIT_FAILURE;
    }

    Iec104Server* server = NULL;
    if (!iec104_server_init(&server, &config, &table)) {
        LOG_ERROR("main", "failed to initialize IEC104 server");
        point_table_destroy(&table);
        return EXIT_FAILURE;
    }

    if (iec104_server_start(server))
        iec104_server_run(server);

    iec104_server_stop(server);
    iec104_server_destroy(server);
    point_table_destroy(&table);

    Thread_sleep(100);
    log_close();
    return EXIT_SUCCESS;
}
