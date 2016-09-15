/*******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 * FILE
 *      keyboard_gatt.c
 *
 * DESCRIPTION
 *      Implementation of the keyboard GATT-related routines
 *
 ******************************************************************************/

/*=============================================================================*
 *  SDK Header Files
 *============================================================================*/

#include <gatt.h>
#include <gatt_prim.h>
#include <att_prim.h>
#include <gap_app_if.h>
#include <ls_app_if.h>
#include <mem.h>
#include <security.h>
#include <time.h>
#include <timer.h>


/*=============================================================================*
 *  Local Header Files
 *============================================================================*/

#include "app_gatt.h"
#include "keyboard.h"
#include "app_gatt_db.h"
#include "keyboard_gatt.h"
#include "nvm_access.h"
#include "gap_service.h"
#include "hid_service.h"
#include "battery_service.h"
#include "scan_param_service.h"
#include "appearance.h"
#include "gap_uuids.h"
#include "gap_conn_params.h"
#include "hid_uuids.h"
#include "battery_uuids.h"
#include "scan_parameters_uuids.h"
#include "dev_info_uuids.h"
#include "user_config.h"
#include "dev_info_service.h"
#include "gatt_service.h"
#include "csr_ota_service.h"
#include "bond_mgmt_service.h"

#ifdef __PROPRIETARY_HID_SUPPORT__

#include "hid_boot_service.h"
#include "hid_boot_service_uuids.h"

#endif /* __PROPRIETARY_HID_SUPPORT__ */

/*=============================================================================*
 *  Private Definitions
 *============================================================================*/

/* This constant is used in the main server app to define some arrays so it
 * should always be large enough to hold the advertisement data.
 */
#define MAX_ADV_DATA_LEN                       (31)

/* Length of Tx Power prefixed with 'Tx Power' AD Type */
#define TX_POWER_VALUE_LENGTH                  (2)

/* Acceptable shortened device name length that can be sent in advData */
#define SHORTENED_DEV_NAME_LEN                 (8)


/*=============================================================================*
 *  Private Function Prototypes
 *============================================================================*/

static void addDeviceNameToAdvData(uint16 adv_data_len, uint16 scan_data_len);
static void gattSetAdvertParams(bool fast_connection,
                                                gap_mode_connect connect_mode);
static void gattAdvertTimerHandler(timer_id tid);
static void handleAccessRead(GATT_ACCESS_IND_T *p_ind);
static void handleAccessWrite(GATT_ACCESS_IND_T *p_ind);
static uint16 GetSupported16BitUUIDServiceList(uint8 *p_service_uuid_ad);

#ifdef __PROPRIETARY_HID_SUPPORT__

static uint16 GetSupported128BitUUIDServiceList(uint8 *p_service_uuid_ad_128);

#endif /* __PROPRIETARY_HID_SUPPORT__ */

/*=============================================================================*
 *  Private Function Implementations
 *============================================================================*/

