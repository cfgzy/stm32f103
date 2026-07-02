/**
 ******************************************************************************
 * @file    main.c
 * @author  Optimized Version
 * @brief   STM32F103ZE 环境监控系统 + ESP8266 巴法云
 * @details 硬件配置：
 *          - 2.8寸LCD显示屏
 *          - DHT11温湿度传感器
 *          - 继电器：PA1控制风扇，PA5控制加热器（低电平触发）
 *          - ESP8266-01S通过USART2连接
 *          - 按键：WKUP切换设置模式，KEY0增加阈值，KEY1减少阈值
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

/* ================== 系统配置常量 ================== */
#define TEMP_LIMIT_MAX          40      /* 温度上限阈值 */
#define TEMP_LIMIT_MIN          10      /* 温度下限阈值 */
#define REFRESH_TICK_MS         100     /* 主循环周期(ms) */
#define SENSOR_READ_INTERVAL    20      /* 传感器读取间隔(20 * 100ms = 2s) */
#define MQTT_HEARTBEAT_INTERVAL 300     /* MQTT心跳间隔(300 * 100ms = 30s) */
#define ESP8266_RETRY_MAX       5       /* ESP8266连接重试次数 */
#define ESP8266_RETRY_DELAY_MS  3000    /* 重试延迟(ms) */
#define MSG_BUF_SIZE            64      /* 消息缓冲区大小 */ 

/* ==== LED指示灯定义 ==== */
#define LED_ON()                GPIO_ResetBits(GPIOB, GPIO_Pin_5)   /* PB5低电平点亮 */
#define LED_OFF()               GPIO_SetBits(GPIOB, GPIO_Pin_5)

/* ================== 全局变量 ================== */
/* 系统时间相关变量 */
u16 year = 2025;
u8 month = 12, day = 5;
u8 hour = 17, min = 55, sec = 0;

/* 环境参数 */
u8 temp = 0;        /* 当前温度(℃) */
u8 humi = 0;        /* 当前湿度(%) */

/* 控制参数 */
u8 heat_target = 22;    /* 加热器启动阈值 */
u8 fan_target = 28;     /* 风扇启动阈值 */

/* 用户界面状态 */
u8 setting_mode = 0;    /* 设置模式: 0-正常, 1-设置加热阈值, 2-设置风扇阈值 */
u8 blink_flag = 0;      /* 闪烁标志，用于设置模式下的视觉提示 */

/* ESP8266 状态 */
u8 mqtt_connected = 0;  /* MQTT连接状态标志 */

/* LED状态和云端指令标志 */
u8 led_state = 0;       /* LED状态: 0-关闭, 1-开启 */
u8 led_cmd_flag = 0;    /* LED指令标志: 0-无指令, 1-开启, 2-关闭 */
u8 query_cmd_flag = 0;  /* 查询指令标志 */

/* ================== 函数声明 ================== */
/* 系统初始化 */
void System_Init(void);

/* 核心任务函数 */
void Task_UpdateTime(void);         /* 更新时间计数 */
void Task_SensorRead(void);         /* 读取DHT11传感器 */
void Task_ControlLogic(void);       /* 执行自动控制逻辑 */
void Task_KeyHandler(void);         /* 处理按键输入 */
void Task_MQTT_Heartbeat(void);     /* MQTT心跳维护 */
void Task_ProcessCloudCmd(void);    /* 处理云端指令 */

