#ifndef IEC104_SLAVE_COMMAND_HANDLER_H
#define IEC104_SLAVE_COMMAND_HANDLER_H

#include <stdbool.h>

#include "cs104_slave.h"
#include "point_table.h"

bool command_handle_asdu(PointTable* table, IMasterConnection connection, CS101_ASDU asdu);

#endif
