/** @file
 * Operations on live (unflattened) device trees
 */

/* Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
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

#include <libos/queue.h>
#include <libos/alloc.h>

#include <errors.h>
#include <devtree.h>
#include <percpu.h>

dt_node_t *create_dev_tree(void)
{
	dt_node_t *node;

	node = alloc_type(dt_node_t);
	if (!node)
		return NULL;

	list_init(&node->children);
	list_init(&node->props);
	list_init(&node->owners);
	list_init(&node->aliases);

	node->name = strdup("");
	if (!node->name) {
		free(node);
		return NULL;
	}

	return node;
}

dt_node_t *unflatten_dev_tree(const void *fdt)
{
	int offset = 0, next;
	dt_node_t *node = NULL, *top = NULL;
	int ret;

	while (1) {
		const char *name;
		uint32_t tag = fdt_next_tag(fdt, offset, &next);

		switch (tag) {
		case FDT_BEGIN_NODE: {
			dt_node_t *parent = node;
			node = alloc_type(dt_node_t);
			if (!node)
				goto nomem;

			node->parent = parent;

			if (!parent)
				top = node;

			name = fdt_get_name(fdt, offset, &ret);
			if (!name)
				goto err;

			node->name = strdup(name);
			if (!node->name)
				goto nomem;

			if (parent)
				list_add(&parent->children, &node->child_node);

			list_init(&node->children);
			list_init(&node->props);
			list_init(&node->owners);
			list_init(&node->aliases);
			break;
		}

		case FDT_END_NODE:
			if (!node) {
				ret = -FDT_ERR_BADSTRUCTURE;
				goto err;
			}

			if (node == top)
				return node;

			node = node->parent;
			break;

		case FDT_PROP: {
			const struct fdt_property *fdtprop;
			dt_prop_t *prop;

			fdtprop = fdt_offset_ptr(fdt, offset, sizeof(*fdtprop));
			if (!fdtprop) {
				ret = -FDT_ERR_TRUNCATED;
				goto err;
			}

			name = fdt_string(fdt, fdt32_to_cpu(fdtprop->nameoff));
			if (!name) {
				ret = -FDT_ERR_BADSTRUCTURE;
				goto err;
			}

			prop = alloc_type(dt_prop_t);
			if (!prop)
				goto nomem;

			list_add(&node->props, &prop->prop_node);

			prop->name = strdup(name);
			if (!prop->name)
				goto nomem;

			prop->len = fdt32_to_cpu(fdtprop->len);
			fdtprop = fdt_offset_ptr(fdt, offset, sizeof(*fdtprop) + prop->len);
			if (!fdtprop) {
				ret = -FDT_ERR_TRUNCATED;
				goto err;
			}

			prop->data = malloc(prop->len);
			if (!prop->data)
				goto nomem;

			memcpy(prop->data, fdtprop->data, prop->len);
			break;
		}

		case FDT_END:
			if (node) {
				ret = -FDT_ERR_BADSTRUCTURE;
				goto err;
			}

			return NULL;

		case FDT_NOP:
			break;

		default:
			ret = -FDT_ERR_BADSTRUCTURE;
			goto err;
		}

		offset = next;
	}

err:
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
	         "unflatten_dev_tree: libfdt error %s (%d)\n",
	         fdt_strerror(ret), ret);
	dt_delete_node(top);
	return NULL;

nomem:
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
	         "unflatten_dev_tree: out of memory\n");
	dt_delete_node(top);
	return NULL;
}

/**
 * Perform an operation on each node of a tree (including the root).
 *
 * @param[in] tree device (sub)tree root
 * @param[in] arg opaque argument to callback functions
 * @param[in] previsit pointer to operation on each node before visting children
 * @param[in] postvisit pointer to operation on each node after visting children
 * @return zero on success, non-zero if the traversal was terminated
 *
 * The callback is performed on each node of the tree.  In postvisit, it
 * is safe to remove nodes as they are visited, but it is not safe to
 * remove the parent or any sibling of the visited node.  In previsit,
 * it is safe to remove children of the visited node, but not the visited
 * node itself.
 *
 * If a callback returns non-zero, the traversal is aborted, and the
 * return code propagated.
 */
int dt_for_each_node(dt_node_t *tree, void *arg,
                     dt_callback_t previsit, dt_callback_t postvisit)
{
	list_t *node = &tree->child_node;
	int backtrack = 0;
	int ret;

	if (!tree)
		return 0;

	while (1) {
		dt_node_t *dtnode = to_container(node, dt_node_t, child_node);
		dt_node_t *parent = dtnode->parent;

		if (!backtrack) {
			if (previsit) {
				ret = previsit(dtnode, arg);
				if (ret)
					return ret;
			}

			if (!list_empty(&dtnode->children)) {
				node = dtnode->children.next;
				continue;
			}
		}

		backtrack = 0;
		node = dtnode->child_node.next;

		if (postvisit) {
			ret = postvisit(dtnode, arg);
			if (ret)
				return ret;
		}

		if (dtnode == tree)
			return 0;

		assert(parent);

		if (node == &parent->children) {
			node = &parent->child_node;
			backtrack = 1;
		}
	}
}

