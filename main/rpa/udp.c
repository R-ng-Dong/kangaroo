#include "udp.h"
#include <string.h> 
#include <stdlib.h> 
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define BUFLEN 1024
#define PORT 8181

typedef struct req_udp_handle_register {
    char cmd[32];
    OnRpcCallbackFunc cbFunc;
    STAILQ_ENTRY(req_udp_handle_register) entries;
}req_udp_handle_register_t;

typedef struct {
    int fd;
    int port;
    bool is_running;
}udp_t;
static udp_t udp;

STAILQ_HEAD(list_cb_udp, req_udp_handle_register);
static struct list_cb_udp onRpcCallbackFuncList;

static void udp_init_list(void) {
    STAILQ_INIT(&onRpcCallbackFuncList);
}

static void udp_init_port(int _port)
{
    udp.port = _port;
    udp.is_running = false;
}

static int udp_send_mess(char* message, ssize_t mess_length, struct sockaddr_in* client_addr, int slen)
{
    ESP_LOGI("UDP", "Send message to IP: %s:%i, len: %d", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), slen);
    if (sendto(udp.fd, message, mess_length, 0, (struct sockaddr*)client_addr, slen) == -1)
    {
        ESP_LOGE("UDP", "UDP sendto error");
        return CODE_ERR;
    }
    return CODE_OK;
}

void udp_register_callback_func(const char* cmd, OnRpcCallbackFunc func)
{
    req_udp_handle_register_t* req_udp_handle_register = NULL;
    STAILQ_FOREACH(req_udp_handle_register, &onRpcCallbackFuncList, entries)
    {
        if (strcmp(cmd, req_udp_handle_register->cmd) == 0)
        {
            req_udp_handle_register->cbFunc = func;
            return;
        }
    }

    req_udp_handle_register = (req_udp_handle_register_t*)malloc(sizeof(req_udp_handle_register_t));
    if (req_udp_handle_register)
    {
        strcpy(req_udp_handle_register->cmd, cmd);
        req_udp_handle_register->cbFunc = func;
        STAILQ_INSERT_TAIL(&onRpcCallbackFuncList, req_udp_handle_register, entries);
    }
}



void UdpOnMessage(char* message, ssize_t mess_length, struct sockaddr_in* client_addr, int slen)
{
    ESP_LOGI("UDP", "message: %.*s", mess_length, message);
    cJSON* payloadJson = cJSON_ParseWithLength(message, mess_length);
    if (payloadJson)
    {
        if (cJSON_HasObjectItem(payloadJson, "process") && cJSON_HasObjectItem(payloadJson, "data"))
        {
            cJSON* process = cJSON_GetObjectItem(payloadJson, "process");
            cJSON* params = cJSON_GetObjectItem(payloadJson, "data");
            if (process && cJSON_IsString(process))
            {
                req_udp_handle_register_t* req_udp = NULL;
                STAILQ_FOREACH(req_udp, &onRpcCallbackFuncList, entries)
                {
                    if (strcmp(process->valuestring, req_udp->cmd) == 0)
                    {
                        ESP_LOGW("UDP", "===>>> PROCESS: %s", process->valuestring);
                        cJSON* respJson = cJSON_CreateObject();
                        if (respJson)
                        {
                            int rs = req_udp->cbFunc(params, respJson);
                            if (rs == CODE_OK)
                            {
                                char* respStr = cJSON_PrintUnformatted(respJson);
                                if (respStr) {
                                    udp_send_mess(respStr, strlen(respStr), client_addr, slen);
                                    free(respStr);
                                }
                            }
                            else if (rs == CODE_DATA_ARRAY)
                            {
                                if (cJSON_IsArray(respJson))
                                {
#if 0
                                    int size_json = cJSON_GetArraySize(respJson);
                                    for (size_t i = 0; i < size_json; i++)
                                    {
                                        cJSON* item = cJSON_GetArrayItem(respJson, i);
                                        char* respStr = cJSON_PrintUnformatted(respJson); //convert JSON -> string
                                        if (respStr)
                                        {
                                            udp_send_mess(respStr, strlen(respStr), client_addr, slen);
                                            free(respStr);
                                        }
                                    }
#endif
                                    /*Cach 2*/
                                    cJSON* item = NULL;
                                    cJSON_ArrayForEach(item, respJson)
                                    {
                                        char* respStr = cJSON_PrintUnformatted(item); // convert JSON -> string
                                        if (respStr)
                                        {
                                            udp_send_mess(respStr, strlen(respStr), client_addr, slen);
                                            free(respStr);
                                        }
                                    }
                                }
                            }
                            else if (rs == CODE_NOT_RESPONSE)
                            {
                                ESP_LOGW("UDP", "UDP no resp");
                            }
                            else if (rs == CODE_EXIT)
                            {
                                ESP_LOGE("UDP", "Call %s OK, rs: %d", process->string, rs);
                                char* respStr = cJSON_PrintUnformatted(respJson); // convert JSON -> string
                                if (respStr)
                                {
                                    udp_send_mess(respStr, strlen(respStr), client_addr, slen);
                                    free(respStr);
                                }
                                exit(1);
                            }
                            else
                            {
                                ESP_LOGE("UDP", "===xxx===>> ERROR Call %s OK, rs: %d", process->string, rs);
                            }
                            cJSON_Delete(respJson);
                        }
                        break;
                    }
                }
            }
        }
        else {
            printf("UDP params NULL\n");
        }
    }
    cJSON_Delete(payloadJson);
}

