/**
 ******************************************************************************
 * @file    main.c
 * @author  Optimized Version
 * @brief   STM32F103ZE 环境监测与控制系统 + ESP8266 联网模块
 * @details 硬件概述：
 *          - 2.8寸LCD显示屏
 *          - DHT11温湿度传感器
 *          - 继电器控制：PA1控制风扇，PA5控制加热器（低电平开启）
 *          - ESP8266-01S通过USART2通信
 *          - 按键功能：WKUP切换设置模式，KEY0增加阈值，KEY1减少阈值
 ******************************************************************************
 */

#include "led.h"
#include "delay.h"
#include "key.h"
#include "sys.h"
#include "gpio.h"
#include "lcd.h"
#include "usart.h"
#include "GBK_LibDrive.h"
#include "dht11.h"
#include "esp8266.h"
#include <stdio.h>
#include <string.h>

/* ================== 系统参数配置 ================== */
#define TEMP_LIMIT_MAX          40      /* 温度上限阈值 */
#define TEMP_LIMIT_MIN          10      /* 温度下限阈值 */
#define REFRESH_TICK_MS         100     /* 主循环刷新周期 */
#define SENSOR_READ_INTERVAL    20      /* 传感器读取间隔 (20 * 100ms = 2s) */
#define MQTT_HEARTBEAT_INTERVAL 300     /* MQTT心跳间隔 (300 * 100ms = 30s) */
#define ESP8266_RETRY_MAX       5       /* ESP8266连接最大重试次数 */
#define ESP8266_RETRY_DELAY_MS  3000    /* 重连延时 */
#define MSG_BUF_SIZE            64      /* 消息缓冲区大小 */ 

/* ==== LED指示灯宏定义 ==== */
#define LED_ON()                GPIO_ResetBits(GPIOB, GPIO_Pin_5)   /* PB5低电平点亮 */
#define LED_OFF()               GPIO_SetBits(GPIOB, GPIO_Pin_5)

/* ================== 全局变量 ================== */
/* 系统时间相关 */
u16 year = 2025;
u8 month = 12, day = 5;
u8 hour = 17, min = 55, sec = 0;

/* 温湿度数据 */
u8 temp = 0;        /* 当前温度(度) */
u8 humi = 0;        /* 当前湿度(%) */

/* 控制参数 */
u8 heat_target = 22;    /* 加热器开启温度阈值 */
u8 fan_target = 28;     /* 风扇开启温度阈值 */

/* 交互与状态 */
u8 setting_mode = 0;    /* 设置模式: 0-正常, 1-设置加热阈值, 2-设置风扇阈值 */
u8 blink_flag = 0;      /* 闪烁标志位，用于设置模式时的闪烁显示 */

/* ESP8266 状态 */
u8 mqtt_connected = 0;  /* MQTT连接状态标志 */

/* LED状态与控制指令 */
u8 led_state = 0;       /* LED状态: 0-关闭, 1-点亮 */
u8 led_cmd_flag = 0;    /* LED指令标志: 0-无指令, 1-点亮, 2-关闭 */
u8 query_cmd_flag = 0;  /* 查询指令标志 */

/* ================== 函数声明 ================== */
/* 系统初始化 */
void System_Init(void);

/* 任务处理函数 */
void Task_UpdateTime(void);         /* 更新时钟日历 */
void Task_SensorRead(void);         /* 读取DHT11温湿度 */
void Task_ControlLogic(void);       /* 执行温度控制逻辑 */
void Task_KeyHandler(void);         /* 处理按键扫描与逻辑 */
void Task_MQTT_Heartbeat(void);     /* MQTT心跳维护 */
void Task_ProcessCloudCmd(void);    /* 处理云端指令 */

/* UI显示刷新函数 */
void UI_DisplayRefresh(void);       /* 刷新LCD显示 */
void ShowDigitalClock(u16 x, u16 y);/* 显示数字时钟 */
void ShowStatus(u16 x);             /* 显示系统状态 */

