/** @file
 * GDB Target Description Generator.
 */

/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
 * Author: Anmol P. Paralkar <anmol@freescale.com>
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gdb-register-defs.h"

FILE *ofile;

static inline void emit_registers(arch_t arch, struct register_description *registers, unsigned int count);
static inline void emit_xml_version(void);
static inline void emit_doc_type_target(void);
static inline void emit_gnu_copyright(void);
static inline void emit_freescale_version(arch_t arch);
static inline void emit_target_version(void);
static inline void emit_architecture_declaration(arch_t arch);
static inline void emit_includes(arch_t arch);
static inline void emit_sprs_feature(arch_t arch);
static inline void emit_pmrs_feature(arch_t arch);
static inline void emit_close_feature(void);
static inline void emit_close_target(void);
static inline void emit_arch_description(arch_t arch);
static inline void emit_power_core_gnu_copyright(void);
static inline void emit_doc_type_feature(void);
static inline void emit_power_core_feature(void);
static inline void emit_power_core_description(arch_t arch);
static inline void emit_power_fpu_feature(void);
static inline void emit_power_fpu_description(arch_t arch);
static inline void emit_freescale_copyright(void);
static inline void emit_num_regs_value(arch_t arch);
static inline void emit_num_reg_bytes_value(arch_t arch);
static inline void emit_buf_max_value(arch_t arch);
static inline void emit_buf_max_hex_value(arch_t arch);
static inline void emit_reg_details(arch_t arch, struct register_description *registers, unsigned int count);
static inline void emit_reg_table(arch_t arch);
static inline void emit_reg_table_entry(arch_t arch);
static inline void emit_include_td_defs(void);
static inline void emit_power_core_descriptions(void);
static inline void emit_power_fpu_descriptions(void);
static inline void emit_arch_descriptions(void);
static inline void emit_num_regs(void);
static inline void emit_num_reg_bytes(void);
static inline void emit_buf_max(void);
static inline void emit_buf_max_hex(void);
static inline void emit_reg_table_global(void);
static inline void emit_reg_table_global_entries(void);

static inline void emit_registers(arch_t arch, struct register_description *registers, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {

		if (registers[i].description)
			fprintf(ofile, "  <!-- %s -->\\n\\\n", registers[i].description);
		fprintf(ofile, "  <reg name=\\\"%s\\\" bitsize=\\\"%s\\\"",
			registers[i].name,
			registers[i].bitsize[arch]);
		if (registers[i].regnum[arch])
			fprintf(ofile, " regnum=\\\"%s\\\"",
				registers[i].regnum[arch]);
		if (registers[i].save_restore)
			fprintf(ofile, " save_restore=\\\"%s\\\"",
				registers[i].save_restore);
		if (registers[i].type[arch])
			fprintf(ofile, " type=\\\"%s\\\"",
				registers[i].type[arch]);
		if (registers[i].group)
			fprintf(ofile, " group=\\\"%s\\\"",
				registers[i].group);
		fprintf(ofile, "/>\\n\\\n\\n\\\n");
	}
}

static inline void emit_xml_version(void)
{
const char *xml_version = "<?xml version=\\\"1.0\\\"?>\\n\\\n\\n\\\n";

	fprintf(ofile, "%s", xml_version);
}

static inline void emit_doc_type_target(void)
{
const char *doc_type_target =
	"<!DOCTYPE target SYSTEM \\\"gdb-target.dtd\\\">\\n\\\n\\n\\\n";

	fprintf(ofile, "%s", doc_type_target);
}

static inline void emit_gnu_copyright(void)
{
	const char *gnu_copyright = "\
<!-- Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.\\n\\\n\
\\n\\\n\
     Copying and distribution of this file, with or without modification,"
"\\n\\\n\
     are permitted in any medium without royalty provided the copyright"
"\\n\\\n\
     notice and this notice are preserved.  -->\\n\\\n\\n\\\n";

	fprintf(ofile, "%s", gnu_copyright);
}

