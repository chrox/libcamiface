/*

 Copyright (c) 2004-2012, John Stowers. All rights reserved.

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

/* Backend for libv4l2-0.2 */
#include "cam_iface.h"

#define V4L2_INCLUDE_FAKE_CAMERA                0
#define V4L2_DEBUG_ENABLE                       0
#define V4L2_DEBUG_FRAME_ACQUSITION_BLOCKING    0
#define V4L2_DEBUG_FRAME_ACQUSITION_STRIDE      0

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>              /* low-level i/o */
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
//#include <media/v4l2-dev.h>
//#include <media/videobuf-core.h>
//#include <media/v4l2-ioctl.h>
#include <asm/types.h>          /* for videodev2.h */
#include <linux/videodev2.h>

#include "cam_iface_v4l2.h"

struct CCv4l2;
// forward declaration

// keep functable in sync across backends
typedef struct {
	cam_iface_constructor_func_t construct;
	void (*destruct)(struct CamContext*);

	void (*CCv4l2)(struct CCv4l2*, int, int, int);
	void (*close)(struct CCv4l2*);
	void (*start_camera)(struct CCv4l2*);
	void (*stop_camera)(struct CCv4l2*);
	void (*get_num_camera_properties)(struct CCv4l2*, int*);
	void (*get_camera_property_info)(struct CCv4l2*, int, CameraPropertyInfo*);
	void (*get_camera_property)(struct CCv4l2*, int, long*, int*);
	void (*set_camera_property)(struct CCv4l2*, int, long, int);
	void (*grab_next_frame_blocking)(struct CCv4l2*, unsigned char*, float);
	void (*grab_next_frame_blocking_with_stride)(struct CCv4l2*, unsigned char*,
			intptr_t, float);
	void (*point_next_frame_blocking)(struct CCv4l2*, unsigned char**, float);
	void (*unpoint_frame)(struct CCv4l2*);
	void (*get_last_timestamp)(struct CCv4l2*, double*);
	void (*get_last_framenumber)(struct CCv4l2*, unsigned long*);
	void (*get_num_trigger_modes)(struct CCv4l2*, int*);
	void (*get_trigger_mode_string)(struct CCv4l2*, int, char*, int);
	void (*get_trigger_mode_number)(struct CCv4l2*, int*);
	void (*set_trigger_mode_number)(struct CCv4l2*, int);
	void (*get_frame_roi)(struct CCv4l2*, int*, int*, int*, int*);
	void (*set_frame_roi)(struct CCv4l2*, int, int, int, int);
	void (*get_max_frame_size)(struct CCv4l2*, int*, int*);
	void (*get_buffer_size)(struct CCv4l2*, int*);
	void (*get_framerate)(struct CCv4l2*, float*);
	void (*set_framerate)(struct CCv4l2*, float);
	void (*get_num_framebuffers)(struct CCv4l2*, int*);
	void (*set_num_framebuffers)(struct CCv4l2*, int);
} CCv4l2_functable;

typedef struct CCv4l2 {
	CamContext inherited;
	int fd;
	char * dev_name;
	int cam_iface_mode_number;
	int max_width; // maximum buffer width
	int max_height; // maximum buffer height

	int width;
	int height;

	int pixel_format;

	int roi_left;
	int roi_top;
	int roi_width;
	int roi_height;

	int buffer_size; // bytes per frame
	int n_buffers;
	struct buffer *buffers;
	unsigned int last_frame_id;
	struct timeval start_timestamp;
	struct timeval last_timestamp;

	int num_trigger_modes;

} CCv4l2;

// frame buffer in memory
struct buffer {
	void * start;
	size_t length;
};

// installed video devices
struct v4l2_devices {
	size_t counts;
	char *dev_names[8];
} v4l2_devices;

// forward declarations
CCv4l2* CCv4l2_construct(int device_number, int NumImageBuffers,
		int mode_number);
void delete_CCv4l2(struct CCv4l2*);