/* ================== 主函数 ================== */
int main(void)
{
    u8 retry_count = 0;
    
    /* 硬件初始化 */
    System_Init();
    
    /* 打印启动信息 */
    printf("\r\n********************************\r\n");
    printf("* STM32 Environment Monitor v2.0 *\r\n");
    printf("* UART1 Debugging Enabled        *\r\n");
    printf("********************************\r\n");

    /* ==== ESP8266 初始化和连接 ==== */
    uart2_init(115200);                 /* 初始化USART2给ESP8266通信 */
    LED_OFF();                          /* 初始关闭LED */
    led_state = 0;                      /* 记录LED状态 */
    
    printf("\r\n=== 开始尝试ESP8266连接头服务器 ===\r\n");
    
    /* 尝试连接云服务器的重试逻辑 */
    while (retry_count < ESP8266_RETRY_MAX && !mqtt_connected)
    {
        delay_ms(8000);                 /* 等待ESP8266稳定工作 */
        
        if (ESP8266_Connect_Server() == 0)
        {
            mqtt_connected = 1;
            printf("? ESP8266 & 联网连接已成功！\r\n");
        }
        else
        {
            retry_count++;
            printf("? 连接失败！%d秒后重试... (%d/%d)\r\n", 
                   ESP8266_RETRY_DELAY_MS / 1000, retry_count, ESP8266_RETRY_MAX);
            delay_ms(ESP8266_RETRY_DELAY_MS);
        }
    }
    
    if (!mqtt_connected)
    {
        printf("? 云服务连接失败！系统将进入本地模式运行。\r\n");
    }
    
    /* ==== 主循环 ==== */
    while (1) 
    {
        Task_KeyHandler();              /* 处理按键扫描与逻辑 */
        Task_UpdateTime();              /* 更新时钟日历 */
        Task_SensorRead();              /* 读取温湿度数据 */
        Task_ControlLogic();            /* 执行设备控制逻辑 */
        Task_ProcessCloudCmd();         /* 处理云端指令 */
        UI_DisplayRefresh();            /* 刷新LCD显示 */
        
        /* ESP8266 接收数据处理 */
        if (mqtt_connected)
        {
            ESP8266_Receive_Proc();     /* 查询并解析USART2接收 */
        }
        
        /* MQTT 心跳维护 */
        Task_MQTT_Heartbeat();
        
        /* 系统心跳指示 - LED闪烁使用LED1作为系统运行指示LED */
        static u8 heartbeat = 0;
        if (++heartbeat >= 5)           /* 每500ms翻转一次 */
        {
            heartbeat = 0;
            LED1 = !LED1;               /* 系统心跳指示灯 */
        }
        
        delay_ms(REFRESH_TICK_MS);      /* 主循环延时 */
    }
}

/**
 * @brief 系统硬件初始化
 */
void System_Init(void)
{
    /* 基础功能初始化 */
    delay_init();                       /* 延时功能初始化 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); /* 设定中断优先级分组 */
    uart_init(115200);                  /* USART1调试串口初始化 */
    
    /* 硬件外设初始化 */
    LED_Init();                         /* LED初始化 */
    GPIO_Configuration();               /* GPIO配置，继电器和其他控制IO */
    KEY_Init();                         /* 按键功能初始化 */
    LCD_Init();                         /* LCD显示屏初始化 */
    GBK_Lib_Init();                     /* GBK字库初始化 */
    DHT11_Init();                       /* DHT11温湿度传感器初始化 */
    
    /* JTAG关闭释放 - 关闭JTAG释放PA15给单总线 */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    
    /* 继电器初始状态 - 高电平关闭，低电平开启 */
    PA5_SetHigh();                      /* 加热器初始关闭 */
    PA1_SetHigh();                      /* 风扇初始关闭 */
    
    /* LCD初始显示 */
    LCD_Clear(WHITE);                   /* 背景设为白色 */
    POINT_COLOR = RED;                  /* 设置字体为红色 */
    LCD_ShowString(50, 40, 240, 24, 24, BLUE, (u8*)"Monitor System");
}

/**
 * @brief 温湿度传感器周期获取数据
 * @note  每2秒读取一次DHT11，成功获取则通过ESP8266上报云端
 */
void Task_SensorRead(void)
{
    static u8 dht_read_cnt = 0;
    
    if (++dht_read_cnt >= SENSOR_READ_INTERVAL)  /* 达到读取周期 */
    {
        dht_read_cnt = 0;
        u8 ret = DHT11_Read_Data(&humi, &temp);  /* 读取温湿度 */
        
        if (ret == 0)  /* 读取成功 */
        {
            printf("温湿度传感器读取成功！温度:%d度 湿度:%d%%\r\n", temp, humi);
            /* 通过ESP8266上报数据到云端 */
            if (mqtt_connected) 
            {
                char msg[MSG_BUF_SIZE];
                sprintf(msg, "Temp:%dC Humi:%d%%", temp, humi);
                ESP8266_Send_Data((u8*)msg);     /* 发送MQTT消息 */
                printf("温湿度数据已上报云端: %s\r\n", msg);
            }
        }
        else  /* 读取失败 */
        {
            printf("DHT11读取失败！错误码: %d\r\n", ret);
        }
    }
}

