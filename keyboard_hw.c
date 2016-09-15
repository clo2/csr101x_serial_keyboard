/*******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 * FILE
 *    keyboard_hw.c
 *
 *  DESCRIPTION
 *    This file defines the keyboard hardware specific routines.
 *
 ******************************************************************************/

/*=============================================================================*
 *  Local Header Files
 *============================================================================*/

#include "keyboard_hw.h"
#include "hid_service.h"
#include "app_gatt_db.h"
#include "user_config.h"
#include "keyboard.h"

#ifdef __PROPRIETARY_HID_SUPPORT__

#include "hid_boot_service.h"

#endif /* __PROPRIETARY_HID_SUPPORT__ */

/*=============================================================================*
 *  SDK Header Files
 *============================================================================*/

#include <sleep.h>
#include <mem.h>

/*=============================================================================*
 *  Private Definitions
 *============================================================================*/

/* PIO direction */
#define PIO_DIRECTION_INPUT               (FALSE)
#define PIO_DIRECTION_OUTPUT              (TRUE)

/* PIO state */
#define PIO_STATE_HIGH                    (TRUE)
#define PIO_STATE_LOW                     (FALSE)

/* Maximum number of HID codes */
#define HIDKEYS                           (6)

/* Number of consumer keys supported by the keyboard application. */
#define N_CONSUMER_KEYS                   (5)

/* Maximum number of scan codes possible for a given keyboard. */
#define MAXMATRIX                         (ROWS * COLUMNS + 1)

/* Maximum size of a look up table made to extract a different HID code based
 * on detection of a special scan code(FUNCTION_KEY here).
 */
#define MAXHIDLUT                         (256)

/* RAWKEYS are used to output the actual scan code in the HID report rather than
 * a HID value. This is used to find out what the mapping is for all the keys on
 * a new keyboard.
 */
#ifndef RAWKEYS

#define FUNCTION_KEY                      (0x8D)
#define CONSUMER_KEYS_BASE                (0xF1)

/* Maximum number of consumer keys that can be sent in one consumer report. */
#define MAX_CONSUMER_KEYS                 (1)

/* If consumer reports with usage ID less than or equal to 0xFF from table 17 of
 * USB HID Usage tables 1.11 is supported, then this length can be reduced to 1.
 */ 
#define ONE_CONSUMER_KEY_REPORT_SIZE      (2)

#else

#define FUNCTION_KEY                      (0x00)

#endif /* RAWKEYS */

/* De-bouncing timer for the pairing removal button */
#define PAIRING_BUTTON_DEBOUNCE_TIME      (100 * MILLISECOND)

/* The time for which the pairing removal button needs to be pressed
 * to remove pairing
 */
#define PAIRING_REMOVAL_TIMEOUT           (1 * SECOND)

/* De-bouncing time for key press */
#define KEY_PRESS_DEBOUNCE_TIME           (25 * MILLISECOND)

/* Key press states */
#define IDLE                              (0)
#define PRESSING                          (1)
#define DOWN                              (2)
#define RELEASING                         (3)

/* Number of scan cycles to be spent in the 'PRESSING' state. */
#define N_SCAN_CYCLES_IN_PRESSING_STATE   (2)

/* Number of scan cycles to be spent in the 'DOWN' state. */
/* In 'DOWN' state, the 'key release' events are ignored assuming that they are
 * due to bouncing. So, the number of scan cycles for which a key will be in
 * 'DOWN' state should be such that all such events are lost by the time the
 * key exits out of 'DOWN' state.
 *
 * NOTE: If this value is changed to suit a different hardware(keyboard),
 * then 'KEY_PRESS_DEBOUNCE_TIME' value should also be increased/decreased
 * by the same time. For example, if this macro is increased to 4, then,
 * 'KEY_PRESS_DEBOUNCE_TIME' which is now 25 ms should be changed to 30 ms
 * (increased by 5 ms which is the time duration of one scan cycle).
 */
#define N_SCAN_CYCLES_IN_DOWN_STATE       (3)

/*=============================================================================*
 *  Private Data Types
 *============================================================================*/

/* Structure which holds the previously generated reports by a key press for all
 * the types of reports used by the keyboard.
 */
