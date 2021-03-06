#ifndef _MSM_KGSL_H
#define _MSM_KGSL_H

#ifndef CONFIG_MSM_KGSL_MAINLINE
#define KGSL_VERSION_MAJOR        3
#define KGSL_VERSION_MINOR        14

/*context flags */
#define KGSL_CONTEXT_SAVE_GMEM		0x00000001
#define KGSL_CONTEXT_NO_GMEM_ALLOC	0x00000002
#define KGSL_CONTEXT_SUBMIT_IB_LIST	0x00000004
#define KGSL_CONTEXT_CTX_SWITCH		0x00000008
#define KGSL_CONTEXT_PREAMBLE		0x00000010
#define KGSL_CONTEXT_TRASH_STATE	0x00000020
#define KGSL_CONTEXT_PER_CONTEXT_TS	0x00000040
#define KGSL_CONTEXT_USER_GENERATED_TS	0x00000080
#define KGSL_CONTEXT_NO_FAULT_TOLERANCE 0x00000200


#define KGSL_CONTEXT_INVALID 0xffffffff

/* Memory allocayion flags */
#define KGSL_MEMFLAGS_GPUREADONLY	0x01000000

#define KGSL_MEMTYPE_MASK		0x0000FF00
#define KGSL_MEMTYPE_SHIFT		8

/* Memory types for which allocations are made */
#define KGSL_MEMTYPE_OBJECTANY			0
#define KGSL_MEMTYPE_FRAMEBUFFER		1
#define KGSL_MEMTYPE_RENDERBUFFER		2
#define KGSL_MEMTYPE_ARRAYBUFFER		3
#define KGSL_MEMTYPE_ELEMENTARRAYBUFFER		4
#define KGSL_MEMTYPE_VERTEXARRAYBUFFER		5
#define KGSL_MEMTYPE_TEXTURE			6
#define KGSL_MEMTYPE_SURFACE			7
#define KGSL_MEMTYPE_EGL_SURFACE		8
#define KGSL_MEMTYPE_GL				9
#define KGSL_MEMTYPE_CL				10
#define KGSL_MEMTYPE_CL_BUFFER_MAP		11
#define KGSL_MEMTYPE_CL_BUFFER_NOMAP		12
#define KGSL_MEMTYPE_CL_IMAGE_MAP		13
#define KGSL_MEMTYPE_CL_IMAGE_NOMAP		14
#define KGSL_MEMTYPE_CL_KERNEL_STACK		15
#define KGSL_MEMTYPE_COMMAND			16
#define KGSL_MEMTYPE_2D				17
#define KGSL_MEMTYPE_EGL_IMAGE			18
#define KGSL_MEMTYPE_EGL_SHADOW			19
#define KGSL_MEMTYPE_MULTISAMPLE		20
#define KGSL_MEMTYPE_KERNEL			255

/*
 * Alignment hint, passed as the power of 2 exponent.
 * i.e 4k (2^12) would be 12, 64k (2^16)would be 16.
 */
#define KGSL_MEMALIGN_MASK		0x00FF0000
#define KGSL_MEMALIGN_SHIFT		16

/* generic flag values */
#define KGSL_FLAGS_NORMALMODE  0x00000000
#define KGSL_FLAGS_SAFEMODE    0x00000001
#define KGSL_FLAGS_INITIALIZED0 0x00000002
#define KGSL_FLAGS_INITIALIZED 0x00000004
#define KGSL_FLAGS_STARTED     0x00000008
#define KGSL_FLAGS_ACTIVE      0x00000010
#define KGSL_FLAGS_RESERVED0   0x00000020
#define KGSL_FLAGS_RESERVED1   0x00000040
#define KGSL_FLAGS_RESERVED2   0x00000080
#define KGSL_FLAGS_SOFT_RESET  0x00000100
#define KGSL_FLAGS_PER_CONTEXT_TIMESTAMPS 0x00000200

/* Clock flags to show which clocks should be controled by a given platform */
#define KGSL_CLK_SRC	0x00000001
#define KGSL_CLK_CORE	0x00000002
#define KGSL_CLK_IFACE	0x00000004
#define KGSL_CLK_MEM	0x00000008
#define KGSL_CLK_MEM_IFACE 0x00000010
#define KGSL_CLK_AXI	0x00000020

/* Server Side Sync Timeout in milliseconds */
#define KGSL_SYNCOBJ_SERVER_TIMEOUT 2000

/*
 * Reset status values for context
 */
enum kgsl_ctx_reset_stat {
	KGSL_CTX_STAT_NO_ERROR				= 0x00000000,
	KGSL_CTX_STAT_GUILTY_CONTEXT_RESET_EXT		= 0x00000001,
	KGSL_CTX_STAT_INNOCENT_CONTEXT_RESET_EXT	= 0x00000002,
	KGSL_CTX_STAT_UNKNOWN_CONTEXT_RESET_EXT		= 0x00000003
};

#define KGSL_CONVERT_TO_MBPS(val) \
	(val*1000*1000U)

/* device id */
enum kgsl_deviceid {
	KGSL_DEVICE_3D0		= 0x00000000,
	KGSL_DEVICE_2D0		= 0x00000001,
	KGSL_DEVICE_2D1		= 0x00000002,
	KGSL_DEVICE_MAX		= 0x00000003
};

enum kgsl_user_mem_type {
	KGSL_USER_MEM_TYPE_PMEM		= 0x00000000,
	KGSL_USER_MEM_TYPE_ASHMEM	= 0x00000001,
	KGSL_USER_MEM_TYPE_ADDR		= 0x00000002,
	KGSL_USER_MEM_TYPE_ION		= 0x00000003,
	KGSL_USER_MEM_TYPE_MAX		= 0x00000004,
};

struct kgsl_devinfo {

	unsigned int device_id;
	/* chip revision id
	* coreid:8 majorrev:8 minorrev:8 patch:8
	*/
	unsigned int chip_id;
	unsigned int mmu_enabled;
	unsigned int gmem_gpubaseaddr;
	/*
	* This field contains the adreno revision
	* number 200, 205, 220, etc...
	*/
	unsigned int gpu_id;
	unsigned int gmem_sizebytes;
};

/* this structure defines the region of memory that can be mmap()ed from this
   driver. The timestamp fields are volatile because they are written by the
   GPU
*/
struct kgsl_devmemstore {
	volatile unsigned int soptimestamp;
	unsigned int sbz;
	volatile unsigned int eoptimestamp;
	unsigned int sbz2;
	volatile unsigned int ts_cmp_enable;
	unsigned int sbz3;
	volatile unsigned int ref_wait_ts;
	unsigned int sbz4;
	unsigned int current_context;
	unsigned int sbz5;
};

#define KGSL_MEMSTORE_OFFSET(ctxt_id, field) \
	((ctxt_id)*sizeof(struct kgsl_devmemstore) + \
	 offsetof(struct kgsl_devmemstore, field))