void CCv4l2_CCv4l2(struct CCv4l2*, int, int, int);
void CCv4l2_close(struct CCv4l2*);
void CCv4l2_start_camera(struct CCv4l2*);
void CCv4l2_stop_camera(struct CCv4l2*);
void CCv4l2_get_num_camera_properties(struct CCv4l2*, int*);
void CCv4l2_get_camera_property_info(struct CCv4l2*, int, CameraPropertyInfo*);
void CCv4l2_get_camera_property(struct CCv4l2*, int, long*, int*);
void CCv4l2_set_camera_property(struct CCv4l2*, int, long, int);
void CCv4l2_grab_next_frame_blocking(struct CCv4l2*, unsigned char*, float);
void CCv4l2_grab_next_frame_blocking_with_stride(struct CCv4l2*, unsigned char*,
		intptr_t, float);
void CCv4l2_point_next_frame_blocking(struct CCv4l2*, unsigned char**, float);
void CCv4l2_unpoint_frame(struct CCv4l2*);
void CCv4l2_get_last_timestamp(struct CCv4l2*, double*);
void CCv4l2_get_last_framenumber(struct CCv4l2*, unsigned long*);
void CCv4l2_get_num_trigger_modes(struct CCv4l2*, int*);
void CCv4l2_get_trigger_mode_string(struct CCv4l2*, int, char*, int);
void CCv4l2_get_trigger_mode_number(struct CCv4l2*, int*);
void CCv4l2_set_trigger_mode_number(struct CCv4l2*, int);
void CCv4l2_get_frame_roi(struct CCv4l2*, int*, int*, int*, int*);
void CCv4l2_set_frame_roi(struct CCv4l2*, int, int, int, int);
void CCv4l2_get_max_frame_size(struct CCv4l2*, int*, int*);
void CCv4l2_get_buffer_size(struct CCv4l2*, int*);
void CCv4l2_get_framerate(struct CCv4l2*, float*);
void CCv4l2_set_framerate(struct CCv4l2*, float);
void CCv4l2_get_num_framebuffers(struct CCv4l2*, int*);
void CCv4l2_set_num_framebuffers(struct CCv4l2*, int);

CCv4l2_functable CCv4l2_vmt = { (cam_iface_constructor_func_t) CCv4l2_construct,
		(void (*)(CamContext*)) delete_CCv4l2, CCv4l2_CCv4l2, CCv4l2_close,
		CCv4l2_start_camera, CCv4l2_stop_camera,
		CCv4l2_get_num_camera_properties, CCv4l2_get_camera_property_info,
		CCv4l2_get_camera_property, CCv4l2_set_camera_property,
		CCv4l2_grab_next_frame_blocking,
		CCv4l2_grab_next_frame_blocking_with_stride,
		CCv4l2_point_next_frame_blocking, CCv4l2_unpoint_frame,
		CCv4l2_get_last_timestamp, CCv4l2_get_last_framenumber,
		CCv4l2_get_num_trigger_modes, CCv4l2_get_trigger_mode_string,
		CCv4l2_get_trigger_mode_number, CCv4l2_set_trigger_mode_number,
		CCv4l2_get_frame_roi, CCv4l2_set_frame_roi, CCv4l2_get_max_frame_size,
		CCv4l2_get_buffer_size, CCv4l2_get_framerate, CCv4l2_set_framerate,
		CCv4l2_get_num_framebuffers, CCv4l2_set_num_framebuffers };

#define CLEAR(x) memset (&(x), 0, sizeof (x))

#define myTLS __thread

#ifdef MEGA_BACKEND
#define BACKEND_GLOBAL(m) v4l2_##m
#else
#define BACKEND_GLOBAL(m) m
#endif

myTLS int BACKEND_GLOBAL(cam_iface_error) = 0;
#define CAM_IFACE_MAX_ERROR_LEN 255
myTLS char BACKEND_GLOBAL(cam_iface_error_string)[CAM_IFACE_MAX_ERROR_LEN] = {
		0x00 }; //...

#if !V4L2_DEBUG_ENABLE
# define DPRINTF(...)
#else
# define DPRINTF(...) printf("DEBUG:    " __VA_ARGS__); fflush(stdout);
#endif

