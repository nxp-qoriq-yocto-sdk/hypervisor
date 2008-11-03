/** @file
 * Operations on live (unflattened) device trees
 */
/* Copyright (C) 2008 Freescale Semiconductor, Inc.
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

#include <errors.h>
#include <devtree.h>

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

			fdtprop = fdt_offset_ptr(fdt, offset, sizeof(*prop));
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
			fdtprop = fdt_offset_ptr(fdt, offset, sizeof(*prop) + prop->len);
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
	delete_node(top);
	return NULL;

nomem:
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
	         "unflatten_dev_tree: out of memory\n");
	delete_node(top);
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
 * The callback is performed on each node of the tree.  When postvisit is
 * set, it is safe to remove nodes as they are visited, but it is not
 * safe to remove the parent or any sibling of the visited node.
 *
 * If a callback returns non-zero, the traversal is aborted, and the
 * return code propagated.
 */
int for_each_node(dt_node_t *tree, void *arg,
                  int (*previsit)(dt_node_t *node, void *arg),
                  int (*postvisit)(dt_node_t *node, void *arg))
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

static void destroy_prop(dt_prop_t *prop)
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
		destroy_prop(prop);
	}
	
	if (node->parent)
		list_del(&node->child_node);

	free(node->name);
	free(node);
	return 0;
}

void delete_node(dt_node_t *tree)
{
	for_each_node(tree, NULL, NULL, destroy_node);
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

	ret = for_each_node(tree, fdt, flatten_pre, flatten_post);
	if (ret)
		return ret;

	return fdt_finish(fdt);
}

/**
 * Non-recursively searches for a subnode with a particular name
 *
 * @param[in] node parent node of the node to be found
 * @param[in] name name of the node to be found
 * @param[in] create if non-zero, create the subnode if not found
 * @return pointer to subnode, or NULL if not found
 */
dt_node_t *dt_get_subnode(dt_node_t *node, const char *name, int create)
{
	list_for_each(&node->children, i) {
		dt_node_t *subnode = to_container(i, dt_node_t, child_node);

		if (!strcmp(name, subnode->name))
			return subnode;
	}

	if (create) {
		dt_node_t *subnode = alloc_type(dt_node_t);
		subnode->name = strdup(name);
		subnode->parent = node;

		list_init(&subnode->children);
		list_init(&subnode->props);

		list_add(&node->children, &subnode->child_node);
		return subnode;
	}

	return NULL;
}

/**
 * Get a property of a node
 *
 * @param[in] node node in which to find/create the property
 * @param[in] name name of the property to get
 * @param[in] create if non-null, create the property if not found
 * @return a pointer to the property, or NULL if not found
 */
dt_prop_t *dt_get_prop(dt_node_t *node, const char *name, int create)
{
	dt_prop_t *prop;

	list_for_each(&node->props, i) {
		prop = to_container(i, dt_prop_t, prop_node);

		if (!strcmp(name, prop->name))
			return prop;
	}

	prop = alloc_type(dt_prop_t);
	if (prop) {
		prop->name = strdup(name);
		list_add(&node->props, &prop->prop_node);
	}

	return prop;
}

/**
 * Set a property of a node
 *
 * @param[in] node node in which to set the property
 * @param[in] name name of the property to set
 * @param[in] data new contents of the property
 * @param[in] len new length of the property
 * @return zero on success
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
				destroy_prop(prop);

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

typedef struct merge_ctx {
	dt_node_t *dest;
	int notfirst;
} merge_ctx_t;

static int merge_pre(dt_node_t *src, void *arg)
{
	merge_ctx_t *ctx = arg;

	if (!ctx->notfirst) {
		ctx->notfirst = 1;
	} else {
		ctx->dest = dt_get_subnode(ctx->dest, src->name, 1);

		if (!ctx->dest)
			return ERR_NOMEM;
	}

	list_for_each(&src->props, i) {
		dt_prop_t *prop = to_container(i, dt_prop_t, prop_node);
		
		int ret = dt_set_prop(ctx->dest, prop->name, prop->data, prop->len);
		if (ret)
			return ret;
	}

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
 * @param[in] deletion if non-zero, honor delete-node, delete-prop, etc.
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
int dt_merge_tree(dt_node_t *dest, dt_node_t *src, int deletion)
{
	merge_ctx_t ctx = { .dest = dest };

	return for_each_node(src, &ctx, merge_pre, merge_post);
}

typedef struct print_ctx {
	queue_t *out;
	int depth;
} print_ctx_t;

static int is_strlist(const char *str, size_t len)
{
	int last_was_null = 0;

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

static int print_pre(dt_node_t *tree, void *arg)
{
	print_ctx_t *ctx = arg;

	for (int i = 0; i < ctx->depth; i++)
		qprintf(ctx->out, "\t");

	if (ctx->depth == 0)
		qprintf(ctx->out, "{\n");
	else
		qprintf(ctx->out, "%s {\n", tree->name);

	ctx->depth++;

	list_for_each(&tree->props, i) {
		dt_prop_t *prop = to_container(i, dt_prop_t, prop_node);

		for (int j = 0; j < ctx->depth; j++)
			qprintf(ctx->out, "\t");
		
		qprintf(ctx->out, "%s", prop->name);

		if (is_strlist(prop->data, prop->len)) {
			const char *str = prop->data;
			size_t pos = 0;

			while (pos < prop->len) {
				qprintf(ctx->out, "%s\"%s\"",
				        pos == 0 ? " = " : ", ", &str[pos]);
				pos += strlen(&str[pos]) + 1;
			}
		} else if (prop->len & 3) {
			uint8_t *data = prop->data;
			
			for (int j = 0; j < prop->len; j++) {
				qprintf(ctx->out, "%s%02x",
				        j == 0 ? " = [" : " ", data[j]);
			}

			qprintf(ctx->out, "]");
		} else if (prop->len != 0) {
			uint32_t *data = prop->data;

			for (int j = 0; j < prop->len / 4; j++)
				qprintf(ctx->out, "%s%#x", j == 0 ? " = <" : " ", data[j]);

			qprintf(ctx->out, ">");
		}

		qprintf(ctx->out, ";\n");
	}

	return 0;
}

static int print_post(dt_node_t *tree, void *arg)
{
	print_ctx_t *ctx = arg;
	ctx->depth--;

	for (int i = 0; i < ctx->depth; i++)
		qprintf(ctx->out, "\t");

	qprintf(ctx->out, "}\n");	
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

	for_each_node(tree, &ctx, print_pre, print_post);
}
