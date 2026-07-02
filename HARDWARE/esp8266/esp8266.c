#include "esp8266.h"
#include "usart.h"      // 包含 USART1 和 USART2 的定义
#include "delay.h"
#include "led.h"        // 用于LED控制宏定义
#include <stdio.h>
#include <string.h>

// ================== 外部变量引用 ==================
extern u8 mqtt_connected;    /* 引用main.c中的MQTT连接状态 */
extern u8 led_state;         /* 引用main.c中的LED状态 */

// ================== 底层发送函数 ==================
void ESP8266_Write_Byte(u8 data)
{
    USART_SendData(USART2, data);
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
}

void ESP8266_Write_String(u8 *str)
{
    while (*str)
    {
        ESP8266_Write_Byte(*str++);
    }
}

// 清空 USART2 接收缓冲区
void ESP8266_Clear_Buf(void)
{
    memset(usart2_rx_buf, 0, sizeof(usart2_rx_buf));
    usart2_rx_cnt = 0;
}

// 发送 AT 指令并等待指定应答
// 返回 0: 成功   1: 失败
u8 ESP8266_Send_Cmd(u8 *cmd, u8 *ack, u16 waittime)
{
    u16 timeout = 0;

    ESP8266_Clear_Buf();

    printf("[发送AT] %s", cmd);
    ESP8266_Write_String(cmd);

    while (timeout < waittime)
    {
        delay_ms(10);
        timeout += 10;

        if (usart2_rx_cnt > 0)
        {
            if (strstr((char *)usart2_rx_buf, (char *)ack) != NULL)
            {
                printf("[收到预期] %s\r\n", ack);
                return 0;  // 成功
            }
        }
    }

    printf("[超时] 未收到 '%s'，实际收到:\r\n%s\r\n", ack, usart2_rx_buf);
    return 1;  // 失败
}

// 连接 WiFi + MQTT + 订阅主题
u8 ESP8266_Connect_Server(void)
{
    printf("\r\n=== 开始连接巴法云 ===\r\n");

    delay_ms(8000);  // 给 ESP8266 足够启动时间

    // 1. 测试通信
    if (ESP8266_Send_Cmd((u8 *)"AT\r\n", (u8 *)"OK", 3000))
    {
        printf("模块无响应\r\n");
        return 1;
    }

    // 2. 设置 STA 模式
    if (ESP8266_Send_Cmd((u8 *)"AT+CWMODE=1\r\n", (u8 *)"OK", 2000))
    {
        printf("设置 STA 模式失败\r\n");
        return 2;
    }

    // 3. 连接 WiFi
    char cmd[128];
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
    if (ESP8266_Send_Cmd((u8 *)cmd, (u8 *)"WIFI GOT IP", 15000))
    {
        printf("WiFi 连接失败\r\n");
        return 3;
    }
    printf("WiFi 已连接\r\n");

    // 4. 设置单连接模式
    ESP8266_Send_Cmd((u8 *)"AT+CIPMUX=0\r\n", (u8 *)"OK", 2000);

    // 5. 配置 MQTT 参数（巴法云专用：client_id = 私钥，user/pass 空）
    sprintf(cmd, "AT+MQTTUSERCFG=0,1,\"%s\",\"\",\"\",0,0,\"\"\r\n", MQTT_CLIENT_ID);
    if (ESP8266_Send_Cmd((u8 *)cmd, (u8 *)"OK", 3000))
    {
        printf("MQTTUSERCFG 失败\r\n");
        return 4;
    }

    // 6. 连接巴法云 MQTT
    sprintf(cmd, "AT+MQTTCONN=0,\"%s\",%s,0\r\n", MQTT_BROKER, MQTT_PORT);
    if (ESP8266_Send_Cmd((u8 *)cmd, (u8 *)"+MQTTCONNECTED", 10000))
    {
        printf("MQTT 连接失败\r\n");
        return 5;
    }
    printf("MQTT 已连接到巴法云\r\n");

    // 7. 订阅主题（QoS=0，避免重复接收问题）
    sprintf(cmd, "AT+MQTTSUB=0,\"%s\",0\r\n", MQTT_TOPIC);
    if (ESP8266_Send_Cmd((u8 *)cmd, (u8 *)"OK", 3000))
    {
        printf("订阅主题失败\r\n");
        return 6;
    }
    printf("已订阅主题: %s\r\n", MQTT_TOPIC);

    printf("=== 巴法云连接完成 ===\r\n\r\n");
    return 0;
}

// 发布数据（温湿度）
void ESP8266_Send_Data(u8 *payload)
{
    char cmd[128];
    sprintf(cmd, "AT+MQTTPUB=0,\"%s\",\"%s\",0,0\r\n", MQTT_TOPIC, payload);
    printf("[发布] %s", cmd);
    ESP8266_Write_String((u8 *)cmd);
    // 不等待响应，让主循环处理后续接收
}

/**
 * @brief 处理接收到的MQTT消息
 * @note  解析云端指令，通过回调函数方式通知main.c执行
 *        避免直接控制硬件，保持模块独立性
 */
void ESP8266_Receive_Proc(void)
{
    static char last_cmd[32] = {0};  /* 记录上次指令，避免重复执行 */
    
    if (usart2_rx_cnt == 0) return;

    char *p = strstr((char *)usart2_rx_buf, "+MQTTSUBRECV:");
    if (p != NULL)
    {
        printf("\r\n[收到云端消息] %s\r\n", usart2_rx_buf);

        // 找到payload起始位置（格式：+MQTTSUBRECV:0,"主题",长度,内容）
        char *payload = strchr(p, ',');
        if (payload) payload = strchr(payload + 1, ',');
        if (payload) payload = strchr(payload + 1, ',');
        if (payload)
        {
            payload++;  // 跳过逗号，指向实际内容
            
            // 去除可能的引号和空格
            while (*payload == ' ' || *payload == '\"') payload++;
            
            // 提取指令（遇到引号或换行结束）
            char cmd[32] = {0};
            int i = 0;
            while (*payload && *payload != '\"' && *payload != '\r' && *payload != '\n' && i < 31)
            {
                cmd[i++] = *payload++;
            }
            cmd[i] = '\0';
            
            // 转换为大写便于比较
            for (i = 0; cmd[i]; i++)
            {
                if (cmd[i] >= 'a' && cmd[i] <= 'z')
                    cmd[i] -= 32;
            }
            
            // 避免重复执行相同指令
            if (strcmp(last_cmd, cmd) == 0)
            {
                printf("→ 重复指令，忽略执行\r\n");
                ESP8266_Clear_Buf();
                return;
            }
            strcpy(last_cmd, cmd);
            
            // 解析并执行指令（通过设置标志位，让main.c处理）
            if (strstr(cmd, "LED_ON") != NULL)
            {
                printf("→ 云端指令：开启LED\r\n");
                // 通知main.c执行LED开启（通过全局变量）
                extern u8 led_cmd_flag;
                led_cmd_flag = 1;  /* 1=开启LED */
            }
            else if (strstr(cmd, "LED_OFF") != NULL)
            {
                printf("→ 云端指令：关闭LED\r\n");
                extern u8 led_cmd_flag;
                led_cmd_flag = 2;  /* 2=关闭LED */
            }
            else if (strstr(cmd, "QUERY") != NULL)
            {
                printf("→ 云端指令：查询状态\r\n");
                extern u8 query_cmd_flag;
                query_cmd_flag = 1;  /* 触发状态上报 */
            }
            else
            {
                printf("→ 未知指令: %s\r\n", cmd);
            }
        }

        ESP8266_Clear_Buf();
    }
}