#define DWARNF(...) fprintf(stderr, "WARN :    " __VA_ARGS__); fflush(stderr);

#define ERROREXIT(m)													\
	fprintf (stderr,"ERROR :    %s (%d): error occurs.\n",__FILE__,__LINE__); fflush(stderr);	\
	fprintf (stderr, m);  fflush(stderr);								\
	exit (EXIT_FAILURE);

#ifdef MEGA_BACKEND
#define CAM_IFACE_ERROR_FORMAT(m)                                       \
  snprintf(v4l2_cam_iface_error_string,CAM_IFACE_MAX_ERROR_LEN,              \
           "%s (%d): %s\n",__FILE__,__LINE__,(m));
#else
#define CAM_IFACE_ERROR_FORMAT(m)                                       \
  snprintf(cam_iface_error_string,CAM_IFACE_MAX_ERROR_LEN,              \
           "%s (%d): %s\n",__FILE__,__LINE__,(m));
#endif

#ifdef MEGA_BACKEND
# define NOT_IMPLEMENTED                                  \
    v4l2_cam_iface_error = -1;                          \
    fprintf(stderr,"WARN :    %s (%d): not yet implemented\n",__FILE__,__LINE__); fflush(stderr); \
    CAM_IFACE_ERROR_FORMAT("not yet implemented");        \
    return;
# define V4L2_ERROR(_code, _msg)                        \
    v4l2_cam_iface_error = _code;                       \
    CAM_IFACE_ERROR_FORMAT(_msg);
#else
# define NOT_IMPLEMENTED                                  \
    cam_iface_error = -1;                                 \
    fprintf(stderr,"WARN :    %s (%d): not yet implemented\n",__FILE__,__LINE__); fflush(stderr); \
    CAM_IFACE_ERROR_FORMAT("not yet implemented");        \
    return;
# define V4L2_ERROR(_code, _msg)                        \
    cam_iface_error = _code;                              \
    CAM_IFACE_ERROR_FORMAT(_msg);
#endif

#define NOT_IMPLEMENTED_WARN                            \
  fprintf(stderr,"WARN :    %s (%d): not yet implemented\n",__FILE__,__LINE__); fflush(stderr);

const char *BACKEND_METHOD(cam_iface_get_driver_name)() {
	return "v4l2";
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

//a blocking wrapper of the ioctl function
static int xioctl(int fd, int request, void *arg) {
	int r;

	do
		r = ioctl(fd, request, arg);
	while (-1 == r && EINTR == errno);

	return r;
}

static void v4l2_open_device(int * fd, char * dev_name) {

	struct stat st;

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev_name, errno,
				strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!S_ISCHR (st.st_mode)) {
		fprintf(stderr, "%s is no device\n", dev_name);
		exit(EXIT_FAILURE);
	}

	*fd = open(dev_name, O_RDWR /* required */| O_NONBLOCK, 0);

	if (-1 == *fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno,
				strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static void v4l2_close_device(int * fd) {
	if (-1 == close(*fd)) {
		fprintf(stderr, "Close Device Error.");
		exit(EXIT_FAILURE);
	}

	*fd = -1;
}

int v4l2_test_device(char * dev_name){
	struct stat st;
	int fd;

	if (-1 == stat(dev_name, &st)) {
		return -1;
	}

	if (!S_ISCHR (st.st_mode)) {
		return -1;
	}

	fd = open(dev_name, O_RDWR /* required */| O_NONBLOCK, 0);

	if (-1 == fd)
		return -1;

	v4l2_close_device(&fd);
	return 0;
}

void v4l2_query_pix_format(char *dev_name,struct v4l2_format *fmt) {
	int fd;
	v4l2_open_device(&fd, dev_name);
	// query pix format
	if (-1 == xioctl(fd, VIDIOC_G_FMT, fmt)) {
		/* Errors ignored. */
	}
	v4l2_close_device(&fd);
}

void v4l2_query_framerate(char *dev_name, struct v4l2_frmivalenum *frt) {
	int fd;
	v4l2_open_device(&fd, dev_name);
	// query frame rate
	if (-1 == xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, frt)) {
		/* Errors ignored. */
	}
	v4l2_close_device(&fd);
}

void v4l2_query_capacity(char *dev_name, struct v4l2_capability *cap) {
	int fd;
	v4l2_open_device(&fd, dev_name);
	// query capacity
	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, cap)) {
		/* Errors ignored. */
	}
	v4l2_close_device(&fd);
}