void dt_delete_prop(dt_prop_t *prop)
{
	list_del(&prop->prop_node);

	free(prop->name);
	free(prop->data);
	free(prop);
}

static int destroy_node(dt_node_t *node, void *arg)
{
	list_for_each_delsafe(&node->props, i, next) {
		dt_prop_t *prop = to_container(i, dt_prop_t, prop_node);
		dt_delete_prop(prop);
	}

	if (node->parent)
		list_del(&node->child_node);

	free(node->name);
	free(node);
	return 0;
}

void dt_delete_node(dt_node_t *tree)
{
	dt_for_each_node(tree, NULL, NULL, destroy_node);
}

static int flatten_pre(dt_node_t *node, void *fdt)
{
	int ret = fdt_begin_node(fdt, node->name);
	if (ret)
		return ret;

	list_for_each(&node->props, i) {
		dt_prop_t *prop = to_container(i, dt_prop_t, prop_node);

		ret = fdt_property(fdt, prop->name, prop->data, prop->len);
		if (ret)
			return ret;
	}

	return 0;
}

static int flatten_post(dt_node_t *node, void *fdt)
{
	return fdt_end_node(fdt);
}

/**
 * Create a flat device tree blob from a live tree.
 *
 * @param[in] tree live device tree root
 * @param[in] fdt address at which to create a flat tree
 * @param[in] fdt_len size of window in which to create a flat tree
 * @return zero on success, -FDT_ERR_NOSPACE if fdt_len is too small
 */
int flatten_dev_tree(dt_node_t *tree, void *fdt, size_t fdt_len)
{
	int ret = fdt_create(fdt, fdt_len);
	if (ret)
		return ret;

	ret = fdt_finish_reservemap(fdt);
	if (ret)
		return ret;

	ret = dt_for_each_node(tree, fdt, flatten_pre, flatten_post);
	if (ret)
		return ret;

	return fdt_finish(fdt);
}

/**
 * Non-recursively searches for a subnode with a particular name,
 * optionally creating it.
 *
 * @param[in] node parent node of the node to be found
 * @param[in] name name of the node to be found; does not
 *   need to be null-terminated
 * @param[in] namelen length of name in bytes
 * @param[in] create if non-zero, create the subnode if not found
 * @return pointer to subnode, or NULL if not found (or out of memory)
 */
dt_node_t *dt_get_subnode_namelen(dt_node_t *node, const char *name,
                                  size_t namelen, int create)
{
	list_for_each(&node->children, i) {
		dt_node_t *subnode = to_container(i, dt_node_t, child_node);

		if (!strncmp(name, subnode->name, namelen)) {
			if (subnode->name[namelen] == 0)
				return subnode;

			/* If the search name has no unit address, and it matches
			 * the base name of the node, then it's a match.
			 */
			if (subnode->name[namelen] == '@' &&
			    !memchr(name, '@', namelen))
				return subnode;
		}
	}

	if (create) {
		dt_node_t *subnode = alloc_type(dt_node_t);
		if (!subnode)
			return NULL;

		subnode->name = malloc(namelen + 1);
		if (!subnode->name) {
			free(subnode);
			return NULL;
		}

		memcpy(subnode->name, name, namelen);
		subnode->name[namelen] = 0;

		subnode->parent = node;

		list_init(&subnode->children);
		list_init(&subnode->props);
		list_init(&subnode->owners);
		list_init(&subnode->aliases);

		list_add(&node->children, &subnode->child_node);
		return subnode;
	}

	return NULL;
}

/**
 * Non-recursively searches for a subnode with a particular name,
 * optionally creating it.
 *
 * @param[in] node parent node of the node to be found
 * @param[in] name name of the node to be found
 * @param[in] create if non-zero, create the subnode if not found
 * @return pointer to subnode, or NULL if not found
 */
dt_node_t *dt_get_subnode(dt_node_t *node, const char *name, int create)
{
	return dt_get_subnode_namelen(node, name, strlen(name), create);
}

/**
 * Get a property of a node, optionally creating it.
 *
 * @param[in] node node in which to find/create the property
 * @param[in] name name of the property to get
 * @param[in] create if non-null, create the property if not found
 * @return a pointer to the property, or NULL if not found (or out of mem)
 */
dt_prop_t *dt_get_prop(dt_node_t *node, const char *name, int create)
{
	dt_prop_t *prop;

	list_for_each(&node->props, i) {
		prop = to_container(i, dt_prop_t, prop_node);

		if (!strcmp(name, prop->name))
			return prop;
	}

	if (!create)
		return NULL;

	prop = alloc_type(dt_prop_t);
	if (prop) {
		prop->name = strdup(name);
		if (!prop->name)
			return NULL;

		list_add(&node->props, &prop->prop_node);
	}

	return prop;
}

