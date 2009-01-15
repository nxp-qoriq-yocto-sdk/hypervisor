/** @file
 * Debug stub related routines
 */
/* Copyright (C) 2009 Freescale Semiconductor, Inc.
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

#include <debug-stub.h>
#include <devtree.h>
#include <percpu.h>

static int find_stub_by_vcpu(dt_node_t *node, void *arg)
{
	dt_node_t **ret = arg;
	dt_prop_t *prop;

	prop = dt_get_prop(node, "debug-cpus", 0);
	if (!prop || prop->len != 8) {
		printlog(LOGTYPE_DEBUG_STUB, LOGLEVEL_ERROR, "Missing/bad debug-cpus at %s\n", node->name);
		return 0;
	}

	/* FIXME: Handle multiple vcpus */
	int vcpu_num = *(const uint32_t *)prop->data;
	if (vcpu_num == get_gcpu()->gcpu_num) {
		*ret = node;
		return 1;
	}

	return 0;
}

/** Find the debug stub node in the configuration device-tree
 * @return pointer to the node
 */
dt_node_t *find_stub_config_node(const char *compatible)
{
	gcpu_t *gcpu = get_gcpu();
	dt_node_t *node = NULL;
	int rc;

	rc = dt_for_each_compatible(gcpu->guest->partition, compatible,
	                       find_stub_by_vcpu, &node);

	return node;
}

extern stub_ops_t hvdbgstub_begin, hvdbgstub_end;

static stub_ops_t *find_stubops(const char *compatible)
{
	for (stub_ops_t *ops = &hvdbgstub_begin; ops < &hvdbgstub_end; ops++) {
		if (!strcmp(compatible, ops->compatible))
			return ops;
	}

	return NULL;
}

/* For this partition, see if a debug stub node exists
 * (compatible="debug-stub"), then look for a defined
 * debug ops struct that matches the specific compatible
 * on the debug stub node.
 */
void init_stubops(guest_t *guest)
{
	stub_ops_t *ops = NULL;
	dt_node_t *node;
	dt_prop_t *prop;
	const char *str;
	size_t pos = 0;

	node = dt_get_first_compatible(guest->partition, "debug-stub");
	if (!node)
		return;  /* no stubs for this partition */

	prop = dt_get_prop(node, "compatible", 0);

	/* for each compatible, look for a matching ops structure
	 * in the .hvdbgstub section
	 */
	for (;;) {
		str = strlist_iterate(prop->data, prop->len, &pos);
		if (!str)
			break;

		ops = find_stubops(str);
		if (ops)
			break;  /* found it */
	}

	if (!guest->stub_ops)
		guest->stub_ops = ops;

}