void BACKEND_METHOD(cam_iface_startup)() {
	DPRINTF("startup\n");
	char *videos[] = {"/dev/video0", "/dev/video1", "/dev/video2",
			"/dev/video3", "/dev/video4", "/dev/video5", "/dev/video6", "/dev/video7"};
	int device_count = 0;
	size_t i;

	const size_t video_count = sizeof(videos) / sizeof(videos[0]);
	for (i = 0; i < video_count; ++i) {
		if (0 == v4l2_test_device(videos[i])){
			v4l2_devices.dev_names[device_count] = videos[i];
			device_count += 1;
		}
	}
	v4l2_devices.counts = device_count;
}

void BACKEND_METHOD(cam_iface_shutdown)() {
	v4l2_devices.counts = 0;
}

int BACKEND_METHOD(cam_iface_get_num_cameras)() {
	return v4l2_devices.counts;
}

void BACKEND_METHOD(cam_iface_get_camera_info)(int device_number,
		Camwire_id *out_camid) {
	struct v4l2_capability cap;
	v4l2_query_capacity(v4l2_devices.dev_names[device_number], &cap);

	snprintf(out_camid->vendor, CAMWIRE_ID_MAX_CHARS, "%s", "");
	snprintf(out_camid->model, CAMWIRE_ID_MAX_CHARS, "%s", cap.card);
	snprintf(out_camid->chip, CAMWIRE_ID_MAX_CHARS, "%s", cap.bus_info);
}

void BACKEND_METHOD(cam_iface_get_num_modes)(int device_number, int *num_modes) {

	*num_modes = 1;
}

void BACKEND_METHOD(cam_iface_get_mode_string)(int device_number,
		int mode_number, char* mode_string, int mode_string_maxlen) {
	char *pix_format_string;
	char framerate_string[10];
	struct v4l2_frmivalenum frt;
	struct v4l2_format fmt;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_query_pix_format(v4l2_devices.dev_names[device_number], &fmt);
	frt.index = 0;
	frt.width = fmt.fmt.pix.width;
	frt.height = fmt.fmt.pix.height;
	frt.pixel_format = fmt.fmt.pix.pixelformat;
	v4l2_query_framerate(v4l2_devices.dev_names[device_number], &frt);
	int framerate = (int)(1.0 * frt.discrete.denominator / frt.discrete.numerator);

	switch (frt.pixel_format) {
	  case V4L2_PIX_FMT_GREY:  			pix_format_string = "MONO8";  break;
	  case V4L2_PIX_FMT_YUV411P: 		pix_format_string = "YUV411"; break;
	  case V4L2_PIX_FMT_YYUV:
	  case V4L2_PIX_FMT_UYVY:
	  case V4L2_PIX_FMT_VYUY:
	  case V4L2_PIX_FMT_YVYU:
	  case V4L2_PIX_FMT_YUYV: 			pix_format_string = "YUV422"; break;
	  case V4L2_PIX_FMT_YUV444: 		pix_format_string = "YUV444"; break;
	  case V4L2_PIX_FMT_RGB332:   		pix_format_string = "RGB8";   break;
	  case V4L2_PIX_FMT_Y16: 			pix_format_string = "MONO16"; break;
	  case V4L2_PIX_FMT_RGB565:  		pix_format_string = "RGB16";  break;
	  case V4L2_PIX_FMT_RGB565X: 		pix_format_string = "RGB16S"; break;
	  default: pix_format_string = "unknown color coding"; break;
	}

	sprintf(framerate_string, "%d fps", framerate);
	snprintf(mode_string, mode_string_maxlen, "%d x %d %s %s", frt.width, frt.height,
			pix_format_string, framerate_string);
}

