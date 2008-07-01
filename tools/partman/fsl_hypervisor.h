
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/**
 * Freescale hypervisor ioctl interface
 *
 * This file is used by the Freescale hypervisor management driver.  It can
 * also be included by applications that need to communicate with the driver
 * via the ioctl interface.
 */

#include <linux/types.h>

/**
 * Freescale hypervisor ioctl parameter
 */
union fsl_hv_ioctl_param {

	/**
	 * @ret: Return value.
	 *
	 * This is always the first word of any structure.
	 */
	__u32 ret;

	/**
	 * struct fsl_hv_ioctl_restart: restart a partition
	 * @ret: return error code from the hypervisor
	 * @partition: the ID of the partition to restart, or -1 for the
	 *             calling partition
	 *
	 * Used by FSL_HV_IOCTL_PARTITION_RESTART
	 */
	struct fsl_hv_ioctl_restart {
		__u32 ret;
		__u32 partition;
	} restart;

	/**
	 * struct fsl_hv_ioctl_status: get a partition's status
	 * @ret: return error code from the hypervisor
	 * @partition: the ID of the partition to query, or -1 for the
	 *             calling partition
	 * @status: The returned status of the partition
	 *
	 * Used by FSL_HV_IOCTL_PARTITION_GET_STATUS
	 *
	 * Values of 'status':
	 *    0 = Stopped
	 *    1 = Running
	 *    2 = Starting
	 *    3 = Stopping
	 */
	struct fsl_hv_ioctl_status {
		__u32 ret;
		__u32 partition;
		__u32 status;
	} status;

	/**
	 * struct fsl_hv_ioctl_start: start a partition
	 * @ret: return error code from the hypervisor
	 * @partition: the ID of the partition to control
	 * @entry_point: The offset within the guest IMA to start execution
	 *
	 * Used by FSL_HV_IOCTL_PARTITION_START
	 */
	struct fsl_hv_ioctl_start {
		__u32 ret;
		__u32 partition;
		__u32 entry_point;
	} start;

	/**
	 * struct fsl_hv_ioctl_stop: stop a partition
	 * @ret: return error code from the hypervisor
	 * @partition: the ID of the partition to stop, or -1 for the calling
	 *             partition
	 *
	 * Used by FSL_HV_IOCTL_PARTITION_STOP
	 */
	struct fsl_hv_ioctl_stop {
		__u32 ret;
		__u32 partition;
	} stop;

	/**
	 * struct fsl_hv_ioctl_memcpy: copy memory between partitions
	 * @ret: return error code from the hypervisor
	 * @source: the partition ID of the source partition, or -1 for this
	 *          partition
	 * @target: the partition ID of the target partition, or -1 for this
	 *          partition
	 * @local_addr: user-space virtual address of a buffer in the local
	 *              partition
	 * @remote_addr: guest physical address of a buffer in the
	 *           remote partition
	 * @count: the number of bytes to copy.  Both the local and remote
	 *         buffers must be at least 'count' bytes long
	 *
	 * Used by FSL_HV_IOCTL_MEMCPY
	 *
	 * The 'local' partition is the partition that calls this ioctl.  The
	 * 'remote' partition is a different partition.  The data is copied from
	 * the 'source' paritition' to the 'target' partition.
	 *
	 * The buffer in the remote partition must be guest physically
	 * contiguous.
	 *
	 * This ioctl does not support copying memory between two remote
	 * partitions or within the same partition, so either 'source' or
	 * 'target' (but not both) must be -1.  In other words, either
	 *
	 *      source == local and target == remote
	 * or
	 *      source == remote and target == local
	 */
	struct fsl_hv_ioctl_memcpy {
		__u32 ret;
		__u32 source;
		__u32 target;
		__u64 local_vaddr;
		__u64 remote_paddr;
		__u64 count;
	} memcpy;
};

/*
 * ioctl commands.
 */
enum {
	FSL_HV_IOCTL_PARTITION_RESTART = 1, /* Boot another partition */
	FSL_HV_IOCTL_PARTITION_GET_STATUS = 2, /* Boot another partition */
	FSL_HV_IOCTL_PARTITION_START = 3, /* Boot another partition */
	FSL_HV_IOCTL_PARTITION_STOP = 4, /* Stop this or another partition */
	FSL_HV_IOCTL_MEMCPY = 5, /* Copy data from one partition to another */
};
