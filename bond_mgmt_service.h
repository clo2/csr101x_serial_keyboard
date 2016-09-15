/*******************************************************************************
 *  Copyright (c) 2014 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 *  FILE
 *      bond_mgmt_service.h
 *
 *  DESCRIPTION
 *      Header definitions for Bond Management Service
 *
 ******************************************************************************/

#ifndef _BOND_MGMT_SERVICE_H_
#define _BOND_MGMT_SERVICE_H_

/*=============================================================================*
 *  SDK Header Files
 *============================================================================*/
#include <types.h>
#include <bt_event_types.h>

/*=============================================================================*
 *  Public Function Prototypes
 *============================================================================*/

/* This function is used to initialise Bond Management service data 
 * structure.
 */
extern void BondMgmtDataInit(void);

/* This function handles read operation on Bond Management service attributes
 * maintained by the application
 */
extern void BondMgmtHandleAccessRead(GATT_ACCESS_IND_T *p_ind);

/* This function handles write operation on Bond Management service attributes 
 * maintained by the application
 */
extern void BondMgmtHandleAccessWrite(GATT_ACCESS_IND_T *p_ind);

/* This function is used to check if the handle belongs to the Bond Management 
 * service
 */
extern bool BondMgmtCheckHandleRange(uint16 handle);

/* This function sets the value of bond deletion flag */
extern void BondMgmtSetBondDeletionFlag(bool value);

/* This function returns the value of bond deletion flag */
extern bool BondMgmtGetBondDeletionFlag(void);

#endif /* _BOND_MGMT_SERVICE_H_ */