/* UI显示函数 */
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

    /* ==== ESP8266 初始化与连接 ==== */
    uart2_init(115200);                 /* 初始化USART2与ESP8266通信 */
    LED_OFF();                          /* 初始关闭LED */
    led_state = 0;                      /* 记录LED状态 */
    
    printf("\r\n=== 正在连接ESP8266与巴法云 ===\r\n");
    
    /* 带重试机制的连接尝试 */
    while (retry_count < ESP8266_RETRY_MAX && !mqtt_connected)
    {
        delay_ms(8000);                 /* 等待ESP8266稳定启动 */
        
        if (ESP8266_Connect_Server() == 0)
        {
            mqtt_connected = 1;
            printf("? ESP8266 & 巴法云连接成功！\r\n");
        }
        else
        {
            retry_count++;
            printf("? 连接失败，%d秒后重试... (%d/%d)\r\n", 
                   ESP8266_RETRY_DELAY_MS / 1000, retry_count, ESP8266_RETRY_MAX);
            delay_ms(ESP8266_RETRY_DELAY_MS);
        }
    }
    
    if (!mqtt_connected)
    {
        printf("? 云端连接失败，系统将以本地模式运行\r\n");
    }
    
    /* ==== 主循环 ==== */
    while (1) 
    {
        Task_KeyHandler();              /* 处理按键输入 */
        Task_UpdateTime();              /* 更新时间计数 */
        Task_SensorRead();              /* 读取温湿度数据 */
        Task_ControlLogic();            /* 执行设备控制逻辑 */
        Task_ProcessCloudCmd();         /* 处理云端指令 */
        UI_DisplayRefresh();            /* 刷新LCD显示 */
        
        /* ESP8266 数据接收处理 */
        if (mqtt_connected)
        {
            ESP8266_Receive_Proc();     /* 接收并解析USART2数据 */
        }
        
        /* MQTT 心跳维护 */
        Task_MQTT_Heartbeat();
        
        /* 系统运行指示 - LED闪烁（使用LED1，不影响主LED） */
        static u8 heartbeat = 0;
        if (++heartbeat >= 5)           /* 每500ms翻转一次 */
        {
            heartbeat = 0;
            LED1 = !LED1;               /* 系统运行指示灯 */
        }
        
        delay_ms(REFRESH_TICK_MS);      /* 主循环延时 */
    }
}

/**
 * @brief 系统硬件初始化
 */
void System_Init(void)
{
    /* 基础外设初始化 */
    delay_init();                       /* 延时功能初始化 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); /* 中断优先级分组 */
    uart_init(115200);                  /* USART1调试串口初始化 */
    
    /* 外设初始化 */
    LED_Init();                         /* LED初始化 */
    GPIO_Configuration();               /* GPIO配置（继电器、其他IO） */
    KEY_Init();                         /* 按键初始化 */
    LCD_Init();                         /* LCD显示屏初始化 */
    GBK_Lib_Init();                     /* GBK字库初始化 */
    DHT11_Init();                       /* DHT11温湿度传感器初始化 */
    
    /* JTAG接口配置 - 禁用JTAG，释放PA15等引脚 */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    
    /* 继电器初始状态 - 高电平关闭（低电平触发） */
    PA5_SetHigh();                      /* 加热器初始关闭 */
    PA1_SetHigh();                      /* 风扇初始关闭 */
    
    /* LCD初始化显示 */
    LCD_Clear(WHITE);                   /* 清屏为白色 */
    POINT_COLOR = RED;                  /* 设置文字颜色 */
    LCD_ShowString(50, 40, 240, 24, 24, BLUE, (u8*)"Monitor System");
}

/**
 * @brief 传感器读取任务
 * @note  每2秒读取一次DHT11，成功读取后通过ESP8266上传至巴法云
 */
void Task_SensorRead(void)
{
    static u8 dht_read_cnt = 0;
    
    if (++dht_read_cnt >= SENSOR_READ_INTERVAL)  /* 达到读取间隔 */
    {
        dht_read_cnt = 0;
        u8 ret = DHT11_Read_Data(&humi, &temp);  /* 读取温湿度 */
        
        if (ret == 0)  /* 读取成功 */
        {
            printf("传感器采集成功→温度:%d℃湿度:%d%%\r\n", temp, humi);
            /* 通过ESP8266上传数据至巴法云 */
            if (mqtt_connected) 
            {
                char msg[MSG_BUF_SIZE];
                sprintf(msg, "Temp:%dC Humi:%d%%", temp, humi);
                ESP8266_Send_Data((u8*)msg);     /* 发送MQTT消息 */
                printf("数据已上传至巴法云: %s\r\n", msg);
            }
        }
        else  /* 读取失败 */
        {
            printf("DHT11读取失败，错误码: %d\r\n", ret);
        }
    }
}

/**
 * @brief 自动控制逻辑
 * @note  根据当前温度与设定阈值控制加热器和风扇（低电平触发）
 *        确保加热阈值始终低于风扇阈值（自动调整）
 */
