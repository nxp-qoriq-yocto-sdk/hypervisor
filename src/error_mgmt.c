/** @file
 * Error Management
 */

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

#include<libos/platform_error.h>

#include <percpu.h>
#include <error_mgmt.h>
#include <error_log.h>
#include <guts.h>

typedef struct setpolicy_ctx {
	dt_node_t *hwnode;
} setpolicy_ctx_t;

error_policy_t cpc_error_policy[CPC_ERROR_COUNT] = {
	[cpc_multiple_errors] = {"multiple errors", "disable", 0},
	[cpc_tag_multi_way_hit] = {"tag multi-way hit", "disable", 0},
	[cpc_tag_status_multi_bit_ecc] = {"tag status multi-bit ecc", "disable", 0},
	[cpc_tag_status_single_bit_ecc] = {"tag status single-bit ecc",  "disable", 0x8},
	[cpc_data_multi_bit_ecc] = {"data multi-bit ecc", "disable", 0},
	[cpc_data_single_bit_ecc] = {"data single-bit ecc", "disable", 0x8},
};

error_policy_t ccf_error_policy[CCF_ERROR_COUNT] = {
	[ccf_multiple_intervention] = {"multiple intervention", "disable", 0},
	[ccf_local_access] = {"local access", "disable", 0},
};

error_policy_t ccm_error_policy[CCM_ERROR_COUNT] = {
	[ccm_mcast_stash] = {"multicast stash", "disable", 0},
	[ccm_unavailable_tgt_id] = {"unavailable target id", "disable", 0},
	[ccm_illegal_coherency_response] = {"illegal coherency response", "disable", 0},
	[ccm_local_access] = {"local access", "disable", 0},

};

error_policy_t misc_error_policy[MISC_ERROR_COUNT] = {
	[internal_ram_multi_bit_ecc] = {"internal ram multi-bit ecc", "disable", 0}
};

error_policy_t pamu_error_policy[PAMU_ERROR_COUNT] = {
	[pamu_operation] = {"operation", "notify", 0},
	[pamu_single_bit_ecc] = {"single-bit ecc", "disable", 0x8},
	[pamu_multi_bit_ecc] = {"multi-bit ecc", "disable", 0},
	[pamu_access_violation] = {"access violation", "notify", 0},
};

error_policy_t ddr_error_policy[DDR_ERROR_COUNT] = {
	[ddr_multiple_errors] = {"multiple errors", "disable", 0},
	[ddr_memory_select] = {"memory select", "disable", 0},
	[ddr_single_bit_ecc] = {"single-bit ecc", "disable", 0x8},
	[ddr_multi_bit_ecc] = {"multi-bit ecc", "disable", 0},
	[ddr_corrupted_data] = {"corrupted data", "disable", 0},
	[ddr_auto_calibration] = {"auto calibration", "disable", 0},
	[ddr_address_parity] = {"address parity", "disable", 0},
};

static void dump_mcheck_error(hv_error_t *err)
{
	mcheck_error_t *mcheck = &err->mcheck;

	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR, "Machine check interrupt, mcsr = %x\n", mcheck->mcsr);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR,
		"mcar = %llx, mcssr0 = %llx, mcsrr1 = %x\n",
		mcheck->mcar, mcheck->mcsrr0, mcheck->mcsrr1);
}

error_domain_t error_domains[ERROR_DOMAIN_COUNT] = {
	[error_mcheck] = {(const char *[]){NULL}, "machine check", NULL, 0, dump_mcheck_error},
	[error_cpc] = {(const char *[]){"fsl,p4080-l3-cache-controller", "fsl,t4240-l3-cache-controller", NULL}, "cpc", cpc_error_policy, CPC_ERROR_COUNT, NULL},
	[error_ccf] = {(const char *[]){"fsl,corenet-cf", NULL}, "ccf", ccf_error_policy, CCF_ERROR_COUNT, NULL},
	[error_ccm] = {(const char *[]){"fsl,corenet2-cf", NULL}, "ccm", ccm_error_policy, CCM_ERROR_COUNT, NULL},
	[error_misc] = {(const char *[]){"fsl,soc-sram-error", NULL}, "misc", misc_error_policy, MISC_ERROR_COUNT, NULL},
	[error_pamu] = {(const char *[]){"fsl,pamu", "fsl,p4080-pamu", NULL}, "pamu", pamu_error_policy, PAMU_ERROR_COUNT, NULL},
	[error_ddr] = {(const char *[]){"fsl,qoriq-memory-controller", "fsl,p4080-memory-controller", NULL}, "ddr", ddr_error_policy, DDR_ERROR_COUNT, NULL},
};

