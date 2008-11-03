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

static int destroy_node(dt_node_t *node, void *arg)
{
	list_for_each_delsafe(&node->props, i, next) {
		dt_prop_t *prop = to_container(i, dt_prop_t, prop_node);
		
		list_del(i);

		free(prop->name);
		free(prop->data);

		free(prop);
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
