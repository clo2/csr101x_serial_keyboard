/*******************************************************************************
 *  Copyright (c) 2012 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 *  FILE
 *          keyboard.h  -  user application for a BTLE keyboard
 *
 *  DESCRIPTION
 *      Header file for a sample keyboard application.
 *
 ******************************************************************************/

#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

/*=============================================================================*
 *  SDK Header Files
 *============================================================================*/

#include <types.h>
#include <bluetooth.h>
#include <timer.h>

/*=============================================================================*
 *  Local Header File
 *============================================================================*/

#include "app_gatt.h"
#include "app_gatt_db.h"
#include "gap_conn_params.h"
#include "user_config.h"

/*=============================================================================*
 *  Public Definitions
 *============================================================================*/

/* Maximum number of words in central device IRK */
#define MAX_WORDS_IRK                   (8)

/*Number of IRKs that application can store */
#define MAX_NUMBER_IRK_STORED           (1)

/*=============================================================================*
 *  Public Data Types
 *============================================================================*/

typedef enum
{

    kbd_init = 0,           /* Initial State */
    kbd_direct_advert,      /* This state is entered while reconnecting to
                             * a bonded host that does not support random 
                             * resolvable address. When privacy feature is 
                             * enabled, this state is entered when the remote 
                             * host has written to the re-connection address 
                             * characteristic during the last connection
                             */
    kbd_fast_advertising,    /* Fast Undirected advertisements configured */
    kbd_slow_advertising,    /* Slow Undirected advertisements configured */
    kbd_passkey_input,      /* State in which the keyboard has to send the user
                             * entered passkey */    
    kbd_connected,          /* Keyboard has established connection to the 
                               host */
    kbd_disconnecting,      /* Enters when disconnect is initiated by the 
                               HID device */
    kbd_idle               /* Enters when no key presses are reported from 
                               connected keyboard for vendor specific time 
                               defined by CONNECTED_IDLE_TIMEOUT_VALUE macro */
} kbd_state;

/* Structure defined for Central device IRK. A device can store more than one
 * IRK for the same remote device. This structure can be extended to accommodate
 * more IRKs for the same remote device.
 */
typedef struct
{
    uint16                              irk[MAX_WORDS_IRK];

} CENTRAL_DEVICE_IRK_T;

/* Structure to hold a key press. It contains a report ID and a report. The
 * length of the report should be the maximum length of all the types of
 * report used in the application as the same array is used while adding all
 * types of report to the queue.
 */
typedef struct
{
    uint8 report_id;
    uint8 report[LARGEST_REPORT_SIZE];

} KEY_STROKE_T;

/* Circular queue for storing pending key strokes */
typedef struct
{
    /* Circular queue buffer */
    KEY_STROKE_T key_stroke[MAX_PENDING_KEY_STROKES];

    /* Starting index of circular queue carrying the oldest key stroke */
    uint8 start_idx;

    /* Out-standing key strokes in the queue */
    uint8 num;

} CQUEUE_KEY_STROKE_T;