/**
 * dt_get_prop_len - get a property of a node and verify the length
 *
 * @param[in] node node in which to find/create the property
 * @param[in] name name of the property to get
 * @param[in] length the length to check, must not be zero
 *
 * This function acts like dt_get_prop(node, name, 0), except that it also
 * returns NULL if 'length' is not equal to the length of the property.
 */
dt_prop_t *dt_get_prop_len(dt_node_t *node, const char *name, size_t length)
{
	dt_prop_t *prop;

	prop = dt_get_prop(node, name, 0);
	if (prop && (prop->len != length))
		return NULL;

	return prop;
}

/**
 * Get a the value of a property as a null-terminated string
 *
 * @param[in] node node in which to look for the property
 * @param[in] name name of the property to get
 * @return a pointer to a NULL-terminated string, or NULL
 *   if the property is not found or is not null-terminated.
 */
char *dt_get_prop_string(dt_node_t *node, const char *name)
{
	dt_prop_t *prop = dt_get_prop(node, name, 0);
	if (!prop)
		return NULL;

	char *str = prop->data;
	if (!str || prop->len == 0 || str[prop->len - 1] != 0)
		return NULL;

	return str;
}

/**
 * Set a property of a node
 *
 * @param[in] node node in which to set the property
 * @param[in] name name of the property to set
 * @param[in] data new contents of the property
 * @param[in] len new length of the property
 * @return zero on success, ERR_NOMEM on failure
 */
int dt_set_prop(dt_node_t *node, const char *name, const void *data, size_t len)
{
	void *newdata = NULL;
	dt_prop_t *prop;

	prop = dt_get_prop(node, name, 1);
	if (!prop)
		return ERR_NOMEM;

	if (len != 0) {
		newdata = malloc(len);
		if (!newdata) {
			if (!prop->data)
				dt_delete_prop(prop);

			return ERR_NOMEM;
		}
	}

	free(prop->data);

	prop->data = newdata;
	prop->len = len;

	if (prop->data)
		memcpy(prop->data, data, len);

	return 0;
}

/**
 * Set a property of a node to a null-terminated string
 *
 * @param[in] node node in which to set the property
 * @param[in] name name of the property to set
 * @param[in] str new contents of the property
 * @return zero on success
 */
int dt_set_prop_string(dt_node_t *node, const char *name, const char *str)
{
	return dt_set_prop(node, name, str, strlen(str) + 1);
}

typedef struct merge_ctx {
	dt_node_t *dest;
	dt_node_t *top; /* Top of tree to consider -- used for phandle merge only */
	dt_merge_flags_t flags;
} merge_ctx_t;

static void delete_nodes_by_strlist(dt_node_t *node,
                                    const char *strlist, size_t len)
{
	size_t pos = 0;
	const char *str = strlist;
	dt_node_t *child;

	for (;;) {
		str = strlist_iterate(strlist, len, &pos);
		if (!str)
			return;

		child = dt_get_subnode(node, str, 0);
		if (child) {
			dt_delete_node(child);

			/* Rewind to try this name again; it may have lacked a unit address,
			 * and thus hit more than one node.
			 */
			pos = str - strlist;
		}
	}
}

static void delete_props_by_strlist(dt_node_t *node,
                                    const char *strlist, size_t len)
{
	size_t pos = 0;
	const char *str = strlist;
	dt_prop_t *prop;

	for (;;) {
		str = strlist_iterate(strlist, len, &pos);
		if (!str)
			return;

		prop = dt_get_prop(node, str, 0);
		if (prop)
			dt_delete_prop(prop);
	}
}

static void prepend_strlist(dt_node_t *dest, dt_node_t *src)
{
	dt_prop_t *prop, *destprop;
	size_t pos = 0;

	prop = dt_get_prop(src, "prepend-stringlist", 0);
	if (!prop)
		return;

	while (1) {
		const char *pname, *pdata;
		char *newstr, *olddata;
		size_t ppos;

		pname = strlist_iterate(prop->data, prop->len, &pos);
		if (!pname)
			return;

		ppos = pos;
		pdata = strlist_iterate(prop->data, prop->len, &pos);
		if (!pdata) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: ignoring junk '%s' at end of "
			         "prepend-stringlist in %s\n",
			         __func__, pname, src->name);
			return;
		}

		destprop = dt_get_prop(dest, pname, 1);
		if (!destprop)
			goto nomem;

		newstr = malloc(pos - ppos + destprop->len);
		if (!newstr)
			goto nomem;

		memcpy(newstr, pdata, pos - ppos);

		if (destprop->len)
			memcpy(newstr + pos - ppos, destprop->data, destprop->len);

		olddata = destprop->data;

		destprop->data = newstr;
		destprop->len += pos - ppos;

		free(olddata);
	}

