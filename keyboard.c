/*******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 * FILE
 *    keyboard.c
 *
 *  DESCRIPTION
 *    This file defines a simple keyboard application.
 *
 ******************************************************************************/

/*=============================================================================*
 *  SDK Header Files
 *============================================================================*/

#include <main.h>
#include <gatt.h>
#include <security.h>
#include <ls_app_if.h>
#include <nvm.h>
#include <pio.h>
#include <mem.h>
#include <time.h>
#include <csr_ota.h>

/*=============================================================================*
 *  Local Header Files
 *============================================================================*/

#include "keyboard.h"
#include "keyboard_hw.h"
#include "app_gatt.h"
#include "gap_service.h"
#include "hid_service.h"
#include "battery_service.h"
#include "scan_param_service.h"
#include "app_gatt_db.h"
#include "keyboard_gatt.h"
#include "nvm_access.h"
#include "gatt_service.h"
#include "csr_ota_service.h"
#include "bond_mgmt_service.h"
#include "uartio.h"

#ifdef __PROPRIETARY_HID_SUPPORT__

#include "hid_boot_service.h"

#endif /* __PROPRIETARY_HID_SUPPORT__ */

/*=============================================================================*
 *  Private Definitions
 *============================================================================*/

/******** TIMERS ********/

/* Maximum number of timers */
#define MAX_APP_TIMERS                      (7)

/* Magic value to check the sanity of NVM region used by the application */
#define NVM_SANITY_MAGIC                    (0xAB06)

/* NVM offset for NVM sanity word */
#define NVM_OFFSET_SANITY_WORD              (0)

/* NVM offset for bonded flag */
#define NVM_OFFSET_BONDED_FLAG              (NVM_OFFSET_SANITY_WORD + 1)

/* NVM offset for bonded device bluetooth address */
#define NVM_OFFSET_BONDED_ADDR              (NVM_OFFSET_BONDED_FLAG + \
                                             sizeof(g_kbd_data.bonded))

/* NVM offset for diversifier */
#define NVM_OFFSET_SM_DIV                   (NVM_OFFSET_BONDED_ADDR + \
                                             sizeof(g_kbd_data.bonded_bd_addr))

/* NVM offset for IRK */
#define NVM_OFFSET_SM_IRK                   (NVM_OFFSET_SM_DIV + \
                                             sizeof(g_kbd_data.diversifier))

/* Number of words of NVM used by application. Memory used by supported
 * services is not taken into consideration here.
 */
#define N_APP_USED_NVM_WORDS                (NVM_OFFSET_SM_IRK + \
                                             MAX_WORDS_IRK)

/* Time after which a L2CAP connection parameter update request will be
 * re-sent upon failure of an earlier sent request.
 */
#define GAP_CONN_PARAM_TIMEOUT              (30 * SECOND)

/* TGAP(conn_pause_peripheral) defined in Core Specification Addendum 3 Revision
 * 2. A Peripheral device should not perform a Connection Parameter Update proc-
 * -edure within TGAP(conn_pause_peripheral) after establishing a connection.
 */
#define TGAP_CPP_PERIOD                     (5 * SECOND)

/* TGAP(conn_pause_central) defined in Core Specification Addendum 3 Revision 2.
 * After the Peripheral device has no further pending actions to perform and the
 * Central device has not initiated any other actions within TGAP(conn_pause_ce-
 * -ntral), then the Peripheral device may perform a Connection Parameter Update
 * procedure.
 */
#define TGAP_CPC_PERIOD                     (1 * SECOND)


/* Size of each buffer associated with the shared memory of PIO controller. */
#define KEYWORDS                            ((ROWS >> 1) + (ROWS % 2))

/* Maximum revoke count allowed before moving to idle state */
#define MAX_REVOKE_COUNT                    (2)

/* Usage ID for the key 'z' and 'Enter' in keyboard. Usage IDs for numbers from
 * 1 to 0 start after 'z'. Refer Table 12: Keyboard/Keypad Page,
 * HID USB usage table 1.11.
 */
#define USAGE_ID_KEY_Z                      (0x1D)
#define USAGE_ID_KEY_ENTER                  (0x28)

/* Maximum number of passkey digits that must be entered during pairing. */
#define PASSKEY_DIGITS_COUNT                (6)

/*=============================================================================*
 *  Private Data
 *============================================================================*/

/* Keyboard application data structure */
KBD_DATA_T g_kbd_data;

/* Declare space for application timers. */
static uint16 app_timers[SIZEOF_APP_TIMER * MAX_APP_TIMERS];

/* Variable that tracks how many times we have revoked upon a encryption
 * request from remote peer.
 */
uint16 g_Revoke_Count = 0;
/*=============================================================================*
 *  Private Function Prototypes
 *============================================================================*/

static void resetQueueData(void);
static void kbdDataInit(void);
static void readPersistentStore(void);

#ifndef __NO_IDLE_TIMEOUT__

static void kbdIdleTimerHandler(timer_id tid);

#endif /* __NO_IDLE_TIMEOUT__ */

static void resetIdleTimer(void);
static void requestConnParamUpdate(timer_id tid);
static void appInitStateExit(void);
static void appSetState(kbd_state new_state);
static void appStartAdvert(void);
static void handleSignalGattAddDBCfm(GATT_ADD_DB_CFM_T *p_event_data);
static void handleSignalGattConnectCfm(GATT_CONNECT_CFM_T* event_data);
static void handleSignalLmEvConnectionComplete(
                                     LM_EV_CONNECTION_COMPLETE_T *p_event_data);
static void handleSignalGattCancelConnectCfm(
        GATT_CANCEL_CONNECT_CFM_T *p_event_data);
static void handleSignalGattAccessInd(GATT_ACCESS_IND_T *event_data);
static void handleSignalLmEvDisconnectComplete(
                               HCI_EV_DATA_DISCONNECT_COMPLETE_T *p_event_data);
static void handleSignalLMEncryptionChange(LM_EVENT_T *event_data);
static void handleSignalSmKeysInd(SM_KEYS_IND_T *event_data);
static void handleSignalSmPairingAuthInd(SM_PAIRING_AUTH_IND_T *p_event_data);
static void handleSignalSmSimplePairingCompleteInd(
                                  SM_SIMPLE_PAIRING_COMPLETE_IND_T *event_data);
static void handleSignalSMpasskeyInputInd(void);
static void handleSignalLsConnParamUpdateCfm(
                                  LS_CONNECTION_PARAM_UPDATE_CFM_T *event_data);
static void handleSignalLmConnectionUpdate(
                                       LM_EV_CONNECTION_UPDATE_T* p_event_data);

static void handleSignalLsConnParamUpdateInd(
                                LS_CONNECTION_PARAM_UPDATE_IND_T *p_event_data);
static void handleSignalGattCharValNotCfm
                                (GATT_CHAR_VAL_IND_CFM_T *p_event_data);
static void handleSignalLsRadioEventInd(void);
static void handleSignalSmDivApproveInd(SM_DIV_APPROVE_IND_T *p_event_data);
static void handleBondingChanceTimerExpiry(timer_id tid);
static void handleGapCppTimerExpiry(timer_id tid);
#ifdef __GAP_PRIVACY_SUPPORT__
static void generatePrivateAddress(void);
static void handleRandomAddrTimeout(timer_id tid);
#endif /* __GAP_PRIVACY_SUPPORT__ */

/* Check if we are already bonded and notify the firmware whether an 
 * LTK is available for this connection.
 */
static void gapNotifyLtkAvailable(TYPED_BD_ADDR_T bd_addr);
/*=============================================================================*
 *  Private Function Implementations
 *============================================================================*/

