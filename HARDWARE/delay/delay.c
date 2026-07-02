#include "delay.h"

void My_Delay(int nms)
{
    int i = 0;
    
    while(nms--)
    {
        i = 1500;
        while(i--);
    }
}
// 确保最后有新行