/* timestamp id*/
enum kgsl_timestamp_type {
	KGSL_TIMESTAMP_CONSUMED = 0x00000001, /* start-of-pipeline timestamp */
	KGSL_TIMESTAMP_RETIRED  = 0x00000002, /* end-of-pipeline timestamp*/
	KGSL_TIMESTAMP_QUEUED   = 0x00000003,
};

/* property types - used with kgsl_device_getproperty */
enum kgsl_property_type {
	KGSL_PROP_DEVICE_INFO     = 0x00000001,
	KGSL_PROP_DEVICE_SHADOW   = 0x00000002,
	KGSL_PROP_DEVICE_POWER    = 0x00000003,
	KGSL_PROP_SHMEM           = 0x00000004,
	KGSL_PROP_SHMEM_APERTURES = 0x00000005,
	KGSL_PROP_MMU_ENABLE 	  = 0x00000006,
	KGSL_PROP_INTERRUPT_WAITS = 0x00000007,
	KGSL_PROP_VERSION         = 0x00000008,
	KGSL_PROP_GPU_RESET_STAT  = 0x00000009,
	KGSL_PROP_PWRCTRL         = 0x0000000E,
	KGSL_PROP_FAULT_TOLERANCE = 0x00000011,
};

/* Fault Tolerance policy flags */
#define  KGSL_FT_DISABLE                  0x00000001
#define  KGSL_FT_REPLAY                   0x00000002
#define  KGSL_FT_SKIPIB                   0x00000004
#define  KGSL_FT_SKIPFRAME                0x00000008
#define  KGSL_FT_DEFAULT_POLICY           (KGSL_FT_REPLAY + KGSL_FT_SKIPIB)

/* Pagefault policy flags */
#define KGSL_FT_PAGEFAULT_INT_ENABLE         0x00000001
#define KGSL_FT_PAGEFAULT_GPUHALT_ENABLE     0x00000002
#define KGSL_FT_PAGEFAULT_LOG_ONE_PER_PAGE   0x00000004
#define KGSL_FT_PAGEFAULT_LOG_ONE_PER_INT    0x00000008
#define KGSL_FT_PAGEFAULT_DEFAULT_POLICY     (KGSL_FT_PAGEFAULT_INT_ENABLE + \
					KGSL_FT_PAGEFAULT_LOG_ONE_PER_PAGE)

/* Fault tolerance config */
struct kgsl_ft_config {
	unsigned int ft_policy;    /* Fault Tolerance policy flags */
	unsigned int ft_pf_policy; /* Pagefault policy flags */
	unsigned int ft_pm_dump;   /* KGSL enable postmortem dump */
	unsigned int ft_detect_ms;
	unsigned int ft_dos_timeout_ms;
};

struct kgsl_shadowprop {
	unsigned int gpuaddr;
	unsigned int size;
	unsigned int flags; /* contains KGSL_FLAGS_ values */
};

struct kgsl_version {
	unsigned int drv_major;
	unsigned int drv_minor;
	unsigned int dev_major;
	unsigned int dev_minor;
};

/* structure holds list of ibs */
struct kgsl_ibdesc {
	unsigned int gpuaddr;
	void *hostptr;
	unsigned int sizedwords;
	unsigned int ctrl;
};

/* ioctls */
#define KGSL_IOC_TYPE 0x09

/* get misc info about the GPU
   type should be a value from enum kgsl_property_type
   value points to a structure that varies based on type
   sizebytes is sizeof() that structure
   for KGSL_PROP_DEVICE_INFO, use struct kgsl_devinfo
   this structure contaings hardware versioning info.
   for KGSL_PROP_DEVICE_SHADOW, use struct kgsl_shadowprop
   this is used to find mmap() offset and sizes for mapping
   struct kgsl_memstore into userspace.
*/
struct kgsl_device_getproperty {
	unsigned int type;
	void  *value;
	unsigned int sizebytes;
};

#define IOCTL_KGSL_DEVICE_GETPROPERTY \
	_IOWR(KGSL_IOC_TYPE, 0x2, struct kgsl_device_getproperty)

/* IOCTL_KGSL_DEVICE_READ (0x3) - removed 03/2012
 */

/* block until the GPU has executed past a given timestamp
 * timeout is in milliseconds.
 */
struct kgsl_device_waittimestamp {
	unsigned int timestamp;
	unsigned int timeout;
};

#define IOCTL_KGSL_DEVICE_WAITTIMESTAMP \
	_IOW(KGSL_IOC_TYPE, 0x6, struct kgsl_device_waittimestamp)

struct kgsl_device_waittimestamp_ctxtid {
	unsigned int context_id;
	unsigned int timestamp;
	unsigned int timeout;
};

#define IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID \
	_IOW(KGSL_IOC_TYPE, 0x7, struct kgsl_device_waittimestamp_ctxtid)

/* issue indirect commands to the GPU.
 * drawctxt_id must have been created with IOCTL_KGSL_DRAWCTXT_CREATE
 * ibaddr and sizedwords must specify a subset of a buffer created
 * with IOCTL_KGSL_SHAREDMEM_FROM_PMEM
 * flags may be a mask of KGSL_CONTEXT_ values
 * timestamp is a returned counter value which can be passed to
 * other ioctls to determine when the commands have been executed by
 * the GPU.
 */
struct kgsl_ringbuffer_issueibcmds {
	unsigned int drawctxt_id;
	unsigned int ibdesc_addr;
	unsigned int numibs;
	unsigned int timestamp; /*output param */
	unsigned int flags;
};

#define IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS \
	_IOWR(KGSL_IOC_TYPE, 0x10, struct kgsl_ringbuffer_issueibcmds)

/* read the most recently executed timestamp value
 * type should be a value from enum kgsl_timestamp_type
 */
struct kgsl_cmdstream_readtimestamp {
	unsigned int type;
	unsigned int timestamp; /*output param */
};

#define IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_OLD \
	_IOR(KGSL_IOC_TYPE, 0x11, struct kgsl_cmdstream_readtimestamp)

#define IOCTL_KGSL_CMDSTREAM_READTIMESTAMP \
	_IOWR(KGSL_IOC_TYPE, 0x11, struct kgsl_cmdstream_readtimestamp)

/* free memory when the GPU reaches a given timestamp.
 * gpuaddr specify a memory region created by a
 * IOCTL_KGSL_SHAREDMEM_FROM_PMEM call
 * type should be a value from enum kgsl_timestamp_type
 */
struct kgsl_cmdstream_freememontimestamp {
	unsigned int gpuaddr;
	unsigned int type;
	unsigned int timestamp;
};

#define IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP \
	_IOW(KGSL_IOC_TYPE, 0x12, struct kgsl_cmdstream_freememontimestamp)

/* Previous versions of this header had incorrectly defined
   IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP as a read-only ioctl instead
   of a write only ioctl.  To ensure binary compatability, the following
   #define will be used to intercept the incorrect ioctl
*/

