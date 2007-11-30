
#include "uv.h"
#include "frame.h"

typedef void (*hcallfp_t)(trapframe_t *regs);

static void unimplemented(trapframe_t *regs)
{
	printf("unimplemented hcall\n");
	regs->gpregs[3] = -1;
}

static void fh_partition_get_status(trapframe_t *regs)
{
	int lpar_id = regs->gpregs[3];
	printf("id = %d\n",lpar_id);

	/* processing goes here */

	/* if calling lpar is not privileged, return error */

	/* return values */
	regs->gpregs[4] = 0;  /* FIXME lpar status */
	regs->gpregs[5] = 2;  /* FIXME # cpus */
	regs->gpregs[6] = 0x1000000;  /* FIXME mem size */

	/* success */
	regs->gpregs[3] = 0;  
}

static void fh_whoami(trapframe_t *regs)
{
	regs->gpregs[4] = 0x99;  /* FIXME */
	regs->gpregs[3] = 0;  /* success */
}

static hcallfp_t hcall_table[] = {
	&fh_whoami,		/* 0x00 */
	&unimplemented,		/* 0x01 */
	&unimplemented,		/* 0x02 */
	&unimplemented,		/* 0x03 */
	&unimplemented,		/* 0x04 */
	&unimplemented,		/* 0x05 */
	&unimplemented,		/* 0x06 */
	&fh_partition_get_status,	/* 0x07 */
	&unimplemented,		/* 0x08 */
	&unimplemented,		/* 0x09 */
	&unimplemented,		/* 0x0a */
	&unimplemented,		/* 0x0b */
	&unimplemented,		/* 0x0c */
	&unimplemented,		/* 0x0d */
	&unimplemented,		/* 0x0e */
	&unimplemented,		/* 0x0f */
	&unimplemented,		/* 0x10 */
	&unimplemented,		/* 0x11 */
	&unimplemented,		/* 0x12 */
	&unimplemented,		/* 0x13 */
	&unimplemented,		/* 0x14 */
	&unimplemented,		/* 0x15 */
	&unimplemented,		/* 0x16 */
	&unimplemented,		/* 0x17 */
	&unimplemented,		/* 0x18 */
	&unimplemented,		/* 0x19 */
	&unimplemented,		/* 0x1a */
	&unimplemented,		/* 0x1b */
	&unimplemented,		/* 0x1c */
	&unimplemented,		/* 0x1d */
};

void hcall(trapframe_t *regs)
{
	unsigned int token = regs->gpregs[11];   /* hcall token is in r11 */

	if (unlikely(token >= sizeof(hcall_table) / sizeof(hcallfp_t))) {
		regs->gpregs[3] = -1; // status
		return;
	}

	hcall_table[token](regs);
}