/*-----------------------------------------------------------------------------*
 *  NAME
 *      addDeviceNameToAdvData
 *
 *  DESCRIPTION
 *      This function is used to add device name to adv or scan response data
 *      It follows below steps:
 *      a. Try to add complete device name to the advertisment packet
 *      b. Try to add complete device name to the scan response packet
 *      c. Try to add shortened device name to the advertisement packet
 *      d. Try to add shortened (max possible) device name to the scan response
 *      packet
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void addDeviceNameToAdvData(uint16 adv_data_len, uint16 scan_data_len)
{

    uint8 *p_device_name = NULL;
    uint16 device_name_adtype_len;

    /* Read device name along with AD Type and its length */
    p_device_name = GapGetNameAndLength(&device_name_adtype_len);

    /* Increment device_name_length by one to account for length field
     * which will be added by the GAP layer.
     */

    /* Check if Complete Device Name can fit in remaining advData space */
    if((device_name_adtype_len + 1) <= (MAX_ADV_DATA_LEN - adv_data_len))
    {
        /* Add complete device name to Advertisement data */
        p_device_name[0] = AD_TYPE_LOCAL_NAME_COMPLETE;

        /* Add Complete Device Name to Advertisement Data */
        if (LsStoreAdvScanData(device_name_adtype_len , p_device_name,
                      ad_src_advertise) != ls_err_none)
        {
            ReportPanic(app_panic_set_advert_data);
            return;
        }

    }
    /* Check if Complete Device Name can fit in Scan response message */
    else if((device_name_adtype_len + 1) <= (MAX_ADV_DATA_LEN - scan_data_len))
    {
        /* Add complete device name to scan response data */
        p_device_name[0] = AD_TYPE_LOCAL_NAME_COMPLETE;

        /* Add Complete Device Name to Scan Response Data */
        if (LsStoreAdvScanData(device_name_adtype_len , p_device_name,
                      ad_src_scan_rsp) != ls_err_none)
        {
            ReportPanic(app_panic_set_scan_rsp_data);
            return;
        }

    }
    /* Check if Shortened Device Name can fit in remaining advData space */
    else if((MAX_ADV_DATA_LEN - adv_data_len) >=
            (SHORTENED_DEV_NAME_LEN + 2)) /* Added 2 for Length and AD type
                                           * added by GAP layer
                                           */
    {
        /* Add shortened device name to Advertisement data */
        p_device_name[0] = AD_TYPE_LOCAL_NAME_SHORT;

       /* Add 1 octet to the length for AD type */
       if (LsStoreAdvScanData((SHORTENED_DEV_NAME_LEN + 1), p_device_name,
                      ad_src_advertise) != ls_err_none)
        {
            ReportPanic(app_panic_set_advert_data);
            return;
        }

    }
    else /* Add device name to remaining Scan reponse data space */
    {
        /* Add as much as can be stored in Scan Response data */
        p_device_name[0] = AD_TYPE_LOCAL_NAME_SHORT;

       if (LsStoreAdvScanData(MAX_ADV_DATA_LEN - scan_data_len,
                                    p_device_name,
                                    ad_src_scan_rsp) != ls_err_none)
        {
            ReportPanic(app_panic_set_scan_rsp_data);
            return;
        }

    }

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      gattSetAdvertParams
 *
 *  DESCRIPTION
 *      This function is used to set advertisement parameters for advertisements.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *  NOTES:
 *  Add device name in the end so that either full name or partial name is added
 *  to AdvData or scan data depending upon the space left in the two fields.
 *----------------------------------------------------------------------------*/

