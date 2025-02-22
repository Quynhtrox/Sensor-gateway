#ifndef HANDLER_SENSOR_H
#define HANDLER_SENSOR_H

#include "shared_data.h"

void add_data(Shared_data *shared, Sensor_data data);
Sensor_data get_data(Shared_data *shared);

#endif //HANDLER_SENSOR_H