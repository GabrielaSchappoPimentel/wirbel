#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Milissegundos desde o boot (derivado do tick do FreeRTOS).
// Para medir tempo decorrido, sempre use: (uint32_t)(agora - antes)
static inline uint32_t agora_ms(void)
{
    return (uint32_t)(((uint64_t)xTaskGetTickCount() * 1000ULL) / configTICK_RATE_HZ);
}