/*-----------------------------------------------------------------------------*
 *  NAME
 *      resetQueueData
 *
 *  DESCRIPTION
 *      This function is used to discard all the data that is in the queue.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void resetQueueData(void)
{
    /* Initialise Key press pending flag */
    g_kbd_data.data_pending = FALSE;

    /* Initialise Circular Queue buffer */
    g_kbd_data.pending_key_strokes.start_idx = 0;
    g_kbd_data.pending_key_strokes.num = 0;

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      kbdDataInit
 *
 *  DESCRIPTION
 *      This function is used to initialise keyboard application data
 *      structure.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void kbdDataInit(void)
{
    /* Initialize the data structure variables used by the application to their
     * default values. Each service data has also to be initialized.
     */
    g_kbd_data.advert_timer_value = TIMER_INVALID;

    TimerDelete(g_kbd_data.app_tid);
    g_kbd_data.app_tid = TIMER_INVALID;

    TimerDelete(g_kbd_data.conn_param_update_tid);
    g_kbd_data.conn_param_update_tid= TIMER_INVALID;
    g_kbd_data.cpu_timer_value = 0;

    /* Delete the bonding chance timer */
    TimerDelete(g_kbd_data.bonding_reattempt_tid);
    g_kbd_data.bonding_reattempt_tid = TIMER_INVALID;

    g_kbd_data.st_ucid = GATT_INVALID_UCID;
    
    /* Reset the authentication failure flag */
    g_kbd_data.auth_failure = FALSE;

    g_kbd_data.encrypt_enabled = FALSE;

    g_kbd_data.pairing_button_pressed = FALSE;

    g_kbd_data.start_adverts = FALSE;

    g_kbd_data.data_tx_in_progress = FALSE;

    g_kbd_data.waiting_for_fw_buffer = FALSE;

    /* Reset the connection parameter variables. */
    g_kbd_data.conn_interval = 0;
    g_kbd_data.conn_latency = 0;
    g_kbd_data.conn_timeout = 0;

    HwDataInit();

    /* LEDs need to be turned off upon disconnection and power recycle. */
    UpdateKbLeds(0x00);

    /* Initialise GAP Data Structure */
    GapDataInit();

    /* HID Service data initialisation */
    HidDataInit();

#ifdef __PROPRIETARY_HID_SUPPORT__

    HidBootDataInit();

#endif /* __PROPRIETARY_HID_SUPPORT__ */

    /* Battery Service data initialisation */
    BatteryDataInit();

    /* Scan Parameter Service data initialisation */
    ScanParamDataInit();
    
    /* Bond Management Service data initialisation */
    BondMgmtDataInit();

    /* Initialise GATT Data structure */
    GattDataInit();

    /* OTA Service data initialisation */
    OtaDataInit();
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      readPersistentStore
 *
 *  DESCRIPTION
 *      This function is used to initialise and read NVM data
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void readPersistentStore(void)
{
    uint16 offset = N_APP_USED_NVM_WORDS;
    uint16 nvm_sanity = 0xffff;

    /* Read persistent storage to know if the device was last bonded
     * to another device
     */

    /* If the device was bonded, trigger advertisements for the bonded
     * host(using whitelist filter). If the device was not bonded, trigger
     * advertisements for any host to connect to the keyboard.
     */

    Nvm_Read(&nvm_sanity, sizeof(nvm_sanity), NVM_OFFSET_SANITY_WORD);

    if(nvm_sanity == NVM_SANITY_MAGIC)
    {
        /* Read Bonded Flag from NVM */
        Nvm_Read((uint16*)&g_kbd_data.bonded, sizeof(g_kbd_data.bonded),
                                                        NVM_OFFSET_BONDED_FLAG);

        if(g_kbd_data.bonded)
        {

            /* Bonded Host Typed BD Address will only be stored if bonded flag
             * is set to TRUE. Read last bonded device address.
             */
            Nvm_Read((uint16*)&g_kbd_data.bonded_bd_addr,
                       sizeof(TYPED_BD_ADDR_T), NVM_OFFSET_BONDED_ADDR);

            /* If the bonded device address is resovable then read the bonded
             * device's IRK
             */
            if(IsAddressResolvableRandom(&g_kbd_data.bonded_bd_addr))
            {
                Nvm_Read(g_kbd_data.central_device_irk.irk,
                                    MAX_WORDS_IRK, NVM_OFFSET_SM_IRK);
            }
        }

        else /* Case when we have only written NVM_SANITY_MAGIC to NVM but
              * didn't get bonded to any host in the last powered session
              */
        {
            g_kbd_data.bonded = FALSE;
        }

        /* Read the diversifier associated with the presently bonded/last bonded
         * device.
         */
        Nvm_Read(&g_kbd_data.diversifier, sizeof(g_kbd_data.diversifier),
                 NVM_OFFSET_SM_DIV);

        /* Read device name and length from NVM */
        GapReadDataFromNVM(&offset);

    }
    else /* NVM Sanity check failed means either the device is being brought up
          * for the first time or memory has got corrupted in which case discard
          * the data and start fresh.
          */
    {
        nvm_sanity = NVM_SANITY_MAGIC;

        /* Write NVM Sanity word to the NVM */
        Nvm_Write(&nvm_sanity, sizeof(nvm_sanity), NVM_OFFSET_SANITY_WORD);

        /* The device will not be bonded as it is coming up for the first time*/
        g_kbd_data.bonded = FALSE;

        /* Write bonded status to NVM */
        Nvm_Write((uint16*)&g_kbd_data.bonded, sizeof(g_kbd_data.bonded),
                                                        NVM_OFFSET_BONDED_FLAG);

        /* The device is not bonded so don't have BD Address to update */

        /* When the keyboard is booting up for the first time after flashing the
         * image to it, it will not have bonded to any device. So, no LTK will
         * be associated with it. So, set the diversifier to 0.
         */
        g_kbd_data.diversifier = 0;

        /* Write the same to NVM. */
        Nvm_Write(&g_kbd_data.diversifier, sizeof(g_kbd_data.diversifier),
                  NVM_OFFSET_SM_DIV);

        /* Write Gap data to NVM */
        GapInitWriteDataToNVM(&offset);

    }

    /* Read HID service data from NVM if the devices are bonded and
     * update the offset with the number of word of NVM required by
     * this service
     */
    HidReadDataFromNVM(g_kbd_data.bonded, &offset);

    /* Read Battery service data from NVM if the devices are bonded and
     * update the offset with the number of word of NVM required by
     * this service
     */
    BatteryReadDataFromNVM(g_kbd_data.bonded, &offset);

    /* Read Scan Parameter service data from NVM if the devices are bonded and
     * update the offset with the number of word of NVM required by
     * this service
     */
    ScanParamReadDataFromNVM(g_kbd_data.bonded, &offset);

    GattReadDataFromNVM(&offset);
}

#ifndef __NO_IDLE_TIMEOUT__

/*-----------------------------------------------------------------------------*
 *  NAME
 *      kbdIdleTimerHandler
 *
 *  DESCRIPTION
 *      This function is used to handle IDLE timer.expiry in connected states.
 *      At the expiry of this timer, application shall disconnect with the
 *      host and shall move to kbd_idle' state.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void kbdIdleTimerHandler(timer_id tid)
{
    /* Trigger Disconnect and move to CON_DISCONNECTING state */

    if(tid == g_kbd_data.app_tid)
    {
        g_kbd_data.app_tid = TIMER_INVALID;

        if(g_kbd_data.state == kbd_connected)
        {
            appSetState(kbd_disconnecting);

            /* Reset circular buffer queue and ignore any pending key strokes */
            resetQueueData();
        }
    } /* Else ignore the timer expiry, it may be due to a race condition */
}

#endif /* __NO_IDLE_TIMEOUT__ */
/*----------------------------------------------------------------------------*
 *  NAME
 *      gapNotifyLtkAvailable
 *
 *  DESCRIPTION
        Check if we are already bonded and notify the firmware whether an 
 *      LTK is available for this connection.
 *  RETURNS
 *      none.
 *
 *---------------------------------------------------------------------------*/
static void gapNotifyLtkAvailable(TYPED_BD_ADDR_T bd_addr)
{
    if(g_kbd_data.bonded)
    {
        /* Not a resolvable random, just compare the BD address */
        if(!IsAddressResolvableRandom(&g_kbd_data.bonded_bd_addr))
        {
             if(!MemCmp(&bd_addr.addr,
                        &g_kbd_data.bonded_bd_addr.addr,
                        sizeof(BD_ADDR_T)))
             {
                  GapLtkAvailable(&bd_addr,TRUE);  
             }
             else
             {
                  GapLtkAvailable(&bd_addr,FALSE);  
             }
        }
        else 
        {
          /* resolvable random, do a SMPrivacyMatchAddress */   
          if(SMPrivacyMatchAddress(&bd_addr,
                                   g_kbd_data.central_device_irk.irk,
                                   MAX_NUMBER_IRK_STORED,
                                   MAX_WORDS_IRK) >= 0)
          {
              GapLtkAvailable(&bd_addr,TRUE);  
          }
          else
          {
              GapLtkAvailable(&bd_addr,FALSE);  
          }
        }
    }
}
/*-----------------------------------------------------------------------------*
 *  NAME
 *      resetIdleTimer
 *
 *  DESCRIPTION
 *      This function deletes and re-creates the idle timer. The idle timer is
 *      reset after sending all the reports generated by user action.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void resetIdleTimer(void)
{
    /* Reset Idle timer */
    TimerDelete(g_kbd_data.app_tid);

    g_kbd_data.app_tid = TIMER_INVALID;

#ifndef __NO_IDLE_TIMEOUT__

    g_kbd_data.app_tid = TimerCreate(CONNECTED_IDLE_TIMEOUT_VALUE,
                                    TRUE, kbdIdleTimerHandler);

