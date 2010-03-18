/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
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

#ifndef ERROR_MGMT_H
#define ERROR_MGMT_H

#include <devtree.h>

typedef struct error_policy {
	const char *error;
	const char *policy;
} error_policy_t;

typedef struct error_domain {
	const char *compatible;
	const char *domain;
	error_policy_t *errors;
	int error_count;
} error_domain_t;

#define ERROR_DOMAIN_COUNT 6
typedef enum {
	error_mcheck,
	error_cpc,
	error_ccf,
	error_misc,
	error_pamu,
	error_ddr
} domains_t;
extern error_domain_t error_domains[];

#define CPC_ERROR_COUNT 6
typedef enum {
	cpc_tag_multi_way_hit,
	cpc_tag_status_multi_bit_ecc,
	cpc_tag_status_single_bit_ecc,
	cpc_data_multi_bit_ecc,
	cpc_data_single_bit_ecc,
	cpc_config
} cpc_errors_t;
extern error_policy_t cpc_error_policy[];


#define CCF_ERROR_COUNT 2
typedef enum {
	ccf_multiple_invervention,
	ccf_local_access
} ccf_errors_t;
extern error_policy_t ccf_error_policy[];

#define MISC_ERROR_COUNT 1
typedef enum {
	internal_ram_multi_bit_ecc
} misc_errors_t;
extern error_policy_t misc_error_policy[];

#define PAMU_ERROR_COUNT 4
typedef enum {
	pamu_operation,
	pamu_single_bit_ecc,
	pamu_multi_bit_ecc,
	pamu_access_violation,
} pamu_errors_t;
extern error_policy_t pamu_error_policy[];

#define DDR_ERROR_COUNT 6
typedef enum {
	ddr_memory_select,
	ddr_single_bit_ecc,
	ddr_multi_bit_ecc,
	ddr_corrupted_data,
	ddr_auto_calibration,
	ddr_address_parity
} ddr_errors_t;
extern error_policy_t ddr_error_policy[];

void set_error_policy(dev_owner_t *owner);
const char *get_error_policy(domains_t domain, int error);
const char *get_error_str(domains_t domain,int error);
const char *get_domain_str(domains_t domain);

#endif