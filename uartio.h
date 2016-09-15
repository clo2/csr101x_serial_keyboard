/******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 *  FILE
 *      uartio.h
 *
 *  DESCRIPTION
 *      UART IO header
 *
 ******************************************************************************/

#ifndef __UARTIO_H__
#define __UARTIO_H__

/*============================================================================*
 *  SDK Header Files
 *============================================================================*/
 
#include <types.h>          /* Commonly used type definitions */
#include <sys_events.h>     /* System event definitions and declarations */
#include <sleep.h>          /* Control the device sleep states */

/*============================================================================*
 *  Public Function Prototypes
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
extern void UartStart(sleep_state last_sleep_state);

/*----------------------------------------------------------------------------*
 *  NAME
 *      ProcessSystemEvent
 *
 *  DESCRIPTION
 *      Prints the system event meaning on to UART.
 *
 * PARAMETERS
 *      id   [in]   System event ID
 *      pData [in]  Event data
 *
 * RETURNS
 *      Nothing
 *----------------------------------------------------------------------------*/
void UartProcessSystemEvent(sys_event_id id, void *pData);

#endif /* __UARTIO_H__ */