nomem:
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
}

/** Record a phandle in the config node's guest_phandle.
 * The phandle will be created if it does not already exist.
 *
 * @param[in] gnode guest node to contain phandle
 * @param[in] cfgnode config-tree node whose guest_phandle should be set
 */
int dt_record_guest_phandle(dt_node_t *gnode, dt_node_t *cfgnode)
{
	uint32_t phandle = dt_get_phandle(gnode, 1);
	if (!phandle)
		return ERR_UNKNOWN; // could be out-of-mem or no phandles available

	cfgnode->guest_phandle = phandle;
	return 0;
}

static int merge_pre(dt_node_t *src, void *arg)
{
	merge_ctx_t *ctx = arg;

	if (!(ctx->flags & dt_merge_notfirst)) {
		ctx->flags |= dt_merge_notfirst;
	} else {
		ctx->dest = dt_get_subnode(ctx->dest, src->name, 1);

		if (!ctx->dest)
			return ERR_NOMEM;
	}

	if (!ctx->dest->upstream)
		ctx->dest->upstream = src;

	if (ctx->flags & dt_merge_special) {
		dt_prop_t *prop;

		if (dt_get_prop(src, "delete-subnodes", 0)) {
			list_for_each_delsafe(&ctx->dest->children, i, next) {
				dt_node_t *node = to_container(i, dt_node_t, child_node);
				dt_delete_node(node);
			}
		}

		prop = dt_get_prop(src, "delete-node", 0);
		if (prop && prop->len > 0)
			delete_nodes_by_strlist(ctx->dest, prop->data, prop->len);

		prop = dt_get_prop(src, "delete-prop", 0);
		if (prop && prop->len > 0)
			delete_props_by_strlist(ctx->dest, prop->data, prop->len);
	}

	if (ctx->flags & dt_merge_new_phandle)
		dt_record_guest_phandle(ctx->dest, src);

	list_for_each(&src->props, i) {
		dt_prop_t *prop = to_container(i, dt_prop_t, prop_node);

		if ((ctx->flags & dt_merge_new_phandle) &&
		    (!strcmp(prop->name, "phandle") ||
		     !strcmp(prop->name, "linux,phandle")))
			continue;

		if (ctx->flags & dt_merge_special) {
			if (!strcmp(prop->name, "delete-node"))
				continue;
			if (!strcmp(prop->name, "delete-subnodes"))
				continue;
			if (!strcmp(prop->name, "delete-prop"))
				continue;
			if (!strcmp(prop->name, "prepend-stringlist"))
				continue;
		}

		int ret = dt_set_prop(ctx->dest, prop->name, prop->data, prop->len);
		if (ret)
			return ret;
	}

	if (ctx->flags & dt_merge_special)
		prepend_strlist(ctx->dest, src);

	return 0;
}

static int merge_post(dt_node_t *src, void *arg)
{
	merge_ctx_t *ctx = arg;

	ctx->dest = ctx->dest->parent;
	return 0;
}

/**
 * Merge one subtree into another.
 *
 * @param[in] dest tree to merge into
 * @param[in] src tree to merge from
 * @param[in] special if non-zero, honor delete-node, delete-prop, etc.
 * @return zero on success, non-zero on failure
 *
 * The contents of src are merged into dest, recursively.
 *
 * Any properties that exist in both the source and destination
 * nodes are resolved in favor of the source node.
 *
 * The names of the toplevel dest and src trees are ignored; that is,
 * this function will not create a new tree with both inputs as siblings
 * if the name does not match, and the name of the resultant tree root
 * will be that of the destination input.
 */
int dt_merge_tree(dt_node_t *dest, dt_node_t *src, dt_merge_flags_t flags)
{
	merge_ctx_t ctx = {
		.dest = dest,
		.flags = flags,
	};

	return dt_for_each_node(src, &ctx, merge_pre, merge_post);
}

/**
 * Process a node-update-handle configuration property.
 *
 * @param[in] src pointer to the node-update-handle node (or a child thereof)
 * @param[in] prop the specific property to process
 * @param[in] target the node in the guest device tree to update
 *
 * The node-update-phandle configuration subnode is intended to handle this
 * situation:
 *
 * Configuration tree:
 *
 * // Create node Ga in the guest device tree
 * Ca {
 * 	device = "/path-to/Ha";
 * 	node-update-phandle = {
 * 		foo = <&Cb>;
 * 	};
 * };
 *
 * // Create node Gb in the guest device tree
 * Cb {
 * 	device = "/path-to/Hb";
 * };
 *
 * Hardware tree:
 *
 * Ha {
 * 	...
 * };
 *
 * Hb {
 *      ...
 * };
 *
 * Resulting guest tree:
 *
 * Ga {
 * 	...
 *      foo = <&Gb>;
 * };
 *
 * Gb {
 * 	...
 * };
 *
 * This function creates the "foo = <&Gb>" property.
 */