static inline void emit_freescale_version(arch_t arch)
{
	const char *freescale_version[] = {
"\
<!-- ************************************************************* -->\\n\\\n\
<!-- *                                                           * -->\\n\\\n\
<!-- *        Freescale ",
           /* <arch> */
                             " machine description. (1.0)        * -->\\n\\\n\
<!-- *                                                           * -->\\n\\\n\
<!-- ************************************************************* -->\\n\\\n"
                                                                     "\\n\\\n",
	};

	fprintf(ofile, "%s", freescale_version[0]);
	fprintf(ofile, "%s", arch_name[arch]);
	fprintf(ofile, "%s", freescale_version[1]);
}

static inline void emit_target_version(void)
{
const char *target_version = "<target version=\\\"1.0\\\">\\n\\\n\\n\\\n";

	fprintf(ofile, "%s", target_version);
}

static inline void emit_architecture_declaration(arch_t arch)
{
	const char *architecture_declaration[] = {
"\
 <!-- Declare ourselves as a member of the PowerPC family. -->\\n\\\n\
 <architecture>powerpc:", /* <arch> */ "</architecture>\\n\\\n\\n\\\n",
	};
	fprintf(ofile, "%s", architecture_declaration[0]);
	fprintf(ofile, "%s", arch_name[arch]);
	fprintf(ofile, "%s", architecture_declaration[1]);
}

static inline void emit_includes(arch_t arch)
{
	/* Define an include per architecture. */
	static const char *includes[arch_count] = {

	[e500mc] =
"\
 <!-- Include standard PowerPC features. -->\\n\\\n\
 <xi:include href=\\\"power-core.xml\\\"/>\\n\\\n\
 <xi:include href=\\\"power-fpu.xml\\\"/>\\n\\\n\\n\\\n",

	[e5500] =
"\
 <!-- Include standard PowerPC features. -->\\n\\\n\
 <xi:include href=\\\"power64-core.xml\\\"/>\\n\\\n\
 <xi:include href=\\\"power-fpu.xml\\\"/>\\n\\\n\\n\\\n",

	};

	if (includes[arch] != NULL) {
		fprintf(ofile, "%s", includes[arch]);
	} else {
		fprintf(stderr, "error: no definition for includes[%s]; exiting...\n", arch_name[arch]);
		exit(EXIT_FAILURE);
	}
}

static inline void emit_sprs_feature(arch_t arch)
{
	const char *sprs_feature[] = {

"\
 <!-- Define the ", /* <arch> */ " SPR's. -->\\n\\\n\
 <feature name=\\\"freescale.", /* <arch> */ ".sprs\\\">\\n\\\n\\n\\\n",

	};

	fprintf(ofile, "%s", sprs_feature[0]);
	fprintf(ofile, "%s", arch_name[arch]);
	fprintf(ofile, "%s", sprs_feature[1]);
	fprintf(ofile, "%s", arch_name[arch]);
	fprintf(ofile, "%s", sprs_feature[2]);
}

static inline void emit_pmrs_feature(arch_t arch)
{
	const char *pmrs_feature[] = {

"\
 <!-- Define the ", /* <arch> */ " PMR's. -->\\n\\\n\
 <feature name=\\\"freescale.", /* <arch> */ ".pmrs\\\">\\n\\\n\\n\\\n",

	};

	fprintf(ofile, "%s", pmrs_feature[0]);
	fprintf(ofile, "%s", arch_name[arch]);
	fprintf(ofile, "%s", pmrs_feature[1]);
	fprintf(ofile, "%s", arch_name[arch]);
	fprintf(ofile, "%s", pmrs_feature[2]);
}

static inline void emit_close_feature(void)
{
	const char *close_feature = "</feature>\\n\\\n\\n\\\n";

	fprintf(ofile, "%s", close_feature);
}

static inline void emit_close_target(void)
{
	const char *close_target = "</target>\\n\\\n";

	fprintf(ofile, "%s", close_target);
}

