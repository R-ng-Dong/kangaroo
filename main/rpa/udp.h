#ifndef RD_UDP_H_
#define RD_UDP_H_

#include <stdio.h>	
#include "stdbool.h"
#include <sys/queue.h>
#include <cJSON.h>

#define PROCESS_PCBA "pcba" 
#define PROCESS_TEST_UART "uartTest" 


#define KEY_MAC_WIFI "mac"
#define KEY_COMMAND "cmd"
#define KEY_SSID "ssid"
#define KEY_RSSI "rssi"
#define KEY_TYPE "type"


typedef enum
{
	CODE_ERR = -1,
	CODE_OK = 0,
	CODE_NOT_RESPONSE,
	CODE_DATA_ARRAY,
	CODE_TIMEOUT,
	CODE_NOT_FOUND_DEVICE,
	CODE_NOT_FOUND_GROUP,
	CODE_NOT_FOUND_SCENE,
	CODE_NOT_FOUND_RULE,
	CODE_NOT_FOUND_ROOM,
	CODE_FORMAT_ERROR,
	CODE_DATABASE_ERROR,
	CODE_MEMORY_ERROR,
	CODE_FILE_ERROR,
	CODE_EXIT,
	CODE_REBOOT,
	CODE_FACTORY,
	CODE_OTA_ERROR,
	CODE_OTA_DATA_ERROR,
	CODE_OTA_CHECK_CRC_ERROR,

	CODE_FLASH_ERR,
} ErrorCode;

typedef int (*OnRpcCallbackFunc)(cJSON* reqJson, cJSON* respJson);

bool udp_is_running(void);
void udp_init_socket(void);
void udp_stop_socket(void);
void udp_register_callback_func(const char* cmd, OnRpcCallbackFunc func);
void udp_init_callback(void);

void test_input_send_msg(uint8_t state);

#endif /* RD_UDP_H_ */