#define IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP_OLD \
	_IOR(KGSL_IOC_TYPE, 0x12, struct kgsl_cmdstream_freememontimestamp)

/* create a draw context, which is used to preserve GPU state.
 * The flags field may contain a mask KGSL_CONTEXT_*  values
 */
struct kgsl_drawctxt_create {
	unsigned int flags;
	unsigned int drawctxt_id; /*output param */
};

#define IOCTL_KGSL_DRAWCTXT_CREATE \
	_IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)

/* destroy a draw context */
struct kgsl_drawctxt_destroy {
	unsigned int drawctxt_id;
};

#define IOCTL_KGSL_DRAWCTXT_DESTROY \
	_IOW(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)

/* add a block of pmem, fb, ashmem or user allocated address
 * into the GPU address space */
struct kgsl_map_user_mem {
	int fd;
	unsigned int gpuaddr;   /*output param */
	unsigned int len;
	unsigned int offset;
	unsigned int hostptr;   /*input param */
	enum kgsl_user_mem_type memtype;
	unsigned int flags;
};

#define IOCTL_KGSL_MAP_USER_MEM \
	_IOWR(KGSL_IOC_TYPE, 0x15, struct kgsl_map_user_mem)

struct kgsl_cmdstream_readtimestamp_ctxtid {
	unsigned int context_id;
	unsigned int type;
	unsigned int timestamp; /*output param */
};

#define IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID \
	_IOWR(KGSL_IOC_TYPE, 0x16, struct kgsl_cmdstream_readtimestamp_ctxtid)

struct kgsl_cmdstream_freememontimestamp_ctxtid {
	unsigned int context_id;
	unsigned int gpuaddr;
	unsigned int type;
	unsigned int timestamp;
};

#define IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP_CTXTID \
	_IOW(KGSL_IOC_TYPE, 0x17, \
	struct kgsl_cmdstream_freememontimestamp_ctxtid)

/* add a block of pmem or fb into the GPU address space */
struct kgsl_sharedmem_from_pmem {
	int pmem_fd;
	unsigned int gpuaddr;	/*output param */
	unsigned int len;
	unsigned int offset;
};

#define IOCTL_KGSL_SHAREDMEM_FROM_PMEM \
	_IOWR(KGSL_IOC_TYPE, 0x20, struct kgsl_sharedmem_from_pmem)

/* remove memory from the GPU's address space */
struct kgsl_sharedmem_free {
	unsigned int gpuaddr;
};

#define IOCTL_KGSL_SHAREDMEM_FREE \
	_IOW(KGSL_IOC_TYPE, 0x21, struct kgsl_sharedmem_free)

struct kgsl_cff_user_event {
	unsigned char cff_opcode;
	unsigned int op1;
	unsigned int op2;
	unsigned int op3;
	unsigned int op4;
	unsigned int op5;
	unsigned int __pad[2];
};

#define IOCTL_KGSL_CFF_USER_EVENT \
	_IOW(KGSL_IOC_TYPE, 0x31, struct kgsl_cff_user_event)

struct kgsl_gmem_desc {
	unsigned int x;
	unsigned int y;
	unsigned int width;
	unsigned int height;
	unsigned int pitch;
};

struct kgsl_buffer_desc {
	void 			*hostptr;
	unsigned int	gpuaddr;
	int				size;
	unsigned int	format;
	unsigned int  	pitch;
	unsigned int  	enabled;
};

struct kgsl_bind_gmem_shadow {
	unsigned int drawctxt_id;
	struct kgsl_gmem_desc gmem_desc;
	unsigned int shadow_x;
	unsigned int shadow_y;
	struct kgsl_buffer_desc shadow_buffer;
	unsigned int buffer_id;
};

#define IOCTL_KGSL_DRAWCTXT_BIND_GMEM_SHADOW \
    _IOW(KGSL_IOC_TYPE, 0x22, struct kgsl_bind_gmem_shadow)

/* add a block of memory into the GPU address space */

/*
 * IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC deprecated 09/2012
 * use IOCTL_KGSL_GPUMEM_ALLOC instead
 */

struct kgsl_sharedmem_from_vmalloc {
	unsigned int gpuaddr;	/*output param */
	unsigned int hostptr;
	unsigned int flags;
};

#define IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC \
	_IOWR(KGSL_IOC_TYPE, 0x23, struct kgsl_sharedmem_from_vmalloc)

#define IOCTL_KGSL_SHAREDMEM_FLUSH_CACHE \
	_IOW(KGSL_IOC_TYPE, 0x24, struct kgsl_sharedmem_free)

struct kgsl_drawctxt_set_bin_base_offset {
	unsigned int drawctxt_id;
	unsigned int offset;
};

#define IOCTL_KGSL_DRAWCTXT_SET_BIN_BASE_OFFSET \
	_IOW(KGSL_IOC_TYPE, 0x25, struct kgsl_drawctxt_set_bin_base_offset)

enum kgsl_cmdwindow_type {
	KGSL_CMDWINDOW_MIN     = 0x00000000,
	KGSL_CMDWINDOW_2D      = 0x00000000,
	KGSL_CMDWINDOW_3D      = 0x00000001, /* legacy */
	KGSL_CMDWINDOW_MMU     = 0x00000002,
	KGSL_CMDWINDOW_ARBITER = 0x000000FF,
	KGSL_CMDWINDOW_MAX     = 0x000000FF,
};

/* write to the command window */
struct kgsl_cmdwindow_write {
	enum kgsl_cmdwindow_type target;
	unsigned int addr;
	unsigned int data;
};

#define IOCTL_KGSL_CMDWINDOW_WRITE \
	_IOW(KGSL_IOC_TYPE, 0x2e, struct kgsl_cmdwindow_write)

struct kgsl_gpumem_alloc {
	unsigned long gpuaddr;
	size_t size;
	unsigned int flags;
};

#define IOCTL_KGSL_GPUMEM_ALLOC \
	_IOWR(KGSL_IOC_TYPE, 0x2f, struct kgsl_gpumem_alloc)

struct kgsl_cff_syncmem {
	unsigned int gpuaddr;
	unsigned int len;
	unsigned int __pad[2]; /* For future binary compatibility */
};

#define IOCTL_KGSL_CFF_SYNCMEM \
	_IOW(KGSL_IOC_TYPE, 0x30, struct kgsl_cff_syncmem)

/*
 * A timestamp event allows the user space to register an action following an
 * expired timestamp. Note IOCTL_KGSL_TIMESTAMP_EVENT has been redefined to
 * _IOWR to support fences which need to return a fd for the priv parameter.
 */

struct kgsl_timestamp_event {
	int type;                /* Type of event (see list below) */
	unsigned int timestamp;  /* Timestamp to trigger event on */
	unsigned int context_id; /* Context for the timestamp */
	void *priv;              /* Pointer to the event specific blob */
	size_t len;              /* Size of the event specific blob */
};

