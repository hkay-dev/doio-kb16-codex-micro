#pragma once

#include_next <mcuconf.h>

#undef STM32_I2C_USE_I2C2
#define STM32_I2C_USE_I2C2 TRUE

#undef STM32_PWM_USE_TIM1
#define STM32_PWM_USE_TIM1 TRUE
