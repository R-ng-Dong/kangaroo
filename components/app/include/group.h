#ifndef GROUP_H_
#define GROUP_H_

#include "common.h"

#define MAX_NUM_GROUPS 5

typedef struct{
    uint16_t group_id;
    uint8_t is_used;
}DaoGroup;

void GroupInit(void);
DaoGroup* GroupGetById(UI16 group_id);
_bool GroupAdd(UI16 group_id);
_bool GroupDelete(UI16 group_id);
_bool GroupResetAll(void);

#endif /* GROUP_H_ */