void Task_ControlLogic(void)
{
    static u8 last_fan_state = 2;      /* 上次风扇状态: 2-未知, 1-开启, 0-关闭 */
    static u8 last_heat_state = 2;     /* 上次加热器状态 */
    
    /* 保证加热阈值小于风扇阈值，避免逻辑冲突 */
    if (heat_target >= fan_target) 
    {
        fan_target = heat_target + 2;
    }
    
    /* ==== 加热器控制 ==== */
    if (temp < heat_target)            /* 温度低于设定值，开启加热 */
    {
        PA5_SetLow();                  /* 低电平触发加热器 */
        if (last_heat_state != 1) 
        {
            printf("[控制事件] 加热器已开启 (温度:%d℃ < 阈值:%d℃)\r\n", temp, heat_target);
            last_heat_state = 1;
        }
    } 
    else                               /* 温度达到设定值，关闭加热 */
    {
        PA5_SetHigh();                 /* 高电平关闭加热器 */
        if (last_heat_state != 0) 
        {
            printf("[控制事件] 加热器已关闭 (温度:%d℃ ≥ 阈值:%d℃)\r\n", temp, heat_target);
            last_heat_state = 0;
        }
    }
    
    /* ==== 风扇控制 ==== */
    if (temp > fan_target)             /* 温度超过设定值，开启风扇 */
    {
        PA1_SetLow();                  /* 低电平触发风扇 */
        if (last_fan_state != 1) 
        {
            printf("[控制事件] 风扇已开启 (温度:%d℃ > 阈值:%d℃)\r\n", temp, fan_target);
            last_fan_state = 1;
        }
    } 
    else                               /* 温度正常，关闭风扇 */
    {
        PA1_SetHigh();                 /* 高电平关闭风扇 */
        if (last_fan_state != 0) 
        {
            printf("[控制事件] 风扇已关闭 (温度:%d℃ ≤ 阈值:%d℃)\r\n", temp, fan_target);
            last_fan_state = 0;
        }
    }
}

/**
 * @brief 按键处理任务
 * @note  WKUP: 切换设置模式 (正常 → 设置加热阈值 → 设置风扇阈值 → 正常)
 *        KEY0: 增加当前设置模式的阈值
 *        KEY1: 减少当前设置模式的阈值
 */
void Task_KeyHandler(void)
{
    u8 key = KEY_Scan(0);              /* 获取按键值（非阻塞模式） */
    
    switch(key)
    {
        case WKUP_PRES:                /* WKUP按键 - 切换设置模式 */
            setting_mode = (setting_mode + 1) % 3;
            blink_flag = 0;            /* 重置闪烁标志 */
            printf("设置模式已切换至: %s\r\n", 
                   setting_mode == 1 ? "加热阈值" : (setting_mode == 2 ? "风扇阈值" : "正常模式"));
            break;
            
        case KEY0_PRES:                /* KEY0按键 - 增加阈值 */
            if (setting_mode == 1 && heat_target < TEMP_LIMIT_MAX)
            {
                heat_target++;
                printf("加热阈值已增加至: %d℃\r\n", heat_target);
            }
            if (setting_mode == 2 && fan_target < TEMP_LIMIT_MAX)
            {
                fan_target++;
                printf("风扇阈值已增加至: %d℃\r\n", fan_target);
            }
            break;
            
        case KEY1_PRES:                /* KEY1按键 - 减少阈值 */
            if (setting_mode == 1 && heat_target > TEMP_LIMIT_MIN)
            {
                heat_target--;
                printf("加热阈值已减少至: %d℃\r\n", heat_target);
            }
            if (setting_mode == 2 && fan_target > TEMP_LIMIT_MIN)
            {
                fan_target--;
                printf("风扇阈值已减少至: %d℃\r\n", fan_target);
            }
            break;
            
        default:
            break;
    }
}

/**
 * @brief 时间更新任务
 * @note  每1秒更新一次时间计数（基于主循环周期累加）
 */
void Task_UpdateTime(void)
{
    static u8 sub_sec = 0;             /* 子秒计数器 */
    
    if (++sub_sec >= 10)               /* 10 * 100ms = 1秒 */
    {
        sub_sec = 0;
        
        if (++sec >= 60)               /* 秒进位 */
        {
            sec = 0;
            if (++min >= 60)           /* 分进位 */
            {
                min = 0;
                hour++;                /* 时进位（简化处理，不处理日期进位） */
            }
        }
    }
}

/**
 * @brief MQTT心跳维护任务
 * @note  每30秒检查一次MQTT连接状态，断开时自动重连
 */