static int do_merge_phandle(dt_node_t *src, void *arg)
{
	merge_ctx_t *ctx = arg;
	uint32_t phandle;
	dt_node_t *node;
	int i, ret = 0;

	/* This function is called recursively, and the top-level node is the
	 * "node-update-phandle" source node.  So we use notfirst to skip the
	 * top level, so that we don't create a node called
	 * "node-update-phandle" in the guest.
	 */
	if (!(ctx->flags & dt_merge_notfirst)) {
		ctx->flags |= dt_merge_notfirst;
	} else {
		/* Make sure the guest has a node with the same name as the source
		 * node, and traverse into that node as well.  ctx->dest is the
		 * node in the guest device tree that we are updating.
		 */
		ctx->dest = dt_get_subnode(ctx->dest, src->name, 1);
		if (!ctx->dest)
			return ERR_NOMEM;
	}

	dt_record_guest_phandle(ctx->dest, src);

	list_for_each(&src->props, l) {
		dt_prop_t *prop = to_container(l, dt_prop_t, prop_node);

		// Validate the property
		if (!prop->len || (prop->len % sizeof(uint32_t)))
			return ERR_BADTREE;

		int count = prop->len / sizeof(uint32_t);
		uint32_t *phandles = malloc(count * sizeof(uint32_t));
		if (!phandles)
			return ERR_NOMEM;

		// Iterate over all the phandles in this property
		for (ret = 0, i = 0; i < count; i++) {
			// Get the phandle from the src node
			phandle = ((uint32_t *) prop->data)[i];

			// Find the other config-tree node the phandle points to
			node = dt_lookup_phandle(ctx->top, phandle);
			if (!node) {
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
				         "%s: node %s/%s: prop %s: invalid phandle 0x%x\n",
				         __func__, src->parent->name,
				         src->name, prop->name, phandle);
				ret = ERR_BADTREE;
				break;
			}
			if (!node->guest_phandle) {
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
				         "%s: node %s/%s: no guest phandle for %s\n",
				         __func__, src->name,
				         src->parent->name, node->name);
				ret = ERR_BADTREE;
				break;
			}

			phandles[i] = node->guest_phandle;
		}

		if (!ret)
			// Finally, put the phandle property into the target node
			ret = dt_set_prop(ctx->dest, prop->name, phandles,
					  count * sizeof(uint32_t));

		free(phandles);
	}

	return 0;
}

/**
 * dt_process_node_update - process the node-update subnodes
 @ @param[in] guest guest whose tree to update
 * @param[in] target target node to update
 * @param[in] config node that contains the "node-update" subnodes
 *
 * This function scans a given configuration node for the node-update and
 * node-update-phandle subnodes, and then it updates the target node
 * accordingly.
 *
 * The phandle portion of the update will be deferred until all
 * non-phandle merging has finished, to give a chance for the phandle
 * linkage to be established with the config-tree nodes.  To run
 * the phandle merges which have been queued up, call
 * dt_run_deferred_phandle_updates().
 */
int dt_process_node_update(guest_t *guest, dt_node_t *target, dt_node_t *config)
{
	int ret;

	list_for_each(&config->children, i) {
		dt_node_t *subnode = to_container(i, dt_node_t, child_node);

		if (!strcmp(subnode->name, "node-update") ||
		    !strncmp(subnode->name, "node-update@", strlen("node-update@"))) {
			ret = dt_merge_tree(target, subnode,
			                    dt_merge_special | dt_merge_new_phandle);
			if (ret < 0) {
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
				         "%s: error %d merging %s on %s\n",
				         __func__, ret, subnode->name, config->name);
				return ret;
			}
		}

		if (!strcmp(subnode->name, "node-update-phandle") ||
		    !strncmp(subnode->name, "node-update-phandle@",
		             strlen("node-update-phandle@"))) {
			update_phandle_t *up = malloc(sizeof(update_phandle_t));
			if (!up) {
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
				         "%s: out of memory\n", __func__);
				return ERR_NOMEM;
			}

			up->src = subnode;
			up->dest = target;
			up->tree = guest->partition;
			list_add(&guest->phandle_update_list, &up->node);
		}
	}

	return 0;
}

void dt_run_deferred_phandle_updates(guest_t *guest)
{
	list_for_each_delsafe(&guest->phandle_update_list, i, next) {
		update_phandle_t *up = to_container(i, update_phandle_t, node);
		int ret;

		/* Merge the phandles into the target  */
		merge_ctx_t ctx = {
			.dest = up->dest,
			.top = up->tree,
		};

		ret = dt_for_each_node(up->src, &ctx,
		                       do_merge_phandle, merge_post);
		if (ret < 0) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: do_merge_phandle on %s/%s returned %d\n",
			         __func__, up->src->parent->name,
			         up->src->name, ret);
		}

		list_del(&up->node);
		free(up);
	}
}