#define IOCTL_KGSL_TIMESTAMP_EVENT_OLD \
	_IOW(KGSL_IOC_TYPE, 0x31, struct kgsl_timestamp_event)

/* A genlock timestamp event releases an existing lock on timestamp expire */

#define KGSL_TIMESTAMP_EVENT_GENLOCK 1

struct kgsl_timestamp_event_genlock {
	int handle; /* Handle of the genlock lock to release */
};

/* A fence timestamp event releases an existing lock on timestamp expire */

#define KGSL_TIMESTAMP_EVENT_FENCE 2

struct kgsl_timestamp_event_fence {
	int fence_fd; /* Fence to signal */
};

/*
 * Set a property within the kernel.  Uses the same structure as
 * IOCTL_KGSL_GETPROPERTY
 */

#define IOCTL_KGSL_SETPROPERTY \
	_IOW(KGSL_IOC_TYPE, 0x32, struct kgsl_device_getproperty)

#define IOCTL_KGSL_TIMESTAMP_EVENT \
	_IOWR(KGSL_IOC_TYPE, 0x33, struct kgsl_timestamp_event)

/*
 * kgsl_get_alloc_size - acquire memory size allocated in kernel space by kgsl
 * @detailed: to indicate if need detailed info,
 *  set detailed to 1, if need the detailed info
 *  of each gfx process for different type memory usage,
 *  said ion, ashmem, kernel ...etc
 *
 * Returns allocated memory size and prints kgsl meminfo in kernel log if set detailed
 */
unsigned int kgsl_get_alloc_size(int detailed);

#ifdef __KERNEL__
#ifdef CONFIG_MSM_KGSL_DRM
int kgsl_gem_obj_addr(int drm_fd, int handle, unsigned long *start,
			unsigned long *len);
#else
#define kgsl_gem_obj_addr(...) 0
#endif
#endif
#else
/*
 * The KGSL version has proven not to be very useful in userspace if features
 * are cherry picked into other trees out of order so it is frozen as of 3.14.
 * It is left here for backwards compatabilty and as a reminder that
 * software releases are never linear. Also, I like pie.
 */

#define KGSL_VERSION_MAJOR        3
#define KGSL_VERSION_MINOR        14

/*context flags */
#define KGSL_CONTEXT_SAVE_GMEM		  0x00000001
#define KGSL_CONTEXT_NO_GMEM_ALLOC	  0x00000002
#define KGSL_CONTEXT_SUBMIT_IB_LIST	  0x00000004
#define KGSL_CONTEXT_CTX_SWITCH		  0x00000008
#define KGSL_CONTEXT_PREAMBLE		  0x00000010
#define KGSL_CONTEXT_TRASH_STATE	  0x00000020
#define KGSL_CONTEXT_PER_CONTEXT_TS	  0x00000040
#define KGSL_CONTEXT_USER_GENERATED_TS	  0x00000080
#define KGSL_CONTEXT_END_OF_FRAME         0x00000100
#define KGSL_CONTEXT_NO_FAULT_TOLERANCE	  0x00000200
/* bits [12:15] are reserved for future use */
#define KGSL_CONTEXT_TYPE_MASK            0x01F00000
#define KGSL_CONTEXT_TYPE_SHIFT           20

#define KGSL_CONTEXT_TYPE_ANY		  0
#define KGSL_CONTEXT_TYPE_GL		  1
#define KGSL_CONTEXT_TYPE_CL		  2
#define KGSL_CONTEXT_TYPE_C2D		  3
#define KGSL_CONTEXT_TYPE_RS		  4

#define KGSL_CONTEXT_INVALID 0xffffffff

/* --- Memory allocation flags --- */

/* General allocation hints */
#define KGSL_MEMFLAGS_GPUREADONLY 0x01000000
#define KGSL_MEMFLAGS_USE_CPU_MAP 0x10000000

/* Memory caching hints */
#define KGSL_CACHEMODE_MASK 0x0C000000
#define KGSL_CACHEMODE_SHIFT 26

#define KGSL_CACHEMODE_WRITECOMBINE 0
#define KGSL_CACHEMODE_UNCACHED 1
#define KGSL_CACHEMODE_WRITETHROUGH 2
#define KGSL_CACHEMODE_WRITEBACK 3

/* Memory types for which allocations are made */
#define KGSL_MEMTYPE_MASK		0x0000FF00
#define KGSL_MEMTYPE_SHIFT		8

#define KGSL_MEMTYPE_OBJECTANY			0
#define KGSL_MEMTYPE_FRAMEBUFFER		1
#define KGSL_MEMTYPE_RENDERBUFFER		2
#define KGSL_MEMTYPE_ARRAYBUFFER		3
#define KGSL_MEMTYPE_ELEMENTARRAYBUFFER		4
#define KGSL_MEMTYPE_VERTEXARRAYBUFFER		5
#define KGSL_MEMTYPE_TEXTURE			6
#define KGSL_MEMTYPE_SURFACE			7
#define KGSL_MEMTYPE_EGL_SURFACE		8
#define KGSL_MEMTYPE_GL				9
#define KGSL_MEMTYPE_CL				10
#define KGSL_MEMTYPE_CL_BUFFER_MAP		11
#define KGSL_MEMTYPE_CL_BUFFER_NOMAP		12
#define KGSL_MEMTYPE_CL_IMAGE_MAP		13
#define KGSL_MEMTYPE_CL_IMAGE_NOMAP		14
#define KGSL_MEMTYPE_CL_KERNEL_STACK		15
#define KGSL_MEMTYPE_COMMAND			16
#define KGSL_MEMTYPE_2D				17
#define KGSL_MEMTYPE_EGL_IMAGE			18
#define KGSL_MEMTYPE_EGL_SHADOW			19
#define KGSL_MEMTYPE_MULTISAMPLE		20
#define KGSL_MEMTYPE_KERNEL			255

/*
 * Alignment hint, passed as the power of 2 exponent.
 * i.e 4k (2^12) would be 12, 64k (2^16)would be 16.
 */
#define KGSL_MEMALIGN_MASK		0x00FF0000
#define KGSL_MEMALIGN_SHIFT		16

/* --- generic KGSL flag values --- */

#define KGSL_FLAGS_NORMALMODE  0x00000000
#define KGSL_FLAGS_SAFEMODE    0x00000001
#define KGSL_FLAGS_INITIALIZED0 0x00000002
#define KGSL_FLAGS_INITIALIZED 0x00000004
#define KGSL_FLAGS_STARTED     0x00000008
#define KGSL_FLAGS_ACTIVE      0x00000010
#define KGSL_FLAGS_RESERVED0   0x00000020
#define KGSL_FLAGS_RESERVED1   0x00000040
#define KGSL_FLAGS_RESERVED2   0x00000080
#define KGSL_FLAGS_SOFT_RESET  0x00000100
#define KGSL_FLAGS_PER_CONTEXT_TIMESTAMPS 0x00000200

