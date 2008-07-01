/** @file
 * GDB Target Description Generator.
 */

/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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

#include <stdio.h>
#include <stdlib.h>

#include "gdb-register-defs.h"

void emit_registers(unsigned int lower, unsigned int upper);
void emit_e500mc_description(void);
void emit_power_core_description(void);
void emit_power_fpu_description(void);
void emit_buf_macros(void);
void emit_e500mc_reg_table(void);

void emit_registers(unsigned int lower, unsigned int upper)
{
	unsigned int i;

	for (i = lower; i < upper; i++) {

		if (e500mc_registers[i].description)
			printf ("  <!-- %s -->\\n\\\n",
				e500mc_registers[i].description);
		printf("  <reg name=\\\"%s\\\" bitsize=\\\"%s\\\"",
			e500mc_registers[i].name,
			e500mc_registers[i].bitsize);
		if (e500mc_registers[i].regnum)
			printf (" regnum=\\\"%s\\\"",
				e500mc_registers[i].regnum);
		if (e500mc_registers[i].save_restore)
			printf (" save_restore=\\\"%s\\\"",
				e500mc_registers[i].save_restore);
		if (e500mc_registers[i].type)
			printf (" type=\\\"%s\\\"",
				e500mc_registers[i].type);
		if (e500mc_registers[i].group)
			printf (" group=\\\"%s\\\"",
				e500mc_registers[i].group);
		printf("/>\\n\\\n\\n\\\n");
	}
}

char *xml_version = "<?xml version=\\\"1.0\\\"?>\\n\\\n\\n\\\n";
char *doc_type_target =
	"<!DOCTYPE target SYSTEM \\\"gdb-target.dtd\\\">\\n\\\n\\n\\\n";
char *e500mc_gnu_copyright = "\
<!-- Copyright (C) 2008 Free Software Foundation, Inc.\\n\\\n\
\\n\\\n\
     Copying and distribution of this file, with or without modification,"
"\\n\\\n\
     are permitted in any medium without royalty provided the copyright"
"\\n\\\n\
     notice and this notice are preserved.  -->\\n\\\n\\n\\\n";
char *freescale_version = "\
<!-- ************************************************************* -->\\n\\\n\
<!-- *                                                           * -->\\n\\\n\
<!-- *        Freescale e500mc machine description. (1.0)        * -->\\n\\\n\
<!-- *                                                           * -->\\n\\\n\
<!-- ************************************************************* -->\\n\\\n"
                                                                     "\\n\\\n";
char *target_version = "<target version=\\\"1.0\\\">\\n\\\n\\n\\\n";
char *architecture_declaration = "\
 <!-- Declare ourselves as a member of the PowerPC family. -->\\n\\\n\
 <architecture>powerpc:e500mc</architecture>\\n\\\n\\n\\\n";
char *includes = "\
 <!-- Include standard PowerPC features. -->\\n\\\n\
 <xi:include href=\\\"power-core.xml\\\"/>\\n\\\n\
 <xi:include href=\\\"power-fpu.xml\\\"/>\\n\\\n\\n\\\n";
char *e500_feature = "\
 <!-- Define the e500mc SPR's. -->\\n\\\n\
 <feature name=\\\"freescale.e500mc.sprs\\\">\\n\\\n\\n\\\n";
char *close_feature = " </feature>\\n\\\n\\n\\\n";
char *close_target = "</target>\\n\\\n";

char *e500mc_description;

void emit_e500mc_description(void)
{
	printf("%s", xml_version);
	printf("%s", doc_type_target);
	printf("%s", e500mc_gnu_copyright);
	printf("%s", freescale_version);
	printf("%s", target_version);
	printf("%s", architecture_declaration);
	printf("%s", includes);
	printf("%s", e500_feature);
	emit_registers(E500MC_SPRS_START, REG_COUNT);
	printf("%s", close_feature);
	printf("%s", close_target);
}

char *power_core_gnu_copyright = "\
<!-- Copyright (C) 2007, 2008 Free Software Foundation, Inc.\\n\\\n\
\\n\\\n\
     Copying and distribution of this file, with or without modification,"
"\\n\\\n\
     are permitted in any medium without royalty provided the copyright"