typedef struct print_ctx {
	queue_t *out;
	int depth;
} print_ctx_t;

static int is_strlist(const char *str, size_t len)
{
	int last_was_null = 0;

	if (len == 0)
		return 0;

	if (str[len - 1] != 0)
		return 0;

	for (size_t i = 0; i < len; i++) {
		if (str[i] == 0) {
			if (last_was_null)
				return 0;

			last_was_null = 1;
		} else if (str[i] < 32 || str[i] > 127) {
			return 0;
		} else {
			last_was_null = 0;
		}
	}

	return 1;
}

static void print_strlist(print_ctx_t *ctx, dt_prop_t *prop)
{
	const char *str = prop->data;
	size_t pos = 0;

	while (pos < prop->len) {
		qprintf(ctx->out, 1, "%s\"%s\"",
		        pos == 0 ? " = " : ", ", &str[pos]);
		pos += strlen(&str[pos]) + 1;
	}
}

static void print_hex_bytes(print_ctx_t *ctx, dt_prop_t *prop)
{
	uint8_t *data = prop->data;

	for (unsigned int j = 0; j < prop->len; j++) {
		qprintf(ctx->out, 1, "%s%02x",
		        j == 0 ? " = [" : " ", data[j]);
	}

	qprintf(ctx->out, 1, "]");
}

static void print_cells(print_ctx_t *ctx, dt_prop_t *prop, int always_dec)
{
	uint32_t *data = prop->data;

	for (unsigned int j = 0; j < prop->len / 4; j++) {
		int dec = always_dec || data[j] < 256;

		qprintf(ctx->out, 1, dec ? "%s%u" : "%s%#x",
		        j == 0 ? " = <" : " ", data[j]);
	}

	qprintf(ctx->out, 1, ">");
}

static int print_pre(dt_node_t *tree, void *arg)
{
	print_ctx_t *ctx = arg;

	for (int i = 0; i < ctx->depth; i++)
		qprintf(ctx->out, 1, "\t");

	if (ctx->depth == 0)
		qprintf(ctx->out, 1, "/ {\n");
	else
		qprintf(ctx->out, 1, "%s {\n", tree->name);

	ctx->depth++;

	list_for_each(&tree->props, i) {
		dt_prop_t *prop = to_container(i, dt_prop_t, prop_node);

		for (int j = 0; j < ctx->depth; j++)
			qprintf(ctx->out, 1, "\t");

		qprintf(ctx->out, 1, "%s", prop->name);

		if (prop->len == 0)
			goto done;

		if ((prop->len & 3) == 0) {
			if (strstr(prop->name, "frequency") ||
			    strstr(prop->name, "-size") ||
			    strstr(prop->name, "-count") ||
			    strstr(prop->name, "-speed") ||
			    strstr(prop->name, "-len") ||
			    strstr(prop->name, "-index") ||
			    strstr(prop->name, "num-") ||
			    strstr(prop->name, "handle")) {
				print_cells(ctx, prop, 1);
				goto done;
			}

			if (!strcmp(prop->name, "reg") ||
			    strstr(prop->name, "ranges") ||
			    strstr(prop->name, "interrupt")) {
				print_cells(ctx, prop, 0);
				goto done;
			}
		}

		if (is_strlist(prop->data, prop->len))
			print_strlist(ctx, prop);
		else if (prop->len & 3)
			print_hex_bytes(ctx, prop);
		else
			print_cells(ctx, prop, 0);

done:
		qprintf(ctx->out, 1, ";\n");
	}

	return 0;
}

static int print_post(dt_node_t *tree, void *arg)
{
	print_ctx_t *ctx = arg;
	ctx->depth--;

	for (int i = 0; i < ctx->depth; i++)
		qprintf(ctx->out, 1, "\t");

	qprintf(ctx->out, 1, "};\n");
	return 0;
}

/**
 * Print a device tree's contents to a libos queue.
 *
 * @param[in] tree device tree to print
 * @param[in] out libos queue to print to
 */
void dt_print_tree(dt_node_t *tree, queue_t *out)
{
	print_ctx_t ctx = { .out = out };

	dt_for_each_node(tree, &ctx, print_pre, print_post);
}

/** Check whether a given node is compatible with a given string.
 *
 * @param[in] node node to check
 * @param[in] compat compatible string to check
 * @return non-zero if the node is compatible
 */
int dt_node_is_compatible(dt_node_t *node, const char *compat)
{
	dt_prop_t *prop;
	const char *str;
	size_t pos = 0;

	prop = dt_get_prop(node, "compatible", 0);
	if (!prop)
		return 0;

	for (;;) {
		str = strlist_iterate(prop->data, prop->len, &pos);
		if (!str)
			return 0;

		if (!strcmp(compat, str))
			return 1;
	}
}