static int validate_domain(const char *domain, dt_node_t *hwnode)
{
	int i, j;
	char *compat_strlist;
	const char *end_compat;
	dt_prop_t *compat = dt_get_prop(hwnode, "compatible", 0);

	if (!compat) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: error: missing compatible on %s",
		          __func__,hwnode->name);
		return 1;
	}

	compat_strlist = compat->data;
	end_compat = compat_strlist + compat->len;

	/* validate the the hwnode compatible corresponds to
           the domain */
	for (i = 0; i < ERROR_DOMAIN_COUNT; i++) {
		if (strcmp(domain, error_domains[i].domain)) /* domain matches? */
			continue;
		for (j = 0; error_domains[i].compatibles[j]; j++) { /* for each supported compatible */
			if (dt_node_is_compatible(hwnode, error_domains[i].compatibles[j]))
				return 0;  /* found it */
		}
	}

	return 1;  /* domain/compatible mismatch */
}

static error_policy_t *get_policy_by_str(const char *domain, const char *error)
{
	int i, j;
	error_policy_t *error_p;

	/* get pointer to errors for this domain */
	for (i = 0; i < ERROR_DOMAIN_COUNT; i++) {
		if (!strcmp(domain, error_domains[i].domain))
			break;
	}

	error_p = error_domains[i].errors;

	for (j = 0; j < error_domains[i].error_count; j++) {
		if (!strcmp(error_p[j].error, error)) {
			return &error_p[j]; /* found it */
		}
	}

	return NULL;  /* policy not found */
}

static uint32_t get_single_bit_ecc_thresh(dt_node_t *node)
{
	uint32_t threshold = 0;
	dt_prop_t *prop;

	prop = dt_get_prop(node, "single-bit-ecc-threshold", 0);
	if (prop && prop->len == 4) {
		if (*(const uint32_t *)prop->data <= 0xff)
			threshold = *(const uint32_t *)prop->data;
		else
			printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				"Invalid single-bit ecc threshold value %d\n",
				 *(const uint32_t *)prop->data);
	}

	return threshold;
}

static int setpolicy_callback(dt_node_t *node, void *arg)
{
	setpolicy_ctx_t *ctx = arg;
	error_policy_t *error_p;

	const char *domain = dt_get_prop_string(node, "domain");
	if (!domain) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: missing domain property on %s",
		          __func__,node->name);
		return 0;
	}

	const char *error = dt_get_prop_string(node, "error");
	if (!error) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: missing error property on %s",
		          __func__,node->name);
		return 0;
	}

	const char *policy = dt_get_prop_string(node, "policy");
	if (!policy) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: missing policy property on %s",
		          __func__,node->name);
		return 0;
	}

	if (validate_domain(domain, ctx->hwnode)) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: error domain on %s not valid\n",
		          __func__,node->name);
		return 0;
	}

	error_p = get_policy_by_str(domain, error);
	if (error_p) {
		error_p->policy = policy; /* override the default */
		if (!strcmp(error, "single-bit ecc") ||
			!strcmp(error, "data single-bit ecc") ||
			!strcmp(error, "tag status single-bit ecc")) {
			uint32_t ret = get_single_bit_ecc_thresh(node);
			if (ret)
				error_p->threshold = ret;
		}

	} else {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: error policy configuration error on %s\n",
		          __func__,node->name);
	}

	return 0;
}

void set_error_policy(dev_owner_t *owner)
{
	setpolicy_ctx_t ctx = {
		.hwnode = owner->hwnode,
	};

	dt_for_each_compatible(owner->cfgnode, "error-config", setpolicy_callback, &ctx);
}

const char *get_error_policy(domains_t domain, int error)
{
	error_policy_t *error_p = error_domains[domain].errors;

	return error_p[error].policy;
}

uint32_t get_error_threshold(domains_t domain, int error)
{
	error_policy_t *error_p = error_domains[domain].errors;

	return error_p[error].threshold;
}

const char *get_error_str(domains_t domain,int error)
{
	error_policy_t *error_p = error_domains[domain].errors;

	return error_p[error].error;

}

const char *get_domain_str(domains_t domain)
{
	return error_domains[domain].domain;
}

void register_error_dump_callback(domains_t domain, dump_error_info err_dump_fn)
{
	if ((domain < ERROR_DOMAIN_COUNT) && (error_domains[domain].error_dump_callback == NULL))
		error_domains[domain].error_dump_callback = err_dump_fn;
}

void dump_domain_error_info(hv_error_t *err, domains_t domain)
{
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_EXTRA, "error domain : %s\n", err->domain);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_EXTRA, "error  : %s\n", err->error);

	if ((domain < ERROR_DOMAIN_COUNT) && (error_domains[domain].error_dump_callback))
		error_domains[domain].error_dump_callback(err);
}

void error_policy_action(hv_error_t *err, domains_t domain, const char *policy)
{
	if (!strcmp(policy, "notify")) {
		error_log(&global_event_queue, err, &global_event_prod_lock);
	} else if (!strcmp(policy, "halt")) {
		halt_system = 1;
		set_crashing(1);
		dump_domain_error_info(err, domain);
		set_crashing(0);
	} else if (!strcmp(policy, "system-reset")) {
		system_reset();
	}

	if (!halt_system)
		error_log(&hv_global_event_queue, err, &hv_queue_prod_lock);
}
