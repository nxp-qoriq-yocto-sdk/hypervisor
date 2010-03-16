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

#include <error_mgmt.h>

typedef struct setpolicy_ctx {
	dt_node_t *hwnode;
} setpolicy_ctx_t;

error_policy_t cpc_error_policy[CPC_ERROR_COUNT] = {
	[cpc_tag_multi_way_hit] = {"tag multi-way hit", "disable"},
	[cpc_tag_status_multi_bit_ecc] = {"tag status multi-bit ecc", "disable"},
	[cpc_tag_status_single_bit_ecc] = {"tag status single-bit ecc",  "disable"},
	[cpc_data_multi_bit_ecc] = {"data multi-bit ecc", "disable"},
	[cpc_data_single_bit_ecc] = {"data single-bit ecc", "disable"},
	[cpc_config] = {"config", "disable"}
};

error_policy_t ccf_error_policy[CCF_ERROR_COUNT] = {
	[ccf_multiple_invervention] = {"multiple invervention", "disable"},
	[ccf_local_access] = {"local access", "disable"},
};

error_policy_t misc_error_policy[MISC_ERROR_COUNT] = {
	[internal_ram_multi_bit_ecc] = {"internal ram multi-bit ecc", "disable"}
};

error_policy_t pamu_error_policy[PAMU_ERROR_COUNT] = {
	[pamu_operation] = {"operation", "notify"},
	[pamu_single_bit_ecc] = {"single-bit ecc", "disable"},
	[pamu_multi_bit_ecc] = {"multi-bit ecc", "disable"},
	[pamu_access_violation] = {"access violation", "disable"},
};

error_policy_t ddr_error_policy[DDR_ERROR_COUNT] = {
	[ddr_memory_select] = {"memory select", "disable"},
	[ddr_single_bit_ecc] = {"single-bit ecc", "disable"},
	[ddr_multi_bit_ecc] = {"multi-bit ecc", "disable"},
	[ddr_corrupted_data] = {"corrupted data", "disable"},
	[ddr_auto_calibration] = {"auto calibration", "disable"},
	[ddr_address_parity] = {"address parity", "disable"},
};

error_domain_t error_domains[ERROR_DOMAIN_COUNT] = {
	[error_mcheck] = { NULL, "machine check", NULL, 0 },
	[error_cpc] = { "fsl,p4080-l3-cache-controller", "cpc", cpc_error_policy, CPC_ERROR_COUNT },
	[error_ccf] = { "fsl,corenet-cf", "ccf", ccf_error_policy, CCF_ERROR_COUNT },
	[error_misc] = { "fsl,soc-sram-error", "misc", misc_error_policy, MISC_ERROR_COUNT },
	[error_pamu] = { "fsl,p4080-pamu", "pamu", pamu_error_policy, PAMU_ERROR_COUNT },
	[error_ddr] = { "fsl,p4080-memory-controller", "ddr", ddr_error_policy, DDR_ERROR_COUNT },
};

static int validate_domain(const char *domain, dt_node_t *hwnode)
{
	int i;
	const char *compat = dt_get_prop_string(hwnode, "compatible");
	if (!compat) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: error: missing compatible on %s",
		          __func__,hwnode->name);
		return 1;
	}

	/* validate the the hwnode compatible corresponds to
           the domain */
	for (i = 0; i < ERROR_DOMAIN_COUNT; i++) {
		if (!strcmp(compat, error_domains[i].compatible) &&
		    !strcmp(domain, error_domains[i].domain))
			return 0;  /* found it */
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
	if (error_p)
		error_p->policy = policy; /* override the default */
	else
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: error policy configuration error on %s\n",
		          __func__,node->name);

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

const char *get_error_str(domains_t domain,int error)
{
	error_policy_t *error_p = error_domains[domain].errors;

	return error_p[error].error;

}

const char *get_domain_str(domains_t domain)
{
	return error_domains[domain].domain;
}
