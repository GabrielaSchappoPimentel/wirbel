#pragma once

#define FW_VERSION "0.4"

// ---- Pinos (ESP32-WROOM-32) ----
// Evitados de proposito: 0, 2, 5, 12, 15 (strapping), 6-11 (flash), 1 e 3 (USB serial)
#define PIN_SERVO_DIRECAO 13
#define PIN_SERVO_FREIO   23
#define PIN_SERVO_DRS     32
#define PIN_MOTOR_PWMA    25
#define PIN_MOTOR_AIN1    26
#define PIN_MOTOR_AIN2    27
#define PIN_MOTOR_PWMB    19
#define PIN_MOTOR_BIN1    18
#define PIN_MOTOR_BIN2    17   // TX2 na placa
#define PIN_MOTOR_STBY    16   // RX2 na placa
#define PIN_VBAT          34   // ADC1: o ADC2 nao funciona com o Wi-Fi ligado

// ---- Tempos ----
#define CONTROLE_PERIODO_MS    10
#define TELEMETRIA_PERIODO_MS  100
#define FAILSAFE_MS            200

// ---- Rede ----
#define UDP_PORTA   4210
#define WIFI_SSID   "Wirbel-EV"
#define WIFI_CANAL  6

// ---- Bateria ----
#define VBAT_BAIXA_MV        6600   // abaixo disso: acelerador pela metade
#define VBAT_CORTE_MV        6200   // abaixo disso por 2 s: motores desligados
#define VBAT_CORTE_ATRASO_MS 2000
#define VBAT_R_TOPO_OHM      100000 // resistor do lado da bateria
#define VBAT_R_BASE_OHM      47000  // resistor do lado do GND
#define VBAT_AJUSTE_PCT      100    // corrige a leitura para bater com o multimetro (ex.: 103 = +3%)

// ---- Motores N20 ----
#define MOTOR_PWM_HZ       20000
#define MOTOR_V_MAX_MV     6000   // tensao nominal do N20: o PWM e limitado para nao passar disso
#define MOTOR_RAMPA_PASSO  2      // % por ciclo de 10 ms (0 a 100% em 0,5 s)
#define MOTOR_A_INVERTIDO  0      // 1 se o motor A girar ao contrario
#define MOTOR_B_INVERTIDO  0      // 1 se o motor B girar ao contrario

// ---- Servos (em microssegundos). CALIBRAR com o modo de calibracao! ----
// Enquanto nao calibrados, todos ficam parados em 1500 us (seguro).
#define SERVO_US_MIN       500
#define SERVO_US_MAX       2500
#define DIR_CENTRO_US      1500
#define DIR_AMPLITUDE_US   0      // quanto vai para cada lado; 0 = direcao parada
#define DIR_INVERTIDO      0      // 1 se virar para o lado errado
#define FREIO_SOLTO_US     1500
#define FREIO_APERTADO_US  1500
#define DRS_ABERTO_US      1500
#define DRS_FECHADO_US     1500

// ---- DRS (asa automatica, em % de abertura: 100 = aberta) ----
#define DRS_ZONA_ABERTA   15   // |direcao| ate aqui: asa totalmente aberta
#define DRS_ZONA_FECHADA  50   // |direcao| daqui pra cima: asa fechada
#define DRS_FREIO_LIMIAR   5   // freio acima disso: fecha na hora
#define DRS_PASSO_ABRE     2   // % por ciclo de 10 ms (abre em ~0,5 s)
#define DRS_PASSO_FECHA    5   // % por ciclo de 10 ms (fecha em ~0,2 s)
