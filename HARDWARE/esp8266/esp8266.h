#ifndef __ESP8266_H
#define __ESP8266_H

#include "sys.h"

// ================== 配置常量（请根据实际情况修改） ==================
#define WIFI_SSID         "Ace"                     // WiFi 名称
#define WIFI_PASSWORD     "20030622"                // WiFi 密码
#define MQTT_CLIENT_ID    "768166ef5bdc47a0be78f4e67514b277"  // 巴法云私钥（Client ID）
#define MQTT_BROKER       "bemfa.com"
#define MQTT_PORT         "9501"
#define MQTT_TOPIC        "esp02"                   // 统一主题（上报 + 控制）

// ================== 外部变量声明 ==================
extern u8 mqtt_connected;        /* MQTT连接状态（在main.c中定义） */
extern u8 led_state;             /* LED状态（在main.c中定义） */

// ================== 函数声明 ==================
void ESP8266_Write_Byte(u8 data);
void ESP8266_Write_String(u8 *str);
void ESP8266_Clear_Buf(void);
u8 ESP8266_Send_Cmd(u8 *cmd, u8 *ack, u16 waittime);
u8 ESP8266_Connect_Server(void);
void ESP8266_Send_Data(u8 *payload);
void ESP8266_Receive_Proc(void);

#endif
