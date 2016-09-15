/*******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 *  FILE
 *    hid_boot_service.h
 *
 *  DESCRIPTION
 *    Header definitions for HID Boot service (for Keyboard application).
 *
 *  NOTE:
 *
 *    HID Boot Service is a CSR specific service maintained to remain 
 *    compatible with our earlier implementations.When this service is 
 *    used the standard HID Service shall not be used in boot mode.
 *
 ******************************************************************************/

#ifdef __PROPRIETARY_HID_SUPPORT__

#ifndef __HID_BOOT_SERVICE_H__
#define __HID_BOOT_SERVICE_H__

/*=============================================================================*
 *  SDK Header Files
 *============================================================================*/

#include <types.h>
#include <bt_event_types.h>

/*=============================================================================*
 *  Local Header Files
 *============================================================================*/

#include "app_gatt.h"

/*=============================================================================*
 *  Public Function Prototypes
 *============================================================================*/

/* This function initializes the data structure of proprietary HID service */
extern void HidBootDataInit(void);

/* This function handles read operation on proprietary HID service attributes
 * maintained by the application
 */
extern void HidBootHandleAccessRead(GATT_ACCESS_IND_T *p_ind);

/* This function handles write operation on proprietary HID service attributes
 * maintained by the application
 */
extern void HidBootHandleAccessWrite(GATT_ACCESS_IND_T *p_ind);

/* This function checks whether notifications have been enabled for
 * the reports of proprietary HID service
 */
extern bool HidBootGetNotificationStatus(void);

/* This function is used to send reports as notifications for the report
 * characteristic of proprietary HID service
 */
extern bool HidSendBootInputReport(uint16 ucid, uint8 report_id, uint8 *data);

/* This function is used to check if the handle belongs to the proprietary HID
 * service
 */
extern bool HidBootCheckHandleRange(uint16 handle);

#endif /* __HID_BOOT_SERVICE_H__ */

#endif /* __PROPRIETARY_HID_SUPPORT__ */