typedef struct compat_ctx {
	dt_callback_t callback;
	const char *compat;
	void *arg;
} compat_ctx_t;

static int compat_callback(dt_node_t *node, void *arg)
{
	compat_ctx_t *ctx = arg;

	if (dt_node_is_compatible(node, ctx->compat))
		return ctx->callback(node, ctx->arg);

	return 0;
}

/** Iterate over each compatible node.
 *
 * @param[in] tree root of tree to search
 * @param[in] compat compatible string to search for
 * @param[in] callback function to call for each compatible node
 * @param[in] arg opaque argument to callback function
 * @return if non-zero, the return value of the final callback function
 *
 * If a callback returns non-zero, the iteration will be aborted.
 */
int dt_for_each_compatible(dt_node_t *tree, const char *compat,
                            dt_callback_t callback, void *arg)
{
	compat_ctx_t ctx = {
		.callback = callback,
		.compat = compat,
		.arg = arg
	};

	return dt_for_each_node(tree, &ctx, compat_callback, NULL);
}

static int first_callback(dt_node_t *node, void *arg)
{
	dt_node_t **ret = arg;
	*ret = node;
	return 1;
}

/** Return the first compatible node, when only one is expected.
 *
 * @param[in] tree root of tree to search
 * @param[in] compat compatible string to search for
 * @return the first compatible node, or NULL if none found
 */
dt_node_t *dt_get_first_compatible(dt_node_t *tree, const char *compat)
{
	dt_node_t *ret = NULL;
	dt_for_each_compatible(tree, compat, first_callback, &ret);
	return ret;
}

typedef struct propvalue_ctx {
	dt_callback_t callback;
	const char *propname;
	const void *value;
	size_t len;
	void *arg;
} propvalue_ctx_t;

static int propvalue_callback(dt_node_t *node, void *arg)
{
	propvalue_ctx_t *ctx = arg;
	dt_prop_t *prop = dt_get_prop(node, ctx->propname, 0);

	if (prop) {
		if (!ctx->value ||
		    (ctx->len == prop->len &&
		     !memcmp(ctx->value, prop->data, prop->len)))
			return ctx->callback(node, ctx->arg);
	}

	return 0;
}


/** Iterate over each node with a given value in a given property.
 *
 * @param[in] tree root of tree to search
 * @paramlin] propname property name to search for
 * @param[in] value value the property must hold, or NULL to find any
 *   instance of the property
 * @param[in] callback function to call for each matching node
 * @param[in] arg opaque argument to callback function
 * @return if non-zero, the return value of the final callback function
 *
 * If a callback returns non-zero, the iteration will be aborted.
 */
int dt_for_each_prop_value(dt_node_t *tree, const char *propname,
                           const void *value, size_t len,
                           dt_callback_t callback, void *arg)
{
	propvalue_ctx_t ctx = {
		.callback = callback,
		.propname = propname,
		.value = value,
		.len = len,
		.arg = arg
	};

	return dt_for_each_node(tree, &ctx, propvalue_callback, NULL);
}

/** Return the node associated with an alias or path.
 *
 * @param[in] tree root of tree to search
 * @param[in] name alias or path to look for
 * @return if non-zero, the found node
 *
 * This function first searches for an alias matching the provided
 * name; if it is not found, it then treats the name as a path.
 */
dt_node_t *dt_lookup_alias(dt_node_t *tree, const char *name)
{
	dt_node_t *node;
	const char *path;

	node = dt_get_subnode(tree, "aliases", 0);
	if (node) {
		path = dt_get_prop_string(node, name);

		if (path)
			name = path;
	}

	return dt_lookup_path(tree, name, 0);
}

/** Return the node associated with a path, optionally creating it.
 *
 * @param[in] tree root of tree to search
 * @param[in] path path to look for
 * @return if non-zero, the found node
 *
 * Leading, consecutive, and trailing slashes are ignored.
 */
dt_node_t *dt_lookup_path(dt_node_t *tree, const char *path, int create)
{
	dt_node_t *node = tree;

	do {
		size_t namelen;
		const char *slash;

		while (*path == '/')
			path++;

		if (*path == 0)
			break;

		slash = strchr(path, '/');
		if (slash)
			namelen = slash - path;
		else
			namelen = strlen(path);

		node = dt_get_subnode_namelen(node, path, namelen, create);
		if (!node)
			return NULL;

		path = slash;
	} while (path);

	return node;
}

/** Return the node associated with a phandle.
 *
 * @param[in] tree root of tree to search
 * @param[in] phandle phandle value to look for
 * @return if non-zero, the found node
 *
 * The ePAPR "phandle" value is searched for first, followed
 * by the legacy "linux,phandle" value.
 */