/* Clock flags to show which clocks should be controled by a given platform */
#define KGSL_CLK_SRC	0x00000001
#define KGSL_CLK_CORE	0x00000002
#define KGSL_CLK_IFACE	0x00000004
#define KGSL_CLK_MEM	0x00000008
#define KGSL_CLK_MEM_IFACE 0x00000010
#define KGSL_CLK_AXI	0x00000020

/* Server Side Sync Timeout in milliseconds */
#define KGSL_SYNCOBJ_SERVER_TIMEOUT 2000

/*
 * Reset status values for context
 */
enum kgsl_ctx_reset_stat {
	KGSL_CTX_STAT_NO_ERROR				= 0x00000000,
	KGSL_CTX_STAT_GUILTY_CONTEXT_RESET_EXT		= 0x00000001,
	KGSL_CTX_STAT_INNOCENT_CONTEXT_RESET_EXT	= 0x00000002,
	KGSL_CTX_STAT_UNKNOWN_CONTEXT_RESET_EXT		= 0x00000003
};

#define KGSL_CONVERT_TO_MBPS(val) \
	(val*1000*1000U)

/* device id */
enum kgsl_deviceid {
	KGSL_DEVICE_3D0		= 0x00000000,
	KGSL_DEVICE_2D0		= 0x00000001,
	KGSL_DEVICE_2D1		= 0x00000002,
	KGSL_DEVICE_MAX		= 0x00000003
};

enum kgsl_user_mem_type {
	KGSL_USER_MEM_TYPE_PMEM		= 0x00000000,
	KGSL_USER_MEM_TYPE_ASHMEM	= 0x00000001,
	KGSL_USER_MEM_TYPE_ADDR		= 0x00000002,
	KGSL_USER_MEM_TYPE_ION		= 0x00000003,
	KGSL_USER_MEM_TYPE_MAX		= 0x00000004,
};

struct kgsl_devinfo {

	unsigned int device_id;
	/* chip revision id
	* coreid:8 majorrev:8 minorrev:8 patch:8
	*/
	unsigned int chip_id;
	unsigned int mmu_enabled;
	unsigned int gmem_gpubaseaddr;
	/*
	* This field contains the adreno revision
	* number 200, 205, 220, etc...
	*/
	unsigned int gpu_id;
	unsigned int gmem_sizebytes;
};

/* this structure defines the region of memory that can be mmap()ed from this
   driver. The timestamp fields are volatile because they are written by the
   GPU
*/
struct kgsl_devmemstore {
	volatile unsigned int soptimestamp;
	unsigned int sbz;
	volatile unsigned int eoptimestamp;
	unsigned int sbz2;
	volatile unsigned int ts_cmp_enable;
	unsigned int sbz3;
	volatile unsigned int ref_wait_ts;
	unsigned int sbz4;
	unsigned int current_context;
	unsigned int sbz5;
};

#define KGSL_MEMSTORE_OFFSET(ctxt_id, field) \
	((ctxt_id)*sizeof(struct kgsl_devmemstore) + \
	 offsetof(struct kgsl_devmemstore, field))

/* timestamp id*/
enum kgsl_timestamp_type {
	KGSL_TIMESTAMP_CONSUMED = 0x00000001, /* start-of-pipeline timestamp */
	KGSL_TIMESTAMP_RETIRED  = 0x00000002, /* end-of-pipeline timestamp*/
	KGSL_TIMESTAMP_QUEUED   = 0x00000003,
};

/* property types - used with kgsl_device_getproperty */
enum kgsl_property_type {
	KGSL_PROP_DEVICE_INFO     = 0x00000001,
	KGSL_PROP_DEVICE_SHADOW   = 0x00000002,
	KGSL_PROP_DEVICE_POWER    = 0x00000003,
	KGSL_PROP_SHMEM           = 0x00000004,
	KGSL_PROP_SHMEM_APERTURES = 0x00000005,
	KGSL_PROP_MMU_ENABLE 	  = 0x00000006,
	KGSL_PROP_INTERRUPT_WAITS = 0x00000007,
	KGSL_PROP_VERSION         = 0x00000008,
	KGSL_PROP_GPU_RESET_STAT  = 0x00000009,
	KGSL_PROP_PWRCTRL         = 0x0000000E,
};

struct kgsl_shadowprop {
	unsigned int gpuaddr;
	unsigned int size;
	unsigned int flags; /* contains KGSL_FLAGS_ values */
};

struct kgsl_version {
	unsigned int drv_major;
	unsigned int drv_minor;
	unsigned int dev_major;
	unsigned int dev_minor;
};

/* Performance counter groups */

#define KGSL_PERFCOUNTER_GROUP_CP 0x0
#define KGSL_PERFCOUNTER_GROUP_RBBM 0x1
#define KGSL_PERFCOUNTER_GROUP_PC 0x2
#define KGSL_PERFCOUNTER_GROUP_VFD 0x3
#define KGSL_PERFCOUNTER_GROUP_HLSQ 0x4
#define KGSL_PERFCOUNTER_GROUP_VPC 0x5
#define KGSL_PERFCOUNTER_GROUP_TSE 0x6
#define KGSL_PERFCOUNTER_GROUP_RAS 0x7
#define KGSL_PERFCOUNTER_GROUP_UCHE 0x8
#define KGSL_PERFCOUNTER_GROUP_TP 0x9
#define KGSL_PERFCOUNTER_GROUP_SP 0xA
#define KGSL_PERFCOUNTER_GROUP_RB 0xB
#define KGSL_PERFCOUNTER_GROUP_PWR 0xC
#define KGSL_PERFCOUNTER_GROUP_VBIF 0xD
#define KGSL_PERFCOUNTER_GROUP_VBIF_PWR 0xE

#define KGSL_PERFCOUNTER_NOT_USED 0xFFFFFFFF

/* structure holds list of ibs */
struct kgsl_ibdesc {
	unsigned int gpuaddr;
	void *hostptr;
	unsigned int sizedwords;
	unsigned int ctrl;
};

/* ioctls */
#define KGSL_IOC_TYPE 0x09

/* get misc info about the GPU
   type should be a value from enum kgsl_property_type
   value points to a structure that varies based on type
   sizebytes is sizeof() that structure
   for KGSL_PROP_DEVICE_INFO, use struct kgsl_devinfo
   this structure contaings hardware versioning info.
   for KGSL_PROP_DEVICE_SHADOW, use struct kgsl_shadowprop
   this is used to find mmap() offset and sizes for mapping
   struct kgsl_memstore into userspace.
*/
struct kgsl_device_getproperty {
	unsigned int type;
	void  *value;
	unsigned int sizebytes;
};

#define IOCTL_KGSL_DEVICE_GETPROPERTY \
	_IOWR(KGSL_IOC_TYPE, 0x2, struct kgsl_device_getproperty)

/* IOCTL_KGSL_DEVICE_READ (0x3) - removed 03/2012
 */

/* block until the GPU has executed past a given timestamp
 * timeout is in milliseconds.
 */
