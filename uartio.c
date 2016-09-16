/******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 *  FILE
 *      uartio.c
 *
 *  DESCRIPTION
 *      UART IO implementation.
 *
 ******************************************************************************/

/*============================================================================*
 *  SDK Header Files
 *============================================================================*/

#include <uart.h>           /* Functions to interface with the chip's UART */

/*============================================================================*
 *  Local Header Files
 *============================================================================*/

#include "uartio.h"         /* Header file to this source file */
#include "byte_queue.h"     /* Byte queue API */
#include "keyboard.h"     /* Byte queue API */

/*============================================================================*
 *  Private Data
 *============================================================================*/
 
 /* The application is required to create two buffers, one for receive, the
  * other for transmit. The buffers need to meet the alignment requirements
  * of the hardware. See the macro definition in uart.h for more details.
  */
/*! @brief Defines the High Baud Rate. 115K2 is the maximum supported baud rate */
#define HIGH_BAUD_RATE                   (0x01d9) /* 115K2*/
/*! @brief Defines the Low Baud Rate. */ 
#define LOW_BAUD_RATE                    (0x000a) /* 2400 */

#define RX_BUFFER_SIZE      UART_BUF_SIZE_BYTES_64
#define TX_BUFFER_SIZE      UART_BUF_SIZE_BYTES_64

/* Create 64-byte receive buffer for UART data */
UART_DECLARE_BUFFER(rx_buffer, RX_BUFFER_SIZE);

/* Create 64-byte transmit buffer for UART data */
UART_DECLARE_BUFFER(tx_buffer, TX_BUFFER_SIZE);

/*============================================================================*
 *  Private Function Prototypes
 *============================================================================*/

/* UART receive callback to receive serial commands */
static uint16 uartRxDataCallback(void   *p_rx_buffer,
                                 uint16  length,
                                 uint16 *p_req_data_length);

/* UART transmit callback when a UART transmission has finished */
static void uartTxDataCallback(void);

/* Transmit waiting data over UART */
static void sendPendingData(void);

/* Transmit the sleep state over UART */
static void printSleepState(sleep_state sleepstate);

/* Send VT100 command to clear the screen over UART */
static void vt100ClearScreen(void);

/*============================================================================*
 *  Private Function Implementations
 *============================================================================*/

/*----------------------------------------------------------------------------*
 *  NAME
 *      uartRxDataCallback
 *
 *  DESCRIPTION
 *      This is an internal callback function (of type uart_data_in_fn) that
 *      will be called by the UART driver when any data is received over UART.
 *      See DebugInit in the Firmware Library documentation for details.
 *
 * PARAMETERS
 *      p_rx_buffer [in]   Pointer to the receive buffer (uint8 if 'unpacked'
 *                         or uint16 if 'packed' depending on the chosen UART
 *                         data mode - this application uses 'unpacked')
 *
 *      length [in]        Number of bytes ('unpacked') or words ('packed')
 *                         received
 *
 *      p_additional_req_data_length [out]
 *                         Number of additional bytes ('unpacked') or words
 *                         ('packed') this application wishes to receive
 *
 * RETURNS
 *      The number of bytes ('unpacked') or words ('packed') that have been
 *      processed out of the available data.
 *----------------------------------------------------------------------------*/
