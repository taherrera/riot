/*
 * Copyright (C) 2014-2015 Eistec AB
 *
 * This file is subject to the terms and conditions of the GNU Lesser General
 * Public License v2.1. See the file LICENSE in the top level directory for more
 * details.
 */

/**
 * @ingroup     board_mulle
 * @{
 *
 * @file
 * @brief       Board specific implementations for the Mulle board
 *
 * @author      Joakim Gebart <joakim.gebart@eistec.se>
 *
 * @}
 */

#include <stddef.h> /* for NULL */
#include <stdio.h>
#include "board.h"
#include "cpu.h"
#include "mcg.h"
#include "periph/gpio.h"
#include "periph/uart.h"
#include "periph/rtc.h"
#include "periph/spi.h"
#include "devicemap.h"
#include "lpm.h"

/**
 * @brief Initialize the boards on-board LEDs
 *
 * The LEDs are initialized here in order to be able to use them in the early
 * boot for diagnostics.
 *
 */
static inline void leds_init(void);

/** @brief Initialize the GPIO pins controlling the power switches. */
static inline void power_pins_init(void);

static inline void trace_gpio_pins_init(void);

/**
 * @brief Set clock prescalers to safe values
 *
 * This should be done before switching to FLL/PLL as clock source to ensure
 * that all clocks remain within the specified limits.
 */
static inline void set_safe_clock_dividers(void);

/** @brief Set the FLL source clock to RTC32k */
static inline void set_fll_source(void);

/** @brief Sleep radio and flash memory if not used */
static inline void set_unused_devices_to_sleep(void);

/** @brief initialize pins for the SPI bus used by the on-board peripherals */
static inline void init_onboard_spi(void);

void board_init(void)
{
    /* initialize the boards LEDs, this is done first for debugging purposes */
    leds_init();

    LED_RED_ON;

    /* Initialize RTC oscillator as early as possible since we are using it as a
     * base clock for the FLL.
     * It takes a while to stabilize the oscillator, therefore we do this as
     * soon as possible during boot in order to let it stabilize while other
     * stuff is initializing. */
    /* If the clock is not stable then the UART will have the wrong baud rate
     * for debug prints as well */
    rtc_init();

    /* Set up clocks */
    set_safe_clock_dividers();

    set_fll_source();

    kinetis_mcg_set_mode(KINETIS_MCG_FEE);

    /* At this point we need to wait for 1 ms until the clock is stable.
     * Since the clock is not yet stable we can only guess how long we must
     * wait. I have tried to make this as short as possible but still being able
     * to read the initialization messages written on the UART.
     * (If the clock is not stable all UART output is garbled until it has
     * stabilized) */
    for (int i = 0; i < 100000; ++i) {
        asm volatile("nop\n");
    }

    /* Update SystemCoreClock global var */
    SystemCoreClockUpdate();

    /* initialize the CPU */
    cpu_init();

    LED_YELLOW_ON;

    LED_GREEN_ON;

    /* Initialize power control pins */
    power_pins_init();

    /* Turn on Vperiph for peripherals */
    gpio_set(MULLE_POWER_VPERIPH);

    /* Turn on AVDD for reading voltages */
    gpio_set(MULLE_POWER_AVDD);

    trace_gpio_pins_init();

    init_onboard_spi();

    set_unused_devices_to_sleep();

    lpm_arch_init();

    LED_RED_OFF;
    LED_YELLOW_OFF;
    LED_GREEN_OFF;
}