cam_iface_constructor_func_t BACKEND_METHOD(cam_iface_get_constructor_func)(
		int device_number) {
	return (CamContext* (*)(int, int, int)) CCv4l2_construct;
}

CCv4l2* CCv4l2_construct(int device_number, int NumImageBuffers,
		int mode_number) {
	CCv4l2* this = NULL;

	this = malloc(sizeof(CCv4l2));
	if (this == NULL) {
		BACKEND_GLOBAL(cam_iface_error) = -1;
		CAM_IFACE_ERROR_FORMAT("error allocating memory");
	} else {
		CCv4l2_CCv4l2(this, device_number, NumImageBuffers, mode_number);
		if (BACKEND_GLOBAL(cam_iface_error)) {
			free(this);
			return NULL;
		}
	}
	return this;
}

void delete_CCv4l2(CCv4l2 *this) {
	CCv4l2_close(this);
	this->inherited.vmt = NULL;
	free(this);

	this = NULL;
}

void CCv4l2_CCv4l2(CCv4l2 *this, int device_number, int NumImageBuffers,
		int mode_number) {
	/* call parent */
	CamContext_CamContext((CamContext*) this, device_number, NumImageBuffers,
			mode_number);
	this->inherited.vmt = (CamContext_functable*) &CCv4l2_vmt;

	/* initialize */
	this->inherited.cam = (void *) NULL;
	this->inherited.backend_extras = (void *) NULL;
	if (!this) {
		BACKEND_GLOBAL(cam_iface_error) = -1;
		CAM_IFACE_ERROR_FORMAT("malloc failed");
		return;
	}

	this->inherited.device_number = device_number;

	this->cam_iface_mode_number = mode_number;
	this->last_frame_id = 0;
	this->start_timestamp.tv_sec = 0;
	this->start_timestamp.tv_usec = 0;
	this->last_timestamp.tv_sec = 0;
	this->last_timestamp.tv_usec = 0;
	this->num_trigger_modes = 0;
	this->n_buffers = NumImageBuffers;

	/* set roi to max width etc */
	this->roi_left = 0;
	this->roi_top = 0;

	struct v4l2_format fmt;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_query_pix_format(v4l2_devices.dev_names[device_number], &fmt);
	this->roi_width = fmt.fmt.pix.width;
	this->roi_height = fmt.fmt.pix.height;
	switch (fmt.fmt.pix.pixelformat) {
		case V4L2_PIX_FMT_GREY:  		this->inherited.coding = CAM_IFACE_MONO8;  break;
		case V4L2_PIX_FMT_YUV411P: 		this->inherited.coding = CAM_IFACE_YUV411;  break;
		case V4L2_PIX_FMT_YYUV:			this->inherited.coding = CAM_IFACE_YYUV;  break;
		case V4L2_PIX_FMT_UYVY:			this->inherited.coding = CAM_IFACE_UYVY;  break;
		case V4L2_PIX_FMT_VYUY:			this->inherited.coding = CAM_IFACE_VYUY;  break;
		case V4L2_PIX_FMT_YVYU:			this->inherited.coding = CAM_IFACE_YVYU;  break;
		case V4L2_PIX_FMT_YUYV: 		this->inherited.coding = CAM_IFACE_YUYV;  break;
		case V4L2_PIX_FMT_YUV444: 		this->inherited.coding = CAM_IFACE_YUV444;  break;
		case V4L2_PIX_FMT_RGB332:   	this->inherited.coding = CAM_IFACE_RGB8;  break;
		case V4L2_PIX_FMT_Y16: 			this->inherited.coding = CAM_IFACE_MONO16;  break;
		case V4L2_PIX_FMT_RGB565:  		this->inherited.coding = CAM_IFACE_RGB16;  break;
		case V4L2_PIX_FMT_RGB565X: 		this->inherited.coding = CAM_IFACE_RGB16S;  break;
	}

	this->inherited.depth = 8 * fmt.fmt.pix.bytesperline / fmt.fmt.pix.width;

	v4l2_open_device(&(this->fd), v4l2_devices.dev_names[device_number]);
	this->dev_name = v4l2_devices.dev_names[device_number];
	this->width = fmt.fmt.pix.width;
	this->height = fmt.fmt.pix.height;
	this->buffer_size = fmt.fmt.pix.sizeimage;
}