static inline void emit_arch_description(arch_t arch)
{
	fprintf(ofile, "\n\t[%s] = \"\\\n", arch_name[arch]);
	emit_xml_version();
	emit_doc_type_target();
	emit_gnu_copyright();
	emit_freescale_version(arch);
	emit_target_version();
	emit_architecture_declaration(arch);
	emit_includes(arch);
	emit_sprs_feature(arch);
	emit_registers(arch, sprs, sprs_count(arch));
	emit_close_feature();
	emit_pmrs_feature(arch);
	emit_registers(arch, pmrs, pmrs_count(arch));
	emit_close_feature();
	emit_close_target();
	fprintf(ofile, "\",\n");
}

static inline void emit_power_core_gnu_copyright(void)
{
const char *power_core_gnu_copyright = "\
<!-- Copyright (C) 2007, 2008, 2009, 2010, 2011 Free Software Foundation, Inc.\\n\\\n\
\\n\\\n\
     Copying and distribution of this file, with or without modification,"
"\\n\\\n\
     are permitted in any medium without royalty provided the copyright"
"\\n\\\n\
     notice and this notice are preserved.  -->\\n\\\n";

	fprintf(ofile, "%s", power_core_gnu_copyright);
}

static inline void emit_doc_type_feature(void)
{
	const char *doc_type_feature = "<!DOCTYPE feature SYSTEM \\\"gdb-target.dtd\\\">\\n\\\n";

	fprintf(ofile, "%s", doc_type_feature);
}

static inline void emit_power_core_feature(void)
{
	const char *power_core_feature = "<feature name=\\\"org.gnu.gdb.power.core\\\">\\n\\\n";

	fprintf(ofile, "%s", power_core_feature);
}

static inline void emit_power_core_description(arch_t arch)
{
	fprintf(ofile, "\n\t[%s] = \"\\\n", arch_name[arch]);
	emit_xml_version();
	emit_power_core_gnu_copyright();
	emit_doc_type_feature();
	emit_power_core_feature();
	emit_registers(arch, power_core_gprs, power_core_gprs_count(arch));
	emit_registers(arch, power_core_pc, power_core_pc_count(arch));
	emit_registers(arch, power_core_msr, power_core_msr_count(arch));
	emit_registers(arch, power_core_cr, power_core_cr_count(arch));
	emit_registers(arch, power_core_sprs, power_core_sprs_count(arch));
	emit_close_feature();
	fprintf(ofile, "\",\n");
}

static inline void emit_power_fpu_feature(void)
{
	const char *power_fpu_feature = "<feature name=\\\"org.gnu.gdb.power.fpu\\\">\\n\\\n\\n\\\n";

	fprintf(ofile, "%s", power_fpu_feature);
}

static inline void emit_power_fpu_description(arch_t arch)
{
	fprintf(ofile, "\n\t[%s] = \"\\\n", arch_name[arch]);
	emit_xml_version();
	emit_power_core_gnu_copyright();
	emit_doc_type_feature();
	emit_power_fpu_feature();
	emit_registers(arch, power_fpu_fprs, power_fpu_fprs_count(arch));
	emit_registers(arch, power_fpu_fpscr, power_fpu_fpscr_count(arch));
	emit_close_feature();
	fprintf(ofile, "\",\n");
}

static inline void emit_freescale_copyright(void)
{
const char *freescale_copyright = "\
/*\n\
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.\n\
 *\n\
 * Redistribution and use in source and binary forms, with or without\n\
 * modification, are permitted provided that the following conditions\n\
 * are met:\n\
 * 1. Redistributions of source code must retain the above copyright\n\
 *    notice, this list of conditions and the following disclaimer.\n\
 * 2. Redistributions in binary form must reproduce the above copyright\n\
 *    notice, this list of conditions and the following disclaimer in the\n\
 *    documentation and/or other materials provided with the distribution.\n\
 *\n\
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR \\\"AS IS\\\" AND ANY EXPRESS OR\n\
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES\n\
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN\n\
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n\
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED\n\
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR\n\
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF\n\
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING\n\
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS\n\
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n\
 */\n";

	fprintf(ofile, "%s", freescale_copyright);
}

