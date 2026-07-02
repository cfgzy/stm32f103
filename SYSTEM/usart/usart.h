#ifndef __USART_H
#define __USART_H

#include "sys.h" 
#include "stdio.h"

// USART1 全局变量声明 (用于电脑调试)
extern u8 usart1_rx_buf[512];
extern u16 usart1_rx_cnt;

// USART2 全局变量声明 (用于 ESP8266)
extern u8 usart2_rx_buf[512];
extern u16 usart2_rx_cnt;
// 标记位：当 ESP8266 接收到完整一包数据（如换行）时，可由应用层置位，这里先预留
extern u8 usart2_rx_flag; 

void uart_init(u32 bound);      // USART1 初始化
void uart2_init(u32 bound);     // USART2 初始化 (新增)

// 供 esp8266.c 调用的辅助函数（可选，如果在中断里直接处理了就不需要）
void ESP8266_Clear_Buf(void);   

#endif
