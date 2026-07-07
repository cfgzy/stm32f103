    #ifndef __ESP8266_H
    #define __ESP8266_H

    #include "sys.h"

    // ================== 常用宏定义（用户需根据实际情况修改） ==================
    #define WIFI_SSID         "YOUR_WIFI_SSID"                     // WiFi 名称
    #define WIFI_PASSWORD     "YOUR_WIFI_PASSWORD"                // WiFi 密码
    // 注意：这里是巴法云的私钥，作为Client ID使用
    #define MQTT_CLIENT_ID    "768166ef5bdc47a0be78f4e67514b277"
    #define MQTT_BROKER       "mqtt.bemfa.com"          // MQTT 服务器地址
    #define MQTT_PORT         "9501"                    // MQTT 端口号
    #define MQTT_PUB_TOPIC    "YOUR_BEMFA_PUB_TOPIC"                 // 温湿度上报主题
    #define MQTT_SUB_TOPIC    "YOUR_BEMFA_SUB_TOPIC"                // 云端控制命令主题

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
    
