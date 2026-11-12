#pragma once

#define FW_VERSION "0.2"

// ---- Pinos (ESP32-WROOM-32) ----
// Evitados de proposito: 0, 2, 5, 12, 15 (strapping), 6-11 (flash), 1 e 3 (USB serial)
#define PIN_SERVO_DIRECAO 13
#define PIN_SERVO_FREIO   23
#define PIN_MOTOR_PWMA    25
#define PIN_MOTOR_AIN1    26
#define PIN_MOTOR_AIN2    27
#define PIN_MOTOR_PWMB    19
#define PIN_MOTOR_BIN1    18
#define PIN_MOTOR_BIN2    17
#define PIN_MOTOR_STBY    16
#define PIN_VBAT          34   // ADC1: o ADC2 nao funciona com o Wi-Fi ligado

// ---- Tempos (iguais aos do carro simulado) ----
#define CONTROLE_PERIODO_MS    10
#define TELEMETRIA_PERIODO_MS  100
#define FAILSAFE_MS            200

// ---- Rede ----
#define UDP_PORTA   4210
#define WIFI_SSID   "Wirbel-EV"
#define WIFI_CANAL  6