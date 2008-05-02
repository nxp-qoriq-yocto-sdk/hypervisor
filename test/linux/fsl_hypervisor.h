/**
 * Freescale hypervisor IOCtl interface
 *
 * This file is used by the Freescale hypervisor management driver.  It can
 * also be included by applications that need to communicate with the driver
 * via the IOCtl interface.
 */

#include <linux/types.h>

/**
 * Freescale hypervisor IOCtl parameter
 */
union fsl_hv_ioctl_param {

	/**
	 *  Restart a partition
	 *
	 *  This structure is used by FSL_HV_IOCTL_PARTITION_RESTART.
	 *
	 *  @partition: the ID of the partition to restart, or -1 for the
	 *              calling partition.
	 */
	struct fsl_hv_ioctl_restart {
		__u32 partition;
	} restart;

	/**
	 *  Get a partition's status
	 *
	 *  This structure is used by FSL_HV_IOCTL_PARTITION_GET_STATUS.
	 *
	 *  @partition: the ID of the partition to query, or -1 for the
	 *              calling partition.
	 *  @status: The returned status of the partition
	 *
	 *  Values of 'status':
	 *    0 = Stopped
	 *    1 = Running
	 *    2 = Starting
	 *    3 = Stopping
	 */
	struct fsl_hv_ioctl_status {
		__u32 partition;
		__u32 status;
	} status;

	/**
	 *  Start a partition
	 *
	 *  This structure is used by FSL_HV_IOCTL_PARTITION_START.
	 *
	 *  @partition: the ID of the partition to control.
	 *  @entry_point: The offset within the guest IMA to start execution
	 */
	struct fsl_hv_ioctl_start {
		__u32 partition;
		__u32 entry_point;
	} start;

	/**
	 *  Stop a partition
	 *
	 *  This structure is used by FSL_HV_IOCTL_PARTITION_STOP.
	 *
	 *  @partition: the ID of the partition to stop, or -1 for the calling
	 *              partition.
	 */
	struct fsl_hv_ioctl_stop {
		__u32 partition;
	} stop;

	/**
	 * Copy a block of memory from one partition to another.  Used by
	 * FSL_HV_IOCTL_MEMCPY.
	 *
	 * The 'local' partition is the partition that calls this IOCtl.  The
	 * 'remote' partition is a different partition.  The data is copied from
	 * the 'source' paritition' to the 'target' partition.
	 *
	 * The buffer in the remote partition must be guest physically
	 * contiguous.
	 *
	 * @source: the partition ID of the source partition, or -1 for this
	 *          partition.
	 * @target: the partition ID of the target partition, or -1 for this
	 *          partition.
	 * @local_addr: user-space virtual address of a buffer in the local
	 *              partition.
	 * @remote_addr: guest physical address of a buffer in the
	 *           remote partition.
	 * @count: the number of bytes to copy.  Both the local and remote
	 *         buffers must be at least 'count' bytes long.
	 *
	 * This IOCtl does not support copying memory between two remote
	 * partitions or within the same partition, so either 'source' or
	 * 'target' (but not both) must be -1.  In other words, either
	 *
	 *      source == local and target == remote
	 * or
	 *      source == remote and target == local
	 */
	struct fsl_hv_ioctl_memcpy {
		__u32 source;
		__u32 target;
		__u64 local_vaddr;
		__u64 remote_paddr;
		__u64 count;
	} memcpy;
};

/* IOCTL commands.  These should match the token values in fsl_hcalls.h.
 *
 * FIXME: We should re-evaluate the numbering before we ship 1.0
 */
enum {
	FSL_HV_IOCTL_PARTITION_RESTART = 5, /* Boot another partition */
	FSL_HV_IOCTL_PARTITION_GET_STATUS = 6, /* Boot another partition */
	FSL_HV_IOCTL_PARTITION_START = 7, /* Boot another partition */
	FSL_HV_IOCTL_PARTITION_STOP = 8, /* Stop this or another partition */
	FSL_HV_IOCTL_MEMCPY = 9, /* Copy data from one partition to another */
};