struct kgsl_device_waittimestamp {
	unsigned int timestamp;
	unsigned int timeout;
};

#define IOCTL_KGSL_DEVICE_WAITTIMESTAMP \
	_IOW(KGSL_IOC_TYPE, 0x6, struct kgsl_device_waittimestamp)

struct kgsl_device_waittimestamp_ctxtid {
	unsigned int context_id;
	unsigned int timestamp;
	unsigned int timeout;
};

#define IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID \
	_IOW(KGSL_IOC_TYPE, 0x7, struct kgsl_device_waittimestamp_ctxtid)

/* issue indirect commands to the GPU.
 * drawctxt_id must have been created with IOCTL_KGSL_DRAWCTXT_CREATE
 * ibaddr and sizedwords must specify a subset of a buffer created
 * with IOCTL_KGSL_SHAREDMEM_FROM_PMEM
 * flags may be a mask of KGSL_CONTEXT_ values
 * timestamp is a returned counter value which can be passed to
 * other ioctls to determine when the commands have been executed by
 * the GPU.
 */
struct kgsl_ringbuffer_issueibcmds {
	unsigned int drawctxt_id;
	unsigned int ibdesc_addr;
	unsigned int numibs;
	unsigned int timestamp; /*output param */
	unsigned int flags;
};

#define IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS \
	_IOWR(KGSL_IOC_TYPE, 0x10, struct kgsl_ringbuffer_issueibcmds)

/* read the most recently executed timestamp value
 * type should be a value from enum kgsl_timestamp_type
 */
struct kgsl_cmdstream_readtimestamp {
	unsigned int type;
	unsigned int timestamp; /*output param */
};

#define IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_OLD \
	_IOR(KGSL_IOC_TYPE, 0x11, struct kgsl_cmdstream_readtimestamp)

#define IOCTL_KGSL_CMDSTREAM_READTIMESTAMP \
	_IOWR(KGSL_IOC_TYPE, 0x11, struct kgsl_cmdstream_readtimestamp)

/* free memory when the GPU reaches a given timestamp.
 * gpuaddr specify a memory region created by a
 * IOCTL_KGSL_SHAREDMEM_FROM_PMEM call
 * type should be a value from enum kgsl_timestamp_type
 */
struct kgsl_cmdstream_freememontimestamp {
	unsigned int gpuaddr;
	unsigned int type;
	unsigned int timestamp;
};

#define IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP \
	_IOW(KGSL_IOC_TYPE, 0x12, struct kgsl_cmdstream_freememontimestamp)

/* Previous versions of this header had incorrectly defined
   IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP as a read-only ioctl instead
   of a write only ioctl.  To ensure binary compatability, the following
   #define will be used to intercept the incorrect ioctl
*/

#define IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP_OLD \
	_IOR(KGSL_IOC_TYPE, 0x12, struct kgsl_cmdstream_freememontimestamp)

/* create a draw context, which is used to preserve GPU state.
 * The flags field may contain a mask KGSL_CONTEXT_*  values
 */
struct kgsl_drawctxt_create {
	unsigned int flags;
	unsigned int drawctxt_id; /*output param */
};

#define IOCTL_KGSL_DRAWCTXT_CREATE \
	_IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)

/* destroy a draw context */
struct kgsl_drawctxt_destroy {
	unsigned int drawctxt_id;
};

#define IOCTL_KGSL_DRAWCTXT_DESTROY \
	_IOW(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)

/* add a block of pmem, fb, ashmem or user allocated address
 * into the GPU address space */
struct kgsl_map_user_mem {
	int fd;
	unsigned int gpuaddr;   /*output param */
	unsigned int len;
	unsigned int offset;
	unsigned int hostptr;   /*input param */
	enum kgsl_user_mem_type memtype;
	unsigned int flags;
};

#define IOCTL_KGSL_MAP_USER_MEM \
	_IOWR(KGSL_IOC_TYPE, 0x15, struct kgsl_map_user_mem)

struct kgsl_cmdstream_readtimestamp_ctxtid {
	unsigned int context_id;
	unsigned int type;
	unsigned int timestamp; /*output param */
};

#define IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID \
	_IOWR(KGSL_IOC_TYPE, 0x16, struct kgsl_cmdstream_readtimestamp_ctxtid)

struct kgsl_cmdstream_freememontimestamp_ctxtid {
	unsigned int context_id;
	unsigned int gpuaddr;
	unsigned int type;
	unsigned int timestamp;
};

#define IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP_CTXTID \
	_IOW(KGSL_IOC_TYPE, 0x17, \
	struct kgsl_cmdstream_freememontimestamp_ctxtid)

/* add a block of pmem or fb into the GPU address space */
struct kgsl_sharedmem_from_pmem {
	int pmem_fd;
	unsigned int gpuaddr;	/*output param */
	unsigned int len;
	unsigned int offset;
};

#define IOCTL_KGSL_SHAREDMEM_FROM_PMEM \
	_IOWR(KGSL_IOC_TYPE, 0x20, struct kgsl_sharedmem_from_pmem)

/* remove memory from the GPU's address space */
struct kgsl_sharedmem_free {
	unsigned int gpuaddr;
};

#define IOCTL_KGSL_SHAREDMEM_FREE \
	_IOW(KGSL_IOC_TYPE, 0x21, struct kgsl_sharedmem_free)

struct kgsl_cff_user_event {
	unsigned char cff_opcode;
	unsigned int op1;
	unsigned int op2;
	unsigned int op3;
	unsigned int op4;
	unsigned int op5;
	unsigned int __pad[2];
};

#define IOCTL_KGSL_CFF_USER_EVENT \
	_IOW(KGSL_IOC_TYPE, 0x31, struct kgsl_cff_user_event)

struct kgsl_gmem_desc {
	unsigned int x;
	unsigned int y;
	unsigned int width;
	unsigned int height;
	unsigned int pitch;
};

struct kgsl_buffer_desc {
	void 			*hostptr;
	unsigned int	gpuaddr;
	int				size;
	unsigned int	format;
	unsigned int  	pitch;
	unsigned int  	enabled;
};

struct kgsl_bind_gmem_shadow {
	unsigned int drawctxt_id;
	struct kgsl_gmem_desc gmem_desc;
	unsigned int shadow_x;
	unsigned int shadow_y;
	struct kgsl_buffer_desc shadow_buffer;
	unsigned int buffer_id;
};

#define IOCTL_KGSL_DRAWCTXT_BIND_GMEM_SHADOW \
    _IOW(KGSL_IOC_TYPE, 0x22, struct kgsl_bind_gmem_shadow)

/* add a block of memory into the GPU address space */

/*
 * IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC deprecated 09/2012
 * use IOCTL_KGSL_GPUMEM_ALLOC instead
 */

struct kgsl_sharedmem_from_vmalloc {
	unsigned int gpuaddr;	/*output param */
	unsigned int hostptr;
	unsigned int flags;
};