static uint16 uartRxDataCallback(void   *p_rx_buffer,
                                 uint16  length,
                                 uint16 *p_additional_req_data_length)
{
    if( length > 0 )
    {
        /* First copy all the bytes received into the byte queue */
        BQForceQueueBytes((const uint8 *)p_rx_buffer, length);
        test( (uint16 *)p_rx_buffer);
    }
    
    /* Send any pending data waiting to be sent */
    sendPendingData();
    
    /* Inform the UART driver that we'd like to receive another byte when it
     * becomes available
     */
    *p_additional_req_data_length = (uint16)1;
    
    /* Return the number of bytes that have been processed */
    return length;
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      uartTxDataCallback
 *
 *  DESCRIPTION
 *      This is an internal callback function (of type uart_data_out_fn) that
 *      will be called by the UART driver when data transmission over the UART
 *      is finished. See DebugInit in the Firmware Library documentation for
 *      details.
 *
 * PARAMETERS
 *      None
 *
 * RETURNS
 *      Nothing
 *----------------------------------------------------------------------------*/
static void uartTxDataCallback(void)
{
    /* Send any pending data waiting to be sent */
    sendPendingData();
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      sendPendingData
 *
 *  DESCRIPTION
 *      Send buffered data over UART that was waiting to be sent. Perform some
 *      translation to ensured characters are properly displayed.
 *
 * PARAMETERS
 *      None
 *
 * RETURNS
 *      Nothing
 *----------------------------------------------------------------------------*/
static void sendPendingData(void)
{
    /* Loop until the byte queue is empty */
    while (BQGetDataSize() > 0)
    {
        uint8 byte = '\0';
        
        /* Read the next byte in the queue */
        if (BQPeekBytes(&byte, 1) > 0)
        {
            bool ok_to_commit = FALSE;
            
            /* Check if Enter key was pressed */
            if (byte == '\r')
            {
                /* Echo carriage return and newline */
                const uint8 data[] = {byte, '\n'};
                
                ok_to_commit = UartWrite(data, sizeof(data)/sizeof(uint8));
            }
            else if (byte == '\b')
            /* If backspace key was pressed */
            {
                /* Issue backspace, overwrite previous character on the
                 * terminal, then issue another backspace
                 */
                const uint8 data[] = {byte, ' ', byte};
                
                ok_to_commit = UartWrite(data, sizeof(data)/sizeof(uint8));
            }
            else
            {
                /* Echo the character */
                ok_to_commit = UartWrite(&byte, 1);
            }

            if (ok_to_commit)
            {
                /* Now that UART driver has accepted this data
                 * remove the data from the buffer
                 */
                BQCommitLastPeek();
            }
            else
            {
                /* If UART doesn't have enough space available to accommodate
                 * this data, postpone sending data and leave it in the buffer
                 * to try again later.
                 */
                break;
            }
        }
        else
        {
            /* Couldn't read data for some reason. Postpone sending data and
             * try again later.
            */
            break;
        }
    }
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      printSleepState
 *
 *  DESCRIPTION
 *      Translate the sleep state code and transmit over UART.
 *
 * PARAMETERS
 *      state [in]     Sleep state code to translate and transmit
 *
 * RETURNS
 *      Nothing
 *----------------------------------------------------------------------------*/
static void printSleepState(sleep_state state)
{
    const uint8  message[]   = "\r\nLast sleep state: ";
    const uint8  message_len = (sizeof(message) - 1)/sizeof(uint8);
    const uint8 *p_state_msg;
    uint8        state_msg_len;
    
    switch (state)
    {
        case sleep_state_cold_powerup:
        {
            /* The device powered up after a long time without power */
            const uint8 state_msg[] = "cold_powerup";
            p_state_msg = state_msg;
            state_msg_len = (sizeof(state_msg) - 1)/sizeof(uint8);
            break;
        }   
        case sleep_state_warm_powerup:
        {
            /* The device powered up after a short time without power (less than
             * ~1 minute, based on how long data remains valid in the persistent
             * memory)
             */
            const uint8 state_msg[] = "warm_powerup";
            p_state_msg = state_msg;
            state_msg_len = (sizeof(state_msg) - 1)/sizeof(uint8);
            break;
        }
        case sleep_state_dormant:
        {
            /*  The device powered up after being placed into the Dormant state
             */
            const uint8 state_msg[] = "dormant";
            p_state_msg = state_msg;
            state_msg_len = (sizeof(state_msg) - 1)/sizeof(uint8);
            break;
        }
        case sleep_state_hibernate:
        {
            /* The device powered up after being placed into the Hibernate state 
             */
            const uint8 state_msg[] = "hibernate";
            p_state_msg = state_msg;
            state_msg_len = (sizeof(state_msg) - 1)/sizeof(uint8);
            break;
        }
        case sleep_state_warm_reset:
        {
            /* The device powered up after an application-triggered warm reset
             */
            const uint8 state_msg[] = "warm_reset";
            p_state_msg = state_msg;
            state_msg_len = (sizeof(state_msg) - 1)/sizeof(uint8);
            break;
        }
        default:
        {
            /* Unrecognised sleep state */
            const uint8 state_msg[] = "unknown";
            p_state_msg = state_msg;
            state_msg_len = (sizeof(state_msg) - 1)/sizeof(uint8);
            break;
        }
    }
    
    /* Add opening message to byte queue */
    BQForceQueueBytes(message, message_len);
    
    /* Add translated sleep state to byte queue */
    BQForceQueueBytes(p_state_msg, state_msg_len);
    
    /* Send byte queue over UART */
    sendPendingData();
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      vt100ClearScreen
 *
 *  DESCRIPTION
 *      Sends a VT100 clear screen command over UART
 *
 * PARAMETERS
 *      None
 *
 * RETURNS
 *      Nothing
 *----------------------------------------------------------------------------*/
static void vt100ClearScreen(void)
{
    /* Construct clear screen command for VT-100 terminal client */
    const uint8 clr_scr_cmd[] = { 0x1B, '[', '2', 'J' };
    
    /* Add clear screen command to the byte queue */
    BQForceQueueBytes(clr_scr_cmd, sizeof(clr_scr_cmd)/sizeof(uint8));
    
    /* Send byte queue over UART */
    sendPendingData();
}

/*============================================================================*
 *  Public Function Implementations
 *============================================================================*/

/*----------------------------------------------------------------------------*
 *  NAME
 *      Start
 *
 *  DESCRIPTION
 *      Run the startup routine.
 *
 * PARAMETERS
 *      last_sleep_state [in]   Last sleep state
 *
 * RETURNS
 *      Nothing
 *----------------------------------------------------------------------------*/
void UartStart(sleep_state last_sleep_state)
{
    /* Initialise UART and configure with default baud rate and port
     * configuration
     */
    UartInit(uartRxDataCallback,
             uartTxDataCallback,
             rx_buffer, RX_BUFFER_SIZE,
             tx_buffer, TX_BUFFER_SIZE,
             uart_data_unpacked);

	/* Configure the UART for high baud rate */
	UartConfig(HIGH_BAUD_RATE,0);
    
    /* Enable UART */
    UartEnable(TRUE);

    /* UART receive threshold is set to 1 byte, so that every single byte
     * received will trigger the receiver callback */
    UartRead(1, 0);

    /* Send clear screen command over UART */
    vt100ClearScreen();

    /* Display the last sleep state */
    printSleepState(last_sleep_state);

    /* Construct welcome message */
    const uint8 message[] = "\r\nType something: ";
    
    /* Add message to the byte queue */
    BQForceQueueBytes(message, sizeof(message)/sizeof(uint8));
    
    /* Transmit the byte queue over UART */
    sendPendingData();
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      ProcessSystemEvent
 *
 *  DESCRIPTION
 *      Prints the system event meaning on to UART.
 *
 * PARAMETERS
 *      id     [in]     System event ID
 *      p_data [in]     Event data
 *
 * RETURNS
 *      Nothing
 *----------------------------------------------------------------------------*/
void UartProcessSystemEvent(sys_event_id id, void *p_data)
{
    const uint8  message[]   = "\r\nSystem event: ";
    const uint8  message_len = (sizeof(message) - 1)/sizeof(uint8);
    const uint8  new_line[] = { '\r', '\n' };
    const uint8  new_line_len = sizeof(new_line)/sizeof(uint8);
    const uint8 *p_event_msg;
    uint8        event_msg_len;
    
    switch (id)
    {
        case sys_event_wakeup:
        {
            /* The system was woken by an edge on the WAKE pin */
            const uint8 event_msg[] = "wakeup";
            p_event_msg = event_msg;
            event_msg_len = (sizeof(event_msg) - 1)/sizeof(uint8);
            break;
        }
        case sys_event_battery_low:
        {
            /* The system battery voltage has moved above or below the
             * monitoring threshold
             */
            const uint8 event_msg[] = "battery_low";
            p_event_msg = event_msg;
            event_msg_len = (sizeof(event_msg) - 1)/sizeof(uint8);
            break;
        }
        case sys_event_pio_changed:
        {
            /* One or more PIOs specified by PioSetEventMask() have changed
             * input level
             */
            const uint8 event_msg[] = "pio_changed";
            p_event_msg = event_msg;
            event_msg_len = (sizeof(event_msg) - 1)/sizeof(uint8);
            break;
        }
        case sys_event_pio_ctrlr:
        {
            /* An event was received from the 8051 PIO Controller */
            const uint8 event_msg[] = "pio_ctrlr";
            p_event_msg = event_msg;
            event_msg_len = (sizeof(event_msg) - 1)/sizeof(uint8);
            break;
        }
        default:
        {
            /* Unrecognised system event */
            const uint8 event_msg[] = "unknown";
            p_event_msg = event_msg;
            event_msg_len = (sizeof(event_msg) - 1)/sizeof(uint8);
            break;
        }
    }
    
    /* Add opening message to byte queue */
    BQForceQueueBytes(message, message_len);
    
    /* Add translated sleep state to byte queue */
    BQForceQueueBytes(p_event_msg, event_msg_len);
    
    /* Add carriage return/new line to byte queue */
    BQForceQueueBytes(new_line, new_line_len);
    
    /* Send byte queue over UART */
    sendPendingData();
}