"\\n\\\n\
     notice and this notice are preserved.  -->\\n\\\n";
char *doc_type_feature = "<!DOCTYPE feature SYSTEM \\\""
			 "gdb-target.dtd\\\">\\n\\\n";
char *power_core_feature = "<feature name=\\\"org.gnu.gdb.power.core"
			   "\\\">\\n\\\n";

void emit_power_core_description(void)
{
	printf("%s", xml_version);
	printf("%s", power_core_gnu_copyright);
	printf("%s", doc_type_feature);
	printf("%s", power_core_feature);
	emit_registers(R0_INDEX, R0_INDEX + 32);
	emit_registers(PC_INDEX, FPSCR_INDEX);
	printf("%s", close_feature);
}

char *power_fpu_feature = "<feature name=\\\"org.gnu.gdb.power.fpu"
			  "\\\">\\n\\\n\\n\\\n";

void emit_power_fpu_description(void)
{
	printf("%s", xml_version);
	printf("%s", power_core_gnu_copyright);
	printf("%s", doc_type_feature);
	printf("%s", power_fpu_feature);
	emit_registers(F0_INDEX, F0_INDEX + 32);
	emit_registers(FPSCR_INDEX, FPSCR_INDEX + 1);
	printf("%s", close_feature);
}

char *freescale_copyright = "\
/*\n\
 * Copyright (C) 2008 Freescale Semiconductor, Inc.\n\
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

void emit_buf_macros(void)
{
	/* BUFMAX defines the maximum number of characters in inbound/outbound
	 * buffers; at least NUMREGBYTES*2 are needed for register packets.
	 */
	int num_reg_bytes = 0;
	int num_regs = REG_COUNT;
	/* buffer_ammount: Add a little extra padding, as the G packet utilizes
	 * 2 * num_reg_bytes bytes and the G prefixed to that means that we drop
	 * a byte if we define BUFMAX to be exactly 2 * num_reg_bytes.
	 * In general, we need:
	 * BUFMAX >= max({length(name(<command>))+length(payload(<command>))
	 *                for <command> in RSP-commands})
	 * For now, 8 seems like a good upper bound on the maximum length an
	 * alternative name can go upto even if G were to be renamed.
	 * PS: We do not have an instance of a larger packet that is
	 * transmitted over the serial line using the RSP.
	 */
	const int buffer_ammount = 8;
	int bufmax;
	int i;

	for (i = 0; i < num_regs; i++) {
		num_reg_bytes += atoi(e500mc_registers[i].bitsize)/8;
	}
	printf("#define NUMREGS %d\n", num_regs);
	printf("#define NUMREGBYTES %d\n", num_reg_bytes);
	bufmax = 2 * num_reg_bytes + buffer_ammount;
	printf("#define BUFMAX %d\n", bufmax);
	printf("#define BUFMAX_HEX \"%x\"\n", bufmax);
}

void emit_e500mc_reg_table(void)
{
	int i;
	printf("\ntypedef enum reg_cat\n"
		"{\n");
	for(i = reg_cat_unk; i < reg_cat_count; i++) {
		printf("\t%s,\n", reg_cat_names[i]);
	}
	printf("} reg_cat_t;\n");
	printf("\nstruct reg_info {\n\tint e500mc_num;\n"
					"\tint bitsize;\n"
					"\treg_cat_t cat;\n};\n");
	printf("\nstruct reg_info e500mc_reg_table[] =\n{\n");
	for (i = 0; i < REG_COUNT; i++) {
		printf("\t{ .e500mc_num=%d, .bitsize=%s, .cat=%s, },\n",
				e500mc_registers[i].inum,
				e500mc_registers[i].bitsize,
				reg_cat_names[e500mc_registers[i].cat]);
	}
	printf("};\n");
}

/* TODO: Emit register table. */
int main(void)
{
	printf("%s", freescale_copyright);

	printf("char *power_core_description = \"\\\n");
	emit_power_core_description();
	printf("\";\n");

	printf("char *power_fpu_description = \"\\\n");
	emit_power_fpu_description();
	printf("\";\n");

	printf("char *e500mc_description = \"\\\n");
	emit_e500mc_description();
	printf("\";\n");

	emit_buf_macros();
	emit_e500mc_reg_table();

	return 0;
}