//alloc buffers and configure the shared memory area
static struct buffer *init_mmap(int fd, char * dev_name, const int n_buffers) {
	struct v4l2_requestbuffers req;
	//buffers is an array of n_buffers length, and every element store a frame
	struct buffer *buffers = NULL;
	int i = 0;
	CLEAR(req);

	req.count = n_buffers;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support "
					"memory mapping\n", dev_name);
			exit(EXIT_FAILURE);
		} else {
			ERROREXIT("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
		exit(EXIT_FAILURE);
	}
	buffers = calloc(req.count, sizeof(*buffers));
	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
	//map every element of the array buffers to the shared memory
	for (i = 0; i < req.count; i++) {
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf)) {
			fprintf(stderr, "VIDIOC_QUERYBUF Error.\n");
			exit(EXIT_FAILURE);
		}

		buffers[i].length = buf.length;
		buffers[i].start = mmap(NULL /* start anywhere */, buf.length,
				PROT_READ | PROT_WRITE /* required */,
				MAP_SHARED /* recommended */, fd, buf.m.offset);

		if (MAP_FAILED == buffers[i].start) {
			fprintf(stderr, "MMAP Error.\n");
			exit(EXIT_FAILURE);
		}
	}
	return buffers;
}

//configure and initialize the hardware device
static void init_device(int fd, char * dev_name, int width, int height,
		int n_buffers, struct buffer ** p_buffers) {

	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	struct buffer * buffers = NULL;
	unsigned int min;

	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s is no V4L2 device\n", dev_name);
			exit(EXIT_FAILURE);
		} else {
			ERROREXIT("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "%s is no video capture device\n", dev_name);
		exit(EXIT_FAILURE);
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
		exit(EXIT_FAILURE);
	}

	/* Select video input, video standard and tune here. */
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
		/* Errors ignored. */
	}

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	crop.c = cropcap.defrect; /* reset to default */

	if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
		switch (errno) {
		case EINVAL:
			/* Cropping not supported. */
			break;
		default:
			/* Errors ignored. */
			break;
		}
	}

	CLEAR(fmt);
	//set image properties
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;

	// query pix format
	if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt)) {
		/* Errors ignored. */
	}
	//*pixel_format = fmt.fmt.pix.pixelformat;

	//fmt.fmt.pix.colorspace  = V4L2_COLORSPACE_SRGB;
	//fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

	if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt)) {
		fprintf(stderr, "\nError: requested format not supported for device: ",
				dev_name);
		exit(EXIT_FAILURE);
	}

	/* Note VIDIOC_S_FMT may change width and height. */

	//check the configuration data
	min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min)
		fmt.fmt.pix.bytesperline = min;
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min)
		fmt.fmt.pix.sizeimage = min;

	//fprintf(stderr, "Video bytespreline = %d\n", fmt.fmt.pix.bytesperline);
	*p_buffers = init_mmap(fd, dev_name, n_buffers);

	return;
}

