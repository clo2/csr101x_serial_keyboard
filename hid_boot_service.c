/*******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 * FILE
 *    hid_boot_service.c
 *
 *  DESCRIPTION
 *    This file defines routines for using CSR's proprietary HID service.
 *
 *  NOTE:
 *
 *    HID Boot Service is a CSR specific service maintained to remain 
 *    compatible with our earlier implementations.When this service is 
 *    used the standard HID Service shall not be used in boot mode.
 *
 ******************************************************************************/
 
/*=============================================================================*
 *  Local Header Files
 *============================================================================*/

#include "user_config.h"

#ifdef __PROPRIETARY_HID_SUPPORT__

#include "app_gatt.h"
#include "hid_boot_service.h"
#include "hid_service.h"
#include "app_gatt_db.h"
#include "keyboard_hw.h"

/*=============================================================================*
 *  SDK Header Files
 *============================================================================*/
 
#include <gatt.h>
#include <gatt_prim.h>
#include <mem.h>
#include <buf_utils.h>

/*=============================================================================*
 *  Private Definitions
 *============================================================================*/

/* HID BOOT Output report length */
#define ATTR_LEN_HID_PROP_BOOT_OUTPUT_REPORT    (1)

/*=============================================================================*
 *  Private Data Declaration
 *============================================================================*/

typedef struct
{
    /* Array to hold output report data.*/
    uint8           boot_output_report[ATTR_LEN_HID_PROP_BOOT_OUTPUT_REPORT];
    
    /* Input Report Client Configuration */
    gatt_client_config  prop_boot_input_client_config;

} HID_BOOT_DATA;


/*=============================================================================*
 *  Private Data
 *============================================================================*/

HID_BOOT_DATA hid_boot_data;

/*=============================================================================*
 *  Private Function Prototypes
 *============================================================================*/

static void handleBootOutputReport(GATT_ACCESS_IND_T *ind);

/*=============================================================================*
 *  Private Function Implementations
 *============================================================================*/
/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleBootOutputReport
 *
 *  DESCRIPTION
 *      This function handles output report.
 *
 *  RETURNS/MODIFIES
 *      Nothing
 *
 *----------------------------------------------------------------------------*/

static void handleBootOutputReport(GATT_ACCESS_IND_T *ind)
{
    MemCopy(hid_boot_data.boot_output_report, ind->value, 
           ATTR_LEN_HID_PROP_BOOT_OUTPUT_REPORT);

    UpdateKbLeds(hid_boot_data.boot_output_report[0]);
}

/*=============================================================================*
 *  Public Function Implementations
 *============================================================================*/

