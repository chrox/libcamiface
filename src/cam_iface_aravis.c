/*

Copyright (c) 2004-2009, John Stowers. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

/* Backend for libaravis-0.2 */
#include "cam_iface.h"

#define ARAVIS_INCLUDE_FAKE_CAMERA 0
#define ARAVIS_DEBUG_ENABLE 1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/select.h>
#include <errno.h>

#include <arv.h>
#include <glib.h>
#include <glib/gprintf.h>

#if ARAVIS_INCLUDE_FAKE_CAMERA
#define GET_ARAVIS_DEVICE_INDEX(i) (i)
#else
#define GET_ARAVIS_DEVICE_INDEX(i) (i+1)
#endif

#if !ARAVIS_DEBUG_ENABLE
#define DPRINTF(...)
#else
#define DPRINTF(...) printf("DEBUG:    " __VA_ARGS__); fflush(stdout);
#endif


struct CCaravis; // forward declaration

// keep functable in sync across backends
typedef struct {
  cam_iface_constructor_func_t construct;
  void (*destruct)(struct CamContext*);

  void (*CCaravis)(struct CCaravis*,int,int,int);
  void (*close)(struct CCaravis*);
  void (*start_camera)(struct CCaravis*);
  void (*stop_camera)(struct CCaravis*);
  void (*get_num_camera_properties)(struct CCaravis*,int*);
  void (*get_camera_property_info)(struct CCaravis*,
                                   int,
                                   CameraPropertyInfo*);
  void (*get_camera_property)(struct CCaravis*,int,long*,int*);
  void (*set_camera_property)(struct CCaravis*,int,long,int);
  void (*grab_next_frame_blocking)(struct CCaravis*,
                                   unsigned char*,
                                   float);
  void (*grab_next_frame_blocking_with_stride)(struct CCaravis*,
                                               unsigned char*,
                                               intptr_t,
                                               float);
  void (*point_next_frame_blocking)(struct CCaravis*,unsigned char**,float);
  void (*unpoint_frame)(struct CCaravis*);
  void (*get_last_timestamp)(struct CCaravis*,double*);
  void (*get_last_framenumber)(struct CCaravis*,unsigned long*);
  void (*get_num_trigger_modes)(struct CCaravis*,int*);
  void (*get_trigger_mode_string)(struct CCaravis*,int,char*,int);
  void (*get_trigger_mode_number)(struct CCaravis*,int*);
  void (*set_trigger_mode_number)(struct CCaravis*,int);
  void (*get_frame_roi)(struct CCaravis*,int*,int*,int*,int*);
  void (*set_frame_roi)(struct CCaravis*,int,int,int,int);
  void (*get_max_frame_size)(struct CCaravis*,int*,int*);
  void (*get_buffer_size)(struct CCaravis*,int*);
  void (*get_framerate)(struct CCaravis*,float*);
  void (*set_framerate)(struct CCaravis*,float);
  void (*get_num_framebuffers)(struct CCaravis*,int*);
  void (*set_num_framebuffers)(struct CCaravis*,int);
} CCaravis_functable;

typedef struct CCaravis {
  CamContext inherited;

  int cam_iface_mode_number;

  int max_width;       // maximum buffer width
  int max_height;      // maximum buffer height

  int roi_left;
  int roi_top;
  int roi_width;
  int roi_height;
  int buffer_size;     // bytes per frame
  unsigned long nframe_hack;

  ArvCamera *camera;

} CCaravis;

// forward declarations
CCaravis* CCaravis_construct( int device_number, int NumImageBuffers,
                              int mode_number);
void delete_CCaravis(struct CCaravis*);

void CCaravis_CCaravis(struct CCaravis*,int,int,int);
void CCaravis_close(struct CCaravis*);
void CCaravis_start_camera(struct CCaravis*);
void CCaravis_stop_camera(struct CCaravis*);
void CCaravis_get_num_camera_properties(struct CCaravis*,int*);
void CCaravis_get_camera_property_info(struct CCaravis*,
                              int,
                              CameraPropertyInfo*);