typedef struct
{
    /* Last report formed by ProcessKeyPress() from the raw data received from
     * the PIO controller shared memory.
     */
    uint8 last_report[LARGEST_REPORT_SIZE];

     /* Different types of reports formed from the last raw_report */
    uint8 last_input_report[ATTR_LEN_HID_INPUT_REPORT];
    uint8 last_consumer_report[ATTR_LEN_HID_CONSUMER_REPORT];
}LAST_REPORTS_T;

/* Key press debounce structure which holds the key press state and tmaintains 
 * a count for the number of times a key press is seen to detect key bouncing
 */
typedef struct 
{
    uint8 state:8;
    uint8 count:8;
} debounce;

/*=============================================================================*
 *  Private Data
 *============================================================================*/

LAST_REPORTS_T last_generated_reports;


/* Timer to hold the debouncing timer for the pairing removal button press.
 * The same timer is used to hold the debouncing timer for the pairing
 * button
 */
timer_id pairing_removal_tid;

/* Debounce structure for each scan code for the keyboard */
debounce debounce_keys[MAXMATRIX];

/* Timer to hold the debouncing timer for the keys of the keyboard */
static timer_id debounce_reset_timer_id = TIMER_INVALID;

#ifndef RAWKEYS

/* USB HID codes */
static uint8 KEYBOARDKEYMATRIX[MAXMATRIX] = {
/*0     1     2     3     4    5      6     7     8     9    0A    0B    0C    0D    0E    0F*/
0x00, 0x00, 0x4A, 0x4B, 0x4E, 0x00, 0x4D, 0x51, 0x00, 0xE6, 0x4D, 0x5A, 0x42, 0x58, 0x72, 0x14, /* 00-0F */
0x00, 0xE4, 0xA1, 0x00, 0x7B, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x49, 0x2A, 0x52, 0x00, 0x4F, 0x65, /* 10-1F */
0x00, 0x46, 0x31, 0x28, 0x73, 0x00, 0x50, 0x36, 0x10, 0x53, 0x30, 0x00, 0x34, 0x33, 0x38, 0x37, /* 20-2F */
0x00, 0x45, 0x2E, 0xA3, 0x0E, 0x0F, 0x2C, 0x00, 0x11, 0x44, 0x43, 0x27, 0x2D, 0x13, 0x2F, 0x0D, /* 30-3F */
0x0B, 0x42, 0x41, 0x25, 0x26, 0x0C, 0x12, 0x00, 0x05, 0x40, 0x3F, 0x23, 0x24, 0x18, 0x1C, 0x0A, /* 40-4F */
0x00, 0x3E, 0x21, 0x22, 0x15, 0x17, 0x07, 0x09, 0x1B, 0x3D, 0x3C, 0x20, 0x1A, 0x08, 0x16, 0x1D, /* 50-5F */
0x19, 0x3B, 0x3A, 0x1E, 0x1F, 0x14, 0x04, 0x08, 0x06, 0x00, 0xE1, 0x23, 0xE5, 0x6A, 0x22, 0x00, /* 60-6F */
0x00, 0x00, 0xE0, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0xE3, 0x24, 0x56, 0x6D, 0x21, 0x4E, /* 70-7F */
0x00, 0x6B, 0xE2, 0x00, 0x37, 0x00, 0x00, 0x00, 0x00, 0x29, 0x32, 0x35, 0x39, 0x60, 0x2B, 0x28, /* 80-8F */
0x00                                                                                            /* 90    */
};

/* Map Function key USB HID codes to the actual function keys used 
 *
 * Mapped combinations:
 *      Fn + F3 = Mute                  -> 0xF1
 *      Fn + F4 = Volume Down           -> 0xF2
 *      Fn + F5 = Volume Up             -> 0xF3
 *      Fn + F6 = Home Page             -> 0xF4
 *      Fn + F7 = Outlook(Mail Reader)  -> 0xF5
 */
/* If more consumer keys are to be added, place the next codes - 0xF6, 0xF7 and
 * so on at appropriate positions in this array.
 */
