/*******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 *  FILE
 *      keyboard_hw.h
 *
 *  DESCRIPTION
 *      Header definitions for keyboard hardware specific functions.
 *
 ******************************************************************************/

#ifndef _KEYBOARD_HW_
#define _KEYBOARD_HW_

/*=============================================================================*
 *  SDK Header Files
 *============================================================================*/

#include <types.h>
#include <pio.h>
#include <pio_ctrlr.h>
#include <timer.h>

/*=============================================================================*
 *   Public Definitions
 *============================================================================*/

#define UART_RX_PIO             (1)
#define UART_TX_PIO             (0)


/* PIO associated with the pairing button on keyboard. */
#define PAIRING_BUTTON_PIO            (10)

/* PIO which serves as power supply for the EEPROM. */
#define EEPROM_POWER_PIO              (2)

/* NumLock LED PIO, the same PIO is used for SPI CLK when connected to a
 * debugger board using SPI interface.
 */
#define NUMLOCK_LED_PIO               (5)
/* CapsLock LED PIO, the same PIO is used for SPI CSB when connected to a
 * debugger board using SPI interface.
 */
#define CAPSLOCK_LED_PIO              (6)
/* Pair LED PIO, the same PIO is used for SPI MOSI when connected to a
 * debugger board using SPI interface.
 */
#define PAIR_LED_PIO                  (7)
/* Low power LED PIO, the same PIO is used for SPI MISO when connected to a
 * debugger board using SPI interface.
 */
#define LOWPOWER_LED_PIO              (8)

/* Bit-mask of a PIO. */
#define PIO_BIT_MASK(pio)             (0x01UL << (pio))
#define UART_RX_PIO_MASK        (PIO_BIT_MASK(UART_RX_PIO)) 
#define UART_TX_PIO_MASK        (PIO_BIT_MASK(UART_TX_PIO)) 
#define NUMLOCK_LED_BIT_MASK      PIO_BIT_MASK(NUMLOCK_LED_PIO)
#define CAPSLOCK_LED_BIT_MASK     PIO_BIT_MASK(CAPSLOCK_LED_PIO)
#define PAIR_LED_BIT_MASK         PIO_BIT_MASK(PAIR_LED_PIO)
#define LOWPOWER_LED_BIT_MASK     PIO_BIT_MASK(LOWPOWER_LED_PIO)

#define PAIRING_BUTTON_PIO_MASK   PIO_BIT_MASK(PAIRING_BUTTON_PIO)

/* Bit-mask of all the LED PIOs used by keyboard, other than PAIR LED. */
#define KEYBOARD_LEDS_BIT_MASK    NUMLOCK_LED_BIT_MASK | CAPSLOCK_LED_BIT_MASK \
                                  | LOWPOWER_LED_BIT_MASK

/* NumLock output report mask */
#define NUMLOCK_OUTPUT_REPORT_MASK    (0x01)

/* CapsLock output report mask */
#define CAPSLOCK_OUTPUT_REPORT_MASK   (0x02)

/* To disable the Pair LED, comment the below macro. */
#define ENABLE_PAIR_LED

#ifdef ENABLE_PAIR_LED

/* Use PWM0 to control the Pair LED */
#define PAIR_LED_PWM_INDEX            (0)

/* PWM0 Configuration Parameters */
/* DULL ON Time - 0 * 30us = 0us */
#define PAIR_LED_DULL_ON_TIME         (0)

/* DULL OFF Time - 200 * 30us =  6000 us */
#define PAIR_LED_DULL_OFF_TIME        (200)

/* DULL HOLD Time - 124 * 16ms =  1984 ms */
#define PAIR_LED_DULL_HOLD_TIME       (124)

/* BRIGHT OFF Time - 1 * 30us = 30us */
#define PAIR_LED_BRIGHT_OFF_TIME      (1)

/* BRIGHT ON Time - 0 * 30us = 0us */
#define PAIR_LED_BRIGHT_ON_TIME       (0)

/* BRIGHT HOLD Time - 1 * 16ms = 16ms */
#define PAIR_LED_BRIGHT_HOLD_TIME     (1)

/* RAMP RATE - 0 * 30us = 0us */
#define PAIR_LED_RAMP_RATE            (0)

#endif /* ENABLE_PAIR_LED */

/* Mask for PIOs used by PIO controller for Key scanning. All the PIOs of
 * CSR 100x except the above used PIOs are configured to be used by the PIO
 * Controller. PIO0, PIO3, PIO4, PIO9 to PIO23 - scan matrix rows and
 * PIO24 to PIO31 - scan matrix columns
 */
#define PIO_CONTROLLER_BIT_MASK       (0xfffffe19UL)

/* Number of rows in PIO controller key matrix */
#define ROWS                          (18)

/* Number of columns in PIO controller key matrix */
#define COLUMNS                       (8)

/*============================================================================*
 *  Public Data Declarations
 *============================================================================*/

extern timer_id pairing_removal_tid;

/*=============================================================================*
 *  Public Function Prototypes
 *============================================================================*/

/* This function intializes keyboard scanning hardware */
extern void InitHardware(void);

/* This function enables/disables the Pair LED controlled through PWM0 */
extern void EnablePairLED(bool enable);

/* This function starts scanning the keyboard's keys */
extern void StartHardware(void);

/* This function initializes the data structure used by keyboard_hw.c */
extern void HwDataInit(void);

/* This function is called when the user presses a key on the keyboard */
extern bool ProcessKeyPress(uint8 *rows, uint8 *report);

/* This function generates different kinds of reports supported by
 * the keyboard from the raw report generated in ProcessKeyPress()
 */
extern bool FormulateReportsFromRaw(uint8 *raw_report);

/* This function updates the status of LEDs in keyboard */
extern void UpdateKbLeds(uint8 output_report);

/* This function handles PIO Changed event */
extern void HandlePIOChangedEvent(uint32 pio_changed);

/* This function is used to debounce key presses */
extern void DebounceFX(uint8 *rows);

/* This function is used to filter ghost keys */
extern bool GhostFX(uint8 *rows);

#endif /* _KEYBOARD_HW_ */