void CCaravis_get_camera_property(struct CCaravis*,int,long*,int*);
void CCaravis_set_camera_property(struct CCaravis*,int,long,int);
void CCaravis_grab_next_frame_blocking(struct CCaravis*,
                              unsigned char*,
                              float);
void CCaravis_grab_next_frame_blocking_with_stride(struct CCaravis*,
                                          unsigned char*,
                                          intptr_t,
                                          float);
void CCaravis_point_next_frame_blocking(struct CCaravis*,unsigned char**,float);
void CCaravis_unpoint_frame(struct CCaravis*);
void CCaravis_get_last_timestamp(struct CCaravis*,double*);
void CCaravis_get_last_framenumber(struct CCaravis*,unsigned long*);
void CCaravis_get_num_trigger_modes(struct CCaravis*,int*);
void CCaravis_get_trigger_mode_string(struct CCaravis*,int,char*,int);
void CCaravis_get_trigger_mode_number(struct CCaravis*,int*);
void CCaravis_set_trigger_mode_number(struct CCaravis*,int);
void CCaravis_get_frame_roi(struct CCaravis*,int*,int*,int*,int*);
void CCaravis_set_frame_roi(struct CCaravis*,int,int,int,int);
void CCaravis_get_max_frame_size(struct CCaravis*,int*,int*);
void CCaravis_get_buffer_size(struct CCaravis*,int*);
void CCaravis_get_framerate(struct CCaravis*,float*);
void CCaravis_set_framerate(struct CCaravis*,float);
void CCaravis_get_num_framebuffers(struct CCaravis*,int*);
void CCaravis_set_num_framebuffers(struct CCaravis*,int);

CCaravis_functable CCaravis_vmt = {
  (cam_iface_constructor_func_t)CCaravis_construct,
  (void (*)(CamContext*))delete_CCaravis,
  CCaravis_CCaravis,
  CCaravis_close,
  CCaravis_start_camera,
  CCaravis_stop_camera,
  CCaravis_get_num_camera_properties,
  CCaravis_get_camera_property_info,
  CCaravis_get_camera_property,
  CCaravis_set_camera_property,
  CCaravis_grab_next_frame_blocking,
  CCaravis_grab_next_frame_blocking_with_stride,
  CCaravis_point_next_frame_blocking,
  CCaravis_unpoint_frame,
  CCaravis_get_last_timestamp,
  CCaravis_get_last_framenumber,
  CCaravis_get_num_trigger_modes,
  CCaravis_get_trigger_mode_string,
  CCaravis_get_trigger_mode_number,
  CCaravis_set_trigger_mode_number,
  CCaravis_get_frame_roi,
  CCaravis_set_frame_roi,
  CCaravis_get_max_frame_size,
  CCaravis_get_buffer_size,
  CCaravis_get_framerate,
  CCaravis_set_framerate,
  CCaravis_get_num_framebuffers,
  CCaravis_set_num_framebuffers
};

// See the following for a hint on how to make thread thread-local without __thread.
// http://lists.apple.com/archives/Xcode-users/2006/Jun/msg00551.html
#ifdef __APPLE__
#define myTLS
#else
#define myTLS __thread
#endif

#ifdef MEGA_BACKEND
  #define BACKEND_GLOBAL(m) aravis_##m
#else
  #define BACKEND_GLOBAL(m) m
#endif

/* globals -- allocate space */
myTLS int BACKEND_GLOBAL(cam_iface_error) = 0;
#define CAM_IFACE_MAX_ERROR_LEN 255
myTLS char BACKEND_GLOBAL(cam_iface_error_string)[CAM_IFACE_MAX_ERROR_LEN]  = {0x00}; //...

