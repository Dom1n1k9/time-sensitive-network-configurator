/* Minimal CMSIS PWM for the two actuator outputs (servo SG90 + buzzer).
 *
 * Why a register shim instead of the Zephyr PWM driver?
 *   The STM32 "timer PWM" device-tree binding (channel sub-nodes, #pwm-cells,
 *   the AF/pinctrl glue) is the least stable part of this port across Zephyr
 *   versions. Two fixed outputs are cheaper — and more portable — as a small,
 *   self-contained shim that only needs the CMSIS device header (RCC/TIM/GPIO
 *   register definitions + SystemCoreClock), which Zephyr already provides.
 *
 * Clock assumptions are the typical NUCLEO-F767ZI values. If your clock tree
 * differs, adjust *_CLK below: the servo just runs a few % off in speed (the
 * echo timing uses the DWT clock, not the PWM, so sonar accuracy is unaffected).
 */
#include "pwm.h"

#include <zephyr/kernel.h>
#include "stm32f7xx.h"   

/* Typical NUCLEO-F767ZI timer clocks (adjust to your clock tree). */
#define SERVO_TIM_CLK   216000000UL   /* TIM1, on APB2 */
#define BUZZER_TIM_CLK  108000000UL   /* TIM3, on APB1 (2x APB1 when presc > 1) */

void htsn_pwm_init(void)
{
	/* Enable the timers + the GPIO ports they mux. */
	RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;                 /* TIM1 (APB2) */
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;                 /* TIM3 (APB1) */
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;

	/* ---- PA8 -> TIM1_CH1 (AF1) ---- */
	GPIOA->MODER   = (GPIOA->MODER & ~(0x3u << 16)) | (2u << 16);  /* PA8 = AF */
	GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~0x0Fu)        | 0x1u;       /* AFR8 = AF1 */
	GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~(0x3u << 16)) | (2u << 16);

	/* TIM1: 1 us tick, 20 ms period (50 Hz), CH1 PWM1 (active high). */
	TIM1->PSC   = (uint32_t)(SERVO_TIM_CLK / 1000000UL) - 1u;  /* 1 MHz tick  */
	TIM1->ARR   = 19999u;                                     /* 20 ms period */
	TIM1->CCMR1 = (TIM1->CCMR1 & ~(0x7u << 4)) | (0x6u << 4); /* OC1M = PWM1  */
	TIM1->CCER  |= 0x04u;                                     /* CC1E         */
	TIM1->BDTR  |= 0x00008000u;                               /* MOE (TIM1)   */
	TIM1->CR1   |= 0x00010000u;                               /* ARPE         */
	TIM1->EGR   = 0x1u;                                       /* UG: reload   */
	TIM1->CCR1  = 1500u;                                      /* 90 deg       */
	TIM1->CR1  |= 0x1u;                                       /* CEN          */

	/* ---- PB1 -> TIM3_CH4 (AF2) ---- */
	GPIOB->MODER = (GPIOB->MODER & ~(0x3u << 2)) | (2u << 2);        /* PB1 = AF */
	GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(0x0Fu << 4)) | (2u << 4);     /* AFR1 = AF2 */

	/* TIM3: 1 us tick, 1 ms period (1 kHz), CH4 PWM1 (active high). */
	TIM3->PSC   = (uint32_t)(BUZZER_TIM_CLK / 1000000UL) - 1u;  /* 1 MHz tick */
	TIM3->ARR   = 999u;                                         /* 1 ms period*/
	TIM3->CCMR2 = (TIM3->CCMR2 & ~(0x7u << 4)) | (0x6u << 4);   /* OC4M = PWM1*/
	TIM3->CCER  |= 0x4000u;                                     /* CC4E       */
	TIM3->CR1   |= 0x00010000u;                                 /* ARPE       */
	TIM3->EGR   = 0x1u;                                         /* UG         */
	TIM3->CCR4  = 0u;                                           /* off        */
	TIM3->CR1  |= 0x1u;                                         /* CEN        */
}

void htsn_pwm_servo_us(uint16_t us)
{
	if (us < 1000) us = 1000;
	if (us > 2000) us = 2000;
	TIM1->CCR1 = us;    /* tick == 1 us, so CCR1 == duty in microseconds */
}

void htsn_pwm_buzzer_duty(uint8_t pct)
{
	if (pct > 100) pct = 100;
	TIM3->CCR4 = ((uint32_t)TIM3->ARR + 1u) * (uint32_t)pct / 100u;
}