#endif /* __NO_IDLE_TIMEOUT__ */

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      requestConnParamUpdate
 *
 *  DESCRIPTION
 *      This function is used to send L2CAP_CONNECTION_PARAMETER_UPDATE_REQUEST
 *      to the remote device when an earlier sent request had failed.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void requestConnParamUpdate(timer_id tid)
{
    ble_con_params app_pref_conn_param;

    if(g_kbd_data.conn_param_update_tid == tid)
    {
        g_kbd_data.conn_param_update_tid= TIMER_INVALID;
        g_kbd_data.cpu_timer_value = 0;

        g_kbd_data.conn_param_update_cnt++;

        /* Decide which parameters values are to be requested.
         */
        if(g_kbd_data.conn_param_update_cnt <= CPU_SELF_PARAMS_MAX_ATTEMPTS)
        {
            app_pref_conn_param.con_max_interval =
                                        PREFERRED_MAX_CON_INTERVAL;
            app_pref_conn_param.con_min_interval =
                                        PREFERRED_MIN_CON_INTERVAL;
            app_pref_conn_param.con_slave_latency =
                                        PREFERRED_SLAVE_LATENCY;
            app_pref_conn_param.con_super_timeout =
                                        PREFERRED_SUPERVISION_TIMEOUT;
        }
        else
        {
            app_pref_conn_param.con_max_interval =
                                        APPLE_MAX_CON_INTERVAL;
            app_pref_conn_param.con_min_interval =
                                        APPLE_MIN_CON_INTERVAL;
            app_pref_conn_param.con_slave_latency =
                                        APPLE_SLAVE_LATENCY;
            app_pref_conn_param.con_super_timeout =
                                        APPLE_SUPERVISION_TIMEOUT;
        }

        /* Send a connection parameter update request only if the remote device
         * has not entered 'suspend' state.
         */
        if(!HidIsStateSuspended())
        {
            if(LsConnectionParamUpdateReq(&(g_kbd_data.con_bd_addr),
                                                    &app_pref_conn_param))
            {
                ReportPanic(app_panic_con_param_update);
            }

        }
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      appInitStateExit
 *
 *  DESCRIPTION
 *      This function is called upon exiting from kbd_init state. The
 *      application starts advertising after exiting this state.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void appInitStateExit(void)
{
    StartHardware();

    /* Application will start advertising upon exiting kbd_init state. So,
     * update the whitelist.
     */
    AppUpdateWhiteList();
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      appSetState
 *
 *  DESCRIPTION
 *      This function is used to set the state of the application.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void appSetState(kbd_state new_state)
{
    /* Check if the new state to be set is not the same as the present state
     * of the application.
     */
    uint16 old_state = g_kbd_data.state;

    if (old_state != new_state)
    {
        /* Handle exiting old state */
        switch (old_state)
        {
            case kbd_init:
                appInitStateExit();
            break;

            case kbd_fast_advertising:
                /* Nothing to do. */
            break;

            case kbd_slow_advertising:
                /* Nothing to do. */
            break;

            case kbd_connected:
                /* Nothing to do. */
            break;

            case kbd_disconnecting:
                /* Nothing to do. */
            break;

            case kbd_passkey_input:
                /* Nothing to do. */
            break;

            case kbd_idle:
                /* Nothing to do. */
            break;

            default:
                /* Nothing to do. */
            break;
        }

        /* Set new state */
        g_kbd_data.state = new_state;

        /* Handle entering new state */
        switch (new_state)
        {

            case kbd_direct_advert:
                /* Directed advertisement doesn't use any timer. Directed
                 * advertisements are done for 1.28 seconds always.
                 */
                g_kbd_data.advert_timer_value = TIMER_INVALID;
                GattStartAdverts(FALSE, gap_mode_connect_directed);
            break;

            case kbd_fast_advertising:
                GattTriggerFastAdverts();
                if(!g_kbd_data.bonded)
                {
                    EnablePairLED(TRUE);
                }
            break;

            case kbd_slow_advertising:
                GattStartAdverts(FALSE, gap_mode_connect_undirected);
            break;

            case kbd_connected:
                /* Common things to do upon entering kbd_connected state */

                /* Cancel Discoverable or Reconnection timer running in
                 * ADVERTISING state and start IDLE timer */
                resetIdleTimer();

                /* Disable the Pair LED, if enabled. */
                EnablePairLED(FALSE);
            break;

            case kbd_passkey_input:
                /* After entering passkey_input state, the user will start
                 * typing in the passkey.
                 */
            break;

            case kbd_disconnecting:
                 if(g_kbd_data.auth_failure)
                 {
                      /* Disconnect with an error - Authentication Failure */
                      GattDisconnectReasonReq(g_kbd_data.st_ucid, 
                            ls_err_authentication);
                 }
                 else
                 {
                      /* Disconnect with the default error */
                      GattDisconnectReq(g_kbd_data.st_ucid);
                 }
            break;

            case kbd_idle:
                /* Disable the Pair LED, if enabled */
                EnablePairLED(FALSE);
            break;

            default:
            break;
        }
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      appStartAdvert
 *
 *  DESCRIPTION
 *      This function is used to start directed advertisements if a valid
 *      reconnection address has been written by the remote device. Otherwise,
 *      it starts fast undirected advertisements.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void appStartAdvert(void)
{
    kbd_state advert_type;

    if(g_kbd_data.bonded &&
       !IsAddressResolvableRandom(&g_kbd_data.bonded_bd_addr) &&
       !IsAddressNonResolvableRandom(&g_kbd_data.bonded_bd_addr))
    {
        advert_type = kbd_direct_advert;

#ifdef __GAP_PRIVACY_SUPPORT__

        /* If re-connection address is not written, start fast undirected
         * advertisements
         */
        if(!GapIsReconnectionAddressValid())
        {
            advert_type = kbd_fast_advertising;
        }

#endif /* __GAP_PRIVACY_SUPPORT__ */
    }
    else /* Start with fast advertisements */
    {
        advert_type = kbd_fast_advertising;
    }

    appSetState(advert_type);
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalGattAddDBCfm
 *
 *  DESCRIPTION
 *      This function handles the signal GATT_ADD_DB_CFM
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void handleSignalGattAddDBCfm(GATT_ADD_DB_CFM_T *p_event_data)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        case kbd_init:
        {
            if(p_event_data->result == sys_status_success)
            {
                appStartAdvert();
            }

            else
            {
                /* Don't expect this to happen */
                ReportPanic(app_panic_db_registration);
            }
        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);
    }
}

/*---------------------------------------------------------------------------
 *
 *  NAME
 *      handleSignalLmEvConnectionComplete
 *
 *  DESCRIPTION
 *      This function handles the signal LM_EV_CONNECTION_COMPLETE.
 *
 *  RETURNS
 *      Nothing.
 *

*----------------------------------------------------------------------------*/
static void handleSignalLmEvConnectionComplete(
                                     LM_EV_CONNECTION_COMPLETE_T *p_event_data)
{
    /* Store the connection parameters. */
     g_kbd_data.conn_interval = p_event_data->data.conn_interval;
     g_kbd_data.conn_latency = p_event_data->data.conn_latency;
     g_kbd_data.conn_timeout = p_event_data->data.supervision_timeout;
}


/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleGapCppTimerExpiry
 *
 *  DESCRIPTION
 *      This function handles the expiry of TGAP(conn_pause_peripheral) timer.
 *      It starts the TGAP(conn_pause_central) timer, during which, if no activ-
 *      -ity is detected from the central device, a connection parameter update
 *      request is sent.
 *
 *  RETURNS
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleGapCppTimerExpiry(timer_id tid)
{
    if(g_kbd_data.conn_param_update_tid == tid)
    {
        g_kbd_data.conn_param_update_tid =
                           TimerCreate(TGAP_CPC_PERIOD, TRUE,
                                       requestConnParamUpdate);
        g_kbd_data.cpu_timer_value = TGAP_CPC_PERIOD;
    }
}

#ifdef __GAP_PRIVACY_SUPPORT__
/*-----------------------------------------------------------------------------*
 *  NAME
 *      generatePrivateAddress
 *
 *  DESCRIPTION
 *      This function generates the random Bluetooth Address
 *
 *  RETURNS/MODIFIES
 *      Nothing
 *
 *----------------------------------------------------------------------------*/
static void generatePrivateAddress(void)
{
    /* Generate Resolvable random address */
    SMPrivacyRegenerateAddress(NULL);

    g_kbd_data.random_addr_tid = TimerCreate(RANDOM_BLUETOOTH_ADDRESS_TIMEOUT,
                                             TRUE, handleRandomAddrTimeout);
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleRandomAddrTimeout
 *
 *  DESCRIPTION
 *      This function handles random Bluetooth Address timeout
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void handleRandomAddrTimeout(timer_id tid)
{
    if(g_kbd_data.random_addr_tid == tid)
    {
        g_kbd_data.random_addr_tid = TIMER_INVALID;
        switch(g_kbd_data.state)
        {
            case kbd_fast_advertising:
            case kbd_slow_advertising:
            case kbd_direct_advert:
            {
                g_kbd_data.random_addr_tid = 
                           TimerCreate((30*SECOND), TRUE,
                                       handleRandomAddrTimeout);
            }
            break;
            default:
                /* Generate private address */
                generatePrivateAddress();
            break;
        }
    }
}
#endif /* __GAP_PRIVACY_SUPPORT__ */


/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalGattConnectCfm
 *
 *  DESCRIPTION
 *      This function handles the signal GATT_CONNECT_CFM
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalGattConnectCfm(GATT_CONNECT_CFM_T* event_data)
{

#ifdef __FORCE_MITM__

    gap_mode_discover discover_mode = gap_mode_discover_limited;

#endif /* __FORCE_MITM__ */

    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        case kbd_fast_advertising:
        case kbd_slow_advertising:
        case kbd_direct_advert:
        {
            g_kbd_data.advert_timer_value = TIMER_INVALID;

            if(event_data->result == sys_status_success)
            {
                /* Store received UCID */
                g_kbd_data.st_ucid = event_data->cid;
                
                gapNotifyLtkAvailable(event_data->bd_addr);

                if(g_kbd_data.bonded &&
                    IsAddressResolvableRandom(&g_kbd_data.bonded_bd_addr) &&
                    (SMPrivacyMatchAddress(&event_data->bd_addr,
                                            g_kbd_data.central_device_irk.irk,
                                            MAX_NUMBER_IRK_STORED,
                                            MAX_WORDS_IRK) < 0))
                {
                    /* Application was bonded to a remote device with resolvable
                     * random address and application has failed to resolve the
                     * remote device address to which we just connected So disc-
                     * -onnect and start advertising again
                     */
                    g_kbd_data.auth_failure = TRUE;
                    /* Disconnect if we are connected */
                    appSetState(kbd_disconnecting);

                }

                else
                {
                    g_kbd_data.con_bd_addr = event_data->bd_addr;
                    
                    /* If we are bonded to this host, then it may be appropriate
                     * to indicate that the database is not now what it had
                     * previously.
                     */
                    if(g_kbd_data.bonded)
                    {
                        GattOnConnection(event_data->cid);
                    }
                    
#ifdef __FORCE_MITM__
                    if(g_kbd_data.bonded)
                    {
                        discover_mode = gap_mode_discover_no;
                    }
                    /* Change the authentication requirements to 'MITM
                     * required'.
                     */
                    GapSetMode(gap_role_peripheral, discover_mode,
                                gap_mode_connect_undirected, gap_mode_bond_yes,
                                gap_mode_security_authenticate);

#endif /* __FORCE_MITM__ */

#ifndef __PROPRIETARY_HID_SUPPORT__

                    /* Security supported by the remote HID host */

                    /* Request Security only if remote device address is not
                     *resolvable random
                     */
                    if(!IsAddressResolvableRandom(&g_kbd_data.con_bd_addr))
                    {

                        SMRequestSecurityLevel(&g_kbd_data.con_bd_addr);

                    }

#endif /* __PROPRIETARY_HID_SUPPORT__ */

                    appSetState(kbd_connected);


                    /* If the current connection parameters being used don't
                     * comply with the application's preferred connection
                     * parameters and the timer is not running and ,
                     * start timer to trigger Connection Parameter
                     * Update procedure
                     */
                    if((g_kbd_data.conn_param_update_tid == TIMER_INVALID) &&
                       (g_kbd_data.conn_interval < PREFERRED_MIN_CON_INTERVAL ||
                        g_kbd_data.conn_interval > PREFERRED_MAX_CON_INTERVAL
#if PREFERRED_SLAVE_LATENCY
                        ||  g_kbd_data.conn_latency < PREFERRED_SLAVE_LATENCY
#endif
                        )
                      )
                    {
                        /* Set the num of conn parameter update attempts to
                         * zero
                         */
                        g_kbd_data.conn_param_update_cnt = 0;

                        /* The application first starts a timer of
                         * TGAP_CPP_PERIOD. During this time, the application
                         * waits for the peer device to do the database
                         * discovery procedure. After expiry of this timer, the
                         * application starts one more timer of period
                         * TGAP_CPC_PERIOD. If the application receives any
                         * GATT_ACCESS_IND during this time, it assumes that
                         * the peer device is still doing device database
                         * discovery procedure or some other configuration and
                         * it should not update the parameters, so it restarts
                         * the TGAP_CPC_PERIOD timer. If this timer expires, the
                         * application assumes that database discovery procedure
                         * is complete and it initiates the connection parameter
                         * update procedure.
                         * Please note that this procedure requires all
                         * characteristic reads/writes to be made IRQ. If
                         * application wants firmware to reply to any of the
                         * request, it shall reply with
                         * "gatt_status_irq_proceed".
                         */
                        g_kbd_data.conn_param_update_tid =
                                        TimerCreate(TGAP_CPP_PERIOD,
                                                TRUE, handleGapCppTimerExpiry);
                        g_kbd_data.cpu_timer_value = TGAP_CPP_PERIOD;
                    } /* Else at the expiry of timer Connection parameter
                       * update procedure will get triggered
                       */
                }

            }
            else
            {

                if((event_data)->result ==
                    HCI_ERROR_DIRECTED_ADVERTISING_TIMEOUT)
                {
                    /* Case where bonding has been removed when directed
                     * advertisements were on-going
                     */
                    if(g_kbd_data.pairing_button_pressed)
                    {
                        /* Reset and clear the whitelist */
                        LsResetWhiteList();
                    }

                    /* Trigger undirected advertisements as directed
                     * advertisements have timed out.
                     */
                    appSetState(kbd_fast_advertising);
                }
                else
                {
                    ReportPanic(app_panic_connection_est);
                }
            }
        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalGattCancelConnectCfm
 *
 *  DESCRIPTION
 *      This function handles the signal GATT_CANCEL_CONNECT_CFM
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalGattCancelConnectCfm(GATT_CANCEL_CONNECT_CFM_T 
                                             *p_event_data)
{
    if(p_event_data->result != sys_status_success)
    {
        return;
    }
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        /* GATT_CANCEL_CONNECT_CFM is received when undirected advertisements
         * are stopped.
         */
        case kbd_fast_advertising:
        case kbd_slow_advertising:
        {
            if(g_kbd_data.pairing_button_pressed)
            {
                /* Reset and clear the whitelist */
                LsResetWhiteList();

                g_kbd_data.pairing_button_pressed = FALSE;

                if(g_kbd_data.state == kbd_fast_advertising)
                {
                     GattTriggerFastAdverts();
                     /* Enable LED(to blink) as pairing has been removed */
                     EnablePairLED(TRUE);
                }
                else
                {
                    appSetState(kbd_fast_advertising);
                }
            }
            else
            {
                if(g_kbd_data.state == kbd_fast_advertising)
                {
                    appSetState(kbd_slow_advertising);
                }
                else if(g_kbd_data.start_adverts)
                {
                    /* Reset the start_adverts flag and start directed/
                     * fast advertisements
                     */
                    g_kbd_data.start_adverts = FALSE;

                    appStartAdvert();
                }
                else
                {
                    /* Slow undirected advertisements have been stopped. Device
                     * shall move to IDLE state until next user activity or
                     * pending notification.
                     */
                    appSetState(kbd_idle);
                }
            }
        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalGattAccessInd
 *
 *  DESCRIPTION
 *      This function handles the signal GATT_ACCESS_IND
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalGattAccessInd(GATT_ACCESS_IND_T *event_data)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        case kbd_connected:
        case kbd_passkey_input:
        {
            /* GATT_ACCESS_IND indicates that the central device is still disco-
             * -vering services. So, restart the connection parameter update
             * timer
             */
            if(g_kbd_data.cpu_timer_value == TGAP_CPC_PERIOD &&
                g_kbd_data.conn_param_update_tid != TIMER_INVALID)
            {
                TimerDelete(g_kbd_data.conn_param_update_tid);
                g_kbd_data.conn_param_update_tid = TimerCreate(TGAP_CPC_PERIOD,
                                                 TRUE, requestConnParamUpdate);
            }

            GattHandleAccessInd(event_data);
        }
        break;

        default:
            /* Control should never reach here. */
        break;
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalLmEvDisconnectComplete
 *
 *  DESCRIPTION
 *      This function handles the signal LM_EV_DISCONNECT_COMPLETE.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalLmEvDisconnectComplete(
                                HCI_EV_DATA_DISCONNECT_COMPLETE_T *p_event_data)
{
    if(OtaResetRequired())
    {
        OtaReset();
        /* The OtaReset function does not return */
    }
    else
    {
        /* Handling signal as per current state */
        switch(g_kbd_data.state)
        {
            /* LM_EV_DISCONNECT_COMPLETE is received by the application when
             * 1. The remote side initiates a disconnection. The remote side can
             *    disconnect in any of the following states:
             *    a. kbd_connected.
             *    b. kbd_passkey_input.
             *    c. kbd_disconnecting.
             * 2. The keyboard application itself initiates a disconnection. The
             *    keyboard application will have moved to kbd_disconnecting
             *    state when it initiates a disconnection.
             * 3. There is a link loss.
             */
            case kbd_disconnecting:
            case kbd_connected:
            case kbd_passkey_input:
            {
                /* Delete the bonding if host has requested for bond deletion */
                HandleDeleteBonding();
                    
                /* Initialize the keyboard data structure. This will expect that
                 * the remote side re-enables encryption on the re-connection
                 * though it was a link loss.
                 */
                kbdDataInit();

                /* The keyboard needs to advertise after disconnection in the
                 * following cases.
                 * 1. If there was a link loss.
                 * 2. If the keyboard is not bonded to any host(A central device
                 *     may have connected, read the device name and
                 *     disconnected).
                 * 3. If a new data is there in the queue.
                 * Otherwise, it can move to kbd_idle state.
                 */
                if(p_event_data->reason == HCI_ERROR_CONN_TIMEOUT ||
                   !g_kbd_data.bonded || g_kbd_data.data_pending)
                {
                    /*  See whether we have reached the maximum revoke count. If
                     *  so move into idle state. This could be because the
                     *  remote peer is not initiating a fresh pairing and
                     *  attempting to connecting using old keys, which we dont
                     *  have any more.
                     */
                    if(!g_kbd_data.bonded)
                    {
                        if(g_Revoke_Count == MAX_REVOKE_COUNT)
                        {
                            appSetState(kbd_idle);

                            /* Reset the revoke count back to zero */
                            g_Revoke_Count = 0;
                            break;
                        }
                    }
                    appStartAdvert();
                }
                else
                {
                    appSetState(kbd_idle);
                }
            }
            break;

            default:
                /* Control should never reach here. */
                ReportPanic(app_panic_invalid_state);
            break;
        }
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalLMEncryptionChange
 *
 *  DESCRIPTION
 *      This function handles the signal LM_EV_ENCRYPTION_CHANGE
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalLMEncryptionChange(LM_EVENT_T *event_data)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        case kbd_connected:
        case kbd_passkey_input:
        {
            HCI_EV_DATA_ENCRYPTION_CHANGE_T *pEvDataEncryptChange =
                                        &event_data->enc_change.data;

            if(pEvDataEncryptChange->status == HCI_SUCCESS)
            {
                g_kbd_data.encrypt_enabled = pEvDataEncryptChange->enc_enable;

                /* Delete the bonding chance timer */
                TimerDelete(g_kbd_data.bonding_reattempt_tid);
                g_kbd_data.bonding_reattempt_tid = TIMER_INVALID;
            }

            if(g_kbd_data.encrypt_enabled)
            {
                /* Update battery status at every connection instance. It
                 * may not be worth updating timer more ofter, but again it
                 * will primarily depend upon application requirements
                 */
                SendBatteryLevelNotification();

#ifndef __NO_IDLE_TIMEOUT__

                ScanParamRefreshNotify(g_kbd_data.st_ucid);

#endif /* __NO_IDLE_TIMEOUT__ */

                /* If any keys are in the queue, send them */
                if(g_kbd_data.data_pending)
                {
#ifdef PENDING_REPORT_WAIT                  
                    /* Create a timer to wait for the host to configure
                     * for notifications.
                     */
                    g_kbd_data.pending_report_tid =
                                  TimerCreate(PENDING_REPORT_WAIT_TIMEOUT,
                                              TRUE,
                                              HandlePendingReportsTimerExpiry);
#else                   
                    /* Send the keys from queue only if a transmission is not
                     * already in progress.
                     */
                    if(AppCheckNotificationStatus()&&
                       !g_kbd_data.data_tx_in_progress &&
                       !g_kbd_data.waiting_for_fw_buffer)
                    {
                        SendKeyStrokesFromQueue();
                    }
#endif  
                }

            }
        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalSmKeysInd
 *
 *  DESCRIPTION
 *      This function handles the signal SM_KEYS_IND
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalSmKeysInd(SM_KEYS_IND_T *event_data)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        case kbd_connected:
        case kbd_passkey_input:
        {
            /* If keys are present, save them */
            if((event_data->keys)->keys_present & (1 << SM_KEY_TYPE_DIV))
            {
                /* Store the diversifier which will be used for accepting/
                 * rejecting the encryption requests.
                 */
                g_kbd_data.diversifier = (event_data->keys)->div;

                /* Write the new diversifier to NVM */
                Nvm_Write(&g_kbd_data.diversifier,
                          sizeof(g_kbd_data.diversifier), NVM_OFFSET_SM_DIV);
            }

            /* Store the IRK, it is used afterwards to validate the identity of
             * connected host
             */
            if((event_data->keys)->keys_present & (1 << SM_KEY_TYPE_ID))
            {
                MemCopy(g_kbd_data.central_device_irk.irk,
                                    (event_data->keys)->irk,
                                    MAX_WORDS_IRK);

                /* store IRK in NVM */
                Nvm_Write(g_kbd_data.central_device_irk.irk,
                                    MAX_WORDS_IRK, NVM_OFFSET_SM_IRK);
            }

        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalSmPairingAuthInd
 *
 *  DESCRIPTION
 *      This function handles the signal SM_PAIRING_AUTH_IND. This message will
 *      only be received when the peer device is initiating 'Just Works'
 8      pairing.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalSmPairingAuthInd(SM_PAIRING_AUTH_IND_T *p_event_data)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        case kbd_connected:
        {
            /* Authorise the pairing request if the Keyboard is NOT bonded */
            if(!g_kbd_data.bonded)
            {
                SMPairingAuthRsp(p_event_data->data, TRUE);
            }
            else /* Otherwise Reject the pairing request */
            {
                SMPairingAuthRsp(p_event_data->data, FALSE);
            }
        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);
        break;
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalSmSimplePairingCompleteInd
 *
 *  DESCRIPTION
 *      This function handles the signal SM_SIMPLE_PAIRING_COMPLETE_IND
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalSmSimplePairingCompleteInd(
                                 SM_SIMPLE_PAIRING_COMPLETE_IND_T *event_data)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        case kbd_connected:
        /* Case when MITM pairing fails, encryption change indication will not
         * have come to application. So, the application will still be in
         * kbd_passkey_input state.
         */
        case kbd_passkey_input:
        {
            if(event_data->status == sys_status_success)
            {
                if(g_kbd_data.bonding_reattempt_tid != TIMER_INVALID)
                {                   
                    /* Delete the bonding chance timer */
                    TimerDelete(g_kbd_data.bonding_reattempt_tid);
                    g_kbd_data.bonding_reattempt_tid = TIMER_INVALID;
                }

                g_kbd_data.bonded = TRUE;
                g_kbd_data.bonded_bd_addr = event_data->bd_addr;

                /* Store bonded host typed bd address to NVM */

                /* Write one word bonded flag */
                Nvm_Write((uint16*)&g_kbd_data.bonded,
                           sizeof(g_kbd_data.bonded), NVM_OFFSET_BONDED_FLAG);

                /* Write typed bd address of bonded host */
                Nvm_Write((uint16*)&g_kbd_data.bonded_bd_addr,
                sizeof(TYPED_BD_ADDR_T), NVM_OFFSET_BONDED_ADDR);

                /* White list is configured with the Bonded host address */
                AppUpdateWhiteList();

                /* Reset the revoke count */
                g_Revoke_Count = 0;

                /* Notify the Gatt service about the pairing */
                GattBondingNotify();
            }
            else
            {
                /* Pairing has failed.
                 * 1. If pairing has failed due to repeated attempts, the
                 *    application should immediately disconnect the link.
                 * 2. The application was bonded and pairing has failed.
                 *    Since the application was using whitelist, so the remote
                 *    device has same address as our bonded device address.
                 *    The remote connected device may be a genuine one but
                 *    instead of using old keys, wanted to use new keys. We
                 *    don't allow bonding again if we are already bonded but we
                 *    will give some time to the connected device to encrypt the
                 *    link using the old keys. if the remote device encrypts the
                 *    link in that time, it's good. Otherwise we will disconnect
                 *    the link.
                 */
                if(event_data->status == sm_status_repeated_attempts)
                {
                    appSetState(kbd_disconnecting);
                }
                else if(g_kbd_data.bonded)
                {
                    g_kbd_data.encrypt_enabled = FALSE;
                    g_kbd_data.bonding_reattempt_tid =
                                          TimerCreate(
                                               BONDING_CHANCE_TIMER,
                                               TRUE,
                                               handleBondingChanceTimerExpiry);
                }
                else
                {
                    /* If the application was not bonded and pairing has failed,
                     * the application will wait for PAIRING_WAIT_TIMER timer
                     * value for remote host to retry pairing.On the timer 
                     * expiry,the application will disconnect the link.
                     * Timer bonding_reattempt_tid has been reused in this case.
                     */
                    if(g_kbd_data.bonding_reattempt_tid == TIMER_INVALID)
                    {
                        g_kbd_data.bonding_reattempt_tid = TimerCreate(
                                PAIRING_WAIT_TIMER,
                                TRUE,
                                handleBondingChanceTimerExpiry);                   
                    }
                }
            }
        }
        break;

        default:
            /* SM_SIMPLE_PAIRING_COMPLETE_IND reaches the application after
             * LM_EV_DISCONNECT_COMPLETE when the remote end terminates
             * the connection without enabling encryption or completing pairing.
             * The application will be either in advertising or idle state in
             * this scenario. So, don't panic
             */
        break;

    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalSMpasskeyInputInd
 *
 *  DESCRIPTION
 *      This function handles the signal SM_PASSKEY_INPUT_IND
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalSMpasskeyInputInd(void)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        /* When the remote device is displaying a passkey for the first time,
         * application will be in 'connected' state. If an earlier passkey
         * entry failed, the remote device may re-try with a new passkey.
         */
        case kbd_connected:
        case kbd_passkey_input:
        {
            g_kbd_data.pass_key = 0;
            appSetState(kbd_passkey_input);
        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalLsConnParamUpdateCfm
 *
 *  DESCRIPTION
 *      This function handles the signal LS_CONNECTION_PARAM_UPDATE_CFM.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void handleSignalLsConnParamUpdateCfm(
                            LS_CONNECTION_PARAM_UPDATE_CFM_T *event_data)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        case kbd_connected:
        case kbd_passkey_input:
        {
            if ((event_data->status !=
                    ls_err_none) && (g_kbd_data.conn_param_update_cnt <
                    MAX_NUM_CONN_PARAM_UPDATE_REQS))
            {
                /* Delete timer if running */
                TimerDelete(g_kbd_data.conn_param_update_tid);

                g_kbd_data.conn_param_update_tid = TimerCreate(
                                                   GAP_CONN_PARAM_TIMEOUT, TRUE,
                                                     requestConnParamUpdate);
                g_kbd_data.cpu_timer_value = GAP_CONN_PARAM_TIMEOUT;
            }
        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);
    }
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalLmConnectionUpdate
 *
 *  DESCRIPTION
 *      This function handles the signal LM_EV_CONNECTION_UPDATE.
 *
 *  RETURNS
 *      Nothing.
 *
 *---------------------------------------------------------------------------*/
static void handleSignalLmConnectionUpdate(
                                       LM_EV_CONNECTION_UPDATE_T* p_event_data)
{
    switch(g_kbd_data.state)
    {
        case kbd_connected:
        case kbd_passkey_input:
        case kbd_disconnecting:
        {
            /* Store the new connection parameters. */
            g_kbd_data.conn_interval = p_event_data->data.conn_interval;
            g_kbd_data.conn_latency = p_event_data->data.conn_latency;
            g_kbd_data.conn_timeout = p_event_data->data.supervision_timeout;
        }
        break;

        default:
            /* Connection parameter update indication received in unexpected
             * application state.
             */
            ReportPanic(app_panic_invalid_state);
        break;
    }
}



/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalLsConnParamUpdateInd
 *
 *  DESCRIPTION
 *      This function handles the signal LS_CONNECTION_PARAM_UPDATE_IND
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalLsConnParamUpdateInd(
                                 LS_CONNECTION_PARAM_UPDATE_IND_T *p_event_data)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        case kbd_connected:
        case kbd_passkey_input:
        {
            /* Delete timer if running */
            TimerDelete(g_kbd_data.conn_param_update_tid);
            g_kbd_data.conn_param_update_tid = TIMER_INVALID;
            g_kbd_data.cpu_timer_value = 0;

            /* The application had already received the new connection
             * parameters while handling event LM_EV_CONNECTION_UPDATE.
             * Check if new parameters comply with application preferred
             * parameters. If not, application shall trigger Connection
             * parameter update procedure
             */

            if( g_kbd_data.conn_interval < PREFERRED_MIN_CON_INTERVAL ||
                g_kbd_data.conn_interval > PREFERRED_MAX_CON_INTERVAL
#if PREFERRED_SLAVE_LATENCY
               ||  g_kbd_data.conn_latency < PREFERRED_SLAVE_LATENCY
#endif
              )
            {
                /* Set the connection parameter update attempts counter to
                 * zero
                 */
                g_kbd_data.conn_param_update_cnt = 0;

                /* Start timer to trigger Connection Paramter Update procedure */
                g_kbd_data.conn_param_update_tid = TimerCreate(
                          GAP_CONN_PARAM_TIMEOUT, TRUE, requestConnParamUpdate);
                g_kbd_data.cpu_timer_value = GAP_CONN_PARAM_TIMEOUT;

            }
        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);

    }
 }

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalGattCharValNotCfm
 *
 *  DESCRIPTION
 *      This function handles the signal GATT_CHAR_VAL_NOT_CFM which is received
 *      as acknowledgement from the firmware that the data sent from application
 *      has been queued to be transmitted
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleSignalGattCharValNotCfm(GATT_CHAR_VAL_IND_CFM_T *p_event_data)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {
        case kbd_connected:
        case kbd_passkey_input:
        {
            /* Check whether the notification is for the data sent from the
             * queue. GATT_CHAR_VAL_NOT_CFM comes for all the notification
             * requests sent from the application, in this case for battery
             * level and scan refresh notifications also
             */
            if(p_event_data->handle == HANDLE_HID_CONSUMER_REPORT ||
               p_event_data->handle == HANDLE_HID_INPUT_REPORT ||
               p_event_data->handle == HANDLE_HID_BOOT_INPUT_REPORT

#ifdef __PROPRIETARY_HID_SUPPORT__

               || p_event_data->handle == HANDLE_HID_PROP_BOOT_REPORT

#endif /* __PROPRIETARY_HID_SUPPORT__ */

            )
            {
                /* Firmware has completed handling our request for queueing the
                 * data to be sent to the remote device. If the data was
                 * successfully queued, we can send the remaining data in the
                 * application queue. If not, application needs to retry sending
                 * the same data again.
                 */
                g_kbd_data.data_tx_in_progress = FALSE;

                if(p_event_data->result == sys_status_success)
                {
                    /* Update the start index of the queue. */
                    g_kbd_data.pending_key_strokes.start_idx =
                    (g_kbd_data.pending_key_strokes.start_idx + 1) %
                    MAX_PENDING_KEY_STROKES;

                    /* Decrement the number of pending key strokes. If more
                     * key strokes are in queue, send them
                     */
                    if(g_kbd_data.pending_key_strokes.num)
                    {
                        -- g_kbd_data.pending_key_strokes.num;
                    }

                    /* If all the data from the application queue is emptied,
                     * reset the data_pending flag.
                     */
                    if(! g_kbd_data.pending_key_strokes.num)
                    {
                        /* All the reports in the queue have been sent. Reset the
                         * idle timer and set the data pending flag to false
                         */
                        resetIdleTimer();
                        g_kbd_data.data_pending = FALSE;
                    }

                    /* If more data is there in the queue, send it. */
                    if(g_kbd_data.data_pending)
                    {
                        if(g_kbd_data.encrypt_enabled &&
                                                   AppCheckNotificationStatus())
                        {
                            SendKeyStrokesFromQueue();
                        }
                    }
                }

                else /* The firmware has not added the data in it's queue to be
                      * sent to the remote device. Mostly it'll have returned
                      * gatt_status_busy. Wait for the firmware to empty it's
                      * buffers by sending data to the remote device. This is
                      * indicated by firmware to application by radio event
                      * tx_data
                      */
                {
                    g_kbd_data.waiting_for_fw_buffer = TRUE;

                    /* Enable the application to receive confirmation that the
                     * data has been transmitted to the remote device by
                     * configuring notifications on tx_data radio events
                     */
                    LsRadioEventNotification(g_kbd_data.st_ucid,
                                                           radio_event_tx_data);
                }
            }
        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalLsRadioEventInd
 *
 *  DESCRIPTION
 *      This function handles the signal LS_RADIO_EVENT_IND
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void handleSignalLsRadioEventInd(void)
{
    /* Radio events notification would have been enabled after the firmware
     * buffers are full and hence incapable of receiving any more notification
     * requests from the application. Disable them now
     */
    LsRadioEventNotification(g_kbd_data.st_ucid, radio_event_none);

    if(g_kbd_data.waiting_for_fw_buffer)
    {
        g_kbd_data.waiting_for_fw_buffer = FALSE;
        if(g_kbd_data.data_pending)
        {
            /* Not necessary to check the data_tx_in_progress flag as this
             * shouldn't be set when we are waiting for firmware to free it's
             * buffers.
             */
            if(g_kbd_data.encrypt_enabled && AppCheckNotificationStatus())
            {
                SendKeyStrokesFromQueue();
            }
        }
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      handleSignalSmDivApproveInd
 *
 *  DESCRIPTION
 *      This function handles the signal SM_DIV_APPROVE_IND.
 *
 *  RETURNS
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

static void handleSignalSmDivApproveInd(SM_DIV_APPROVE_IND_T *p_event_data)
{
    /* Handling signal as per current state */
    switch(g_kbd_data.state)
    {

        /* Request for approval from application comes only when pairing is not
         * in progress. So, this event can't come in kbd_passkey_input state
         */
        case kbd_connected:
        {
            sm_div_verdict approve_div = SM_DIV_REVOKED;

            /* Check whether the application is still bonded(bonded flag gets
             * reset upon 'connect' button press by the user). Then check whether
             * the diversifier is the same as the one stored by the application
             */
            if(g_kbd_data.bonded)
            {
                if(g_kbd_data.diversifier == p_event_data->div)
                {
                    approve_div = SM_DIV_APPROVED;
                    /* Reset the revoke count */
                    g_Revoke_Count = 0;
                }
            }

            if(approve_div == SM_DIV_REVOKED)
            {
                /* Increment the revoke count */
                g_Revoke_Count++;
            }

#ifdef __PROPRIETARY_HID_SUPPORT__

            /* Proprietary HID host device doesn't handle encryption rejection.
             * It always enables notifications after connection. So, always
             * accept the encryption request
             */
            approve_div = SM_DIV_APPROVED;

#endif /* __PROPRIETARY_HID_SUPPORT__ */

            SMDivApproval(p_event_data->cid, approve_div);
        }
        break;

        default:
            ReportPanic(app_panic_invalid_state);
    }
}


/*----------------------------------------------------------------------------*
 *  NAME
 *      handleBondingChanceTimerExpiry
 *
 *  DESCRIPTION
 *      This function is handle the expiry of bonding chance timer.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
static void handleBondingChanceTimerExpiry(timer_id tid)
{
    if(g_kbd_data.bonding_reattempt_tid== tid)
    {
        g_kbd_data.bonding_reattempt_tid= TIMER_INVALID;
        /* The bonding chance timer has expired. This means the remote device
         * has not encrypted the link using old keys. Disconnect the link.
         */
        appSetState(kbd_disconnecting);
    }/* Else it may be due to some race condition. Ignore it. */
}

/*=============================================================================*
 *  Public Function Implementations
 *============================================================================*/

/*----------------------------------------------------------------------------*
 *  NAME
 *      ReportPanic
 *
 *  DESCRIPTION
 *      This function raises the panic.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *---------------------------------------------------------------------------*/
extern void ReportPanic(app_panic_code panic_code)
{
    /* If we want any debug prints, we can put them here */
    Panic(panic_code);
}


#ifdef NVM_TYPE_FLASH
/*----------------------------------------------------------------------------*
 *  NAME
 *      WriteApplicationAndServiceDataToNVM
 *
 *  DESCRIPTION
 *      This function writes the application data to NVM. This function should
 *      be called on getting nvm_status_needs_erase
 *
 *  RETURNS
 *      Nothing.
 *
 *---------------------------------------------------------------------------*/
extern void WriteApplicationAndServiceDataToNVM(void)
{
    uint16 nvm_sanity = 0xffff;
    nvm_sanity = NVM_SANITY_MAGIC;

    /* Write NVM sanity word to the NVM */
    Nvm_Write(&nvm_sanity, sizeof(nvm_sanity), NVM_OFFSET_SANITY_WORD);

    /* Write Bonded flag to NVM. */
    Nvm_Write((uint16*)&g_kbd_data.bonded,
               sizeof(g_kbd_data.bonded),
               NVM_OFFSET_BONDED_FLAG);


    /* Write Bonded address to NVM. */
    Nvm_Write((uint16*)&g_kbd_data.bonded_bd_addr,
              sizeof(TYPED_BD_ADDR_T),
              NVM_OFFSET_BONDED_ADDR);

    /* Write the diversifier to NVM */
    Nvm_Write(&g_kbd_data.diversifier,
                sizeof(g_kbd_data.diversifier),
                NVM_OFFSET_SM_DIV);

    /* Store the IRK to NVM */
    Nvm_Write(g_kbd_data.central_device_irk.irk,
                MAX_WORDS_IRK,
                NVM_OFFSET_SM_IRK);

    /* Write GAP service data into NVM */
    WriteGapServiceDataInNVM();
    
    /* Write Gatt service data into NVM. */
    WriteGattServiceDataInNvm();   

    /* Write HID service data into NVM */
    WriteHIDServiceDataInNvm();

    /* Write Scan Parameter service data into NVM */
    WriteScanParamServiceDataInNvm();

    /* Write Battery service data into NVM */
    WriteBatteryServiceDataInNvm();
}
#endif /* NVM_TYPE_FLASH */


/*-----------------------------------------------------------------------------*
 *  NAME
 *      AppIsDeviceBonded
 *
 *  DESCRIPTION
 *      This function returns the status wheather the connected device is
 *      bonded or not.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern bool AppIsDeviceBonded(void)
{
    return (g_kbd_data.bonded);
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      AppGetConnectionCid
 *
 *  DESCRIPTION
 *      This function returns the connection identifier for the connection
 *
 *  RETURNS/MODIFIES
 *      Connection identifier.
 *
 
*----------------------------------------------------------------------------*/
extern uint16 AppGetConnectionCid(void)
{
    return g_kbd_data.st_ucid;
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      AppUpdateWhiteList
 *
 *  DESCRIPTION
 *      This function updates the whitelist with bonded device address if
 *      it's not private, and also reconnection address when it has been written
 *      by the remote device.
 *
 *----------------------------------------------------------------------------*/

extern void AppUpdateWhiteList(void)
{
    LsResetWhiteList();

    if(g_kbd_data.bonded &&
            (!IsAddressResolvableRandom(&g_kbd_data.bonded_bd_addr)) &&
            (!IsAddressNonResolvableRandom(&g_kbd_data.bonded_bd_addr)))
    {
        /* If the device is bonded and bonded device address is not private
         * (resolvable random or non-resolvable random), configure White list
         * with the Bonded host address
         */

        if(LsAddWhiteListDevice(&g_kbd_data.bonded_bd_addr) != ls_err_none)
        {
            ReportPanic(app_panic_add_whitelist);
        }
    }

#ifdef __GAP_PRIVACY_SUPPORT__

    if(GapIsReconnectionAddressValid())
    {
        TYPED_BD_ADDR_T temp_addr;

        temp_addr.type = ls_addr_type_random;
        MemCopy(&temp_addr.addr, GapGetReconnectionAddress(), sizeof(BD_ADDR_T));
        if(LsAddWhiteListDevice(&temp_addr) != ls_err_none)
        {
            ReportPanic(app_panic_add_whitelist);
        }
    }

#endif /* __GAP_PRIVACY_SUPPORT__ */

}


/*-----------------------------------------------------------------------------*
 *  NAME
 *      ProcessReport
 *
 *  DESCRIPTION
 *      This function processes the raw reports received from PIO controller
 *
 *  RETURNS
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void ProcessReport(uint8* raw_report)
{
    if(g_kbd_data.state == kbd_passkey_input)
    {
        /* Normally while entering a passkey, the user will enter one
         * key after another. So, it is safe to assume that only one key
         * will be pressed at a time. When only one key is pressed, its
         * value will be the third byte of the report which is the first
         * data byte.
         */
        uint8 key_pressed = raw_report[2];

        if(key_pressed)
        {
            static int passkey_count = 0;
            if((key_pressed > USAGE_ID_KEY_Z) &&
                                 (key_pressed < USAGE_ID_KEY_ENTER))
            {
                /* Each time a new key press is detected during passkey
                 * entry, the passkey value needs to be updated. The
                 * earlier entered digit is multiplied by 10 and the
                 * new key is added.
                 */
                if(passkey_count < PASSKEY_DIGITS_COUNT)
                {
                    g_kbd_data.pass_key = g_kbd_data.pass_key * 10 +
                                     (key_pressed - USAGE_ID_KEY_Z)%10;
                }
                passkey_count++;
               
            }
            else if(key_pressed == USAGE_ID_KEY_ENTER)
            {
                /* Passkey will be non-zero if any number keys are
                 * pressed. Send the passkey response if the passkey
                 * is non-zero. Otherwise, send a negative passkey
                 * response(When enter is pressed without pressing
                 * any key or only alphabetic keys are pressed
                 * followed by enter key press).
                 */
                
                if(g_kbd_data.pass_key && (passkey_count==PASSKEY_DIGITS_COUNT))
                {
                    SMPasskeyInput(&g_kbd_data.con_bd_addr,
                                                 &g_kbd_data.pass_key);
                }
                else
                {
                    SMPasskeyInputNeg(&g_kbd_data.con_bd_addr);
                }
                
                /* Reset the passkey_count */
                passkey_count = 0;

                /* Now that the passkey is sent, change the application
                 * state back to 'connected'
                 */
                appSetState(kbd_connected);
            }
        }
    }

    else
    {
        if(FormulateReportsFromRaw(raw_report))
        {

            if(g_kbd_data.state == kbd_connected &&
                                             g_kbd_data.encrypt_enabled)
            {
                /* If the data transmission is not already in progress,
                 * then send the stored keys from queue
                 */
                if(AppCheckNotificationStatus()&&
                   !g_kbd_data.data_tx_in_progress &&
                   !g_kbd_data.waiting_for_fw_buffer)
                {
                    SendKeyStrokesFromQueue();
                }
            }
             /* If the keyboard is slow advertising, start fast
             * advertisements
             */
            else if(g_kbd_data.state == kbd_slow_advertising)
            {
                g_kbd_data.start_adverts = TRUE;

                /* Delete the advertisement timer */
                TimerDelete(g_kbd_data.app_tid);
                g_kbd_data.app_tid = TIMER_INVALID;
                g_kbd_data.advert_timer_value = TIMER_INVALID;

                GattStopAdverts();
            }

            /* If the keyboard is already fast advertising, we need
             * not do anything. If the keyboard state is kbd_init,
             * it will start advertising. If the keyboard is in
             * kbd_disconnecting state, it will start advertising
             * after disconnection is complete and it finds that
             * data is pending in the queue.
             */
            else if(g_kbd_data.state == kbd_idle)
            {
                appStartAdvert();
            }
        }
    }
}


/*-----------------------------------------------------------------------------*
 *  NAME
 *      AddKeyStrokeToQueue
 *
 *  DESCRIPTION
 *      This function is used to add key strokes to circular queue maintained
 *      by application. The key strokes will get notified to Host machine once
 *      notifications are enabled by the remote client.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void AddKeyStrokeToQueue(uint8 report_id, uint8 *report,
                                                            uint8 report_length)
{
    uint8 *p_temp_input_report = NULL;
    uint8 add_idx;

    /* Add new key stroke to the end of circular queue. If Max circular queue
     * length has reached the oldest key stroke will get overwritten
     */
    add_idx = (g_kbd_data.pending_key_strokes.start_idx +
    g_kbd_data.pending_key_strokes.num)% MAX_PENDING_KEY_STROKES;

    SET_CQUEUE_INPUT_REPORT_ID(add_idx, report_id);

    p_temp_input_report = GET_CQUEUE_INPUT_REPORT_REF(add_idx);

    MemCopy(p_temp_input_report, report, report_length);

    if(g_kbd_data.pending_key_strokes.num < MAX_PENDING_KEY_STROKES)
        ++ g_kbd_data.pending_key_strokes.num;
    else /* Oldest key stroke overwritten, move the index */
        g_kbd_data.pending_key_strokes.start_idx = (add_idx + 1)
                                                  % MAX_PENDING_KEY_STROKES;

    g_kbd_data.data_pending = TRUE;
}


/*-----------------------------------------------------------------------------*
 *  NAME
 *      HandlePairingButtonPress
 *
 *  DESCRIPTION
 *      This function is called when the pairing removal button is pressed down
 *      for a period specified by PAIRING_REMOVAL_TIMEOUT
 *
 *  RETURNS
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
extern void HandlePairingButtonPress(timer_id tid)
{
    if(tid == pairing_removal_tid)
    {
        /* The firmware will have deleted the pairing removal timer. Set the
         * value of the timer maintained by application to TIMER_INVALID
         */
        pairing_removal_tid = TIMER_INVALID;

        /* Handle pairing button press only if the last pairing button press
         * handling is complete(app_data.pairing_button_pressed will be set
         * to false in such a case)
         */
        if(!g_kbd_data.pairing_button_pressed)
        {
            /* Pairing button pressed for PAIRING_REMOVAL_TIMEOUT period -
             * Remove bonding information
             */
            g_kbd_data.bonded = FALSE;

            /* Write bonded status to NVM */
            Nvm_Write((uint16*)&g_kbd_data.bonded, sizeof(g_kbd_data.bonded),
                                                        NVM_OFFSET_BONDED_FLAG);

            /* Reset circular buffer queue and ignore any pending key strokes */
            resetQueueData();

            /* Disconnect if we are connected */
            if((g_kbd_data.state == kbd_connected) || (g_kbd_data.state ==
                kbd_passkey_input))
            {
                /* Reset and clear the whitelist */
                LsResetWhiteList();

                appSetState(kbd_disconnecting);
            }
            else
            {
                /* Initialise application and services data structures */
                kbdDataInit();

                if(IS_KBD_ADVERTISING())
                {
                    g_kbd_data.pairing_button_pressed = TRUE;

                    if(g_kbd_data.state != kbd_direct_advert)
                    {
                        /* Delete the advertising timer as in race conditions,
                         * it may expire before GATT_CANCEL_CONNECT_CFM reaches
                         * the application. If this happens, we end up calling
                         * GattStopAdverts() again
                         */

                        TimerDelete(g_kbd_data.app_tid);
                        g_kbd_data.app_tid = TIMER_INVALID;
                        g_kbd_data.advert_timer_value = TIMER_INVALID;

                        /* Stop advertisements as it may be making use of white
                         * list
                         */
                        GattStopAdverts();
                    }
                }

                /* For kbd_idle state */
                else if(g_kbd_data.state == kbd_idle)
                {
                    /* Reset and clear the whitelist */
                    LsResetWhiteList();

                    g_kbd_data.pairing_button_pressed = FALSE;

                    /* Start undirected advertisements */
                    appSetState(kbd_fast_advertising);

                } /* Else the keyboard will be in kbd_init state. It will
                   * anyways start advertising
                   */
            }
        }
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      HandleDeleteBonding
 *
 *  DESCRIPTION
 *      This function is called when the bonding needs to be deleted
 *
 *  RETURNS
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void HandleDeleteBonding(void)
{
    if(BondMgmtGetBondDeletionFlag() == TRUE)
    {
        if((g_kbd_data.state == kbd_connected) || (g_kbd_data.state ==
           kbd_passkey_input))
        {
            /* Disconnect the link */
            appSetState(kbd_disconnecting);        
            
            /* Reset circular buffer queue and ignore any pending key
             * strokes
             */
            resetQueueData();
        }
        else if(g_kbd_data.state == kbd_disconnecting)
        {
            /* Update the bonding status */
            g_kbd_data.bonded = FALSE;
            
            /* Update the bonding status in NVM */
            Nvm_Write((uint16*)&g_kbd_data.bonded, sizeof(g_kbd_data.bonded),
                      NVM_OFFSET_BONDED_FLAG);
            
            GapLtkAvailable(&g_kbd_data.bonded_bd_addr,FALSE);
            
            /* Reset and clear the whitelist */
            LsResetWhiteList();
            
            /* Reset the bond deletion flag */
            BondMgmtSetBondDeletionFlag(FALSE);
        }
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      SendKeyStrokesFromQueue
 *
 *  DESCRIPTION
 *      This function is used to send key strokes in circular queue
 *      maintained by application.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void SendKeyStrokesFromQueue(void)
{
    uint8 start_idx = g_kbd_data.pending_key_strokes.start_idx;

#ifdef __PROPRIETARY_HID_SUPPORT__

    /* If notifications are enabled on proprietary HID report handle, it
     * means that only boot mode data has to be sent on this handle.
     */

    if(HidBootGetNotificationStatus())
    {
        if(HidSendBootInputReport(g_kbd_data.st_ucid,
            GET_CQUEUE_INPUT_REPORT_ID(start_idx),
            GET_CQUEUE_INPUT_REPORT_REF(start_idx)))
        {

            /* Set the data being transferred flag to TRUE */
            g_kbd_data.data_tx_in_progress = TRUE;
        }
    }
    else

#endif /* __PROPRIETARY_HID_SUPPORT__ */

    {
        if(HidSendInputReport(g_kbd_data.st_ucid,
                           GET_CQUEUE_INPUT_REPORT_ID(start_idx),
                           GET_CQUEUE_INPUT_REPORT_REF(start_idx)))
        {

            /* Set the data being transferred flag to TRUE */
            g_kbd_data.data_tx_in_progress = TRUE;
        }
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      AppCheckNotificationStatus
 *
 *  DESCRIPTION
 *      This function checks the notification status of HID service. First, the
 *      notification status of proprietary HID service is checked followed by
 *      notification on HID service.
 *
 *  RETURNS
 *      TRUE if notification is enabled on proprietary HID service or HID
 *      service.
 *
 *  NOTE
 *      This function can be removed when Proprietary HID service is not
 *      supported. Wherever this function is called, it can be replaced
 *      by a call to HidIsNotificationEnabled().
 *
 *----------------------------------------------------------------------------*/

extern bool AppCheckNotificationStatus(void)
{
    bool notification_status = FALSE;

    /* First check whether notifications are enabled on Prorietary HID service.
     * If not check for the HID service notification status.
     */
#ifdef __PROPRIETARY_HID_SUPPORT__

    if(HidBootGetNotificationStatus())
    {
        notification_status = TRUE;
    }

    else

#endif /* __PROPRIETARY_HID_SUPPORT__ */
    {
        if(HidIsNotificationEnabled())
        {
            notification_status = TRUE;
        }
    }
    return notification_status;
}

#ifdef PENDING_REPORT_WAIT
/*-----------------------------------------------------------------------------*
 *  NAME
 *      HandlePendingReportsTimerExpiry
 *
 *  DESCRIPTION
 *      This function sends pending reports if any after the remote hosts
 *      has configured the notifications.
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
extern void HandlePendingReportsTimerExpiry(timer_id tid)
{
    if(tid == g_kbd_data.pending_report_tid)
    {
       g_kbd_data.pending_report_tid = TIMER_INVALID;

       /* Send the keys from queue only if a transmission is not
        * already in progress.
        */
       if(AppCheckNotificationStatus()&&
         !g_kbd_data.data_tx_in_progress &&
         !g_kbd_data.waiting_for_fw_buffer)
       {
          SendKeyStrokesFromQueue();
       }
    }
}
#endif /*PENDING_REPORT_WAIT*/
/*-----------------------------------------------------------------------------*
 *  NAME
 *      AppPowerOnReset
 *
 *  DESCRIPTION
 *      This function is called just after a power-on reset (including after
 *      a firmware panic).
 *
 *      NOTE: this function should only contain code to be executed after a
 *      power-on reset or panic. Code that should also be executed after an
 *      HCI_RESET should instead be placed in the reset() function.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
void AppPowerOnReset(void)
{
    /* Configure the application constants */
}


/*-----------------------------------------------------------------------------*
 *  NAME
 *      AppInit
 *
 *  DESCRIPTION
 *      This function is called after a power-on reset (including after a
 *      firmware panic) or after an HCI Reset has been requested.
 *
 *      NOTE: In the case of a power-on reset, this function is called
 *      after app_power_on_reset.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
void AppInit(sleep_state LastSleepState)
{
    uint16 gatt_database_length;
    uint16 *gatt_database_pointer = NULL;

    /* Initialise the application timers */
    TimerInit(MAX_APP_TIMERS, (void*)app_timers);

    /* Initialise GATT entity */
    GattInit();

    /* Install GATT Server support for the optional Write procedures */
    GattInstallServerWrite();

#ifdef __GAP_PRIVACY_SUPPORT__ 
    generatePrivateAddress();
#endif /* __GAP_PRIVACY_SUPPORT__ */

#ifdef NVM_TYPE_EEPROM
    /* Configure the NVM manager to use I2C EEPROM for NVM store */
    NvmConfigureI2cEeprom();
#elif NVM_TYPE_FLASH
    /* Configure the NVM Manager to use SPI flash for NVM store. */
    NvmConfigureSpiFlash();
#endif /* NVM_TYPE_EEPROM */

    Nvm_Disable();

    /* HID Service Initialisation on Chip reset */
    HidInitChipReset();

    /* Battery Service Initialisation on Chip reset */
    BatteryInitChipReset();

    /* Scan Parameter Service Initialisation on Chip reset */
    ScanParamInitChipReset();

    /* Read persistent storage */
    readPersistentStore();

    /* Tell Security Manager module about the value it needs to initialize it's
     * diversifier to.
     */
    SMInit(g_kbd_data.diversifier);

    /* If the keyboard supports a display also, the IO capability should be set
     * to SM_IO_CAP_KEYBOARD_DISPLAY
     */
    SMSetIOCapabilities(SM_IO_CAP_KEYBOARD_ONLY);

    /* Initialise Keyboard application data structure */
    kbdDataInit();

    /* Initialize the queue related variables */
    resetQueueData();

    /* Initialise Hardware to set PIO controller for PIOs scanning */
    InitHardware();

    /* Initialise Keyboard state */
    g_kbd_data.state = kbd_init;

    /* Initialize the revoke count */
    g_Revoke_Count = 0;

    /* Tell GATT about our database. We will get a GATT_ADD_DB_CFM event when
     * this has completed.
     */
    gatt_database_pointer = GattGetDatabase(&gatt_database_length);
    GattAddDatabaseReq(gatt_database_length, gatt_database_pointer);

	/* Run the startup routine (from uartio.h) */
	UartStart(LastSleepState);

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      AppProcessSystemEvent
 *
 *  DESCRIPTION
 *      This user application function is called whenever a system event, such
 *      as a battery low notification, is received by the system.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
void AppProcessSystemEvent(sys_event_id id, void *data)
{
    switch(id)
    {
    case sys_event_battery_low:
    {
        /* Battery low event received - notify the connected host. If
         * not connected, the battery level will get notified when
         * device gets connected again
         */
        if((g_kbd_data.state == kbd_connected) || (g_kbd_data.state ==
                                                             kbd_passkey_input))
        {
            BatteryUpdateLevel(g_kbd_data.st_ucid,TRUE);
        }
     }
     break;
    case sys_event_pio_ctrlr: /* if the event is from the PIO controller. */
    {
        /* In case keyboard is
         * CONNECTED     - Notify any key presses to connected host
         * IDLE          - Trigger directed advertisements to reconnect to the
         *                 host and then transfer key presses to the host.
         * FAST_ADVERTISING /
         * SLOW_ADVERTISING - Key presses will get queued in this case, till the
         *                 connection is established with the host.
         * INACTIVE        - Trigger undirected advertisements to connect to any
         *                  available HID host and then transfer key presses to
         *                  the host.
         */

        /* Array to hold the keys copied from the PIO controller. */
        uint8  rows[ROWS];
        uint8  raw_report[LARGEST_REPORT_SIZE];
        bool   new_report = FALSE;
        uint16 i;
        uint16 c;
        uint16 *p_shared_data = NULL;

        /* Copy shared data to a safe area before it gets over written by the
         * PIO controller. They are 16 bit words copy back into 2 * 8 bit row
         * info for easy processing.
         */
        p_shared_data = (uint16*)data;

        /* First word in memory points to which key buffer is in use. 0 = first
         * buffer.
         */
        if(*p_shared_data == 0)
        {
            ++ p_shared_data;
        }
        else
        {
            p_shared_data = p_shared_data + KEYWORDS + 1;  /* add 18 bytes */
        }

        c=0;

        /* The bits corresponding to the pressed keys will be in 'LOW' state.
         * So, the scanned data needs to be inverted before copying.
         */
        for(i = 0; i < KEYWORDS; i++)
        {
            rows[c++] = (~(*p_shared_data ))& 0x00FF;
            rows[c++] = (~((*p_shared_data ) >> 8)) & 0x00FF;
            ++ p_shared_data;
        }

        /* PIO event need not be processed further if Ghost Keys are detected */
        if (GhostFX(rows)) break;

        /* Filter bouncing key press */
        DebounceFX(rows);

        if (GhostFX(rows)) break;

        MemSet(raw_report, 0, LARGEST_REPORT_SIZE);

        /* A single key press generates many PIO Controller events. So, it's
         * important to verify whether this system event is because of a new
         * key press or the same old one. This is taken care by ProcessKeyPress().
         */
        new_report = ProcessKeyPress(rows, raw_report);

        if(new_report)
        {
            /* Process new report */
            ProcessReport(raw_report);
        }
    }
    break;

    case sys_event_pio_changed:
    {
        HandlePIOChangedEvent(((pio_changed_data*)data)->pio_cause);
    }
    break;

    default:
        /* Do nothing. */
    break;
    }
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      AppProcessLmEvent
 *
 *  DESCRIPTION
 *      This user application function is called whenever a LM-specific event is
 *      received by the system.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

bool AppProcessLmEvent(lm_event_code event_code, LM_EVENT_T *event_data)
{
    switch(event_code)
    {

        /* Below messages are received in kbd_init state */
        case GATT_ADD_DB_CFM:
            handleSignalGattAddDBCfm((GATT_ADD_DB_CFM_T*)event_data);
        break;

        case LM_EV_CONNECTION_COMPLETE:
            /* Handle the LM connection complete event. */
            handleSignalLmEvConnectionComplete(
                                      (LM_EV_CONNECTION_COMPLETE_T*)event_data);
        break;

        /* Below messages are received in advertising state. */
        case GATT_CONNECT_CFM:
            handleSignalGattConnectCfm((GATT_CONNECT_CFM_T*)event_data);
        break;

        case GATT_CANCEL_CONNECT_CFM:
            handleSignalGattCancelConnectCfm(
                    (GATT_CANCEL_CONNECT_CFM_T*)event_data);
        break;

        /* Below messages are received in kbd_connected/kbd_passkey_input state
         */
        case GATT_ACCESS_IND:
        {
            /* Received when HID Host tries to read or write to any
             * characteristic with FLAG_IRQ.
             */
            handleSignalGattAccessInd((GATT_ACCESS_IND_T*)event_data);
        }
        break;

        case LM_EV_DISCONNECT_COMPLETE:
            handleSignalLmEvDisconnectComplete(
            &((LM_EV_DISCONNECT_COMPLETE_T *)event_data)->data);
        break;

        case LM_EV_ENCRYPTION_CHANGE:
            handleSignalLMEncryptionChange(event_data);
        break;

        case SM_KEYS_IND:
            handleSignalSmKeysInd((SM_KEYS_IND_T*)event_data);
        break;

        case SM_PAIRING_AUTH_IND:
            /* Authorize or Reject the pairing request */
            handleSignalSmPairingAuthInd((SM_PAIRING_AUTH_IND_T*)event_data);
        break;

        case SM_SIMPLE_PAIRING_COMPLETE_IND:
            handleSignalSmSimplePairingCompleteInd(
                (SM_SIMPLE_PAIRING_COMPLETE_IND_T*)event_data);
        break;

        case SM_PASSKEY_INPUT_IND:
            handleSignalSMpasskeyInputInd();
        break;

        /* Received in response to the L2CAP_CONNECTION_PARAMETER_UPDATE request
         * sent from the slave after encryption is enabled. If the request has
         * failed, the device should again send the same request only after
         * Tgap(conn_param_timeout). Refer Bluetooth 4.0 spec Vol 3 Part C,
         * Section 9.3.9 and HID over GATT profile spec section 5.1.2.
         */
        case LS_CONNECTION_PARAM_UPDATE_CFM:
            handleSignalLsConnParamUpdateCfm((LS_CONNECTION_PARAM_UPDATE_CFM_T*)
                event_data);
        break;


        case LM_EV_CONNECTION_UPDATE:
            /* This event is sent by the controller on connection parameter
             * update.
             */
            handleSignalLmConnectionUpdate(
                            (LM_EV_CONNECTION_UPDATE_T*)event_data);
        break;

        case LS_CONNECTION_PARAM_UPDATE_IND:
            handleSignalLsConnParamUpdateInd(
                            (LS_CONNECTION_PARAM_UPDATE_IND_T *)event_data);
        break;

        case SM_DIV_APPROVE_IND:
            handleSignalSmDivApproveInd((SM_DIV_APPROVE_IND_T *)event_data);
        break;

        /* Received when the data sent from application has been successfully
         * queued to be sent to the remote device
         */
        case GATT_CHAR_VAL_NOT_CFM:
            handleSignalGattCharValNotCfm((GATT_CHAR_VAL_IND_CFM_T *)event_data);
        break;

        /* tx_data radio events are enabled while sending data from the queue.
         * This event is received by the application when data has been
         * successfully sent to the remote device
         */
        case LS_RADIO_EVENT_IND:
            handleSignalLsRadioEventInd();
        break;

        default:
            /* Unhandled event. */
        break;

    }

    return TRUE;
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      SetStateDisconnect
 *
 *  DESCRIPTION
 *      This function is used for changing the application state to
 *      disconnecting
 *
 *  RETURNS
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/
extern void SetStateDisconnect(void)
{
    appSetState(kbd_disconnecting);
}