uint32_t aravis_num_cameras = 0;
ArvCamera **aravis_cameras = NULL;
char **aravis_device_names = NULL;

#ifdef MEGA_BACKEND
#define CAM_IFACE_ERROR_FORMAT(m)                                       \
  snprintf(aravis_cam_iface_error_string,CAM_IFACE_MAX_ERROR_LEN,              \
           "%s (%d): %s\n",__FILE__,__LINE__,(m));
#else
#define CAM_IFACE_ERROR_FORMAT(m)                                       \
  snprintf(cam_iface_error_string,CAM_IFACE_MAX_ERROR_LEN,              \
           "%s (%d): %s\n",__FILE__,__LINE__,(m));
#endif

#ifdef MEGA_BACKEND
#define NOT_IMPLEMENTED                                 \
  aravis_cam_iface_error = -1;                                 \
  CAM_IFACE_ERROR_FORMAT("not yet implemented");        \
  return;
#else
#define NOT_IMPLEMENTED                                 \
  cam_iface_error = -1;                                 \
  CAM_IFACE_ERROR_FORMAT("not yet implemented");        \
  return;
#endif

#define NOT_IMPLEMENTED_WARN fprintf(stderr,"%s (%d): not yet implemented\n",__FILE__,__LINE__); fflush(stderr);

#include "cam_iface_aravis.h"

const char *BACKEND_METHOD(cam_iface_get_driver_name)() {
  return "aravis";
}

void BACKEND_METHOD(cam_iface_clear_error)() {
  BACKEND_GLOBAL(cam_iface_error) = 0;
}

int BACKEND_METHOD(cam_iface_have_error)() {
  return BACKEND_GLOBAL(cam_iface_error);
}

const char * BACKEND_METHOD(cam_iface_get_error_string)() {
  return BACKEND_GLOBAL(cam_iface_error_string);
}

const char* BACKEND_METHOD(cam_iface_get_api_version)() {
  return CAM_IFACE_API_VERSION;
}

void BACKEND_METHOD(cam_iface_startup)() {
  unsigned int i;

  DPRINTF("startup\n");

#if !GLIB_CHECK_VERSION (2, 31, 0)
    g_thread_init (NULL);
#endif

  g_type_init ();

  /* this creates an association between list index and device IDs. This association
  will not change until the next call to this function, so I consider the list
  index to be canonical */
  arv_update_device_list ();

  aravis_num_cameras = arv_get_n_devices ();
  aravis_cameras = calloc(aravis_num_cameras, sizeof(ArvCamera *));
  aravis_device_names = calloc(aravis_num_cameras, sizeof(const char *));

  if (aravis_cameras == NULL || aravis_device_names == NULL) {
    BACKEND_GLOBAL(cam_iface_error) = -1;
    CAM_IFACE_ERROR_FORMAT("error allocating memory");
    return;
  }

  for (i = 0; i < aravis_num_cameras; i++) {
    aravis_device_names[i] = g_strdup( arv_get_device_id (i) );
  }

}

void BACKEND_METHOD(cam_iface_shutdown)() {
  arv_shutdown ();
}

int BACKEND_METHOD(cam_iface_get_num_cameras)() {
#if ARAVIS_INCLUDE_FAKE_CAMERA
  return aravis_num_cameras;
#else
  return aravis_num_cameras - 1;
#endif
}

static ArvCamera * _lazy_init_camera(int device_number) {
  int device_index = GET_ARAVIS_DEVICE_INDEX(device_number);

  if (aravis_cameras[device_index])
    return aravis_cameras[device_index];

  DPRINTF("laxy init device_number:%u aravis_id:%u aravis_name:%s\n", 
          device_number, GET_ARAVIS_DEVICE_INDEX(device_number),
          aravis_device_names[device_index]);

  aravis_cameras[device_index] = arv_camera_new ( aravis_device_names[device_index] );
  if (!aravis_cameras[device_index]) {
    BACKEND_GLOBAL(cam_iface_error) = CAM_IFACE_CAMERA_NOT_AVAILABLE_ERROR;
    CAM_IFACE_ERROR_FORMAT("camera not available");
    return NULL;
  }

  return aravis_cameras[device_index];
}

