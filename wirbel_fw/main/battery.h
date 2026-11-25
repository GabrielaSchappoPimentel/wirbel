#pragma once

void bat_init(void);

// Tensao da bateria em mV (media de 8 leituras, ja corrigida pelo divisor)
int bat_ler_mv(void);
