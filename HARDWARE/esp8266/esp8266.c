#include "esp8266.h"
#include "usart.h"      // 包含 USART1 和 USART2 的驱动头文件
#include "delay.h"
#include "led.h"        // 包含LED控制相关宏定义
#include <stdio.h>
#include <string.h>

// ================== 外部变量声明 ==================
extern u8 mqtt_connected;    /* 引入 main.c 中的 MQTT 连接状态变量 */
extern u8 led_state;         /* 引入 main.c 中的 LED 状态变量 */

// ================== 底层发送函数 ==================
/**
 * @brief  向ESP8266发送一个字节数据
 * @param  data: 要发送的字节
 */
void ESP8266_Write_Byte(u8 data)
{
    USART_SendData(USART2, data);
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET); // 等待发送完成
}

/**
 * @brief  向ESP8266发送字符串
 * @param  str: 要发送的字符串指针
 */
void ESP8266_Write_String(u8 *str)
{
    while (*str)
    {
        ESP8266_Write_Byte(*str++);
    }
}

/**
 * @brief  清空 USART2 接收缓冲区
 */
void ESP8266_Clear_Buf(void)
{
    memset(usart2_rx_buf, 0, sizeof(usart2_rx_buf));
    usart2_rx_cnt = 0;
}

/**
 * @brief  发送 AT 指令并等待预期响应
 * @param  cmd: 发送的AT指令字符串
 * @param  ack: 预期的响应字符串
 * @param  waittime: 最大等待时间 (单位: 10ms)
 * @retval 0: 成功收到响应; 1: 超时或未收到响应
 */
u8 ESP8266_Send_Cmd(u8 *cmd, u8 *ack, u16 waittime)
{
    u16 timeout = 0;

    ESP8266_Clear_Buf();
    printf("[DEBUG] Send AT: %s", cmd); 
    ESP8266_Write_String(cmd);

    while (timeout < waittime)
    {
        delay_ms(10);
        timeout += 10;

        if (usart2_rx_cnt > 0)
        {
            if (strstr((char *)usart2_rx_buf, (char *)ack) != NULL)
            {
                printf("[ACK] Success: %s\r\n", ack);
                return 0;  // 成功
            }
        }
    }
    printf("[ERROR] Timeout! Expected '%s', Got:\r\n%s\r\n", ack, usart2_rx_buf);
    return 1;  // 失败
}

/**
 * @brief  连接 WiFi 并配置 MQTT 参数
 * @retval 0: 成功; 其他值: 失败对应的错误码
 */
u8 ESP8266_Connect_Server(void)
{
    printf("\r\n=== System Init ===\r\n");

    delay_ms(8000);  // 给 ESP8266 足够的启动时间

    // 1. 测试 AT 指令通信
    if (ESP8266_Send_Cmd((u8 *)"AT\r\n", (u8 *)"OK", 3000))
    {
        printf("Error: Module no response\r\n");
        return 1;
    }

    // 2. 设置 WiFi 模式为 Station (STA)
    if (ESP8266_Send_Cmd((u8 *)"AT+CWMODE=1\r\n", (u8 *)"OK", 2000))
    {
        printf("Error: Set STA mode failed\r\n");
        return 2;
    }

    // 3. 连接指定 WiFi 热点
    char cmd[128];
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
    if (ESP8266_Send_Cmd((u8 *)cmd, (u8 *)"WIFI GOT IP", 15000))
    {
        printf("Error: WiFi connect failed\r\n");
        return 3;
    }
    printf("Status: WiFi Connected\r\n");

    // 4. 设置为单连接模式
    ESP8266_Send_Cmd((u8 *)"AT+CIPMUX=0\r\n", (u8 *)"OK", 2000);

    // 5. 配置 MQTT 用户参数
    sprintf(cmd, "AT+MQTTUSERCFG=0,1,\"%s\",\"\",\"\",0,0,\"\"\r\n", MQTT_CLIENT_ID);
    if (ESP8266_Send_Cmd((u8 *)cmd, (u8 *)"OK", 3000))
    {
        printf("Error: MQTTUSERCFG failed\r\n");
        return 4;
    }

    // 6. 连接到 MQTT 服务器
    sprintf(cmd, "AT+MQTTCONN=0,\"%s\",%s,0\r\n", MQTT_BROKER, MQTT_PORT);
    if (ESP8266_Send_Cmd((u8 *)cmd, (u8 *)"+MQTTCONNECTED", 10000))
    {
        printf("Error: MQTT connect failed\r\n");
        return 5;
    }
    printf("Status: MQTT Connected to Cloud\r\n");

    // 7. 订阅控制指令主题 (QoS=0)
    sprintf(cmd, "AT+MQTTSUB=0,\"%s\",0\r\n", MQTT_SUB_TOPIC);
    if (ESP8266_Send_Cmd((u8 *)cmd, (u8 *)"OK", 3000))
    {
        printf("Error: Subscribe topic failed\r\n");
        return 6;
    }
    printf("Status: Subscribed Topic: %s\r\n", MQTT_SUB_TOPIC);

    printf("=== Init Complete ===\r\n\r\n");
    return 0;
}

