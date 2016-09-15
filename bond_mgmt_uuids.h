/*******************************************************************************
 *  Copyright (c) 2014 - 2016 Qualcomm Technologies International, Ltd.
 *  Part of CSR uEnergy SDK 2.6.1
 *  Application version 2.6.1.0
 *
 *  FILE
 *      bond_mgmt_uuids.h
 *
 *  DESCRIPTION
 *      UUID MACROs for Bond Management service
 *
 ******************************************************************************/

#ifndef _BOND_MGMT_UUIDS_H_
#define _BOND_MGMT_UUIDS_H_

/*=============================================================================*
 *         Public Definitions
 *============================================================================*/

/* Brackets should not be used around the value of a macro. The parser which 
 * creates .c and .h files from .db file doesn't understand brackets and will
 * raise syntax errors. 
 */
 
/* BOND Management Service UUID */
#define BOND_MGMT_SERVICE_UUID                  0x181E

/* Bond Management Control Point UUID */
#define BOND_MGMT_CONTROL_POINT_UUID            0x2AA4

/* Bond Management Feature UUID */
#define BOND_MGMT_FEATURE_UUID                  0x2AA5

#endif /* _BOND_MGMT_UUIDS_H_ */
