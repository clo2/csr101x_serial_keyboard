/*******************************************************************************
 *  Copyright (c) 2014 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 *  FILE
 *      bond_mgmt_service.c
 *
 *  DESCRIPTION
 *      This file defines routines for using Bond Management Service.
 *
 ******************************************************************************/

/*=============================================================================*
 *  SDK Header Files
 *============================================================================*/

#include <gatt.h>
#include <gatt_prim.h>
#include <buf_utils.h>

/*=============================================================================*
 *  Local Header Files
 *============================================================================*/

#include "app_gatt.h"
#include "bond_mgmt_service.h"
#include "app_gatt_db.h"
#include "keyboard.h"

/*=============================================================================*
 *  Private Data Types
 *============================================================================*/

typedef struct
{
    bool send_delete_bonding;    
} BOND_MGMT_DATA_T;

/*=============================================================================*
 *  Private Data
 *============================================================================*/

BOND_MGMT_DATA_T bond_data;

/*=============================================================================*
 *  Private Definitions
 *============================================================================*/

/* Bond Management Feature Delete Bond of Requesting device on LE only 
 * transport 
 */
#define BOND_MGMT_FEATURE_DELETE_BOND_REQ_LE_ONLY               (0x10)

/* Bond Management Control Point Opcode to delete bond of Requesting device 
 * for LE transport only
 */
#define BOND_MGMT_CONTROL_POINT_DELETE_BOND_REQ_LE_ONLY         (0x03)

/*=============================================================================*
 *  Public Function Implementations
 *============================================================================*/

/*-----------------------------------------------------------------------------*
 *  NAME
 *      BondMgmtDataInit
 *
 *  DESCRIPTION
 *      This function is used to initialise Bond Management service data 
 *      structure.
 *
 *  RETURNS/MODIFIES
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void BondMgmtDataInit(void)
{
    bond_data.send_delete_bonding = FALSE;    
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      BondMgmtHandleAccessRead
 *
 *  DESCRIPTION
 *      This function handles read operation on Bond Management service 
 *      attributes maintained by the application and responds with the 
 *      GATT_ACCESS_RSP message.
 *
 *  RETURNS
 *      Nothing.
 *
 *----------------------------------------------------------------------------*/

extern void BondMgmtHandleAccessRead(GATT_ACCESS_IND_T *p_ind)
{
    uint16 length = 0;
    uint8  value[1];
    sys_status rc = sys_status_success;

    switch(p_ind->handle)
    {

        case HANDLE_BOND_MGMT_FEATURE:
        {
            length = 1; /* One Octet */
            value[0] = BOND_MGMT_FEATURE_DELETE_BOND_REQ_LE_ONLY;
        }
        break;

        default:
            /* No more IRQ characteristics */
            rc = gatt_status_read_not_permitted;
        break;

    }

    /* Send Access response */
    GattAccessRsp(p_ind->cid, p_ind->handle, rc,
                  length, value);

}

/*-----------------------------------------------------------------------------*
 *  NAME
 *      BondMgmtHandleAccessWrite
 *
 *  DESCRIPTION
 *      This function handles write operation on Bond Management service 
 *      attributes maintained by the application.and responds with the 
 *      GATT_ACCESS_RSP message.
 *
 *  RETURNS
 *      Nothing
 *
 *----------------------------------------------------------------------------*/

extern void BondMgmtHandleAccessWrite(GATT_ACCESS_IND_T *p_ind)
{
    uint8 *p_value = p_ind->value;
    uint8 control_point;
    sys_status rc = sys_status_success;

    switch(p_ind->handle)
    {
        case HANDLE_BOND_MGMT_CONTROL_POINT:
        {
            control_point = BufReadUint8(&p_value);

            if(control_point == BOND_MGMT_CONTROL_POINT_DELETE_BOND_REQ_LE_ONLY)
            {
                /*  The bond deletion needs to be handled here  */
                bond_data.send_delete_bonding = TRUE;
            }
            else
            {
                /* Return error as other opcodes are not supported */
                rc = gatt_status_opcode_not_supported;
            }
        }
        break;

        default:
            rc = gatt_status_write_not_permitted;
        break;
    }

    /* Send ACCESS RESPONSE */
    GattAccessRsp(p_ind->cid, p_ind->handle, rc, 0, NULL);
    
    /* Bonding deletion needs to be done after disconnecting the link
     */
    HandleDeleteBonding();
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *       BondMgmtCheckHandleRange
 *
 *  DESCRIPTION
 *      This function is used to check if the handle belongs to the  Bond 
 *      Management service
 *
 *  RETURNS
 *      Boolean - Indicating whether handle falls in range or not.
 *
 *----------------------------------------------------------------------------*/

extern bool  BondMgmtCheckHandleRange(uint16 handle)
{
    return ((handle >= HANDLE_BOND_MGMT_SERVICE) &&
            (handle <= HANDLE_BOND_MGMT_SERVICE_END))
            ? TRUE : FALSE;
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *       BondMgmtSetBondDeletionFlag
 *
 *  DESCRIPTION
 *      This function sets the value of bond deletion flag
 *
 *  RETURNS
 *      Mothing
 *
 *----------------------------------------------------------------------------*/

extern void BondMgmtSetBondDeletionFlag(bool value)
{
  bond_data.send_delete_bonding = value;    
}

/*-----------------------------------------------------------------------------*
 *  NAME
 *       BondMgmtGetBondDeletionFlag
 *
 *  DESCRIPTION
 *      This function returns the value of bond deletion flag
 *
 *  RETURNS
 *      Mothing
 *
 *----------------------------------------------------------------------------*/

extern bool BondMgmtGetBondDeletionFlag(void)
{
    return bond_data.send_delete_bonding;    
}

