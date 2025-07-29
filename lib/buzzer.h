#ifndef BUZZER_H
#define BUZZER_H

#include "pico/stdlib.h"
#include "hardware/pwm.h"

// Defina os pinos dos buzzers
#define BUZZER_A 21
#define BUZZER_B 10

// Notas musicais
#define C4 262   // Dó4
#define D4 294   // Ré4
#define E4 330   // Mi4
#define G4 392   // Sol4
#define A4 440   // Lá4
#define C5 524   // Dó5
#define E5 660   // Mi5
#define G5 784   // Sol5

// Protótipos
void init_buzzer_pwm(uint gpio);
void set_buzzer_tone(uint gpio, uint freq);
void stop_buzzer(uint gpio);
void play_alarm_critic(void);  // Alarme para condições críticas
void play_alarm_rain(void);    // Alarme para chuva

#endif