struct sockaddr_in client_addr;
static void UdpHandleMessage(void* param)
{
    ESP_LOGI("UDP", "Start UdpHandleMessage");

    struct sockaddr_in server_addr;
    ssize_t recv_len;

    /* cap phat vao vung nho SPIRAM, neu khong duoc thi cap phat vao internal RAM*/
    char* buf = (char*)heap_caps_malloc_prefer(BUFLEN, 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
    if (!buf) {
        ESP_LOGI("UDP", "malloc buf fail, cancel Task");
        vTaskDelete(NULL);
    }

    // create a UDP socket
    if ((udp.fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        ESP_LOGE("UDP", "create socket fail");
        free(buf);
        vTaskDelete(NULL);
    }

    int broadcastEnable = 1;
    setsockopt(udp.fd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    // zero out the structure
    memset((char*)&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(udp.port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind socket to port
    if (bind(udp.fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        ESP_LOGE("UDP", "bind fail");
        close(udp.fd);
        free(buf);
        vTaskDelete(NULL);
    }

    // keep listening for data
    while (udp.is_running)
    {
        ESP_LOGI("UDP", "UDP Waiting for data...");
        fflush(stdout);
        socklen_t slen = sizeof(struct sockaddr_in);
        // try to receive some data, this is a blocking call
        if ((recv_len = recvfrom(udp.fd, buf, BUFLEN, 0, (struct sockaddr*)&client_addr, (socklen_t*)&slen)) == -1)
        {
            ESP_LOGE("UDP", "recvfrom fail");
            break;
        }

        buf[recv_len] = '\0';
        UdpOnMessage(buf, recv_len, &client_addr, slen);
    }
    if (udp.fd >= 0) {
        close(udp.fd);
        udp.fd = -1;
        ESP_LOGE("UDP", "Socket closed");
    }

    ESP_LOGE("UDP", "free memory buff, cancel task");
    if (buf) {
        free(buf);
    }
    vTaskDelete(NULL);
}

void udp_init_socket(void)
{
    static bool is_first_init = true;
    if (is_first_init) {
        is_first_init = false;
        udp_init_list();
        udp_init_callback();
    }
    ESP_LOGW("UDP", "init socket datagram port %d", PORT);
    udp_init_port(PORT);
    if (!udp.is_running)
    {
        udp.is_running = true;
        ESP_LOGI("UDP", "Free memory: %ld bytes, internal: %ld bytes", esp_get_free_heap_size(), esp_get_free_internal_heap_size());
        if (xTaskCreate(UdpHandleMessage, "UdpHandleMessage", 2048 * 5, NULL, 10, NULL) != pdPASS)
        {
            ESP_LOGE("UDP", "Failed to create task");
            // todo
        }
        vTaskDelay(10);
    }
}

void udp_stop_socket(void)
{
    ESP_LOGE("UDP", "stop socket datagram");
    if (udp.fd >= 0) {
        close(udp.fd);
        udp.fd = -1;
        ESP_LOGE("UDP", "Socket closed");
    }
    udp.is_running = false;
}

bool udp_is_running(void)
{
    return udp.is_running;
}

extern char *get_mac_wifi(void);
void test_input_send_msg(uint8_t state){
    if(!udp.is_running) return;
    socklen_t slen = sizeof(struct sockaddr_in);
    cJSON* respJson = cJSON_CreateObject();
    cJSON_AddStringToObject(respJson, "process", PROCESS_PCBA);
    cJSON *data_param = cJSON_CreateObject();
    cJSON_AddStringToObject(data_param, KEY_COMMAND, "input");
    cJSON_AddStringToObject(data_param, KEY_MAC_WIFI, get_mac_wifi());
    cJSON_AddStringToObject(data_param, KEY_TYPE, "response");
    cJSON_AddNumberToObject(data_param, "state", state);
    cJSON_AddItemToObject(respJson, "data", data_param);
    if(respJson)
    {
        char* respStr = cJSON_PrintUnformatted(respJson);
        if(respStr)
        {
            udp_send_mess(respStr, strlen(respStr), &client_addr, slen);
            free(respStr);
        }
        cJSON_Delete(respJson);
    }
}







