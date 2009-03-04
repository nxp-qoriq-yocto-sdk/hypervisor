/*
 * Copyright (C) 2009 Freescale Semiconductor, Inc.
 * Author: Timur Tabi <timur@freescale.com>
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
 * Device tree parser
 *
 * This module reads /proc/device-tree and creates a live tree of the
 * contents.
 */

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "parse_dt.h"

/** Root node for the whole device tree */
static gdt_node_t *root_node;

/**
 * Read a device tree property file into memory
 */
static int read_property(const char *filename, void **buffer, size_t *size)
{
	off_t off;
	int f;
	struct stat buf;
	int ret;

	f = open(filename, O_RDONLY);
	if (f == -1)
		return errno;

	if (fstat(f, &buf)) {
		ret = errno;
		goto fail;
	}

	off = buf.st_size;
	if (off) {
		*buffer = malloc(off);
		if (!*buffer) {
			ret = errno;
			goto fail;
		}

		if (read(f, *buffer, off) != off) {
			ret = errno;
			goto fail;
		}
	} else
		*buffer = NULL;


	if (size)
		*size = off;

	close(f);
	return 0;

fail:
	free(*buffer);
	close(f);

	return ret;
}

/**
 * Returns non-zero if the property is compatible
 * @param[in] haystack: pointer to NULL-terminated strings that contain
 *                      the value of the 'compatible' property to test
 * @param[in] length: length of 'haystack'
 * @param[in] needle: compatible string to search for
 *
 * Returns zero if not compatible, non-zero if compatible
 */
static int is_compatible(const char *haystack, size_t length, const char *needle)
{
	/* Length must be non-zero, and the last character of haystack must
	 * be zero.  Otherwise, the strcmp() below could read past the end of
	 * haystack.
	 */
	if (!length || haystack[length - 1])
		return 0;

	while (length > 0) {
		int l;

		if (strcmp(haystack, needle) == 0)
			return 1;
		l = strlen(haystack) + 1;
		haystack += l;
		length -= l;
	}

	return 0;
}

/**
 * gdt_is_compatible - returns true or false if the given node is compatible
 * @param node: node to test
 * @param compatible: compatible string to search for
 *
 * Returns non-zero if this node is compatible with the given string, or
 * zero if it's not compatible or doesn't have a 'compatible' property.
 */
int gdt_is_compatible(gdt_node_t *node, const char *compatible)
{
	gdt_prop_t *prop = gdt_get_property(node, "compatible");

	if (!prop || !prop->data)
		return 0;

	return is_compatible(prop->data, prop->len, compatible);
}

/**
 * gdt_find_next_compatible - find the next compatible node
 * @param start: the previous node that was compatible, or NULL for the
 *               first
 * @param compatible: the compatible string to search for
 *
 * This function scans the list of nodes for the first one that matches the
 * @compatible string.  If @start is NULL, the function starts at the root
 * node.  Otherwise, it starts at the node *after* the given node.  This
 * allows the caller to pass the previously found node to find the next one.
 *
 * Returns the next matching node, or NULL if there are no more.
 */
gdt_node_t *gdt_find_next_compatible(gdt_node_t *start, const char *compatible)
{
	gdt_node_t *node;

	if (start)
		node = start->next;
	else
		node = root_node;

	while (node) {
		if (gdt_is_compatible(node, compatible))
			break;
		node = node->next;
	}

	return node;
}

/**
 * gdt_get_property - return the specified property for a given node
 * @param node: the node
 * @param name: the name of the property
 *
 * Returns the found property, or NULL
 */
gdt_prop_t *gdt_get_property(gdt_node_t *node, const char *name)
{
	gdt_prop_t *prop = node->props;

	while (prop) {
		if (strcmp(prop->name, name) == 0)
			return prop;
		prop = prop->sibling;
	}

	return NULL;
}

/**
 * gdt_find_next_name - find the next node matching the name
 * @param start: the previous node that was compatible, or NULL for the
 *               first
 * @param name: the compatible string to search for
 *
 * This function scans the list of nodes for the first one that matches the
 * @name string.  If @start is NULL, the function starts at the root
 * node.  Otherwise, it starts at the node *after* the given node.  This
 * allows the caller to pass the previously found node to find the next one.
 *
 * Returns the next matching node, or NULL if there are no more.
 */
gdt_node_t *gdt_find_next_name(gdt_node_t *start, const char *name)
{
	gdt_node_t *node;

	if (start)
		node = start->next;
	else
		node = root_node->next;

	while (node) {
		if (strcmp(node->name, name) == 0)
			break;
		node = node->next;
	}

	return node;
}

/**
 * parse_node_directory - parse a /proc/device-tree path and its subdirs
 * @param[in] name name of the directory/node to parse
 *
 * The current directory must be the the desired starting point in
 * /proc/device-tree.
 *
 * This function parses a directory heirarchy and creates a live tree
 * representation of it.
 *
 * Every call to this function creates (if it doesn't fail) a new gdt_node_t
 * object, and populates it with the properties for that node.  If there are
 * child nodes in the current directory, this functions will call itself
 * recursively to creates those nodes and link them to the current node.
 *
 * Returns a pointer to the newly created root node, or NULL if error.  If
 * NULL is returned, there will probably be a memory leak.
 */
static gdt_node_t *parse_node_directory(const char *name)
{
	static gdt_node_t *link = NULL;
	struct dirent *dp;
	gdt_node_t *node, *child, *prev_node = NULL;
	gdt_prop_t *prop, *prev_prop = NULL;

	node = calloc(1, sizeof(gdt_node_t));
	if (!node)
		return node;

	// The root node has no name
	if (name) {
		chdir(name);
		node->name = strdup(name);
	}

	if (link)
		link->next = node;
	link = node;

	DIR *dir = opendir(".");
	if (!dir)
		return NULL;

	while ((dp = readdir(dir)) != NULL) {
		// Is it a child node?
		if ((dp->d_type == DT_DIR) && (dp->d_name[0] != '.')) {
			child = parse_node_directory(dp->d_name);
			if (!child)
				return NULL;

			if (prev_node)
				prev_node->sibling = child;
			else
				node->child = child;

			child->parent = node;
			prev_node = node;
		}
		// Is it a property?
		if (dp->d_type == DT_REG) {
			prop = malloc(sizeof(gdt_prop_t));
			if (!prop)
				return NULL;

			if (prev_prop)
				prev_prop->sibling = prop;
			else
				node->props = prop;

			prop->sibling = NULL;
			prop->name = strdup(dp->d_name);

			int ret = read_property(prop->name, &prop->data, &prop->len);
			if (ret)
				return NULL;

			prev_prop = prop;
		}
	}

	closedir(dir);

	if (name)
		chdir("..");

	return node;
}

/**
 * gdt_load_tree - parses a device tree file heirarchy into a live tree
 * @param[in] root_path path to starting directory
 *
 * This function takes a path to a directory in /proc/device-tree, and scans
 * the directory heirarchy underneath it.  The resulting live tree is
 * assigned to the global variable root_node.
 *
 * Returns zero if failure, non-zero if success.
 */
int gdt_load_tree(const char *root_path)
{
	char current[PATH_MAX];

	getcwd(current, PATH_MAX);

	chdir(root_path);

	root_node = parse_node_directory(NULL);

	chdir(current);

	return root_node ? 1 : 0;
}