typedef struct
{
    kbd_state state;

    /* Value for which advertisement timer needs to be started. 
     *
     * For bonded devices, the timer is initially started for 30 seconds to 
     * enable fast connection by bonded device to the sensor.
     * This is then followed by reduced power advertisements for 1 minute.
     */
    uint32 advert_timer_value;
    
    /* Store timer id in 'FAST_ADVERTISING', 'SLOW_ADVERTISING' and 
     * 'CONNECTED' states.
     */
    timer_id app_tid;

    /* Timer to hold the time elapsed after the last
     * L2CAP_CONNECTION_PARAMETER_UPDATE Request failed.
     */
     timer_id conn_param_update_tid;

    /* A counter to keep track of the number of times the application has tried 
     * to send L2CAP connection parameter update request. When this reaches 
     * MAX_NUM_CONN_PARAM_UPDATE_REQS, the application stops re-attempting to
     * update the connection parameters */
    uint8 conn_param_update_cnt;

    /* Connection Parameter Update timer value. Upon a connection, it's started
     * for a period of TGAP_CPP_PERIOD, upon the expiry of which it's restarted
     * for TGAP_CPC_PERIOD. When this timer is running, if a GATT_ACCESS_IND is
     * received, it means, the central device is still doing the service discov-
     * -ery procedure. So, the connection parameter update timer is deleted and
     * recreated. Upon the expiry of this timer, a connection parameter update
     * request is sent to the central device.
     */
    uint32 cpu_timer_value;


    /* TYPED_BD_ADDR_T of the host to which keyboard is connected */
    TYPED_BD_ADDR_T con_bd_addr;

    /* Track the UCID as Clients connect and disconnect */
    uint16 st_ucid;
    
	/* Flag to track the authentication failure during the disconnection */
	bool auth_failure;

    /* Boolean flag to indicated whether the device is bonded */
    bool bonded;

    /* TYPED_BD_ADDR_T of the host to which keyboard is bonded. Heart rate
     * sensor can only bond to one collector 
     */
    TYPED_BD_ADDR_T bonded_bd_addr;

    /* Diversifier associated with the LTK of the bonded device */
    uint16 diversifier;

    /* Other option is to use a independent global varibale for this in 
     *the seperate file */
    /*Central Private Address Resolution IRK  Will only be used when
     *central device used resolvable random address. */
    CENTRAL_DEVICE_IRK_T central_device_irk;

    /* Value of TK to be supplied to the Security Manager upon Passkey entry */
    uint32 pass_key;

    /* Boolean flag to indicate whether encryption is enabled with the bonded 
     * host
     */
    bool encrypt_enabled;

    /* Boolean flag set to transfer key press data post connection with the 
     * host.
     */
    bool data_pending;

    /* Circular queue for storing pending key strokes */
    CQUEUE_KEY_STROKE_T pending_key_strokes;

    /* Boolean flag set to indicate pairing button press */
    bool pairing_button_pressed;

    /* If a key press occurs in slow advertising state, the application shall 
     * move to directed or fast advertisements state. This flag is used to 
     * know whether the application needs to move to directed or fast 
     * advertisements state while handling GATT_CANCEL_CONNECT_CFM message.
     */
    bool start_adverts;

    /* Boolean to indicate whether the application has sent data to the firmware
     * and waiting for confirmation from it.
     */
    bool data_tx_in_progress;

    /* Boolean which indicates that the application is waiting for the firmware
     * to free its buffers. This flag is set when the application tries to add
     * some data(which has to be sent to the remote device) to the the firmware
     * and receives a gatt_status_busy response in GATT_CHAR_VAL_NOT_CFM.
     */
    bool waiting_for_fw_buffer;

    /* This timer will be used if the application is already bonded to the 
     * remote host address but the remote device wanted to rebond which we had 
     * declined. In this scenario, we give ample time to the remote device to 
     * encrypt the link using old keys. If remote device doesn't encrypt the 
     * link, we will disconnect the link on this timer expiry.
     */
    timer_id bonding_reattempt_tid;

    /* Varibale to store the current connection interval being used. */
    uint16   conn_interval;

    /* Variable to store the current slave latency. */
    uint16   conn_latency;

    /*Variable to store the current connection timeout value. */
    uint16   conn_timeout;
	
    /* This timer will be used to send pending reports if any, after the
	 * remote host has configured for notifications.
	 */
	timer_id pending_report_tid;

#ifdef __GAP_PRIVACY_SUPPORT__
    /* This timer will be used to change the random Bluetooth address */
    timer_id random_addr_tid;
#endif /* __GAP_PRIVACY_SUPPORT__ */

} KBD_DATA_T;

/*=============================================================================*
 *  Public Data
 *============================================================================*/

/* The data structure used by the keyboard application. */
extern KBD_DATA_T g_kbd_data;

/*=============================================================================*
 *  Public Definitions
 *============================================================================*/

#define IS_KBD_ADVERTISING() ((g_kbd_data.state == kbd_fast_advertising) || \
                              (g_kbd_data.state == kbd_slow_advertising)|| \
                              (g_kbd_data.state == kbd_direct_advert))

/* Get the pointer to the input report stored in the queue at index 'i' */
#define GET_CQUEUE_INPUT_REPORT_REF(i) \
                   (g_kbd_data.pending_key_strokes.key_stroke[(i)].report)

/* Get the report ID of the report stored in the queue at index 'i' */
#define GET_CQUEUE_INPUT_REPORT_ID(i) \
                   (g_kbd_data.pending_key_strokes.key_stroke[(i)].report_id)

/* Set the report ID of the report stored in the queue at index 'i' */
#define SET_CQUEUE_INPUT_REPORT_ID(i, j) \
             (g_kbd_data.pending_key_strokes.key_stroke[(i)].report_id = (j))

/*=============================================================================*
 *  Public Function Prototypes
 *============================================================================*/

/* This function is called when the pairing removal button is pressed down for a
 * period specified by PAIRING_REMOVAL_TIMEOUT
 */
extern void HandlePairingButtonPress(timer_id tid);

/* This function sends key strokes in circular queue maintained by application */
extern void SendKeyStrokesFromQueue(void);

/* This function checks the notification status of HID service */
extern bool AppCheckNotificationStatus(void);

/* This function processes the raw reports received from PIO controller */
extern void ProcessReport(uint8* raw_report);

/* This function adds new reports to the application queue */
extern void AddKeyStrokeToQueue(uint8 report_id, uint8 *report,
                                uint8 report_length);

/* This function is used for changing the application state to disconnecting */
extern void SetStateDisconnect(void);

/* This function deletes the bonding with the connected device */
extern void HandleDeleteBonding(void);

#ifdef PENDING_REPORT_WAIT
/* This timer function sends the buffered keyboard input reports. */
extern void HandlePendingReportsTimerExpiry(timer_id tid);
#endif /* PENDING_REPORT_WAIT */
#endif /* __KEYBOARD_H__ */
