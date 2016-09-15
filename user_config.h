/*******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 * FILE
 *      user_config.h
 *
 * DESCRIPTION
 *      This file contains definitions which will enable customization of the
 *      application.
 *
 ******************************************************************************/

#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

/*=============================================================================*
 *  Public Definitions
 *============================================================================*/

/* Un-comment below macro to enable GAP Peripheral Privacy support */
/* If Privacy is supported, the keyboard uses resolvable random address for
 * advertising. Peripheral privacy flag and re-connection address
 * characteristics are supported when privacy is supported.
 */
/* #define __GAP_PRIVACY_SUPPORT__ */

/* This macro is used to show whether the keyboard uses a proprietary HID
 * service or not.
 *
 * HID Boot Service is a CSR specific service maintained to remain
 * compatible with our earlier implementations.When this service is
 * used the standard HID Service shall not be used in boot mode.
 */
/* Uncomment the macro here and in app_gatt_db.db to include support for this
 * service.
 */
/* #define __PROPRIETARY_HID_SUPPORT__ */

/* A macro to force MITM while pairing is in progress. Enable this macro to
 * force MITM while pairing with devices using random resolvable address.
 */
//#define __FORCE_MITM__

/* If the keyboard should never disconnect from the host, uncomment this. */
/* #define __NO_IDLE_TIMEOUT__                      1 */

/* Vendor Id */
#define VENDOR_ID                               0x000A
#define PRODUCT_ID                              0x014C
#define PRODUCT_VER                             0x0100

/* Maximum number of Key strokes buffered in the circular queue. This value can
 * be changed depending on the requirement to buffer keys pressed before
 * connection.
 */
#define MAX_PENDING_KEY_STROKES                 30

/* The debouncing timer to be used for the keys of the keyboard */
#define DEBOUNCE_TIMER                          40 * MILLISECOND

/*************** HID related customizable things ******************************/

/* The input report with report ID 1 in the report descriptor. */
#define HID_INPUT_REPORT_ID                     1

/* The input report with report ID 3 in the report descriptor. Report ID 3 is
 * used for consumer reports as can be seen from the report descriptor in
 * hid_service_db.db.
 */
#define HID_CONSUMER_REPORT_ID                  3

/* HID service may use different reports of different sizes. This macro
 * indicates the size of the report of largest size.
 */
#define LARGEST_REPORT_SIZE                     8

/* Parser version (UINT16) - Version number of the base USB HID specification
 * Format - 0xJJMN (JJ - Major Version Number, M - Minor Version
 *                  Number and N - Sub-minor version number)
 */
#define HID_FLAG_CLASS_SPEC_RELEASE             0x0213

/* Country Code (UINT8)- Identifies the country for which the hardware is
 * localized. If the hardware is not localized the value shall be Zero.
 */
#define HID_FLAG_COUNTRY_CODE                   0x00

#define REMOTE_WAKEUP_SUPPORTED                 0x01
#define NORMALLY_CONNECTABLE                    0x02

/* If the application supports normally connectable feature, bitwise or the
 * HID_INFO_FLAGS value with NORMALLY_CONNECTABLE
 */
#define HID_INFO_FLAGS                          REMOTE_WAKEUP_SUPPORTED

/* Consumer report size = MAX_CONSUMER_KEYS * ONE_CONSUMER_KEY_REPORT_SIZE */
#define CONSUMER_REPORT_SIZE                    2

/* Idle timer value in Connected state. At the expiry of this timer,
 * the Keyboard will disconnect itself from the Host.
 */
#define CONNECTED_IDLE_TIMEOUT_VALUE            (30 * MINUTE)

/* Time to wait before sending the buffered input reports. */
#define PENDING_REPORT_WAIT_TIMEOUT             (6 * SECOND)

/* Macro to enable/disable whether the keyboard need to wait for the host
 * to configure for notifications upon re-connection and send HID reports.
 */
/*#define PENDING_REPORT_WAIT */

#ifdef __GAP_PRIVACY_SUPPORT__
/* Uncomment the following if this application is using a resolvable random 
 * address
 */
#define USE_RESOLVABLE_RANDOM_ADDRESS
#endif /* __GAP_PRIVACY_SUPPORT__ */

#ifdef __GAP_PRIVACY_SUPPORT__
#define RANDOM_BLUETOOTH_ADDRESS_TIMEOUT        (15 * MINUTE)
#endif /* __GAP_PRIVACY_SUPPORT__ */

#endif /* __USER_CONFIG_H__ */
