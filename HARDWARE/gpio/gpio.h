#ifndef __GPIO_H
#define __GPIO_H

#include "stm32f10x.h"

void GPIO_Configuration(void);

// PA5 øÿ÷∆
void PA5_SetHigh(void);
void PA5_SetLow(void);
void PA5_Toggle(void);

// PA1 øÿ÷∆£®±£¡Ù£©
void PA1_SetHigh(void);
void PA1_SetLow(void);
void PA1_Toggle(void);

#endif