static void start_capturing(int fd, int n_buffers) {
	unsigned int i;
	enum v4l2_buf_type type;

	for (i = 0; i < n_buffers; ++i) {
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) {
			fprintf(stderr, "\nError: failed to queue buffers.");
			exit(EXIT_FAILURE);
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	//start the capture from the device
	if (-1 == xioctl(fd, VIDIOC_STREAMON, &type)) {
		fprintf(stderr, "\nError: failed to start capturing.");
		exit(EXIT_FAILURE);
	}
}

void CCv4l2_start_camera(CCv4l2 *this) {
	init_device(this->fd, this->dev_name, this->width, this->height, this->n_buffers, &(this->buffers));
	start_capturing(this->fd, this->n_buffers);
}

//free the shared memory area
static void v4l2_uninit_device(int * n_buffers, struct buffer * buffers) {
	unsigned int i;

	for (i = 0; i < *n_buffers; ++i)
		if (-1 == munmap(buffers[i].start, buffers[i].length)) {
			fprintf(stderr, "MUNMAP Error.");
			exit(EXIT_FAILURE);
		}
	free(buffers);
}

void CCv4l2_close(CCv4l2 *this) {
	v4l2_uninit_device(&(this->n_buffers), this->buffers);
	v4l2_close_device(&(this->fd));
}

static void stop_capturing(int fd) {
	enum v4l2_buf_type type;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	//this call to xioctl allows to stop the stream from the capture device
	if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
		fprintf(stderr, "\nError: failed to stop capturing.");
	exit(EXIT_FAILURE);
}

void CCv4l2_stop_camera(CCv4l2 *this) {
	stop_capturing(this->fd);
}

void CCv4l2_get_num_camera_properties(CCv4l2 *this, int* num_properties) {
	*num_properties = 0;
}

void CCv4l2_get_camera_property_info(CCv4l2 *this, int property_number,
		CameraPropertyInfo *info) {

	int imin, imax;
	double dmin, dmax;

	/* nice cameras do no bother with dirty scaled values */
	info->is_scaled_quantity = 0;

	/* nice cameras have auto and manual mode */
	info->has_auto_mode = 1;
	info->has_manual_mode = 1;

	/* nice cameras can read their properties */
	info->readout_capable = 0;

	/* backen implementations of absolute functions is not consistent. Flydra camnodes do
	 not use this API AFAICT */
	info->absolute_capable = 0;
	info->absolute_control_mode = 0;
	info->absolute_min_value = 0.0;
	info->absolute_max_value = 0.0;

	info->available = 0;
	info->is_present = 0;

	switch (property_number) {
	default:
		info->available = 0;
		info->is_present = 0;
		info->name = "";
		info->min_value = info->max_value = 0;
		V4L2_ERROR(CAM_IFACE_HARDWARE_FEATURE_NOT_AVAILABLE,
				"unknown property");
		break;
	}
}

void CCv4l2_get_camera_property(CCv4l2 *this, int property_number, long* Value,
		int* Auto) {

	switch (property_number) {
	default:
		*Value = 0;
		*Auto = 0;
		V4L2_ERROR(CAM_IFACE_HARDWARE_FEATURE_NOT_AVAILABLE,
				"unknown property");
		break;
	}
}

void CCv4l2_set_camera_property(CCv4l2 *this, int property_number, long Value,
		int Auto) {

}

// single thread to read frame
void CCv4l2_grab_next_frame_blocking_with_stride(CCv4l2 *this,
		unsigned char *out_bytes, intptr_t stride0, float timeout) {
	struct v4l2_buffer buf; //needed for memory mapping
	unsigned int i;
	unsigned int Bpf; //bytes per frame

	fd_set fds;
	struct timeval tv;
	int r;

	FD_ZERO(&fds);
	FD_SET(this->fd, &fds);

	/* Select Timeout */
	tv.tv_sec = -timeout;
	tv.tv_usec = 0;

	//the classic select function, who allows to wait up to 2 seconds,
	//until we have captured data,
	r = select(this->fd + 1, &fds, NULL, NULL, &tv);

	if (0 == r) {
		ERROREXIT("select timeout\n");
	}
	this->last_frame_id += 1;
	if (this->start_timestamp.tv_sec == 0 && this->start_timestamp.tv_usec == 0)
		 gettimeofday(&(this->start_timestamp), NULL);
	gettimeofday(&(this->last_timestamp), NULL);
	CLEAR(buf);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(this->fd, VIDIOC_DQBUF, &buf)) {
		switch (errno) {
		case EAGAIN:
			return;
		case EIO: //EIO ignored

		default:
			ERROREXIT("VIDIOC_DQBUF");
			break;
		}
	}

	assert(buf.index < this->n_buffers);

	void * ret;
	ret = memcpy(out_bytes, (this->buffers)[buf.index].start,
			this->buffer_size);
	if (-1 == xioctl(this->fd, VIDIOC_QBUF, &buf)){
		ERROREXIT("VIDIOC_QBUF");
	}

	return;
}

