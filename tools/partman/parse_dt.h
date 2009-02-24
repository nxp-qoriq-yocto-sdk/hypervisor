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
 * Device tree parser header file
 *
 * This module reads /proc/device-tree and creates a live tree of the
 * contents.
 */

typedef struct gdt_prop {
	struct gdt_prop *sibling;
	char *name;
	void *data;
	size_t len;
} gdt_prop_t;

typedef struct gdt_node {
	char *name;
	struct gdt_node *parent;
	struct gdt_node *sibling;
	struct gdt_node *child;
	struct gdt_node *next;
	struct gdt_prop *props;
} gdt_node_t;

int gdt_load_tree(const char *root_path);
gdt_node_t *gdt_find_next_compatible(gdt_node_t *node, const char *compatible);
gdt_node_t *gdt_find_next_name(gdt_node_t *start, const char *name);
gdt_prop_t *gdt_get_property(gdt_node_t *node, const char *property);
int gdt_is_compatible(gdt_node_t *node, const char *compatible);
