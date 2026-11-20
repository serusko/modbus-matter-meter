idf_component_register(SRCS "modbus_rtu.c" "le03mw.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_uart)