/**
 * @brief 温度控制逻辑
 * @note  根据当前温度与设定阈值控制加热器风扇，低电平开启。
 *        确保加热阈值始终小于风扇阈值避免控制冲突。
 */
void Task_ControlLogic(void)
{
    static u8 last_fan_state = 2;      /* 上次风扇状态: 2-未知, 1-开启, 0-关闭 */
    static u8 last_heat_state = 2;     /* 上次加热器状态 */
    
    /* 保证加热阈值小于风扇阈值，避免控制冲突 */
    if (heat_target >= fan_target) 
    {
        fan_target = heat_target + 2;
    }
    
    /* ==== 加热器控制逻辑 ==== */
    if (temp < heat_target)            /* 温度低于设定值则开启加热 */
    {
        PA5_SetLow();                  /* 低电平开启加热器 */
        if (last_heat_state != 1) 
        {
            printf("[控制逻辑] 加热器已开启 (温度:%d度 < 阈值:%d度)\r\n", temp, heat_target);
            last_heat_state = 1;
        }
    } 
    else                               /* 温度达到设定值则关闭加热 */
    {
        PA5_SetHigh();                 /* 高电平关闭加热器 */
        if (last_heat_state != 0) 
        {
            printf("[控制逻辑] 加热器已关闭 (温度:%d度 >= 阈值:%d度)\r\n", temp, heat_target);
            last_heat_state = 0;
        }
    }
    
    /* ==== 风扇控制逻辑 ==== */
    if (temp > fan_target)             /* 温度超过设定值则开启风扇 */
    {
        PA1_SetLow();                  /* 低电平开启风扇 */
        if (last_fan_state != 1) 
        {
            printf("[控制逻辑] 风扇已开启 (温度:%d度 > 阈值:%d度)\r\n", temp, fan_target);
            last_fan_state = 1;
        }
    } 
    else                               /* 温度降下来则关闭风扇 */
    {
        PA1_SetHigh();                 /* 高电平关闭风扇 */
        if (last_fan_state != 0) 
        {
            printf("[控制逻辑] 风扇已关闭 (温度:%d度 <= 阈值:%d度)\r\n", temp, fan_target);
            last_fan_state = 0;
        }
    }
}

/**
 * @brief 处理按键扫描与逻辑
 * @note  WKUP: 切换设置模式 (正常 -> 设置加热阈值 -> 设置风扇阈值 -> 正常)
 *        KEY0: 增加当前设置模式下的阈值
 *        KEY1: 减少当前设置模式下的阈值
 */
void Task_KeyHandler(void)
{
    u8 key = KEY_Scan(0);              /* 获取按键值并处理设置模式 */
    
    switch(key)
    {
        case WKUP_PRES:                /* WKUP按键 - 切换设置模式 */
            setting_mode = (setting_mode + 1) % 3;
            blink_flag = 0;            /* 重置闪烁标志 */
            printf("设置模式已切换: %s\r\n", 
                   setting_mode == 1 ? "设置加热阈值" : (setting_mode == 2 ? "设置风扇阈值" : "正常模式"));
            break;
            
        case KEY0_PRES:                /* KEY0按键 - 增加阈值 */
            if (setting_mode == 1 && heat_target < TEMP_LIMIT_MAX)
            {
                heat_target++;
                printf("加热阈值增加设置为: %d度\r\n", heat_target);
            }
            if (setting_mode == 2 && fan_target < TEMP_LIMIT_MAX)
            {
                fan_target++;
                printf("风扇阈值增加设置为: %d度\r\n", fan_target);
            }
            break;
            
        case KEY1_PRES:                /* KEY1按键 - 减少阈值 */
            if (setting_mode == 1 && heat_target > TEMP_LIMIT_MIN)
            {
                heat_target--;
                printf("加热阈值减少设置为: %d度\r\n", heat_target);
            }
            if (setting_mode == 2 && fan_target > TEMP_LIMIT_MIN)
            {
                fan_target--;
                printf("风扇阈值减少设置为: %d度\r\n", fan_target);
            }
            break;
            
        default:
            break;
    }
}

/**
 * @brief 时钟日历维护
 * @note  每1秒钟更新一次时间，在主循环中累加。
 */
