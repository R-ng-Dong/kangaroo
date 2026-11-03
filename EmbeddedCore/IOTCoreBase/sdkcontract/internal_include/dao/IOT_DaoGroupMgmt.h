#pragma once
#include "dao/IOT_DaoType.h"
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"

// ============================================================================
// Group member struct — parsed representation of one device in a group
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    uint16_t eid;
    uint8_t *elemData;    // Element binding data: a list of element ids (NULL if none)
    uint16_t elemDataLen; // Byte length of elemData (0 = control all device elements)
} IOT_GroupMember_t;

typedef struct
{
    IOT_GroupMember_t *members;
    uint16_t count;
} IOT_GroupMemberList_t;

// ============================================================================
// Group DAO API
// ============================================================================

/**
 * @brief Get parsed group member list (BLOCKING)
 * @param groupId   Group EID (> IOT_GROUP_EID_BASE)
 * @param outList   Output: parsed member list (caller must call IOT_DaoGroupFreeMemberList)
 * @return IOT_OK, IOT_ERR_DATA_NOT_FOUND if group doesn't exist
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoGroupGetMemberList(uint16_t groupId, IOT_GroupMemberList_t *outList);

/**
 * @brief Free a member list returned by IOT_DaoGroupGetMemberList
 * Blocks: no. Safe from a timer callback: yes. Task: any.
 */
void IOT_DaoGroupFreeMemberList(IOT_GroupMemberList_t *list);

#ifdef __cplusplus
}
#endif