static inline void leds_init(void)
{
    /* The pin configuration can be found in board.h and periph_conf.h */
    gpio_init(LED_RED_GPIO, GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_init(LED_YELLOW_GPIO, GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_init(LED_GREEN_GPIO, GPIO_DIR_OUT, GPIO_NOPULL);
}

static inline void power_pins_init(void)
{
    gpio_init(MULLE_POWER_AVDD, GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_init(MULLE_POWER_VPERIPH, GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_init(MULLE_POWER_VSEC, GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_clear(MULLE_POWER_AVDD);
    gpio_clear(MULLE_POWER_VPERIPH);
    gpio_clear(MULLE_POWER_VSEC);
}

static inline void trace_gpio_pins_init(void)
{
#ifdef LPM_TRACE_LPM_ENTRY_GPIO
    gpio_init(LPM_TRACE_LPM_ENTRY_GPIO, GPIO_DIR_OUT, GPIO_NOPULL);
#endif
#ifdef LPM_TRACE_LPM_EXIT_GPIO
    gpio_init(LPM_TRACE_LPM_EXIT_GPIO, GPIO_DIR_OUT, GPIO_NOPULL);
#endif
#ifdef LPM_TRACE_WAIT_GPIO
    gpio_init(LPM_TRACE_WAIT_GPIO, GPIO_DIR_OUT, GPIO_NOPULL);
#endif
#ifdef LPM_TRACE_STOP_GPIO
    gpio_init(LPM_TRACE_STOP_GPIO, GPIO_DIR_OUT, GPIO_NOPULL);
#endif
#ifdef LPM_TRACE_VLPS_GPIO
    gpio_init(LPM_TRACE_VLPS_GPIO, GPIO_DIR_OUT, GPIO_NOPULL);
#endif
#ifdef LPM_TRACE_LLS_GPIO
    gpio_init(LPM_TRACE_LLS_GPIO, GPIO_DIR_OUT, GPIO_NOPULL);
#endif
}

static inline void set_safe_clock_dividers(void)
{
    /*
     * We want to achieve the following clocks:
     * Core/system: <100MHz
     * Bus: <50MHz
     * FlexBus: <50MHz
     * Flash: <25MHz
     *
     * using dividers 1-2-2-4 will obey the above limits when using a 96MHz FLL source.
     */
    SIM->CLKDIV1 = (
        SIM_CLKDIV1_OUTDIV1(CONFIG_CLOCK_K60_SYS_DIV) | /* Core/System clock divider */
        SIM_CLKDIV1_OUTDIV2(CONFIG_CLOCK_K60_BUS_DIV) | /* Bus clock divider */
        SIM_CLKDIV1_OUTDIV3(CONFIG_CLOCK_K60_FB_DIV) | /* FlexBus divider, not used in Mulle */
        SIM_CLKDIV1_OUTDIV4(CONFIG_CLOCK_K60_FLASH_DIV)); /* Flash clock divider */

}

static inline void set_fll_source(void)
{
    /* Select FLL as source (as opposed to PLL) */
    SIM->SOPT2 &= ~(SIM_SOPT2_PLLFLLSEL_MASK);
    /* Use external 32kHz RTC clock as source for OSC32K */
    /* This is also done by hwtimer_arch, but we need it sooner than
     * hwtimer_init. */
#if K60_CPU_REV == 1
    SIM->SOPT1 |= SIM_SOPT1_OSC32KSEL_MASK;
#elif K60_CPU_REV == 2
    SIM->SOPT1 = (SIM->SOPT1 & ~(SIM_SOPT1_OSC32KSEL_MASK)) | SIM_SOPT1_OSC32KSEL(2);
#else
#error Unknown K60 CPU revision
#endif

    /* Select RTC 32kHz clock as reference clock for the FLL */
#if K60_CPU_REV == 1
    /* Rev 1 parts */
    SIM->SOPT2 |= SIM_SOPT2_MCGCLKSEL_MASK;
#elif K60_CPU_REV == 2
    /* Rev 2 parts */
    MCG->C7 = (MCG_C7_OSCSEL_MASK);
#else
#error Unknown K60 CPU revision
#endif
}

static inline void set_unused_devices_to_sleep(void)
{
    /* Deep power down flash */
    /* Flash driver not yet implemented */
    gpio_clear(FLASH0_CS);
    spi_transfer_byte(SPI_0, 0xb9, NULL); /* DP (Deep Power down) command */
    gpio_set(FLASH0_CS);

#if !(MODULE_NG_AT86RF212B) && !(MODULE_AT86RF231)
    /* Sleep radio */
    uint8_t in = 0;
    gpio_clear(AT86RF231_CS);
    spi_transfer_reg(AT86RF231_SPI, 0x81, 0x00, (char *) &in); /* Read TRX_STATUS register */
    gpio_set(AT86RF231_CS);
    while (in != 0x08) {
        /* Reset radio state machine (FORCE_TRX_OFF) */
        gpio_clear(AT86RF231_CS);
        spi_transfer_reg(AT86RF231_SPI, 0xc2, 0x03, NULL);
        gpio_set(AT86RF231_CS);

        for (int i = 0; i < 10000; ++i) {
            asm volatile("nop\n");
        }
        gpio_clear(AT86RF231_CS);
        spi_transfer_reg(AT86RF231_SPI, 0x81, 0x00, (char *) &in);
        gpio_set(AT86RF231_CS);
    }

    gpio_set(AT86RF231_SLEEP);
#endif
}

static inline void init_onboard_spi(void)
{
    gpio_init(AT86RF231_SLEEP, GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_clear(AT86RF231_SLEEP);

    gpio_init(AT86RF231_CS, GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_set(AT86RF231_CS);
    gpio_init(LIS3DH_CS, GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_set(LIS3DH_CS);
    gpio_init(NVRAM0_CS, GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_set(NVRAM0_CS);
    gpio_init(FLASH0_CS, GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_set(FLASH0_CS);

    spi_init_master(AT86RF231_SPI, SPI_CONF_FIRST_RISING, SPI_SPEED_5MHZ);
}