/*-----------------------------------------------------------------------------*
 *  NAME
 *      HidBootDataInit
 *
 *  DESCRIPTION
 *      This function is used to initialise HID BOOT service data 
 *      structure.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void HidBootDataInit(void)
{
    /* Initialise Boot Input Characteristic Client Configuration */
    hid_boot_data.prop_boot_input_client_config = gatt_client_config_none;

    MemSet(hid_boot_data.boot_output_report, 0x00,
                                        ATTR_LEN_HID_PROP_BOOT_OUTPUT_REPORT);

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      HidBootHandleAccessRead
 *
 *  DESCRIPTION
 *      This function handles Read operation on HID BOOT service attributes
 *      maintained by the application and responds with the GATT_ACCESS_RSP 
 *      message.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void HidBootHandleAccessRead(GATT_ACCESS_IND_T *p_ind)
{
    uint16 length = 0;
    uint8  *p_value = NULL, val[2];
    sys_status rc = sys_status_success;


    switch(p_ind->handle)
    {
        /* Client Characteristic Configuration Descriptor of
         * HID_PROP_BOOT_REPORT is being read by the remote host.
         */
        case HANDLE_HID_PROP_BOOT_RPT_CLIENT_CONFIG:
        {
            p_value = val;

            BufWriteUint16(&p_value, hid_boot_data.prop_boot_input_client_config);
            length = 2;

        }
        break;

        default:
        {
            rc = gatt_status_read_not_permitted;
        }
        break;

    }

    GattAccessRsp(p_ind->cid, p_ind->handle, rc,
                  length, val);

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      HidBootHandleAccessWrite
 *
 *  DESCRIPTION
 *      This function handles Write operation on HID BOOT service attributes 
 *      maintained by the application.and responds with the GATT_ACCESS_RSP 
 *      message.
 *
 *  RETURNS/MODIFIES
 *      Nothing
 *
 *----------------------------------------------------------------------------*/

extern void HidBootHandleAccessWrite(GATT_ACCESS_IND_T *p_ind)
{
    uint16 client_config;
    uint8 *p_value = p_ind->value;
    sys_status rc = sys_status_success;

    switch(p_ind->handle)
    {
        /* Client Characteristic Configuration Descriptor of
         * HID_PROP_BOOT_REPORT is being written by the remote host,
         * thereby enabling/disabling notifications on this handle.
         */
        case HANDLE_HID_PROP_BOOT_RPT_CLIENT_CONFIG:
        {

            client_config = BufReadUint16(&p_value);


            /* Client Configuration is bit field value so ideally bitwise 
             * comparison should be used but since the application supports only 
             * notifications, direct comparison is being used.
             */
            if((client_config == gatt_client_config_notification) ||
               (client_config == gatt_client_config_none))
            {

                hid_boot_data.prop_boot_input_client_config = client_config;
            }
            else
            {
                /* INDICATION or RESERVED */

                /* Return Error as only Notifications are supported for 
                   Keyboard application */

                rc = gatt_status_desc_improper_config;
            }
        }
        break;

        case HANDLE_HID_PROP_BOOT_REPORT:
        {
            if(p_ind->size_value == ATTR_LEN_HID_PROP_BOOT_OUTPUT_REPORT)
            {
                /* When the remote host writes to the input report
                 * characteristic, it has to be considered as the output report
                 * and update the keyboard LEDs accordingly.
                 */
                handleBootOutputReport(p_ind);
            }
            else
            {
                rc = gatt_status_invalid_length;
            }
        }
        break;

        default:
        {
            rc = gatt_status_write_not_permitted;
        }
        break;
    }

    /* Send ACCESS RESPONSE */
    GattAccessRsp(p_ind->cid, p_ind->handle, rc, 0, NULL);

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      HidBootGetNotificationStatus
 *
 *  DESCRIPTION
 *      This function returns whether notifications are enabled on Boot Input 
 *      reports
 *
 *  RETURNS/MODIFIES
 *      TRUE/FALSE: Noticiatiosn configured or not
 *
 *----------------------------------------------------------------------------*/

extern bool HidBootGetNotificationStatus(void)
{
    return (hid_boot_data.prop_boot_input_client_config & 
                                               gatt_client_config_notification);
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      HidSendBootInputReport
 *
 *  DESCRIPTION
 *      This function is used to notify key presses to connected host
 *
 *  RETURNS
 *      True if data is notified to connected host
 *
 *----------------------------------------------------------------------------*/

extern bool HidSendBootInputReport(uint16 ucid, uint8 report_id, uint8 *data)
{
    bool notification_sent = FALSE;
    
    switch(report_id)
    {
        case HID_INPUT_REPORT_ID:
        {
            GattCharValueNotification(ucid,
                              HANDLE_HID_PROP_BOOT_REPORT,
                              ATTR_LEN_HID_PROP_BOOT_REPORT,
                              data);
            
            notification_sent = TRUE;
        }
        break;

        case HID_CONSUMER_REPORT_ID:
        {
            GattCharValueNotification(ucid,
                              HANDLE_HID_CONSUMER_REPORT,
                              ATTR_LEN_HID_CONSUMER_REPORT,
                              data);

            notification_sent = TRUE;
        }
        break;

        default:
        break;
    }

    return notification_sent;
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      HidBootCheckHandleRange
 *
 *  DESCRIPTION
 *      This function is used to check if the handle belongs to the HID 
 *      BOOT service
 *
 *  RETURNS/MODIFIES
 *      Boolean - Indicating whether handle falls in range or not.
 *
 *----------------------------------------------------------------------------*/

extern bool HidBootCheckHandleRange(uint16 handle)
{
    return ((handle >= HANDLE_HID_PROP_BOOT_SERVICE) &&
            (handle <= HANDLE_HID_PROP_BOOT_SERVICE_END))
            ? TRUE : FALSE;
}

#endif /* __PROPRIETARY_HID_SUPPORT__ */