static uint8 HIDLUT[MAXHIDLUT] = {
0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x5D, 0x59, 0x5A, 0x5B, /* 00 - 0F */
0x62, 0x11, 0x5E, 0x56, 0x14, 0x15, 0x16, 0x17, 0x5C, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, /* 10 - 1F */
0x20, 0x21, 0x22, 0x23, 0x5F, 0x60, 0x61, 0x55, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, /* 20 - 2F */
0x30, 0x31, 0x32, 0x57, 0x34, 0x35, 0x36, 0x63, 0x54, 0x39, 0x3A, 0x3B, 0xF1, 0xF2, 0xF3, 0xF4, /* 30 - 3F */
0xF5, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x48, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, /* 40 - 4F */
0x50, 0x51, 0x52, 0x47, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, /* 50 - 5F */
0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, /* 60 - 6F */
0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0xF1, /* 70 - 7F */
0xF2, 0xF3, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, /* 80 - 8F */
0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, /* 90 - 9F */
0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, /* A0 - AF */
0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0x00, 0x00, 0xBC, 0xBD, 0xBE, 0x00, /* B0 - BF */
0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, /* C0 - CF */
0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, /* D0 - DF */
0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, /* E0 - EF */
0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF, /* F0 - FF */
};

/* The array from which the consumer keys are picked and populated to the
 * consumer report array. These values are the Usage IDs Table 17: Consumer
 * usage page of USB HID usage table 1.1.
 *
 * Value from HIDLUT         Corresponding consumer key
 *      F1                          MUTE
 *      F2                          VOLUME DOWN
 *      F3                          VOLUME UP
 *      F4                          HOME PAGE
 *      F5                          EMAIL READER
 */
static uint16 CONSUMERKEYS[N_CONSUMER_KEYS] = {
              0x00E2, /* Mute */
              0x00EA, /* Volume down */
              0x00E9, /* Volume Up */
              0x0223, /* AC Home Page */
              0x018A  /* AL Email Reader */
};

#endif /* !RAWKEYS */

/*=============================================================================*
 *  Private Function Prototypes
 *============================================================================*/

void pio_ctrlr_code(void);  /* Included externally in PIO controller code.*/
static void handlePairPioStatusChange(timer_id tid);
static bool handleDebounceKey(bool key,uint8 scan_code, bool* press_to_down, bool* is_in_down);
static void debounceResetTimer(timer_id tid);


/*=============================================================================*
 *  Private Function Implementations
 *============================================================================*/


/*-----------------------------------------------------------------------------*
 *  NAME
 *      handlePairPioStatusChange
 *
 *  DESCRIPTION
 *      This function is called upon detecting a pairing button press. On the
 *      actual hardware this may be seen as a 'connect' button.
 *
 *  RETURNS
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void handlePairPioStatusChange(timer_id tid)
{
    if(tid == pairing_removal_tid)
    {
        
        /* Get the PIOs status to check whether the pairing button is pressed or
         * released
         */
        uint32 pios = PioGets();

        /* Reset the pairing removal timer */
        pairing_removal_tid = TIMER_INVALID;

        /* If the pairing button is still pressed after the expiry of debounce
         * time, then create a timer of PAIRING_REMOVAL_TIMEOUT after which
         * pairing will be removed. Before the expiry of this
         * timer(PAIRING_REMOVAL_TIMEOUT), if a pairing button removal is
         * detected, then this timer will be deleted
         */
        if(!(pios & PAIRING_BUTTON_PIO_MASK))
        {
            /* Create a timer after the expiry of which pairing information
             * will be removed
             */
            pairing_removal_tid = TimerCreate(
                   PAIRING_REMOVAL_TIMEOUT, TRUE, HandlePairingButtonPress);
        }

        /* Re-enable events on the pairing button */
        PioSetEventMask(PAIRING_BUTTON_PIO_MASK, pio_event_mode_both);

    } /* Else ignore the function call as it may be due to a race condition */
}
 
/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleDebounceKey
 *
 *  DESCRIPTION
 *      This function handles debouncing for each scan code
 *
 *  RETURNS
 *      BOOLEAN - TRUE: Key press detected
 *                FALSE: Key press not detected
 *
 *----------------------------------------------------------------------------*/