static void gattSetAdvertParams(bool fast_connection,
                                                gap_mode_connect connect_mode)
{
    uint8 advert_data[MAX_ADV_DATA_LEN];
    uint16 length;
    uint32 adv_interval_min = RP_ADVERTISING_INTERVAL_MIN;
    uint32 adv_interval_max = RP_ADVERTISING_INTERVAL_MAX;
    int8 tx_power_level; /* Unsigned value */
    gap_mode_discover discover_mode = gap_mode_discover_limited;
    TYPED_BD_ADDR_T temp_addr;

    /* A variable to keep track of the data added to AdvData. The limit is
     * MAX_ADV_DATA_LEN. GAP layer will add AD Flags to AdvData which is 3
     * bytes. Refer BT Spec 4.0, Vol 3, Part C, Sec 11.1.3.
     */
    uint16 length_added_to_adv = 3;
    uint16 length_added_to_scan = 0;

    /* Tx power level value prefixed with 'Tx Power' AD Type */
    uint8 device_tx_power[TX_POWER_VALUE_LENGTH] = {
                AD_TYPE_TX_POWER};

    uint8 device_appearance[ATTR_LEN_DEVICE_APPEARANCE + 1] = {
                AD_TYPE_APPEARANCE,
                WORD_LSB(APPEARANCE_KEYBOARD_VALUE),
                WORD_MSB(APPEARANCE_KEYBOARD_VALUE)
        };

/* If privacy is enabled, reconnection address will be used as Initiator's
 * address for directed advertisements. Otherwise, the bonded host address
 * shall be used as Initiator's address.
 */
#ifdef __GAP_PRIVACY_SUPPORT__

    temp_addr.type = ls_addr_type_random;
    MemCopy(&temp_addr.addr, GapGetReconnectionAddress(), sizeof(BD_ADDR_T));

#else /* __GAP_PRIVACY_SUPPORT__ */

    temp_addr.type = ls_addr_type_public;
    MemCopy(&temp_addr.addr, &g_kbd_data.bonded_bd_addr.addr, sizeof(BD_ADDR_T));

#endif /* ! __GAP_PRIVACY_SUPPORT__ */

    if(fast_connection)
    {
        adv_interval_min = FC_ADVERTISING_INTERVAL_MIN;
        adv_interval_max = FC_ADVERTISING_INTERVAL_MAX;
    }

    if(g_kbd_data.bonded)
    {
        /* As the device is bonded, we don't want it to be discoverable any
         * more
         */
        discover_mode = gap_mode_discover_no;
    }

    if((GapSetMode(gap_role_peripheral, discover_mode,
                   connect_mode, gap_mode_bond_yes,
                   gap_mode_security_unauthenticate) != ls_err_none) ||
       (connect_mode == gap_mode_connect_directed &&
                   GapSetAdvAddress(&temp_addr)
                   != ls_err_none) ||
        /* Advertisement interval will be ignored for directed advertisement */
       (GapSetAdvInterval(adv_interval_min, adv_interval_max)
                        != ls_err_none))
    {
        ReportPanic(app_panic_set_advert_params);
        return;
    }

    /* Reset existing advertising data as application is again going to add
     * data for undirected advertisements.
     */
    if(LsStoreAdvScanData(0, NULL, ad_src_advertise) != ls_err_none)
    {
        ReportPanic(app_panic_set_advert_data);
        return;
    }

    /* Reset existing scan response data as application is again going to add
     * data for undirected advertisements.
     */
    if(LsStoreAdvScanData(0, NULL, ad_src_scan_rsp) != ls_err_none)
    {
        ReportPanic(app_panic_set_scan_rsp_data);
        return;
    }

    /* Add to advData only if undirected advertisements are to be done. */
    if(connect_mode == gap_mode_connect_undirected)
    {

        /* Set up the advertising data. The GAP layer will automatically add the
         * AD Flags field so all we need to do here is add 16-bit supported
         * services UUID and Complete Local Name type. Applications are free to
         * add any other AD types based upon the profile requirements and
         * subject to the maximum AD size of 31 octets.
         */

#ifdef __PROPRIETARY_HID_SUPPORT__

        /* Add 128-bit UUID list of the services supported by the device */
        length = GetSupported128BitUUIDServiceList(advert_data);
        length_added_to_adv += length + 1;

        if (LsStoreAdvScanData(length, advert_data,
                 ad_src_advertise) != ls_err_none)
        {
            ReportPanic(app_panic_set_advert_data);
            return;
        }

#endif /* __PROPRIETARY_HID_SUPPORT__ */

        /* Add 16-bit UUID list of the services supported by the device */
        length = GetSupported16BitUUIDServiceList(advert_data);

        /* Before adding data to the ADV_IND, increment 'lengthAddedToAdv' to
         * keep track of the total number of bytes added to ADV_IND. At the end
         * while adding the device name to the ADV_IND, this can be used to
         * verify whether the complete name can fit into the AdvData adhering to
         * the limit of 31 octets. In addition to the above populated fields, a
         * 'length' field will also be added to AdvData by GAP layer. Refer
         * BT 4.0 spec, Vol 3, Part C, Figure 11.1.
         */
        length_added_to_adv += (length + 1);
        if (LsStoreAdvScanData(length, advert_data,
                 ad_src_advertise) != ls_err_none)
        {
            ReportPanic(app_panic_set_advert_data);
            return;
        }

        length_added_to_adv += (sizeof(device_appearance) + 1);
        /* Add device appearance as advertisement data */
        if (LsStoreAdvScanData(sizeof(device_appearance), device_appearance,
                              ad_src_advertise) != ls_err_none)
        {
            ReportPanic(app_panic_set_advert_data);
            return;
        }

        /* If 128-bit proprietary service UUID is added to the advData, there
         * will be no space left for any more data. So don't add the
         * TxPowerLevel to the advData.
         */

        /* Read tx power of the chip */
        if(LsReadTransmitPowerLevel(&tx_power_level) != ls_err_none)
        {
            /* Readin tx power failed */
            ReportPanic(app_panic_read_tx_pwr_level);
        }

        /* Add the read tx power level to g_device_tx_power
         * Tx power level value is of 1 byte
         */
        device_tx_power[TX_POWER_VALUE_LENGTH - 1] = (uint8 )tx_power_level;

        length_added_to_scan += TX_POWER_VALUE_LENGTH + 1;
        /* Add tx power value of device to the scan response data */
        if (LsStoreAdvScanData(TX_POWER_VALUE_LENGTH, device_tx_power,
                              ad_src_scan_rsp) != ls_err_none)
        {
            ReportPanic(app_panic_set_scan_rsp_data);
            return;
        }

        addDeviceNameToAdvData(length_added_to_adv, length_added_to_scan);
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      gattAdvertTimerHandler
 *
 *  DESCRIPTION
 *      This function is used to stop on-going advertisements at the expiry of
 *      SLOW_CONNECTION_ADVERT_TIMEOUT_VALUE or
 *      FAST_CONNECTION_ADVERT_TIMEOUT_VALUE timer.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void gattAdvertTimerHandler(timer_id tid)
{
    /* Based upon the timer id, stop on-going advertisements */
    if(g_kbd_data.app_tid == tid)
    {
        if(g_kbd_data.state == kbd_fast_advertising)
        {
            /* No advertisement timer for reduced power connections */
            g_kbd_data.advert_timer_value = SLOW_CONNECTION_ADVERT_TIMEOUT_VALUE;
        }

        /* Stop on-going advertisements */
        GattStopAdverts();

        g_kbd_data.app_tid = TIMER_INVALID;
    } /* Else ignore timer expiry, could be because of some race condition */
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleAccessRead
 *
 *  DESCRIPTION
 *      This function handles Read operation on attributes (as received in
 *      GATT_ACCESS_IND message) maintained by the application and respond
 *      with the GATT_ACCESS_RSP message.
 *
 *  RETURNS/MODIFIES
 *      Nothing
 *
 *----------------------------------------------------------------------------*/

static void handleAccessRead(GATT_ACCESS_IND_T *p_ind)
{

    /* Check all the services that support attribute 'Read' operation handled
     * by application
     */

    if(GapCheckHandleRange(p_ind->handle))
    {
        GapHandleAccessRead(p_ind);
    }
    else if(GattCheckHandleRange(p_ind->handle))
    {
        /* Attribute handle belongs to Gatt service */
        GattHandleAccessRead(p_ind);
    }
    else if(OtaCheckHandleRange(p_ind->handle))
    {
        /* Attribute handle belongs to OTA service */
        OtaHandleAccessRead(p_ind);
    }
    else if(HidCheckHandleRange(p_ind->handle))
    {
        HidHandleAccessRead(p_ind);
    }

#ifdef __PROPRIETARY_HID_SUPPORT__

    else if(HidBootCheckHandleRange(p_ind->handle))
    {
        HidBootHandleAccessRead(p_ind);
    }

#endif /* __PROPRIETARY_HID_SUPPORT__ */

    else if(BatteryCheckHandleRange(p_ind->handle))
    {
        BatteryHandleAccessRead(p_ind);
    }
    else if(ScanParamCheckHandleRange(p_ind->handle))
    {
        ScanParamHandleAccessRead(p_ind);
    }
    else if(DeviceInfoCheckHandleRange(p_ind->handle))
    {
        DeviceInfoHandleAccessRead(p_ind);
    }
    else if(BondMgmtCheckHandleRange(p_ind->handle))
    {
        BondMgmtHandleAccessRead(p_ind);
    }
    else
    {
        GattAccessRsp(p_ind->cid, p_ind->handle,
                      gatt_status_read_not_permitted,
                      0, NULL);
    }

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleAccessWrite
 *
 *  DESCRIPTION
 *      This function handles Write operation on attributes (as received in
 *      GATT_ACCESS_IND message) maintained by the application.
 *
 *  RETURNS/MODIFIES
 *      Nothing
 *
 *----------------------------------------------------------------------------*/

static void handleAccessWrite(GATT_ACCESS_IND_T *p_ind)
{

    /* Check all the services that support attribute 'Write' operation handled
     * by application
     */
    if(GapCheckHandleRange(p_ind->handle))
    {
        GapHandleAccessWrite(p_ind);
    }
    else if(GattCheckHandleRange(p_ind->handle))
    {
        /* Attribute handle belongs to Gatt service */
        GattHandleAccessWrite(p_ind);
    }
    else if(OtaCheckHandleRange(p_ind->handle))
    {
        /* Attribute handle belongs to OTA service */
        OtaHandleAccessWrite(p_ind);
    }
    else if(HidCheckHandleRange(p_ind->handle))
    {
        HidHandleAccessWrite(p_ind);
    }

#ifdef __PROPRIETARY_HID_SUPPORT__

    else if(HidBootCheckHandleRange(p_ind->handle))
    {
        HidBootHandleAccessWrite(p_ind);
    }

#endif /* __PROPRIETARY_HID_SUPPORT__ */

    else if(BatteryCheckHandleRange(p_ind->handle))
    {
        BatteryHandleAccessWrite(p_ind);
    }
    else if(ScanParamCheckHandleRange(p_ind->handle))
    {
        ScanParamHandleAccessWrite(p_ind);
    }
    else if(BondMgmtCheckHandleRange(p_ind->handle))
    {
        BondMgmtHandleAccessWrite(p_ind);
    }
    else
    {
        GattAccessRsp(p_ind->cid, p_ind->handle,
                      gatt_status_write_not_permitted,
                      0, NULL);
    }

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      GetSupported16BitUUIDServiceList
 *
 *  DESCRIPTION
 *      This function prepares the list of supported 16-bit service UUIDs to be
 *      added to Advertisement data. It also adds the relevant AD Type to the
 *      starting of AD array.
 *
 *  RETURNS/MODIFIES
 *      Return the size AD Service UUID data.
 *
 *----------------------------------------------------------------------------*/

static uint16 GetSupported16BitUUIDServiceList(uint8 *p_service_uuid_ad)
{
    uint8   i = 0;

    /* Add 16-bit UUID for Standard HID service  */
    p_service_uuid_ad[i++] = AD_TYPE_SERVICE_UUID_16BIT_LIST;

    p_service_uuid_ad[i++] = WORD_LSB(HID_SERVICE_UUID);
    p_service_uuid_ad[i++] = WORD_MSB(HID_SERVICE_UUID);

    return ((uint16)i);

}

#ifdef __PROPRIETARY_HID_SUPPORT__

/*-----------------------------------------------------------------------------*
 *  NAME
 *      GetSupported128BitUUIDServiceList
 *
 *  DESCRIPTION
 *      This function prepares the list of supported Sevice UUIDs to be added
 *      to Advertisement data. It also adds the relevant AD Type to the starting
 *      of AD array.
 *
 *  RETURNS/MODIFIES
 *      Return the size AD Service UUID data.
 *
 *----------------------------------------------------------------------------*/

static uint16 GetSupported128BitUUIDServiceList(uint8 *p_service_uuid_ad_128)
{

    uint16   i = 0;

    /* Add 16-bit UUID for Standard HID service  */
    p_service_uuid_ad_128[i++] = AD_TYPE_SERVICE_UUID_128BIT_LIST;

    p_service_uuid_ad_128[i++] = WORD_LSB(HID_SERV_UUID_7);
    p_service_uuid_ad_128[i++] = WORD_MSB(HID_SERV_UUID_7);
    p_service_uuid_ad_128[i++] = WORD_LSB(HID_SERV_UUID_6);
    p_service_uuid_ad_128[i++] = WORD_MSB(HID_SERV_UUID_6);
    p_service_uuid_ad_128[i++] = WORD_LSB(HID_SERV_UUID_5);
    p_service_uuid_ad_128[i++] = WORD_MSB(HID_SERV_UUID_5);
    p_service_uuid_ad_128[i++] = WORD_LSB(HID_SERV_UUID_4);
    p_service_uuid_ad_128[i++] = WORD_MSB(HID_SERV_UUID_4);
    p_service_uuid_ad_128[i++] = WORD_LSB(HID_SERV_UUID_3);
    p_service_uuid_ad_128[i++] = WORD_MSB(HID_SERV_UUID_3);
    p_service_uuid_ad_128[i++] = WORD_LSB(HID_SERV_UUID_2);
    p_service_uuid_ad_128[i++] = WORD_MSB(HID_SERV_UUID_2);
    p_service_uuid_ad_128[i++] = WORD_LSB(HID_SERV_UUID_1);
    p_service_uuid_ad_128[i++] = WORD_MSB(HID_SERV_UUID_1);
    p_service_uuid_ad_128[i++] = WORD_LSB(HID_SERV_UUID_0);
    p_service_uuid_ad_128[i++] = WORD_MSB(HID_SERV_UUID_0);
    return (i);
}

#endif /* __PROPRIETARY_HID_SUPPORT__ */

/*=============================================================================*
 *  Public Function Implementations
 *============================================================================*/

/*-----------------------------------------------------------------------------*
 *  NAME
 *      GattStartAdverts
 *
 *  DESCRIPTION
 *      This function is used to start undirected advertisements.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void GattStartAdverts(bool fast_connection, gap_mode_connect connect_mode)
{
    uint16 connect_flags = L2CAP_CONNECTION_SLAVE_UNDIRECTED |
                          L2CAP_OWN_ADDR_TYPE_PUBLIC;

    /* Set UCID to INVALID_UCID */
    g_kbd_data.st_ucid = GATT_INVALID_UCID;

    /* Set advertisement parameters */
    gattSetAdvertParams(fast_connection, connect_mode);

    if(g_kbd_data.bonded &&
        !IsAddressResolvableRandom(&g_kbd_data.bonded_bd_addr) &&
        !IsAddressNonResolvableRandom(&g_kbd_data.bonded_bd_addr))
    {

        if(connect_mode == gap_mode_connect_directed)
        {
            connect_flags = L2CAP_CONNECTION_SLAVE_DIRECTED |
                            L2CAP_OWN_ADDR_TYPE_PUBLIC |
                            L2CAP_PEER_ADDR_TYPE_PUBLIC;
        }
        else
        {
            /* When the device is bonded, set the advertising filter policy to
             * "process scan and connection requests only from devices in white
             * list"
             */
            connect_flags = L2CAP_CONNECTION_SLAVE_WHITELIST |
                               L2CAP_OWN_ADDR_TYPE_PUBLIC;
        }
    }

#ifdef __GAP_PRIVACY_SUPPORT__

    /*Check if privacy enabled */
    if(IsGapPeripheralPrivacyEnabled())
    {
        if(connect_mode == gap_mode_connect_directed)
        {
            /* Set advAddress to reconnection address. */
            GapSetRandomAddress(GapGetReconnectionAddress());

            connect_flags = L2CAP_CONNECTION_SLAVE_DIRECTED |
                            L2CAP_OWN_ADDR_TYPE_RANDOM |
                            L2CAP_PEER_ADDR_TYPE_RANDOM;
        }
        else
        {
            if(g_kbd_data.bonded_bd_addr.type == L2CA_RANDOM_ADDR_TYPE)
            {
                /* Change to random address */
                connect_flags = L2CAP_CONNECTION_SLAVE_UNDIRECTED |
                                 L2CAP_OWN_ADDR_TYPE_RANDOM |
                                 L2CAP_PEER_ADDR_TYPE_RANDOM;
            }
            else
            {
                            /* Change to random address */
                connect_flags = L2CAP_CONNECTION_SLAVE_UNDIRECTED |
                                 L2CAP_OWN_ADDR_TYPE_RANDOM |
                                 L2CAP_PEER_ADDR_TYPE_PUBLIC;
            }
        }

    }

#endif /* __GAP_PRIVACY_SUPPORT__ */

    /* Start GATT connection in Slave role */
    GattConnectReq(NULL, connect_flags);

     /* Start advertisement timer */
    if(g_kbd_data.advert_timer_value)
    {
        TimerDelete(g_kbd_data.app_tid);

        /* Start advertisement timer  */
        g_kbd_data.app_tid = TimerCreate(g_kbd_data.advert_timer_value, TRUE,
                                        gattAdvertTimerHandler);
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      GattStopAdverts
 *
 *  DESCRIPTION
 *      This function is used to stop on-going advertisements.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
extern void GattStopAdverts(void)
{
    GattCancelConnectReq();
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      GattHandleAccessInd
 *
 *  DESCRIPTION
 *      This function handles GATT_ACCESS_IND message for attributes maintained
 *      by the application.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void GattHandleAccessInd(GATT_ACCESS_IND_T *p_ind)
{
 if(p_ind->flags == (ATT_ACCESS_WRITE | ATT_ACCESS_PERMISSION |
                                                     ATT_ACCESS_WRITE_COMPLETE))
    {
        bool old_hid_notify_status;
       
        old_hid_notify_status = AppCheckNotificationStatus();
        
        handleAccessWrite(p_ind);
        
#ifdef PENDING_REPORT_WAIT      
        if(g_kbd_data.pending_report_tid != TIMER_INVALID)
        {
           /* Delete the pending timer */   
           TimerDelete(g_kbd_data.pending_report_tid);
           g_kbd_data.pending_report_tid = TIMER_INVALID;
           
           /* Recreate the timer */
           g_kbd_data.pending_report_tid =
                                  TimerCreate(PENDING_REPORT_WAIT_TIMEOUT,
                                              TRUE,
                                              HandlePendingReportsTimerExpiry);
        }   
#else /* PENDING_REPORT_WAIT */
        /* If notifications are just enabled and Key press is pending/buffered
         * notify all buffered key presses to the remote host
         */
         bool new_hid_notify_status;

        new_hid_notify_status = AppCheckNotificationStatus();

        if(!old_hid_notify_status && new_hid_notify_status
            && g_kbd_data.data_pending && g_kbd_data.encrypt_enabled
            && !g_kbd_data.data_tx_in_progress &&
            !g_kbd_data.waiting_for_fw_buffer)
        {
            /* Scanning already on-going, send key-press data stored in queue */
            SendKeyStrokesFromQueue();
        }
#endif 
    }
    else if(p_ind->flags == (ATT_ACCESS_READ | ATT_ACCESS_PERMISSION))
    {
        handleAccessRead(p_ind);
    }

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      GattTriggerFastAdverts
 *
 *  DESCRIPTION
 *      This function is used to start advertisements for fast connection
 *      parameters
 *
 *  RETURNS/MODIFIES
 *      Nothing
 *
 *----------------------------------------------------------------------------*/

extern void GattTriggerFastAdverts(void)
{

    g_kbd_data.advert_timer_value = FAST_CONNECTION_ADVERT_TIMEOUT_VALUE;

    /* Trigger fast conections */
    GattStartAdverts(TRUE, gap_mode_connect_undirected);
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      IsAddressResolvableRandom
 *
 *  DESCRIPTION
 *      This function checks if the address is resolvable random or not.
 *
 *  RETURNS/MODIFIES
 *      Boolean - True (Resolvable Random Address) /
 *                     False (Not a Resolvable Random Address)
 *
 *----------------------------------------------------------------------------*/

extern bool IsAddressResolvableRandom(TYPED_BD_ADDR_T *addr)
{
    if (addr->type != L2CA_RANDOM_ADDR_TYPE ||
        (addr->addr.nap & BD_ADDR_NAP_RANDOM_TYPE_MASK)
                                      != BD_ADDR_NAP_RANDOM_TYPE_RESOLVABLE)
    {
        /* This isn't a resolvable private address... */
        return FALSE;
    }
    return TRUE;
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      IsAddressNonResolvableRandom
 *
 *  DESCRIPTION
 *      This function checks if the address is non-resolvable random
 *      (reconnection address) or not.
 *
 *  RETURNS/MODIFIES
 *      Boolean - True (Non-resolvable Random Address) /
 *                     False (Not a Non-resolvable Random Address)
 *
 *----------------------------------------------------------------------------*/

extern bool IsAddressNonResolvableRandom(TYPED_BD_ADDR_T *addr)
{
    if (addr->type != L2CA_RANDOM_ADDR_TYPE ||
        (addr->addr.nap & BD_ADDR_NAP_RANDOM_TYPE_MASK)
                                      != BD_ADDR_NAP_RANDOM_TYPE_NONRESOLV)
    {
        /* This isn't a non-resolvable private address... */
        return FALSE;
    }
    return TRUE;
}
