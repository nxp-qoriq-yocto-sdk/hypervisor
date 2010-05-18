/** @file
 * Guest object handles
 */
/*
 * Copyright (C) 2007-2010 Freescale Semiconductor, Inc.
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

#ifndef HANDLE_H
#define HANDLE_H

struct handle;

/** General handle operations that many types of handle will implement.
 * Any member may be NULL.
 */
typedef struct {
	/** Reset the handle to partition boot state.
	 * If the handle was dynamically created, rather than
	 * device-tree-originated, then close the handle.
	 */
	void (*reset)(struct handle *h);
} handle_ops_t;

/* An extremely crude form of RTTI/multiple interfaces...
 * Add pointers here for other handle types as they are needed.
 */
typedef struct handle {
	handle_ops_t *ops;
	int id; /**< Guest handle ID number */

	struct byte_chan_handle *bc;
	struct vmpic_interrupt *intr;
	struct ipi_doorbell_handle *db;
	struct pamu_handle *pamu;
	struct ppid_handle *ppid;
	struct guest *guest;
	struct dev_owner *dev_owner;
} handle_t;

#endif /* HANDLE_H */
