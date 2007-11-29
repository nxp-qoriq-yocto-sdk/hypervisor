/*
 *
 *
 *
 */

#define STACK_R11 0x8
#define STACK_R12 0xC

.text

.globl hcall_ret1
hcall_ret1:

	mfcr	%r0
	stw	%r0,-4(%r1)

	lwz	%r11,STACK_R11(%r1)  /* the token is on the caller's stack */

	sc	1

	/* r3 has status, r4-r10 any return values */

	lwz	%r12,STACK_R12(%r1)  /* r12 = return arg array */
	stw	%r4,(0*4)(%r12)

	lwz	%r0,-4(%r1)
	mfcr	%r0

	blr

.globl hcall_ret3
hcall_ret3:

	mfcr	%r0
	stw	%r0,-4(%r1)

	lwz	%r11,STACK_R11(%r1)  /* the token is on the caller's stack */

	sc	1

	/* r3 has status, r4-r10 any return values */

	lwz	%r12,STACK_R12(%r1)  /* r12 = return arg array */
	stw	%r4,(0*4)(%r12)
	stw	%r5,(1*4)(%r12)
	stw	%r6,(2*4)(%r12)

	lwz	%r0,-4(%r1)
	mfcr	%r0

	blr
