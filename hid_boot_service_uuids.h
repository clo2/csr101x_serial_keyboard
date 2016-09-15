/*******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 *  FILE
 *      hid_boot_service_uuids.h
 *
 *  DESCRIPTION
 *      UUID MACROs for proprietary HID service
 *
 ******************************************************************************/

#ifndef __HID_BOOT_UUIDS_H__
#define __HID_BOOT_UUIDS_H__

/*=============================================================================*
 *  Public Definitions
 *============================================================================*/

/* Brackets should not be used around the value of a macro. The parser 
 * which creates .c and .h files from .db file doesn't understand  brackets 
 * and will raise syntax errors.
 */

/* 128-bit HID Service UUID 0x38373635343332313837363534333231
 * in 8 chunks 
 */
#define HID_SERV_UUID_0          0x3837
#define HID_SERV_UUID_1          0x3635
#define HID_SERV_UUID_2          0x3433
#define HID_SERV_UUID_3          0x3231
#define HID_SERV_UUID_4          0x3837
#define HID_SERV_UUID_5          0x3635
#define HID_SERV_UUID_6          0x3433
#define HID_SERV_UUID_7          0x3231
#define HID_PROP_SERV_UUID       0x38373635343332313837363534333231

/* 128-bit HID boot report UUID 0x38363931313134313836393131313431 
 * in 8 chunks 
 */
#define HID_BOOT_UUID_0          0x3836
#define HID_BOOT_UUID_1          0x3931
#define HID_BOOT_UUID_2          0x3131
#define HID_BOOT_UUID_3          0x3431
#define HID_BOOT_UUID_4          0x3836
#define HID_BOOT_UUID_5          0x3931
#define HID_BOOT_UUID_6          0x3131
#define HID_BOOT_UUID_7          0x3431
#define HID_BOOT_REPORT_UUID     0x38363931313134313836393131313431

#endif /* __HID_BOOT_UUIDS_H__ */