#define IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC \
	_IOWR(KGSL_IOC_TYPE, 0x23, struct kgsl_sharedmem_from_vmalloc)

/*
 * This is being deprecated in favor of IOCTL_KGSL_GPUMEM_CACHE_SYNC which
 * supports both directions (flush and invalidate). This code will still
 * work, but by definition it will do a flush of the cache which might not be
 * what you want to have happen on a buffer following a GPU operation.  It is
 * safer to go with IOCTL_KGSL_GPUMEM_CACHE_SYNC
 */

#define IOCTL_KGSL_SHAREDMEM_FLUSH_CACHE \
	_IOW(KGSL_IOC_TYPE, 0x24, struct kgsl_sharedmem_free)

struct kgsl_drawctxt_set_bin_base_offset {
	unsigned int drawctxt_id;
	unsigned int offset;
};

#define IOCTL_KGSL_DRAWCTXT_SET_BIN_BASE_OFFSET \
	_IOW(KGSL_IOC_TYPE, 0x25, struct kgsl_drawctxt_set_bin_base_offset)

enum kgsl_cmdwindow_type {
	KGSL_CMDWINDOW_MIN     = 0x00000000,
	KGSL_CMDWINDOW_2D      = 0x00000000,
	KGSL_CMDWINDOW_3D      = 0x00000001, /* legacy */
	KGSL_CMDWINDOW_MMU     = 0x00000002,
	KGSL_CMDWINDOW_ARBITER = 0x000000FF,
	KGSL_CMDWINDOW_MAX     = 0x000000FF,
};

/* write to the command window */
struct kgsl_cmdwindow_write {
	enum kgsl_cmdwindow_type target;
	unsigned int addr;
	unsigned int data;
};

#define IOCTL_KGSL_CMDWINDOW_WRITE \
	_IOW(KGSL_IOC_TYPE, 0x2e, struct kgsl_cmdwindow_write)

struct kgsl_gpumem_alloc {
	unsigned long gpuaddr;
	size_t size;
	unsigned int flags;
};

#define IOCTL_KGSL_GPUMEM_ALLOC \
	_IOWR(KGSL_IOC_TYPE, 0x2f, struct kgsl_gpumem_alloc)

struct kgsl_cff_syncmem {
	unsigned int gpuaddr;
	unsigned int len;
	unsigned int __pad[2]; /* For future binary compatibility */
};

#define IOCTL_KGSL_CFF_SYNCMEM \
	_IOW(KGSL_IOC_TYPE, 0x30, struct kgsl_cff_syncmem)

/*
 * A timestamp event allows the user space to register an action following an
 * expired timestamp. Note IOCTL_KGSL_TIMESTAMP_EVENT has been redefined to
 * _IOWR to support fences which need to return a fd for the priv parameter.
 */

struct kgsl_timestamp_event {
	int type;                /* Type of event (see list below) */
	unsigned int timestamp;  /* Timestamp to trigger event on */
	unsigned int context_id; /* Context for the timestamp */
	void *priv;              /* Pointer to the event specific blob */
	size_t len;              /* Size of the event specific blob */
};

#define IOCTL_KGSL_TIMESTAMP_EVENT_OLD \
	_IOW(KGSL_IOC_TYPE, 0x31, struct kgsl_timestamp_event)

/* A genlock timestamp event releases an existing lock on timestamp expire */

#define KGSL_TIMESTAMP_EVENT_GENLOCK 1

struct kgsl_timestamp_event_genlock {
	int handle; /* Handle of the genlock lock to release */
};

/* A fence timestamp event releases an existing lock on timestamp expire */

#define KGSL_TIMESTAMP_EVENT_FENCE 2

struct kgsl_timestamp_event_fence {
	int fence_fd; /* Fence to signal */
};

/*
 * Set a property within the kernel.  Uses the same structure as
 * IOCTL_KGSL_GETPROPERTY
 */

#define IOCTL_KGSL_SETPROPERTY \
	_IOW(KGSL_IOC_TYPE, 0x32, struct kgsl_device_getproperty)

#define IOCTL_KGSL_TIMESTAMP_EVENT \
	_IOWR(KGSL_IOC_TYPE, 0x33, struct kgsl_timestamp_event)

/**
 * struct kgsl_gpumem_alloc_id - argument to IOCTL_KGSL_GPUMEM_ALLOC_ID
 * @id: returned id value for this allocation.
 * @flags: mask of KGSL_MEM* values requested and actual flags on return.
 * @size: requested size of the allocation and actual size on return.
 * @mmapsize: returned size to pass to mmap() which may be larger than 'size'
 * @gpuaddr: returned GPU address for the allocation
 *
 * Allocate memory for access by the GPU. The flags and size fields are echoed
 * back by the kernel, so that the caller can know if the request was
 * adjusted.
 *
 * Supported flags:
 * KGSL_MEMFLAGS_GPUREADONLY: the GPU will be unable to write to the buffer
 * KGSL_MEMTYPE*: usage hint for debugging aid
 * KGSL_MEMALIGN*: alignment hint, may be ignored or adjusted by the kernel.
 * KGSL_MEMFLAGS_USE_CPU_MAP: If set on call and return, the returned GPU
 * address will be 0. Calling mmap() will set the GPU address.
 */
struct kgsl_gpumem_alloc_id {
	unsigned int id;
	unsigned int flags;
	unsigned int size;
	unsigned int mmapsize;
	unsigned long gpuaddr;
/* private: reserved for future use*/
	unsigned int __pad[2];
};

#define IOCTL_KGSL_GPUMEM_ALLOC_ID \
	_IOWR(KGSL_IOC_TYPE, 0x34, struct kgsl_gpumem_alloc_id)

/**
 * struct kgsl_gpumem_free_id - argument to IOCTL_KGSL_GPUMEM_FREE_ID
 * @id: GPU allocation id to free
 *
 * Free an allocation by id, in case a GPU address has not been assigned or
 * is unknown. Freeing an allocation by id with this ioctl or by GPU address
 * with IOCTL_KGSL_SHAREDMEM_FREE are equivalent.
 */
struct kgsl_gpumem_free_id {
	unsigned int id;
/* private: reserved for future use*/
	unsigned int __pad;
};

#define IOCTL_KGSL_GPUMEM_FREE_ID \
	_IOWR(KGSL_IOC_TYPE, 0x35, struct kgsl_gpumem_free_id)

/**
 * struct kgsl_gpumem_get_info - argument to IOCTL_KGSL_GPUMEM_GET_INFO
 * @gpuaddr: GPU address to query. Also set on return.
 * @id: GPU allocation id to query. Also set on return.
 * @flags: returned mask of KGSL_MEM* values.
 * @size: returned size of the allocation.
 * @mmapsize: returned size to pass mmap(), which may be larger than 'size'
 * @useraddr: returned address of the userspace mapping for this buffer
 *
 * This ioctl allows querying of all user visible attributes of an existing
 * allocation, by either the GPU address or the id returned by a previous
 * call to IOCTL_KGSL_GPUMEM_ALLOC_ID. Legacy allocation ioctls may not
 * return all attributes so this ioctl can be used to look them up if needed.
 *
 */