void Task_MQTT_Heartbeat(void)
{
    static u32 mqtt_check_tick = 0;
    
    if (++mqtt_check_tick >= MQTT_HEARTBEAT_INTERVAL)  /* 达到心跳间隔 */
    {
        mqtt_check_tick = 0;
        
        if (mqtt_connected) 
        {
            /* 发送心跳查询命令 */
            ESP8266_Send_Cmd((u8*)"AT+MQTTCONN?\r\n", (u8*)"OK", 500);
            
            /* 检查连接状态 */
            if (strstr((char*)usart2_rx_buf, "+MQTTCONNECTED") == NULL) 
            {
                printf("? MQTT连接已断开，正在尝试重连...\r\n");
                mqtt_connected = 0;
                
                /* 清理MQTT会话并重连 */
                ESP8266_Send_Cmd((u8*)"AT+MQTTCLEAN=0\r\n", (u8*)"OK", 2000);
                delay_ms(1000);
                
                if (ESP8266_Connect_Server() == 0) 
                {
                    mqtt_connected = 1;
                    printf("? MQTT重连成功！\r\n");
                }
                else
                {
                    printf("? MQTT重连失败\r\n");
                }
            }
            
            ESP8266_Clear_Buf();        /* 清空接收缓冲区 */
        }
    }
}

/**
 * @brief 处理云端下发的指令
 * @note  根据ESP8266驱动设置标志位，执行相应的控制操作
 */
void Task_ProcessCloudCmd(void)
{
    /* 处理LED控制指令 */
    if (led_cmd_flag != 0)
    {
        if (led_cmd_flag == 1)  /* 开启LED */
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
        led_cmd_flag = 0;  /* 清除指令标志 */
    }
    
    /* 处理查询指令 - 上报当前状态 */
    if (query_cmd_flag != 0)
    {
        if (mqtt_connected)
        {
            char status_msg[MSG_BUF_SIZE];
            sprintf(status_msg, "Status:Temp=%dC,Humi=%d%%,LED=%s,Heat=%dC,Fan=%dC",
                    temp, humi, led_state ? "ON" : "OFF", heat_target, fan_target);
            ESP8266_Send_Data((u8*)status_msg);
            printf("[云端] 状态已上报: %s\r\n", status_msg);
        }
        query_cmd_flag = 0;
    }
}

/**
 * @brief UI显示刷新任务
 * @note  控制设置模式下参数的闪烁效果
 */
void UI_DisplayRefresh(void)
{
    static u8 blink_cnt = 0;
    
    /* 闪烁控制 - 每500ms改变一次闪烁状态 */
    if (++blink_cnt >= 5)              /* 5 * 100ms = 500ms */
    {
        blink_cnt = 0;
        
        /* 仅在设置模式下切换闪烁标志 */
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
    ShowDigitalClock(50, 80);          /* 显示时间 */
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
    
    /* 显示加热阈值（设置模式下闪烁显示） */
    u16 heat_color = (setting_mode == 1 && blink_flag) ? WHITE : DARKBLUE;
    sprintf(buf, "Heater Set: %2d C", heat_target);
    LCD_ShowString(x, 145, 240, 16, 16, heat_color, (u8*)buf);
    
    /* 显示风扇阈值（设置模式下闪烁显示） */
    u16 fan_color = (setting_mode == 2 && blink_flag) ? WHITE : DARKBLUE;
    sprintf(buf, "Fan Set : %2d C", fan_target);
    LCD_ShowString(x, 170, 240, 16, 16, fan_color, (u8*)buf);
    
    /* 显示设备运行状态 */
    LCD_ShowString(x, 200, 240, 16, 16, GRAY, (u8*)"Device:");
    
    if (GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_5) == 0)       /* 加热器运行 */
    {
        LCD_ShowString(x + 60, 200, 80, 16, 16, RED, (u8*)"HEATING ");
    }
    else if (GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_1) == 0)  /* 风扇运行 */
    {
        LCD_ShowString(x + 60, 200, 80, 16, 16, BLUE, (u8*)"FAN RUN ");
    }
    else                                                       /* 空闲状态 */
    {
        LCD_ShowString(x + 60, 200, 80, 16, 16, GREEN, (u8*)"IDLE ");
    }
    
    /* 显示LED状态 - 第一行状态显示 */
    LCD_ShowString(x, 225, 240, 16, 16, GRAY, (u8*)"LED:");
    if (led_state)
    {
        LCD_ShowString(x + 40, 225, 80, 16, 16, YELLOW, (u8*)"ON ");
    }
    else
    {
        LCD_ShowString(x + 40, 225, 80, 16, 16, GRAY, (u8*)"OFF");
    }
    
    /* 显示MQTT连接状态 - 放在LED的下一行（Y坐标增加16像素） */
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