static bool handleDebounceKey(bool key,uint8 scan_code, bool* press_to_down, bool* is_in_down)
{
    bool out_key = FALSE;
    
    /* Check and update the state of each key */
    switch(debounce_keys[scan_code].state)
    {
        case IDLE:
        {
            /* If a key press is detected, move the state to 'PRESSING' */
            if(key)
            {
                debounce_keys[scan_code].state = PRESSING;
                debounce_keys[scan_code].count = 1;
            }
        }
        break;
        
        case PRESSING:
        /* If the key is pressed for two more continuous scan cycles, then
         * move to 'DOWN' state. Otherwise move back to 'IDLE' state
         */
        {
            if(key)
            {
                if(debounce_keys[scan_code].count++ >=
                                                N_SCAN_CYCLES_IN_PRESSING_STATE)
                {
                    debounce_keys[scan_code].state = DOWN;
                    debounce_keys[scan_code].count = 1;
                    out_key = TRUE;
                    /* While moving to 'DOWN' state set the 'press_to_down'
                     * variable to TRUE so that the 'debounce_reset_timer_tid'
                     * will be started, upon the expiry of which, the state of
                     * the key will be updated back to 'IDLE', if no event is
                     * received after exiting out of 'DOWN' state to 'RELEASING'
                     * state
                     */
                    *press_to_down = TRUE;
                }
            } 
            else
            {
                debounce_keys[scan_code].state = IDLE;
            }
        }
        break;
        
        case DOWN:
        {
            /* The key comes to 'DOWN' state after the key presses are
             * received continuously for 3 scan cycles. If any release event
             * is received in this state, ignore them as they are due to
             * bouncing and send that the key is pressed for the next 3 scan
             * cycles. If a valid key release event is lost in these scan cycles,
             * the 'debounce_reset_timer_tid' expiry will bring the state of the
             * key back to 'IDLE' state
             */
            out_key = TRUE;
            *is_in_down = TRUE;
 
            if(debounce_keys[scan_code].count++ >= N_SCAN_CYCLES_IN_DOWN_STATE)
            {
                debounce_keys[scan_code].state = RELEASING;
                debounce_keys[scan_code].count = 1;
            }
        }
        break;
        
        case RELEASING:
        {
            /* If a key press is received in this state, stay in the same state.
             * Upon receiving a key release, move back to 'IDLE' state
             */
            if(key)
            {
                out_key = TRUE;
            }
            else  /* key up */
            {
                debounce_keys[scan_code].state = IDLE;
                debounce_keys[scan_code].count = 1;
            }
        }
        break;
    }

    return(out_key);
}


/*-----------------------------------------------------------------------------*
 *  NAME
 *      debounceResetTimer
 *
 *  DESCRIPTION
 *      This function handles expiry of debounce timer
 *
 *  RETURNS
 *      Nothing
 *
 *----------------------------------------------------------------------------*/

static void debounceResetTimer(timer_id tid)
{
    uint8  raw_report[LARGEST_REPORT_SIZE];

    if (debounce_reset_timer_id == tid)
    {
        debounce_reset_timer_id = TIMER_INVALID;
        
        /* Reset the keys to IDLE state and send the zeroed HID report */
        MemSet(debounce_keys, 0, sizeof(debounce_keys));

        MemSet(raw_report, 0, sizeof(raw_report));
        ProcessReport(raw_report);
    }
}

/*=============================================================================*
 *  Public Function Implementations
 *============================================================================*/

/*-----------------------------------------------------------------------------*
 *  NAME
 *      InitHardware  -  intialise keyboard scanning hardware
 *
 *  DESCRIPTION
 *      This function is called when a connection has been established and
 *      is used to start the scanning of the keyboard's keys.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

void InitHardware(void)
{
    /* Initialise debounce keys structure to zero */
    MemSet(debounce_keys, 0, sizeof(debounce_keys));

    /* Set up the PIO controller. */
    PioCtrlrInit((uint16*)&pio_ctrlr_code);

    /* Give the PIO controller access to the keyboard PIOs */
    PioSetModes(PIO_CONTROLLER_BIT_MASK, pio_mode_pio_controller);

    /* Set the pull mode of the PIOs. We need strong pull ups on inputs and
       outputs because outputs need to be open collector. This allows rows and
       columns to be shorted together in the key matrix */
    PioSetPullModes(PIO_CONTROLLER_BIT_MASK, pio_mode_strong_pull_up);

    /* Don't wakeup on UART RX line toggling */