void BACKEND_METHOD(cam_iface_get_camera_info)(int device_number, Camwire_id *out_camid) {
  ArvCamera *camera;

  if (out_camid==NULL) {
    BACKEND_GLOBAL(cam_iface_error) = -1;
    CAM_IFACE_ERROR_FORMAT("return structure NULL");
    return;
  }
  
  if ((camera = _lazy_init_camera(device_number)) == NULL)
    return;

  snprintf(out_camid->vendor, CAMWIRE_ID_MAX_CHARS, "%s", arv_camera_get_vendor_name(camera));
  snprintf(out_camid->model, CAMWIRE_ID_MAX_CHARS, "%s", arv_camera_get_model_name(camera));
  snprintf(out_camid->chip, CAMWIRE_ID_MAX_CHARS, "%s", arv_camera_get_device_id(camera));

}

void BACKEND_METHOD(cam_iface_get_num_modes)(int device_number, int *num_modes) {
  ArvCamera *camera;
  guint n_formats;
  gint64 *formats;

  if ((camera = _lazy_init_camera(device_number)) == NULL) {
    *num_modes = 0;
    return;
  }

  formats = arv_camera_get_available_pixel_formats (camera, &n_formats);
  *num_modes = n_formats;

  g_free(formats);
}

void BACKEND_METHOD(cam_iface_get_mode_string)(int device_number,
                               int mode_number,
                               char* mode_string,
                               int mode_string_maxlen) {

  ArvCamera *camera;
  guint n_formats;
  gint64 *formats;

  if ((camera = _lazy_init_camera(device_number)) == NULL) {
    *mode_string = '\0';
    return;
  }

  formats = arv_camera_get_available_pixel_formats (camera, &n_formats);

  snprintf(mode_string,mode_string_maxlen,"%s",
           arv_pixel_format_to_gst_caps_string(formats[mode_number]));

  g_free(formats);
}

cam_iface_constructor_func_t BACKEND_METHOD(cam_iface_get_constructor_func)(int device_number) {
  return (CamContext* (*)(int, int, int))CCaravis_construct;
}

CCaravis* CCaravis_construct( int device_number, int NumImageBuffers,
                              int mode_number) {
  CCaravis* this=NULL;

  this = malloc(sizeof(CCaravis));
  if (this==NULL) {
    BACKEND_GLOBAL(cam_iface_error) = -1;
    CAM_IFACE_ERROR_FORMAT("error allocating memory");
  } else {
    CCaravis_CCaravis( this,
                       device_number, NumImageBuffers,
                       mode_number);
    if (BACKEND_GLOBAL(cam_iface_error)) {
      free(this);
      return NULL;
    }
  }
  return this;
}

void delete_CCaravis( CCaravis *this ) {
  CCaravis_close(this);
  this->inherited.vmt = NULL;
  free(this);
  this = NULL;
}

void CCaravis_CCaravis( CCaravis *this,
                        int device_number, int NumImageBuffers,
                        int mode_number) {
  DPRINTF("construct\n");

  // call parent
  CamContext_CamContext((CamContext*)this,device_number,NumImageBuffers,mode_number);
  this->inherited.vmt = (CamContext_functable*)&CCaravis_vmt;

  /* initialize */
  this->inherited.cam = (void *)NULL;
  this->inherited.backend_extras = (void *)NULL;
  if (!this) {
    BACKEND_GLOBAL(cam_iface_error) = -1;
    CAM_IFACE_ERROR_FORMAT("malloc failed");
    return;
  }

  this->cam_iface_mode_number = mode_number;
  this->nframe_hack=0;

  /* FIXME: take a ref here too */
  this->camera = _lazy_init_camera(device_number);

}

