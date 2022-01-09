// remember that function names and signatures have to match these macros

#define IS_EVENT(_ptr, _mod, _evt) \
  is_ ## _mod ## _event(&_ptr->module._mod.header) && \
  _ptr->module._mod.type == _evt

#define SEND_EVENT(_mod, _type)  \
  struct _mod ## _event *event = new_ ## _mod ## _event(); \
  event->type = _type; \
  EVENT_SUBMIT(event)