//    SleepWakeOnUartRX(FALSE);

    /* Overlay the SPI debug lines with LEDS */
    PioSetModes((uint32)KEYBOARD_LEDS_BIT_MASK, pio_mode_user);
    
    /* All the LED PIOs need to be configured for output direction. The
     * KEYBOARD_LEDS_BIT_MASK itself will serve as the output parameter
     * for PioSetDirs as it has the relevant bits set.
     */
    PioSetDirs((uint32)KEYBOARD_LEDS_BIT_MASK, (uint32)KEYBOARD_LEDS_BIT_MASK);

    /* Switch off all the LEDs. */
    PioSets((uint32)KEYBOARD_LEDS_BIT_MASK, (uint32)PIO_STATE_LOW);

    /* set up pairing button on PIO1 with a pull up (was UART) */
    PioSetMode(PAIRING_BUTTON_PIO, pio_mode_user);
    PioSetDir(PAIRING_BUTTON_PIO, PIO_DIRECTION_INPUT);
    PioSetPullModes(PAIRING_BUTTON_PIO_MASK, pio_mode_strong_pull_up);
    /* Setup button on PIO1 */
    PioSetEventMask(PAIRING_BUTTON_PIO_MASK, pio_event_mode_both);

	/* Set the UART Rx PIO to user mode */
	PioSetModes(UART_RX_PIO_MASK, pio_mode_user);
	PioSetDir(UART_RX_PIO, PIO_DIRECTION_INPUT);
	PioSetPullModes(UART_RX_PIO_MASK, pio_mode_strong_pull_up);
	PioSetEventMask(UART_RX_PIO_MASK, pio_event_mode_disable);

	/* Set the UART Tx PIO to user mode */
	PioSetModes(UART_TX_PIO_MASK, pio_mode_user);
	PioSetDir(UART_TX_PIO, PIO_DIRECTION_OUTPUT);
	PioSetPullModes(UART_TX_PIO_MASK, pio_mode_strong_pull_up);
	PioSetEventMask(UART_TX_PIO_MASK, pio_event_mode_disable);


#ifdef ENABLE_PAIR_LED

    /* Set up the pair LED to normal user mode. When the application wants
     * to glow it, it will be re-configured to be controlled through PWM0.
     */
    PioSetMode(PAIR_LED_PIO, pio_mode_user);
    PioSetDir(PAIR_LED_PIO, PIO_DIRECTION_OUTPUT);
    PioSet(PAIR_LED_PIO, FALSE);

#endif /* ENABLE_PAIR_LED */
 
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      EnablePairLED  -  Enable/Disable the Pair LED mapped to PWM0
 *
 *  DESCRIPTION
 *      This function is called to enable/disable the Pair LED controlled 
 *      through PWM0
 *
 *  RETURNS/MODIFIES
 *      Nothing.