dt_node_t *dt_lookup_phandle(dt_node_t *tree, uint32_t phandle)
{
	dt_node_t *node = NULL;

	dt_for_each_prop_value(tree, "phandle", &phandle, 4,
	                       first_callback, &node);

	if (!node)
		dt_for_each_prop_value(tree, "linux,phandle", &phandle, 4,
		                       first_callback, &node);

	return node;
}

static int find_free_phandle_callback(dt_node_t *node, void *arg)
{
	uint32_t phandle;
	uint32_t *free_phandle = (uint32_t *) arg;

	phandle = dt_get_phandle(node, 0);

	// Check for wrap-around
	if (phandle == ~0U) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			 "%s: no free phandles\n", __func__);
		return ERR_BADTREE;
	}

	if (phandle && (phandle >= *free_phandle))
		*free_phandle = phandle + 1;

	return 0;
}

/**
 * find_free_phandle - return the smallest unusued phandle
 *
 * Every time this function is called, it returns a new number.
 *
 * returns 0 if error, or >0 phandle to use
 */
static uint32_t find_free_phandle(void)
{
	static uint32_t free_phandle;	// The next free phandle to use
	static uint32_t lock;
	uint32_t phandle = 0;
	register_t saved = spin_lock_intsave(&lock);
	int ret;

	/* The first time we're called, find the lowest phandle in the
	 * hardware tree.
	 */
	if (!free_phandle) {
		ret = dt_for_each_node(hw_devtree, &free_phandle,
				       find_free_phandle_callback, NULL);
		if (ret)
			goto out;

		// What if there are no phandles in the device tree at all?
		if (!free_phandle)
			free_phandle = 1;
	}

	if (free_phandle == ~0U) {
		spin_unlock_intsave(&lock, saved);
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			 "%s: no more phandles\n", __func__);
		return 0;
	}

	phandle = free_phandle++;

out:
	spin_unlock_intsave(&lock, saved);
	return phandle;
}

/** Return the phandle of a node.
 *
 * @param[in] node node of which to retrieve phandle
 * @param[in] create if non-zero, create a new phandle
 * @return if non-zero, the node's phandle
 *
 * The ePAPR "phandle" value is searched for first, followed
 * by the legacy "linux,phandle" value.
 *
 * If 'create' is zero, then 'tree' is ignored.
 */
uint32_t dt_get_phandle(dt_node_t *node, int create)
{
	dt_prop_t *prop = dt_get_prop(node, "phandle", 0);

	if (!prop)
		prop = dt_get_prop(node, "linux,phandle", 0);

	if (!prop && create) {
		// Create the phandle
		uint32_t phandle;
		int ret;

		phandle = find_free_phandle();
		if (!phandle)
			return 0;

		ret = dt_set_prop(node, "phandle", &phandle, 4);
		if (ret)
			goto nomem;

		// Linux expects linux,phandle
		ret = dt_set_prop(node, "linux,phandle", &phandle, 4);
		if (ret)
			goto nomem;

		return phandle;
	}

	if (!prop || prop->len != 4)
		return 0;

	return *(const uint32_t *)prop->data;

nomem:
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
	         "%s: out of memory, cannot set phandle in %s\n", __func__,
	         node->name);
	return 0;
}

/** Retrieve the full path of a node, or a path relative to a subtree
 *
 * @param[in] tree root node of subtree to find path relative to, or NULL
 *   to find full path.
 * @param[in] node node of which to retrieve path
 * @param[in] buf buffer in which to place path
 * @param[in] buflen length of buf, including null terminator
 * @return the number of bytes required to hold the full path
 *
 * The function succeeds if the return value <= buflen.
 */
size_t dt_get_path(dt_node_t *tree, dt_node_t *node, char *buf, size_t buflen)
{
	size_t len = 1, used_pos = buflen - 1, pos = buflen - 1;

	if (buflen != 0)
		buf[buflen - 1] = 0;

	while (node != tree && node->parent) {
		size_t namelen = strlen(node->name);

		pos -= namelen + 1;
		len += namelen + 1;

		if (len <= buflen) {
			buf[pos] = '/';
			memcpy(&buf[pos + 1], node->name, namelen);
			used_pos = pos;
		}

		node = node->parent;
	}

	if (buflen != 0)
		memmove(buf, buf + used_pos, buflen - used_pos);

	return len;
}

/**
 * dt_copy_properties - copy the properties of a node to another node
 * @source: the node to copy from
 * @target: the node to copy to
 *
 * This function copies the contents of a node.  The target node can be in
 * the same tree or another tree.
 *
 * If a particular property already exists in the target node, then its
 * contents are simply overridden by those from the property in the source
 * code.
 *
 * @return 0 for success, or error code
 */
int dt_copy_properties(dt_node_t *source, dt_node_t *target)
{
	int ret;

	list_for_each(&source->props, i) {
		dt_prop_t *prop = to_container(i, dt_prop_t, prop_node);

		ret = dt_set_prop(target, prop->name, prop->data, prop->len);
		if (ret)
			return ret;
	}

	return 0;
}


