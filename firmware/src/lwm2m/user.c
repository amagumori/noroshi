#include <lwm2m_object.h>
#include <lwm2m_engine.h>

static struct lwm2m_engine_obj user;
static struct lwm2m_engine_obj_field fields[] = {
  OBJ_FIELD_DATA(USER_ID, RW, U32);
  // etc




};