static inline void emit_num_regs_value(arch_t arch)
{
	fprintf(ofile, "\n\t[%s] = %d,\n", arch_name[arch], num_regs(arch));
}

static inline void emit_num_reg_bytes_value(arch_t arch)
{
	fprintf(ofile, "\n\t[%s] = %d,\n", arch_name[arch], num_reg_bytes(arch));
	}

static inline void emit_buf_max_value(arch_t arch)
{
	fprintf(ofile, "\n\t[%s] = %d,\n", arch_name[arch], buf_max(arch));
}

static inline void emit_buf_max_hex_value(arch_t arch)
{
	fprintf(ofile, "\n\t[%s] = \"%x\",\n", arch_name[arch], buf_max(arch));
}

static const char *reg_cat_name[] =
{
	[reg_cat_unk]   = "reg_cat_unk",
	[reg_cat_gpr]   = "reg_cat_gpr",
	[reg_cat_fpr]   = "reg_cat_fpr",
	[reg_cat_pc]    = "reg_cat_pc",
	[reg_cat_msr]   = "reg_cat_msr",
	[reg_cat_cr]    = "reg_cat_cr",
	[reg_cat_spr]   = "reg_cat_spr",
	[reg_cat_pmr]   = "reg_cat_pmr",
	[reg_cat_fpscr] = "reg_cat_fpscr",
};

static inline void emit_reg_details(arch_t arch, struct register_description *registers, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		fprintf(ofile, "\t{ .inum=%d, .bitsize=%s, .cat=%s, },\n",
		       registers[i].inum, registers[i].bitsize[arch],
		       reg_cat_name[registers[i].cat]);
	}
}

static inline void emit_reg_table(arch_t arch)
{
	fprintf(ofile, "\n"
	       "/* reg_table indices are the numbers GDB uses to refer to a register.\n"
	       " * The order in which the registers appear in the %s reg_table are the\n"
	       " * order in which they will appear in the g/G packets in the RSP.\n"
	       " * GDB will refer to the %s registers using the indices [0, num_regs[%s])\n"
	       " */\n", arch_name[arch], arch_name[arch], arch_name[arch]);
	fprintf(ofile, "static struct reg_info %s_reg_table[] __attribute__ ((unused)) = {\n", arch_name[arch]);
	emit_reg_details(arch, power_core_gprs, power_core_gprs_count(arch));
	emit_reg_details(arch, power_fpu_fprs, power_fpu_fprs_count(arch));
	emit_reg_details(arch, power_core_pc, power_core_pc_count(arch));
	emit_reg_details(arch, power_core_msr, power_core_msr_count(arch));
	emit_reg_details(arch, power_core_cr, power_core_cr_count(arch));
	emit_reg_details(arch, power_core_sprs, power_core_sprs_count(arch));
	emit_reg_details(arch, power_fpu_fpscr, power_fpu_fpscr_count(arch));
	emit_reg_details(arch, sprs, sprs_count(arch));
	emit_reg_details(arch, pmrs, pmrs_count(arch));
	fprintf(ofile, "};\n");
}

static inline void emit_reg_table_entry(arch_t arch)
{
	fprintf(ofile, "\n\t[%s] = %s_reg_table,\n", arch_name[arch], arch_name[arch]);
}

static inline void emit_include_td_defs(void)
{
	fprintf(ofile, "\n#include \"gdb-td-defs.h\"\n");
}

static inline void emit_power_core_descriptions(void)
{
	fprintf(ofile, "\nstatic const char *power_core_description[arch_count] __attribute__ ((unused)) = {\n");
	for (arch_t arch = initial_arch; arch < arch_count; arch++)
		emit_power_core_description(arch);
	fprintf(ofile, "};\n");
}

