/*******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 *  FILE
 *      keyboard_gatt.h
 *
 *  DESCRIPTION
 *      Header file for Keyboard GATT-related routines
 *
 ******************************************************************************/

#ifndef __KEYBOARD_GATT_H__
#define __KEYBOARD_GATT_H__

/*=============================================================================*
 *  SDK Header Files
 *============================================================================*/

#include <types.h>
#include <gap_types.h>
#include <bt_event_types.h>

/*=============================================================================*
 *  Public Function Prototypes
 *============================================================================*/

/* This function starts advertisements */
extern void GattStartAdverts(bool fast_connection, gap_mode_connect connect_mode);

/* This function stops on-going advertisements */
extern void GattStopAdverts(void);

/* This function handles GATT_ACCESS_IND message for attributes maintained by
 * the application
 */
extern void GattHandleAccessInd(GATT_ACCESS_IND_T *p_ind);

/* This function starts advertisements using fast connection parameters */
extern void GattTriggerFastAdverts(void);

/* This function checks if the address is resolvable random or not */
extern bool IsAddressResolvableRandom(TYPED_BD_ADDR_T *addr);

/* This function checks if the address is non-resolvable random or not */
extern bool IsAddressNonResolvableRandom(TYPED_BD_ADDR_T *addr);

#endif /* __KEYBOARD_GATT_H__ */