*------------------------------------------------------------------------------*/
void EnablePairLED(bool enable)
{

#ifdef ENABLE_PAIR_LED

    if(enable)
    {
        /* Setup Pair LED - PIO-7 controlled through PWM0.*/
        PioSetMode(PAIR_LED_PIO, pio_mode_pwm0);
        
        /* Configure the PWM0 parameters */
        PioConfigPWM(PAIR_LED_PWM_INDEX, pio_pwm_mode_push_pull,
                     PAIR_LED_DULL_ON_TIME, PAIR_LED_DULL_OFF_TIME,
                     PAIR_LED_DULL_HOLD_TIME, PAIR_LED_BRIGHT_OFF_TIME,
                     PAIR_LED_BRIGHT_ON_TIME, PAIR_LED_BRIGHT_HOLD_TIME,
                     PAIR_LED_RAMP_RATE);

        /* Enable the PWM0 */
        PioEnablePWM(PAIR_LED_PWM_INDEX, TRUE);
    }
    else
    {
        /* Disable the PWM0 */
        PioEnablePWM(PAIR_LED_PWM_INDEX, FALSE);
        /* Reconfigure PAIR_LED_PIO to pio_mode_user. This reconfiguration has
         * been done because when PWM is disabled, PAIR_LED_PIO value may remain
         * the same as it was, at the exact time of disabling. So if
         * PAIR_LED_PIO was on, it may remain ON even after disabling PWM. So,
         * it is better to reconfigure it to user mode. It will get reconfigured
         * to PWM mode when we re-enable the LED.
         */
        PioSetMode(PAIR_LED_PIO, pio_mode_user);
        PioSetDir(PAIR_LED_PIO, PIO_DIRECTION_OUTPUT);
        PioSet(PAIR_LED_PIO, FALSE);

    }

#endif /* ENABLE_PAIR_LED */

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      StartHardware  -  start scanning the keyboard's keys
 *
 *  DESCRIPTION
 *      This function is called when a connection has been established and
 *      is used to start the scanning of the keyboard's keys.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
void StartHardware(void)
{
    /* Start running the PIO controller code */
    PioCtrlrStart();
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      HwDataInit
 *
 *  DESCRIPTION
 *      This function is used to initialise data structure used by keyboard_hw.c
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void HwDataInit(void)
{
    MemSet(last_generated_reports.last_report, 0, LARGEST_REPORT_SIZE);
    MemSet(last_generated_reports.last_input_report, 0,
                                                  ATTR_LEN_HID_INPUT_REPORT);
    MemSet(last_generated_reports.last_consumer_report, 0,
                                                  ATTR_LEN_HID_CONSUMER_REPORT);

}


/*-----------------------------------------------------------------------------*
 *  NAME
 *      DebounceFX
 *
 *  DESCRIPTION
 *      This function is used to debounce key presses
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void DebounceFX(uint8 *rows)
{
    uint16 i;
    uint16 j;
    uint8 row,scan_code=1;
    bool key,out_key, found_key_pressed = FALSE;
    bool press_to_down = FALSE, is_in_down = FALSE;

    /* Check the key matrix for keys pressed */
    for (i=0; i<ROWS; i++)
    {
        row = rows[i];  /* take a local copy of the row. */

        /* pick out pressed keys */
        for (j = 0; j < COLUMNS; j++)
        {
            /* check each bit of the word for pressed keys */
            if (row & 0x01)  /* mask bottom bit */
            {
                /* have a key pressed - valid scan code */
                key =1;
                found_key_pressed = TRUE;
            }
            else
            {
                /* No key pressed */
                key = 0;
            }

            /* Deal with state and count per bit update matrix*/
            out_key = handleDebounceKey(key,scan_code, &press_to_down, &is_in_down);
            
            /* Update rows[i] */
            if(out_key)
            {
                rows[i] |= 0x01 << j;
            }
            else
            {
                rows[i] &= ~(0x01 << j);
            }
            
            row = row >> 1;
            scan_code++;
        }

     }

    if(!is_in_down)
    {
        /* Kill the timer if set since none of the keys are in DOWN state */
        if(debounce_reset_timer_id != TIMER_INVALID)
        {
            TimerDelete(debounce_reset_timer_id);
            debounce_reset_timer_id = TIMER_INVALID;
        }
    }

    if(press_to_down)
    {
        /* (Re)start the clean-up timer for the keys entering the DOWN state */
        if(debounce_reset_timer_id != TIMER_INVALID)
        {
            TimerDelete(debounce_reset_timer_id);
        }

        debounce_reset_timer_id = TimerCreate(KEY_PRESS_DEBOUNCE_TIME, TRUE, 
                                              debounceResetTimer);
    }
    
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      GhostFX
 *
 *  DESCRIPTION
 *      This function is used to filter ghost keys 
 *
 *  RETURNS/MODIFIES
 *      BOOLEAN - TRUE: Ghost key detected
 *                FALSE: Ghost key not detected
 *
 *----------------------------------------------------------------------------*/

extern bool GhostFX(uint8 *rows)
{
    uint16 r;
    uint16 c,k;
    uint8 row_out;
    bool ghost = FALSE;
    uint8 row_shorted;
        
    /* Check the key matrix for keys pressed */
    for (r = 0; r < ROWS; r++)
    {
        if (rows[r])  /* see if there is a key pressed in the row */
        {
            row_shorted = 0;
            row_out = 0;
            /* look for pressed keys */
            for (c = 0; c < COLUMNS; c++)
            {
                /* check each bit of the row for pressed keys */
                if (rows[r] & (1 << c))  
                {
                    row_shorted++;

                    if (row_shorted >= 2) /* 2 or more keys in row */
                    {
                        /* check all other rows to see if there is a shorted 
                         * column 
                         */
                        for (k = 0; k < ROWS; k++)
                        {
                           if ((rows[k] & rows[r]) && (k != r))
                           {
                                ghost = TRUE;
                                return ghost;
                           }
                        }
                    }
                }
            }
        }
    }
    return ghost;
}


/*-----------------------------------------------------------------------------*
 *  NAME
 *      ProcessKeyPress
 *
 *  DESCRIPTION
 *      This function is when the user presses a key on the keyboard and hence 
 *      there is a "sys_event_pio_ctrlr" event that has to be handled by the
 *      application. The function also checks for ghost keys.
 *
 *  RETURNS/MODIFIES
 *      True if there is new data to be sent.
 *
 *----------------------------------------------------------------------------*/

extern bool ProcessKeyPress(uint8 *rows, uint8 *raw_report)
{
    uint16 i;
    uint16 j;
    
    uint8  *p = NULL;
    uint8  *p_modifier = NULL;

    bool   function_key = FALSE;
    bool   report_changed = FALSE;
    
    uint8  scan_code=1;  /* don't use the 0 scan code */
    uint8  keys_added = 0;
    uint8  hid_code;
    uint8  row;

    /* 
     * Structure of Keyboard Input Report 
     * Byte 0 - Modifier Keys (i.e. ctrl, alt, shift)
     * Byte 1 - Reserved
     * Byte 2
     * to 7   - Key presses
     */

    p_modifier = raw_report;

    /* Skip reserved byte of the report */
    p = &raw_report[2]; /* set a pointer to the HID codes */
    
    /* Check the PIO controller key matrix for keys pressed */
    for (i=0; i < ROWS; i++)
    {
        row = rows[i];  /* take a local copy of the row. */
        
        /* if there is a key pressed in the row check which position and assign
         * it a HID code.
         */             
        if (row)
        {

            /* pick out pressed keys */
            for (j = 0; j < COLUMNS; j++)
            {
                /* check each bit of the word for pressed keys */
                if (row & 0x01)  /* mask bottom bit */
                {
                    if (scan_code == FUNCTION_KEY)
                    {
                        function_key = TRUE;
                    }
                    else
                    {
                        /* if new key bit is set then we have a new key pressed */
                        /* if there is room save key.
                         */
                        if (keys_added < HIDKEYS)
                        {
                            keys_added++;
                            
#ifdef RAWKEYS

                            hid_code = scan_code; /* raw scan code */

#else

                            /* look up HID code */
                            hid_code = KEYBOARDKEYMATRIX[scan_code];

#endif /* RAWKEYS */

                            /* Is it a modifier? */
                            if (hid_code >= 224 && hid_code <= 231) 
                            {
                                hid_code = hid_code - 224;
                                *p_modifier = *p_modifier | (1<<hid_code);
                                /* Decrement the keys_added as no key is added
                                 * to the field of input report that is supposed
                                 * to have keys pressed.
                                 */
                                keys_added--;
                            }
                            else
                            {
                                *p++ = hid_code;
                            }
                        }
                        /* if we have run out of key space - max 6 */
                        else 
                        {
                            /* set roll over for last HID char */
                            raw_report[LARGEST_REPORT_SIZE - 1] = 0x01;
                        }
                    }
                }

                row = row >> 1;
                scan_code++;
            }
        }
        else
        {
            scan_code += COLUMNS;
        }
    }

#ifndef RAWKEYS

    /* look up new HID keys to overlay keys that are valid when the
     * function key is pressed
     */
    if (function_key)
    {
        for (i = 2; i < LARGEST_REPORT_SIZE; i++)
        {
            raw_report[i] = HIDLUT[raw_report[i]];
        }
    }

#endif /* RAWKEYS */

    /* Check if the last sent report is not the same as the new raw
     * report
     */
    if(MemCmp(raw_report, last_generated_reports.last_report,
                                                   LARGEST_REPORT_SIZE))
    {
        report_changed = TRUE;

        /* Update the last generated report */
        MemCopy(last_generated_reports.last_report, raw_report,
                                                   LARGEST_REPORT_SIZE);
    }

    /* Return true if there is a new report generated. */
    return report_changed;
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      FormulateReportsFromRaw
 *
 *  DESCRIPTION
 *      This function is to generate different kinds of reports supported by
 *      the keyboard from the raw report generated in ProcessKeyPress()
 *
 *  RETURNS/MODIFIES
 *      True if there is new data in the application queue to be sent to the
 *      remote device
 *
 *----------------------------------------------------------------------------*/
 
extern bool FormulateReportsFromRaw(uint8 *raw_report)
{
    uint8 input_report[ATTR_LEN_HID_INPUT_REPORT];
    uint8 consumer_report[ATTR_LEN_HID_CONSUMER_REPORT];
    uint8 *p_input_report = input_report;
    uint8 i;
    bool new_data = FALSE;

#ifndef RAWKEYS
    
    uint8 *p_consumer_report = consumer_report;
    uint8 n_consumer_keys_added = 0;

#endif /* RAWKEYS */

    MemSet(input_report, 0, ATTR_LEN_HID_INPUT_REPORT);
    MemSet(consumer_report, 0, ATTR_LEN_HID_CONSUMER_REPORT);

    /* The first byte of 'raw_report' is modifier byte and the second is the
     * reserved byte. So copy these to the 'input_report'.
     */
    MemCopy(input_report, raw_report, 2);
    p_input_report += 2;

    /* Parse through the raw_report and populate the different types of report
     * supported by the keyboard.
     */
    for(i = 2; i < LARGEST_REPORT_SIZE; i++)
    {

#ifndef RAWKEYS

        if(raw_report[i] >= CONSUMER_KEYS_BASE)
        {
            if(n_consumer_keys_added < MAX_CONSUMER_KEYS)
            {
                MemCopyUnPack(p_consumer_report,
                            &(CONSUMERKEYS[raw_report[i] - CONSUMER_KEYS_BASE]),
                              ONE_CONSUMER_KEY_REPORT_SIZE);
                ++n_consumer_keys_added;
                p_consumer_report += ONE_CONSUMER_KEY_REPORT_SIZE;
            }
        }

        else

#endif /* RAWKEYS */
        
        {
            MemCopy(p_input_report++, &(raw_report[i]), 1);
        }
    }

    /* If a new input report is created, add it to queue and update the last
     * generated input report.
     */
    if(MemCmp(input_report, last_generated_reports.last_input_report,
                                                     ATTR_LEN_HID_INPUT_REPORT))
    {
        AddKeyStrokeToQueue(HID_INPUT_REPORT_ID, input_report,
                                                     ATTR_LEN_HID_INPUT_REPORT);
        MemCopy(last_generated_reports.last_input_report, input_report,
                                                     ATTR_LEN_HID_INPUT_REPORT);
        new_data = TRUE;
    }

    /* If a new consumer report is created, add it to queue and update the last
     * generated consumer report.
     */
    if(MemCmp(consumer_report, last_generated_reports.last_consumer_report,
                                                  ATTR_LEN_HID_CONSUMER_REPORT))
    {
        AddKeyStrokeToQueue(HID_CONSUMER_REPORT_ID, consumer_report,
                                                  ATTR_LEN_HID_CONSUMER_REPORT);
        MemCopy(last_generated_reports.last_consumer_report, consumer_report,
                                                  ATTR_LEN_HID_CONSUMER_REPORT);
        new_data = TRUE;
    }
    /* Further report types can be added if supported by the keyboard. */

    return new_data;
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      UpdateKbLedsStatus
 *
 *  DESCRIPTION
 *      This function is used to update the status of LEDs upon receiving an
 *      output report. At present only two LEDs are present on the keyboard.
 *      When there are more LEDs available, the function can be extended.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
extern void UpdateKbLeds(uint8 output_report)
{
    PioSet(NUMLOCK_LED_PIO, output_report & NUMLOCK_OUTPUT_REPORT_MASK);
    PioSet(CAPSLOCK_LED_PIO, output_report & CAPSLOCK_OUTPUT_REPORT_MASK);
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      HandlePIOChangedEvent
 *
 *  DESCRIPTION
 *      This function handles PIO Changed event
 *
 *  RETURNS
 *      Nothing.
 *
 *---------------------------------------------------------------------------*/

extern void HandlePIOChangedEvent(uint32 pio_changed)
{
    if(pio_changed & PAIRING_BUTTON_PIO_MASK)
    {
        /* Get the PIOs status to check whether the pairing button is
         * pressed or released
         */
        uint32 pios = PioGets();

        /* Delete the presently running timer */
        TimerDelete(pairing_removal_tid);

        /* If the pairing button is pressed....*/
        if(!(pios & PAIRING_BUTTON_PIO_MASK))
        {

            /* Disable any further events due to change in status of
             * pairing button PIO. These events are re-enabled once the
             * debouncing timer expires
             */
            PioSetEventMask(PAIRING_BUTTON_PIO_MASK, pio_event_mode_disable);

            pairing_removal_tid = TimerCreate(PAIRING_BUTTON_DEBOUNCE_TIME,
                                               TRUE, handlePairPioStatusChange);
        }

        else /* It's a pairing button release */
        {
            pairing_removal_tid = TIMER_INVALID;
        }                
    }
}
