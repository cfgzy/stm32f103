#include "stm32f10x.h"

/**
 * @brief 初始化 PA5 和 PA1 为推挽输出模式
 */
void GPIO_Configuration(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    // 使能 GPIOA 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // 配置 PA5 和 PA1 为通用推挽输出，最大速度 50MHz
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_1;  // ← 改为 PA5 + PA1
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
}

// ============ PA5 控制函数（原 PA0 改过来的）============

/**
 * @brief 设置 PA5 输出高电平
 */
void PA5_SetHigh(void)
{
    GPIO_SetBits(GPIOA, GPIO_Pin_5);  // ← 改为 PA5
}

/**
 * @brief 设置 PA5 输出低电平
 */
void PA5_SetLow(void)
{
    GPIO_ResetBits(GPIOA, GPIO_Pin_5);  // ← 改为 PA5
}

/**
 * @brief 切换 PA5 电平状态（翻转）
 */
void PA5_Toggle(void)
{
    GPIO_WriteBit(GPIOA, GPIO_Pin_5, 
        (BitAction)(1 - GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_5)));  // ← 改为 PA5
}

// ============ PA1 控制函数（保持不变）============

/**
 * @brief 设置 PA1 输出高电平
 */
void PA1_SetHigh(void)
{
    GPIO_SetBits(GPIOA, GPIO_Pin_1);
}

/**
 * @brief 设置 PA1 输出低电平
 */
void PA1_SetLow(void)
{
    GPIO_ResetBits(GPIOA, GPIO_Pin_1);
}

/**
 * @brief 切换 PA1 电平状态（翻转）
 */
void PA1_Toggle(void)
{
    GPIO_WriteBit(GPIOA, GPIO_Pin_1, 
        (BitAction)(1 - GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_1)));
}
