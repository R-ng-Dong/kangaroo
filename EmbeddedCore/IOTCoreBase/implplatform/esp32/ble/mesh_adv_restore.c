#include "mesh_adv_restore.h"

#include <string.h>

MeshAdvRestoreResult_t meshAdvRestoreRecord(MeshAdvRestore_t *s, uint8_t instance, bool connectable,
                                            uint8_t ownAddrType, const uint8_t *adv, uint8_t advLen,
                                            const uint8_t *rsp, uint8_t rspLen)
{
    if (s == NULL || advLen > MESH_ADV_RESTORE_MAX_LEN || rspLen > MESH_ADV_RESTORE_MAX_LEN ||
        (adv == NULL && advLen > 0) || (rsp == NULL && rspLen > 0))
    {
        return MESH_ADV_RESTORE_BAD_ARG;
    }

    /* One legacy advertising set, one remembered advert. Replacing a different instance
     * would let a beacon silently evict the change-WiFi recovery advert. */
    if (s->wanted && s->instance != instance)
    {
        return MESH_ADV_RESTORE_SLOT_TAKEN;
    }

    memset(s, 0, sizeof(*s));
    s->wanted = true;
    s->instance = instance;
    s->connectable = connectable;
    s->ownAddrType = ownAddrType;
    if (advLen > 0)
    {
        memcpy(s->adv, adv, advLen);
    }
    s->advLen = advLen;
    if (rspLen > 0)
    {
        memcpy(s->rsp, rsp, rspLen);
    }
    s->rspLen = rspLen;
    return MESH_ADV_RESTORE_OK;
}

bool meshAdvRestoreForget(MeshAdvRestore_t *s, uint8_t instance)
{
    if (s == NULL || !s->wanted || s->instance != instance)
    {
        return false;
    }
    s->wanted = false;
    return true;
}

void meshAdvRestoreOnConnect(MeshAdvRestore_t *s)
{
    if (s != NULL)
    {
        s->wanted = false;
    }
}

bool meshAdvRestoreShouldApply(const MeshAdvRestore_t *snap, bool advActive, bool connectInProgress)
{
    return snap != NULL && snap->wanted && !advActive && !connectInProgress;
}