void Task_UpdateTime(void)
{
    static u8 sub_sec = 0;             /* 亚秒计数器 */
    
    if (++sub_sec >= 10)               /* 10 * 100ms = 1秒 */
    {
        sub_sec = 0;
        
        if (++sec >= 60)               /* 秒进位 */
        {
            sec = 0;
            if (++min >= 60)           /* 分钟位 */
            {
                min = 0;
                hour++;                /* 时钟位进位，达到这里说明跨天了暂不做日处理 */
            }
        }
    }
}

/**
 * @brief MQTT心跳维护函数
 * @note  每30秒查询一次MQTT连接状态，断线时尝试重连。
 */
void Task_MQTT_Heartbeat(void)
{
    static u32 mqtt_check_tick = 0;
    static u8 mqtt_fail_count = 0;
    u8 query_failed = 0;

    if (++mqtt_check_tick >= MQTT_HEARTBEAT_INTERVAL)  /* 达到心跳周期 */
    {
        mqtt_check_tick = 0;

        if (mqtt_connected)
        {
            /*
             * AT+MQTTCONN? 的返回格式不一定包含 +MQTTCONNECTED。
             * 以前用“找不到 +MQTTCONNECTED”判断断线，会误判后主动 MQTTCLEAN，造成反复重连。
             * 这里先以查询命令是否正常返回 OK 为准，并打印实际返回内容便于确认固件格式。
             */
            query_failed = ESP8266_Send_Cmd((u8*)"AT+MQTTCONN?\r\n", (u8*)"OK", 1500);
            printf("[MQTT状态查询返回]\r\n%s\r\n", usart2_rx_buf);

            /* 只有明确查询失败，或模块主动返回断开/错误信息时，才累计失败次数 */
            if (query_failed ||
                strstr((char*)usart2_rx_buf, "ERROR") != NULL ||
                strstr((char*)usart2_rx_buf, "DISCONNECT") != NULL ||
                strstr((char*)usart2_rx_buf, "+MQTTDISCONNECTED") != NULL)
            {
                mqtt_fail_count++;
                printf("? MQTT状态异常，失败次数:%d\r\n", mqtt_fail_count);
            }
            else
            {
                mqtt_fail_count = 0;
                printf("? MQTT状态查询正常\r\n");
            }

            /* 连续多次异常才重连，避免一次查询超时或返回格式差异导致误重连 */
            if (mqtt_fail_count >= 3)
            {
                printf("? MQTT连续异常，正在尝试重连...\r\n");
                mqtt_connected = 0;
                mqtt_fail_count = 0;

                ESP8266_Send_Cmd((u8*)"AT+MQTTCLEAN=0\r\n", (u8*)"OK", 2000);
                delay_ms(1000);

                if (ESP8266_Connect_Server() == 0)
                {
                    mqtt_connected = 1;
                    printf("? MQTT重连成功！\r\n");
                }
                else
                {
                    printf("? MQTT重连失败，稍后继续尝试\r\n");
                }
            }

            ESP8266_Clear_Buf();        /* 清空接收栈缓冲区 */
        }
        else
        {
            /* 离线状态也要保留自动恢复能力，避免一次重连失败后永久不再尝试 */
            printf("? MQTT当前离线，尝试重新连接...\r\n");
            ESP8266_Send_Cmd((u8*)"AT+MQTTCLEAN=0\r\n", (u8*)"OK", 2000);
            delay_ms(1000);

            if (ESP8266_Connect_Server() == 0)
            {
                mqtt_connected = 1;
                mqtt_fail_count = 0;
                printf("? MQTT重连成功！\r\n");
            }
            else
            {
                printf("? MQTT仍未恢复，下次心跳继续尝试\r\n");
            }

            ESP8266_Clear_Buf();
        }
    }
}

/**
 * @brief 处理云端下发指令
 * @note  解析ESP8266接收的标志位，执行相应动作并处理。
 */
void Task_ProcessCloudCmd(void)
{
    /* 处理LED开关指令 */
    if (led_cmd_flag != 0)
    {
        if (led_cmd_flag == 1)  /* 点亮LED */
        {
            LED_ON();
            led_state = 1;
            printf("[云端] LED已开启\r\n");
        }
        else if (led_cmd_flag == 2)  /* 关闭LED */
        {
            LED_OFF();
            led_state = 0;
            printf("[云端] LED已关闭\r\n");
        }
        led_cmd_flag = 0;  /* 清除标志 */
    }
    
    /* 处理状态查询指令 - 回报当前状态 */
    if (query_cmd_flag != 0)
    {
        if (mqtt_connected)
        {
            char status_msg[MSG_BUF_SIZE];
            sprintf(status_msg, "Status:Temp=%dC,Humi=%d%%,LED=%s,Heat=%dC,Fan=%dC",
                    temp, humi, led_state ? "ON" : "OFF", heat_target, fan_target);
            ESP8266_Send_Data((u8*)status_msg);
            printf("[云端] 状态已回报: %s\r\n", status_msg);
        }
        query_cmd_flag = 0;
    }
}

