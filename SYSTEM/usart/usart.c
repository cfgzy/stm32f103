#include "sys.h"
#include "usart.h"	  
#include <stdio.h>
#include <string.h>

#define EN_USART1_RX 1  // 保持开启，用于电脑调试
#define EN_USART2_RX 1  // 【NEW】强制开启 USART2 接收，用于 ESP8266

// ================== 全局变量定义 ==================

// USART1 缓冲区 (电脑调试用)
u8 usart1_rx_buf[512];  
u16 usart1_rx_cnt = 0;  

// USART2 缓冲区 (ESP8266 用) 【NEW】
u8 usart2_rx_buf[512];  
u16 usart2_rx_cnt = 0;  
u8  usart2_rx_flag = 0; // 接收完成标志，可在中断中根据特定字符（如\n）置位，或由应用层轮询处理
// ===================================================

#if SYSTEM_SUPPORT_OS
#include "includes.h"
#endif

//////////////////////////////////////////////////////////////////////////////////	 
// 重定义 fputc 函数 (保持使用 USART1 输出到电脑)
#if 1
#pragma import(__use_no_semihosting)             
struct __FILE { int handle; };
FILE __stdout;       
void _sys_exit(int x) { x = x; } 

// printf 依然走 USART1 (PA9)，这样你可以在电脑串口助手看到日志
int fputc(int ch, FILE *f)
{      
	while((USART1->SR & 0X40) == 0); // 等待发送完成
    USART1->DR = (u8) ch;      
	return ch;
}
#endif 

// ================== USART1 初始化 (保持不变，用于电脑) ==================
#if EN_USART1_RX   
void uart_init(u32 bound){
  GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);	
  
  // TX PA9
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; 
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	
  GPIO_Init(GPIOA, &GPIO_InitStructure);
   
  // RX PA10
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(GPIOA, &GPIO_InitStructure);  

  // NVIC
  NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;	
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			
	NVIC_Init(&NVIC_InitStructure);	
  
  // USART
	USART_InitStructure.USART_BaudRate = bound;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
  USART_Init(USART1, &USART_InitStructure); 
  
  USART_ITConfig(USART1, USART_IT_RXNE, ENABLE); // 开启接收中断
  USART_Cmd(USART1, ENABLE); 
}

// USART1 中断服务函数 (保持不变，接收电脑发来的数据或单纯打印调试)
void USART1_IRQHandler(void)
{
	u8 Res;
#if SYSTEM_SUPPORT_OS 
	OSIntEnter();    
#endif

	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
	{
		Res = USART_ReceiveData(USART1);
        // 这里可以简单存一下，或者如果你不需要电脑控制单片机，可以忽略
        if(usart1_rx_cnt < 511) {
            usart1_rx_buf[usart1_rx_cnt++] = Res;
        } else {
            usart1_rx_cnt = 0;
        }
	} 
#if SYSTEM_SUPPORT_OS 
	OSIntExit();  											 
#endif
} 
#endif

// ================== 【NEW】USART2 初始化 (用于 ESP8266) ==================
#if EN_USART2_RX
void uart2_init(u32 bound){
  GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	 
	// 1. 开启时钟：GPIOA 和 USART2 (注意 USART2 在 APB1 上！)
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);	
  
  // TX PA2 (复用推挽)
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2; 
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	
  GPIO_Init(GPIOA, &GPIO_InitStructure);
   
  // RX PA3 (浮空输入)
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(GPIOA, &GPIO_InitStructure);  

  // NVIC (USART2 中断通道)
  NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; // 优先级稍高一点，确保及时接收 ESP8266 数据
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;	
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			
	NVIC_Init(&NVIC_InitStructure);	
  
  // USART2 配置
	USART_InitStructure.USART_BaudRate = bound;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
  USART_Init(USART2, &USART_InitStructure); 
  
  USART_ITConfig(USART2, USART_IT_RXNE, ENABLE); // 开启接收中断
  USART_Cmd(USART2, ENABLE); 
}

// ================== 【NEW】USART2 中断服务函数 (核心逻辑) ==================
void USART2_IRQHandler(void)
{
	u8 Res;
#if SYSTEM_SUPPORT_OS 
	OSIntEnter();    
#endif

	if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
	{
		Res = USART_ReceiveData(USART2); // 读取 ESP8266 发来的数据
		
        // 【逻辑】存入 usart2_rx_buf
        // 注意：ESP8266 的 AT 指令返回通常以 "\r\n" 结尾。
        // 这里我们采用“累加”模式，由 esp8266.c 的主循环去判断是否接收完成。
        if(usart2_rx_cnt < 511) 
        {
            usart2_rx_buf[usart2_rx_cnt++] = Res;
            
            // 【可选优化】如果检测到换行符 '\n'，可以置位标志，通知主循环处理
            // 但为了兼容性，通常让 esp8266.c 里的状态机自己去解析更稳妥
            if(Res == '\n') {
                usart2_rx_flag = 1; // 标记收到了一行数据
            }
        }
        else
        {
            // 缓冲区溢出保护：重置计数，防止数组越界
            // 策略：丢弃旧数据，从当前字节重新开始？或者保持最后511字节？
            // 简单策略：清零，重新存当前字节
            usart2_rx_cnt = 0;
            usart2_rx_buf[usart2_rx_cnt++] = Res;
        }
	} 
#if SYSTEM_SUPPORT_OS 
	OSIntExit();  											 
#endif
} 
#endif

//// 辅助函数：清空 ESP8266 缓冲区
//void ESP8266_Clear_Buf(void) {
//    usart2_rx_cnt = 0;
//    usart2_rx_flag = 0;
//    memset(usart2_rx_buf, 0, sizeof(usart2_rx_buf));
//}
