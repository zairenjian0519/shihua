#ifndef IEC104_SLAVE_SERVER_H
#define IEC104_SLAVE_SERVER_H

#include <stdbool.h>

#include "config.h"
#include "point_table.h"

typedef struct Iec104Server Iec104Server;

bool iec104_server_init(Iec104Server** server, const Iec104Config* config, PointTable* table);
bool iec104_server_start(Iec104Server* server);
void iec104_server_run(Iec104Server* server);
void iec104_server_stop(Iec104Server* server);
void iec104_server_destroy(Iec104Server* server);
void iec104_server_request_stop(void);

#endif