void CCv4l2_grab_next_frame_blocking(CCv4l2 *this, unsigned char *out_bytes,
		float timeout) {
	intptr_t stride = this->roi_width * this->inherited.depth / 8;
	CCv4l2_grab_next_frame_blocking_with_stride(this, out_bytes, stride,
			timeout);
}

void CCv4l2_point_next_frame_blocking(CCv4l2 *this, unsigned char **buf_ptr,
		float timeout) {
	NOT_IMPLEMENTED;
}

void CCv4l2_unpoint_frame(CCv4l2 *this) {
	NOT_IMPLEMENTED;
}

void CCv4l2_get_last_timestamp(CCv4l2 *this, double* timestamp) {
	/* from microseconds to seconds */
	long int secs = this->last_timestamp.tv_sec - this->start_timestamp.tv_sec;
	long int usecs = this->last_timestamp.tv_usec - this->start_timestamp.tv_usec;
	*timestamp = (double) (secs + (usecs * 1e-6));
}

void CCv4l2_get_last_framenumber(CCv4l2 *this, unsigned long* framenumber) {
	*framenumber = this->last_frame_id;
}

void CCv4l2_get_num_trigger_modes(CCv4l2 *this, int *num_trigger_modes) {
	*num_trigger_modes = 0;
}

void CCv4l2_get_trigger_mode_string(CCv4l2 *this, int trigger_mode_number,
		char* trigger_mode_string, //output parameter
		int trigger_mode_string_maxlen) {
	snprintf(trigger_mode_string, trigger_mode_string_maxlen, "%s", "");
}

void CCv4l2_get_trigger_mode_number(CCv4l2 *this, int *trigger_mode_number) {
	*trigger_mode_number = this->num_trigger_modes;
}

void CCv4l2_set_trigger_mode_number(CCv4l2 *this, int trigger_mode_number) {
	DPRINTF("set trigger mode: %d\n", trigger_mode_number);
	if (trigger_mode_number >= this->num_trigger_modes) {
		V4L2_ERROR(CAM_IFACE_HARDWARE_FEATURE_NOT_AVAILABLE,
				"unknown trigger mode");
		return;
	}
}

void CCv4l2_get_frame_roi(CCv4l2 *this, int *left, int *top, int* width,
		int* height) {
	*left = this->roi_left;
	*top = this->roi_top;
	*width = this->roi_width;
	*height = this->roi_height;
}

void CCv4l2_set_frame_roi(CCv4l2 *this, int left, int top, int width,
		int height) {

	DPRINTF("set roi: %d,%d %dx%d\n", left, top, width, height);
	//NOT_IMPLEMENTED;
}

void CCv4l2_get_framerate(CCv4l2 *this, float *framerate) {
	struct v4l2_frmivalenum frt;
	struct v4l2_format fmt;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_query_pix_format(this->dev_name, &fmt);
	frt.width = fmt.fmt.pix.width;
	frt.height = fmt.fmt.pix.height;
	frt.pixel_format = fmt.fmt.pix.pixelformat;
	v4l2_query_framerate(this->dev_name, &frt);
	*framerate = 1.0 * frt.discrete.denominator / frt.discrete.numerator;
}

void CCv4l2_set_framerate(CCv4l2 *this, float framerate) {
	//NOT_IMPLEMENTED;
}

void CCv4l2_get_max_frame_size(CCv4l2 *this, int *width, int *height) {
	struct v4l2_frmivalenum frt;
	struct v4l2_format fmt;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_query_pix_format(this->dev_name, &fmt);
	*width = fmt.fmt.pix.width;
	*height = fmt.fmt.pix.height;
}

void CCv4l2_get_buffer_size(CCv4l2 *this, int *size) {
	*size = this->buffer_size;
}

void CCv4l2_get_num_framebuffers(CCv4l2 *this, int *num_framebuffers) {
	*num_framebuffers = this->n_buffers;
}

void CCv4l2_set_num_framebuffers(CCv4l2 *this, int num_framebuffers) {
	NOT_IMPLEMENTED;
}

