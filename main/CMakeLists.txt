set(PROJECT_NAME "modbus_serial_slave")

idf_component_register(SRCS "app_main.c"
                            "button.c"
			     			"http_server.c"
			    			"generic_api.c"
						    "slave.c"
						    "modbus_params.c"
						    "http_ota.c"
                    INCLUDE_DIRS ".")