/**
 * @brief  通过 MQTT 发送数据（如温湿度）
 * @param  payload: 要发送的数据内容
 */
void ESP8266_Send_Data(u8 *payload)
{
    char cmd[128];
    sprintf(cmd, "AT+MQTTPUB=0,\"%s\",\"%s\",0,0\r\n", MQTT_PUB_TOPIC, payload);
    printf("[UPLOAD] %s\r\n", payload);
    ESP8266_Write_String((u8 *)cmd);
    // 不等待响应，直接返回（防止阻塞主循环）
}

/**
 * @brief  处理接收到的 MQTT 消息
 * @note   解析下行指令，通过回调/标志位的方式通知 main.c 执行
 *         这里不直接操作硬件，保持模块解耦
 */
void ESP8266_Receive_Proc(void)
{
    static char last_cmd[32] = {0};  /* 记录上一次接收到的指令，防止重复触发 */
    
    if (usart2_rx_cnt == 0) return;

    // 检查是否收到 MQTT 数据包
    char *p = strstr((char *)usart2_rx_buf, "+MQTTSUBRECV:");
    if (p != NULL)
    {
        printf("\r\n[MQTT RECV] Raw Data: %s\r\n", usart2_rx_buf);

        // 解析 MQTT 接收数据格式: +MQTTSUBRECV:0,"Topic",QoS,Data
        // 找到 payload 的起始位置（第3个逗号之后）
        char *payload = strchr(p, ',');
        if (payload) payload = strchr(payload + 1, ',');
        if (payload) payload = strchr(payload + 1, ',');
        if (payload)
        {
            payload++;  // 跳过逗号，指向实际数据
            
            // 去除可能存在的引号和空格
            while (*payload == ' ' || *payload == '\"') payload++;
            
            // 提取有效指令字符串（遇到换行或引号停止）
            char cmd[32] = {0};
            int i = 0;
            while (*payload && *payload != '\"' && *payload != '\r' && *payload != '\n' && i < 31)
            {
                cmd[i++] = *payload++;
            }
            cmd[i] = '\0';
            
            // 转为大写，便于匹配指令
            for (i = 0; cmd[i]; i++)
            {
                if (cmd[i] >= 'a' && cmd[i] <= 'z')
                    cmd[i] -= 32;
            }
            
            // 防抖：若与上次指令相同则忽略
            if (strcmp(last_cmd, cmd) == 0)
            {
                printf("[Ignore] Duplicate Command\r\n");
                ESP8266_Clear_Buf();
                return;
            }
            strcpy(last_cmd, cmd);
            
            // 匹配指令并置位对应标志位，通知 main.c 处理
            if (strstr(cmd, "LED_ON") != NULL)
            {
                printf("[CMD] Turn ON LED\r\n");
                extern u8 led_cmd_flag;
                led_cmd_flag = 1;  /* 1=打开LED */
            }
            else if (strstr(cmd, "LED_OFF") != NULL)
            {
                printf("[CMD] Turn OFF LED\r\n");
                extern u8 led_cmd_flag;
                led_cmd_flag = 2;  /* 2=关闭LED */
            }
            else if (strstr(cmd, "QUERY") != NULL)
            {
                printf("[CMD] Query Status\r\n");
                extern u8 query_cmd_flag;
                query_cmd_flag = 1;  /* 触发状态上报 */
            }
            else
            {
                printf("[CMD] Unknown: %s\r\n", cmd);
            }
        }

        ESP8266_Clear_Buf();
    }
}