static inline void emit_power_fpu_descriptions(void)
{
	fprintf(ofile, "\nstatic const char *power_fpu_description[arch_count] __attribute__ ((unused)) = {\n");
	for (arch_t arch = initial_arch; arch < arch_count; arch++)
		emit_power_fpu_description(arch);
	fprintf(ofile, "};\n");
}

static inline void emit_arch_descriptions(void)
{
	fprintf(ofile, "\nstatic const char *description[arch_count] __attribute__ ((unused)) = {\n");
	for (arch_t arch = initial_arch; arch < arch_count; arch++)
		emit_arch_description(arch);
	fprintf(ofile, "};\n");
}

static inline void emit_num_regs(void)
{
	fprintf(ofile, "\nstatic const unsigned int num_regs[arch_count] __attribute__ ((unused)) = {\n");
	for (arch_t arch = initial_arch; arch < arch_count; arch++)
		emit_num_regs_value(arch);
	fprintf(ofile, "};\n");
}

static inline void emit_num_reg_bytes(void)
{
	fprintf(ofile, "\nstatic const unsigned int num_reg_bytes[arch_count] __attribute__ ((unused)) = {\n");
	for (arch_t arch = initial_arch; arch < arch_count; arch++)
		emit_num_reg_bytes_value(arch);
	fprintf(ofile, "};\n");
}

static inline void emit_buf_max(void)
{
	fprintf(ofile, "\nstatic const unsigned int buf_max[arch_count] __attribute__ ((unused)) = {\n");
	for (arch_t arch = initial_arch; arch < arch_count; arch++)
		emit_buf_max_value(arch);
	fprintf(ofile, "};\n");
}

static inline void emit_buf_max_hex(void)
{
	fprintf(ofile, "\nstatic const char *buf_max_hex[arch_count] __attribute__ ((unused)) = {\n");
	for (arch_t arch = initial_arch; arch < arch_count; arch++)
		emit_buf_max_hex_value(arch);
	fprintf(ofile, "};\n");
}

static inline void emit_reg_table_global(void)
{
	for (arch_t arch = initial_arch; arch < arch_count; arch++)
		emit_reg_table(arch);
}

static inline void emit_reg_table_global_entries(void)
{
	fprintf(ofile, "\n/* The reg_table contains all the individual reg_table's. */\n");
	fprintf(ofile, "static struct reg_info *reg_table[arch_count] __attribute__ ((unused)) = {\n");
	for (arch_t arch = initial_arch; arch < arch_count; arch++)
		emit_reg_table_entry(arch);
	fprintf(ofile, "};\n");
}

int main(int argc, char *argv[])
{
	bool reg_tab_status;

	if (argc == 3 && strcmp(argv[1], "-o") == 0) {

		if ((ofile = fopen(argv[2], "w")) == NULL) {
			fprintf(stderr, "error: Unable to open %s for writing", argv[2]);
			exit(EXIT_FAILURE);
		}

		if ((reg_tab_status = valid_register_tables()) == true) {

			emit_freescale_copyright();
			emit_include_td_defs();
			emit_power_core_descriptions();
			emit_power_fpu_descriptions();
			emit_arch_descriptions();
			emit_num_regs();
			emit_num_reg_bytes();
			emit_buf_max();
			emit_buf_max_hex();
			emit_reg_table_global();
			emit_reg_table_global_entries();
		}

		if (fclose(ofile)) {
			fprintf(stderr, "error: Unable to close %s\n", argv[2]);
			exit(EXIT_FAILURE);
		}

		if (reg_tab_status == false) {

			fprintf(stderr, "error: invalid/inconsistent register tables; exiting...\n");

			if (unlink(argv[2])) {
				fprintf(stderr, "error: Unable to delete %s", argv[2]);
				exit(EXIT_FAILURE);
			}

			exit(EXIT_FAILURE);
		}

		exit(EXIT_SUCCESS);

	} else {

		fprintf(stderr, "error: usage: %s -o <filename>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
}
