/* internal structures for v4l2 implementation */

#ifdef MEGA_BACKEND
  #define BACKEND_METHOD(m) v4l2_##m
#else
  #define BACKEND_METHOD(m) m
#endif

#include "cam_iface_static_functions.h"