/**
 * @brief UI显示刷新函数
 * @note  根据系统设置模式产生闪烁效果。
 */
void UI_DisplayRefresh(void)
{
    static u8 blink_cnt = 0;
    
    /* 闪烁计数器 - 每500ms改变一次闪烁状态 */
    if (++blink_cnt >= 5)              /* 5 * 100ms = 500ms */
    {
        blink_cnt = 0;
        
        /* 只有在设置模式下才切换闪烁标志 */
        if (setting_mode != 0) 
        {
            blink_flag = !blink_flag;
        }
        else
        {
            blink_flag = 0;
        }
    }
    
    /* 刷新显示内容 */
    ShowDigitalClock(50, 80);          /* 显示时钟 */
    ShowStatus(50);                    /* 显示状态信息 */
}

/**
 * @brief 显示数字时钟
 * @param x X坐标位置
 * @param y Y坐标位置
 */
void ShowDigitalClock(u16 x, u16 y)
{
    char time_str[32];
    
    sprintf(time_str, "%04d-%02d-%02d %02d:%02d:%02d", 
            year, month, day, hour, min, sec);
    LCD_ShowString(x, y, 240, 16, 16, BLACK, (u8*)time_str);
}

/**
 * @brief 显示系统状态信息
 * @param x X坐标位置
 */
void ShowStatus(u16 x)
{
    char buf[32];
    
    /* 显示温湿度数据 */
    sprintf(buf, "Temp:%2d C Humi:%2d %%", temp, humi);
    LCD_ShowString(x, 120, 240, 16, 16, RED, (u8*)buf);
    
    /* 显示加热器阈值，设置模式下闪烁显示 */
    u16 heat_color = (setting_mode == 1 && blink_flag) ? WHITE : DARKBLUE;
    sprintf(buf, "Heater Set: %2d C", heat_target);
    LCD_ShowString(x, 145, 240, 16, 16, heat_color, (u8*)buf);
    
    /* 显示风扇阈值，设置模式下闪烁显示 */
    u16 fan_color = (setting_mode == 2 && blink_flag) ? WHITE : DARKBLUE;
    sprintf(buf, "Fan Set : %2d C", fan_target);
    LCD_ShowString(x, 170, 240, 16, 16, fan_color, (u8*)buf);
    
    /* 显示设备控制状态 */
    LCD_ShowString(x, 200, 240, 16, 16, GRAY, (u8*)"Device:");
    
    if (GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_5) == 0)       /* 加热器工作中 */
    {
        LCD_ShowString(x + 60, 200, 80, 16, 16, RED, (u8*)"HEATING ");
    }
    else if (GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_1) == 0)  /* 风扇工作中 */
    {
        LCD_ShowString(x + 60, 200, 80, 16, 16, BLUE, (u8*)"FAN RUN ");
    }
    else                                                       /* 空闲状态 */
    {
        LCD_ShowString(x + 60, 200, 80, 16, 16, GREEN, (u8*)"IDLE ");
    }
    
    /* 显示LED状态 - 统一用状态显示 */
    LCD_ShowString(x, 225, 240, 16, 16, GRAY, (u8*)"LED:");
    if (led_state)
    {
        LCD_ShowString(x + 40, 225, 80, 16, 16, YELLOW, (u8*)"ON ");
    }
    else
    {
        LCD_ShowString(x + 40, 225, 80, 16, 16, GRAY, (u8*)"OFF");
    }
    
    /* 显示MQTT连接状态 - 如果LED亮着则统一显示，这里16行截断 */
    LCD_ShowString(x, 245, 240, 16, 16, GRAY, (u8*)"Cloud:");
    if (mqtt_connected)
    {
        LCD_ShowString(x + 50, 245, 140, 16, 16, GREEN, (u8*)"Connected");
    }
    else
    {
        LCD_ShowString(x + 50, 245, 140, 16, 16, RED, (u8*)"Disconnected");
    }
}