void CCaravis_close(CCaravis *this) {
  NOT_IMPLEMENTED;
}

void CCaravis_start_camera( CCaravis *this ) {
  NOT_IMPLEMENTED;
}

void CCaravis_stop_camera( CCaravis *this ) {
  NOT_IMPLEMENTED;
}

void CCaravis_get_num_camera_properties(CCaravis *this,
                                        int* num_properties) {
  *num_properties = 0;
  NOT_IMPLEMENTED_WARN;
}

void CCaravis_get_camera_property_info(CCaravis *this,
                                       int property_number,
                                       CameraPropertyInfo *info) {
  NOT_IMPLEMENTED;
}

void CCaravis_get_camera_property(CCaravis *this,
                                  int property_number,
                                  long* Value,
                                  int* Auto ) {
  NOT_IMPLEMENTED;
}

void CCaravis_set_camera_property(CCaravis *this,
                                  int property_number,
                                  long Value,
                                  int Auto ) {
  NOT_IMPLEMENTED;
}

void CCaravis_grab_next_frame_blocking_with_stride( CCaravis *this,
                                                    unsigned char *out_bytes,
                                                    intptr_t stride0, float timeout) {
  NOT_IMPLEMENTED;
}

void CCaravis_grab_next_frame_blocking( CCaravis *this, unsigned char *out_bytes, float timeout) {
  NOT_IMPLEMENTED;
}

void CCaravis_point_next_frame_blocking( CCaravis *this, unsigned char **buf_ptr, float timeout) {
  NOT_IMPLEMENTED;
}

void CCaravis_unpoint_frame( CCaravis *this){
  NOT_IMPLEMENTED;
}

void CCaravis_get_last_timestamp( CCaravis *this, double* timestamp ) {
  NOT_IMPLEMENTED;
}

void CCaravis_get_last_framenumber( CCaravis *this, unsigned long* framenumber ){
  NOT_IMPLEMENTED;
}

void CCaravis_get_num_trigger_modes( CCaravis *this,
                                     int *num_trigger_modes ) {
  NOT_IMPLEMENTED;
}

void CCaravis_get_trigger_mode_string( CCaravis *this,
                                       int trigger_mode_number,
                                       char* trigger_mode_string, //output parameter
                                       int trigger_mode_string_maxlen) {
  NOT_IMPLEMENTED;
}

void CCaravis_get_trigger_mode_number( CCaravis *this,
                                       int *trigger_mode_number ) {
  NOT_IMPLEMENTED;
}

void CCaravis_set_trigger_mode_number( CCaravis *this,
                                       int trigger_mode_number ) {
  NOT_IMPLEMENTED;
}

void CCaravis_get_frame_roi( CCaravis *this,
                             int *left, int *top, int* width, int* height ) {
  arv_camera_get_region (this->camera, left, top, width, height);
}

void CCaravis_set_frame_roi( CCaravis *this,
                             int left, int top, int width, int height ) {
  NOT_IMPLEMENTED;
}

void CCaravis_get_framerate( CCaravis *this,
                             float *framerate ) {
  NOT_IMPLEMENTED;
}

void CCaravis_set_framerate( CCaravis *this,
                             float framerate ) {
  NOT_IMPLEMENTED;
}

void CCaravis_get_max_frame_size( CCaravis *this,
                                  int *width, int *height ){
  NOT_IMPLEMENTED;
}

void CCaravis_get_buffer_size( CCaravis *this,
                               int *size) {
  NOT_IMPLEMENTED;
}

void CCaravis_get_num_framebuffers( CCaravis *this,
                                    int *num_framebuffers ) {
  *num_framebuffers = 0;
  NOT_IMPLEMENTED_WARN;
}

void CCaravis_set_num_framebuffers( CCaravis *this,
                                    int num_framebuffers ) {
  NOT_IMPLEMENTED;
}