struct kgsl_gpumem_get_info {
	unsigned long gpuaddr;
	unsigned int id;
	unsigned int flags;
	unsigned int size;
	unsigned int mmapsize;
	unsigned long useraddr;
/* private: reserved for future use*/
	unsigned int __pad[4];
};

#define IOCTL_KGSL_GPUMEM_GET_INFO\
	_IOWR(KGSL_IOC_TYPE, 0x36, struct kgsl_gpumem_get_info)

/**
 * struct kgsl_gpumem_sync_cache - argument to IOCTL_KGSL_GPUMEM_SYNC_CACHE
 * @gpuaddr: GPU address of the buffer to sync.
 * @id: id of the buffer to sync. Either gpuaddr or id is sufficient.
 * @op: a mask of KGSL_GPUMEM_CACHE_* values
 *
 * Sync the L2 cache for memory headed to and from the GPU - this replaces
 * KGSL_SHAREDMEM_FLUSH_CACHE since it can handle cache management for both
 * directions
 *
 */
struct kgsl_gpumem_sync_cache {
	unsigned int gpuaddr;
	unsigned int id;
	unsigned int op;
/* private: reserved for future use*/
	unsigned int __pad[2]; /* For future binary compatibility */
};

#define KGSL_GPUMEM_CACHE_CLEAN (1 << 0)
#define KGSL_GPUMEM_CACHE_TO_GPU KGSL_GPUMEM_CACHE_CLEAN

#define KGSL_GPUMEM_CACHE_INV (1 << 1)
#define KGSL_GPUMEM_CACHE_FROM_GPU KGSL_GPUMEM_CACHE_INV

#define KGSL_GPUMEM_CACHE_FLUSH \
	(KGSL_GPUMEM_CACHE_CLEAN | KGSL_GPUMEM_CACHE_INV)

#define IOCTL_KGSL_GPUMEM_SYNC_CACHE \
	_IOW(KGSL_IOC_TYPE, 0x37, struct kgsl_gpumem_sync_cache)

/**
 * struct kgsl_perfcounter_get - argument to IOCTL_KGSL_PERFCOUNTER_GET
 * @groupid: Performance counter group ID
 * @countable: Countable to select within the group
 * @offset: Return offset of the reserved counter
 *
 * Get an available performance counter from a specified groupid.  The offset
 * of the performance counter will be returned after successfully assigning
 * the countable to the counter for the specified group.  An error will be
 * returned and an offset of 0 if the groupid is invalid or there are no
 * more counters left.  After successfully getting a perfcounter, the user
 * must call kgsl_perfcounter_put(groupid, contable) when finished with
 * the perfcounter to clear up perfcounter resources.
 *
 */
struct kgsl_perfcounter_get {
	unsigned int groupid;
	unsigned int countable;
	unsigned int offset;
/* private: reserved for future use */
	unsigned int __pad[2]; /* For future binary compatibility */
};

#define IOCTL_KGSL_PERFCOUNTER_GET \
	_IOWR(KGSL_IOC_TYPE, 0x38, struct kgsl_perfcounter_get)

/**
 * struct kgsl_perfcounter_put - argument to IOCTL_KGSL_PERFCOUNTER_PUT
 * @groupid: Performance counter group ID
 * @countable: Countable to release within the group
 *
 * Put an allocated performance counter to allow others to have access to the
 * resource that was previously taken.  This is only to be called after
 * successfully getting a performance counter from kgsl_perfcounter_get().
 *
 */
struct kgsl_perfcounter_put {
	unsigned int groupid;
	unsigned int countable;
/* private: reserved for future use */
	unsigned int __pad[2]; /* For future binary compatibility */
};

#define IOCTL_KGSL_PERFCOUNTER_PUT \
	_IOW(KGSL_IOC_TYPE, 0x39, struct kgsl_perfcounter_put)

/**
 * struct kgsl_perfcounter_query - argument to IOCTL_KGSL_PERFCOUNTER_QUERY
 * @groupid: Performance counter group ID
 * @countable: Return active countables array
 * @size: Size of active countables array
 * @max_counters: Return total number counters for the group ID
 *
 * Query the available performance counters given a groupid.  The array
 * *countables is used to return the current active countables in counters.
 * The size of the array is passed in so the kernel will only write at most
 * size or counter->size for the group id.  The total number of available
 * counters for the group ID is returned in max_counters.
 * If the array or size passed in are invalid, then only the maximum number
 * of counters will be returned, no data will be written to *countables.
 * If the groupid is invalid an error code will be returned.
 *
 */
struct kgsl_perfcounter_query {
	unsigned int groupid;
	/* Array to return the current countable for up to size counters */
	unsigned int *countables;
	unsigned int count;
	unsigned int max_counters;
/* private: reserved for future use */
	unsigned int __pad[2]; /* For future binary compatibility */
};

#define IOCTL_KGSL_PERFCOUNTER_QUERY \
	_IOWR(KGSL_IOC_TYPE, 0x3A, struct kgsl_perfcounter_query)

/**
 * struct kgsl_perfcounter_query - argument to IOCTL_KGSL_PERFCOUNTER_QUERY
 * @groupid: Performance counter group IDs
 * @countable: Performance counter countable IDs
 * @value: Return performance counter reads
 * @size: Size of all arrays (groupid/countable pair and return value)
 *
 * Read in the current value of a performance counter given by the groupid
 * and countable.
 *
 */

struct kgsl_perfcounter_read_group {
	unsigned int groupid;
	unsigned int countable;
	uint64_t value;
};

struct kgsl_perfcounter_read {
	struct kgsl_perfcounter_read_group *reads;
	unsigned int count;
/* private: reserved for future use */
	unsigned int __pad[2]; /* For future binary compatibility */
};

#define IOCTL_KGSL_PERFCOUNTER_READ \
	_IOWR(KGSL_IOC_TYPE, 0x3B, struct kgsl_perfcounter_read)

/*
 * kgsl_get_alloc_size - acquire memory size allocated in kernel space by kgsl
 * @detailed: to indicate if need detailed info,
 *  set detailed to 1, if need the detailed info
 *  of each gfx process for different type memory usage,
 *  said ion, ashmem, kernel ...etc
 *
 * Returns allocated memory size and prints kgsl meminfo in kernel log if set detailed
 */
unsigned int kgsl_get_alloc_size(int detailed);

#ifdef __KERNEL__
#ifdef CONFIG_MSM_KGSL_DRM
int kgsl_gem_obj_addr(int drm_fd, int handle, unsigned long *start,
			unsigned long *len);
#else
#define kgsl_gem_obj_addr(...) 0
#endif
#endif
#endif
#endif /* _MSM_KGSL_H */
