
/*
 * Initial hack:
 *   hypervisor is at 0x10000000 physical
 *   guest is at 0x20000000 physical
 *
 */

#include <libos/libos.h>
#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/trapframe.h>
#include <libos/trap_booke.h>
#include <libos/spr.h>

extern uint8_t init_stack_top;

cpu_t cpu0 = {
        .kstack = &init_stack_top - FRAMELEN,
        .client = 0,
};

void branch_to_guest(register_t r3, register_t r4, register_t r5,
                     register_t r6, register_t r7, uint32_t vaddr);

void guest_main(void);
void test_privileged_instructions(void);
void test_givors_rw(void);
void init(unsigned long devtree_ptr);
static void core_init(void);
static void tlb1_init(void);
void guest_main(void);

unsigned int srr0;
unsigned int srr1;

/* hardcoded hack for now */
#define CCSRBAR_PA              0xfe000000
#define CCSRBAR_VA              0x01000000
#define CCSRBAR_SIZE            TLB_TSIZE_16M
#define UART_OFFSET 0x11c500

#define GUEST_PA              0x10000000
#define GUEST_VA              0x00000000
#define GUEST_SIZE            TLB_TSIZE_16M
#define GUEST_TID             0x0
#define GUEST_GS              0x1

void start(unsigned long devtree_ptr)
{
	init(devtree_ptr);

	printf("gs-tester...\n");
	printf("MSR[GS=0]\n");

	tlb1_set_entry(1, GUEST_VA, GUEST_PA, GUEST_SIZE, TLB_MAS2_MEM,
		TLB_MAS3_KERN, 0, 0, TLB_MAS8_GUEST);

	tlb1_set_entry(2, CCSRBAR_VA, CCSRBAR_PA, CCSRBAR_SIZE, TLB_MAS2_MEM,
		TLB_MAS3_KERN, 0, 0, TLB_MAS8_GUEST);

	/* do some initialization of registers for the tests */
	/* GIVOR init */
	mtspr(SPR_GIVOR2,0x20);
	mtspr(SPR_GIVOR3,0x30);
	mtspr(SPR_GIVOR4,0x40);
	mtspr(SPR_GIVOR8,0x80);
	mtspr(SPR_GIVOR13,0x130);
	mtspr(SPR_GIVOR14,0x140);

	printf("Switching to guest state...\n");

        asm volatile("mfmsr %%r3; oris %%r3, %%r3, 0x1000;"
                     "li %%r4, 0; li %%r5, 0; li %%r6, 0; li %%r7, 0;"
                     "mtsrr0 %0; mtsrr1 %%r3; lis %%r3, 0x00f0; rfi" : :
                     "r" (&guest_main) :
                     "r3", "r4", "r5", "r6", "r7", "r8");

	/* this never returns */
    
}


void init(unsigned long devtree_ptr)
{

	core_init();

	console_init(CCSRBAR_VA + UART_OFFSET);

}


static void core_init(void)
{

    /* set up a TLB entry for CCSR space */
    tlb1_init();

}

/*
 *    after tlb1_init:
 *        TLB1[0]  = CCSR
 *        TLB1[15] = OS image 16M
 */



extern int print_ok;  /* set to indicate printf can work now */

static void tlb1_init(void)
{
	tlb1_set_entry(0, CCSRBAR_VA, CCSRBAR_PA, CCSRBAR_SIZE, TLB_MAS2_IO,
		TLB_MAS3_KERN, 0, 0, 0);

	print_ok = 1;
}

kstack_t guest_kstack;
cpu_t guest_cpu = {
        .kstack = guest_kstack,
        .client = 0,
};

volatile int exception_type;

void guest_main(void)
{
	unsigned long stack_top = (unsigned long)guest_kstack + KSTACK_SIZE - FRAMELEN;

	/* set gsprg0 and r1 for guest context */

	asm volatile( 	"mtspr 592,%0;"
			"mr %%r1, %1;" : :
			 "r" (&guest_cpu), "r" (stack_top));

	printf("MSR[GS=0]\n");
	printf("Starting tests...\n\n");

	test_givors_rw();

	test_privileged_instructions();

	printf("\ntests done.\n");

	/* end of simulation */
	__asm__ volatile("mr 21, 21");
}

#define GIVOR_TST(str,num,expected) do { \
	unsigned int tmp; \
	printf("Testing %s read...",str); \
	exception_type = -1; \
	tmp = mfspr(num); \
	if (tmp == expected && exception_type == -1) { \
	    printf("\tgot 0x%x...PASSED\n",tmp); \
	} else { \
	    printf("FAILED...value=%04x, exception_type=%d\n",tmp,exception_type); \
	} \
	printf("Testing %s write...",str); \
	exception_type = -1; \
	mtspr(num,tmp); \
	if (exception_type == EXC_EHPRIV) { \
		printf("\tgot ehpriv...PASSED\n"); \
	} else { \
		printf("FAILED got %d\n",exception_type); \
	} \
    } while (0)

void test_givors_rw(void)
{

	printf("\nGIVOR tests...\n");

	GIVOR_TST("givor2",SPR_GIVOR2,0x20);
	GIVOR_TST("givor3",SPR_GIVOR3,0x30);
	GIVOR_TST("givor4",SPR_GIVOR4,0x40);
	GIVOR_TST("givor8",SPR_GIVOR8,0x80);
	GIVOR_TST("givor13",SPR_GIVOR13,0x130);
	GIVOR_TST("givor14",SPR_GIVOR14,0x140);


}

#define STR(x) #x

#define test_tlbxx_no_operands(tlbinstr,masreg,masval) ({ \
   exception_type = -1; \
   printf("testing "STR(tlb##tlbinstr)"..."); \
   if (masreg) \
       mtspr(masreg, (masval)); \
   __asm__ volatile("isync; "STR(tlb##tlbinstr)"; isync; msync;" \
                    : : : "memory"); \
   if (exception_type == EXC_EHPRIV) { \
       printf("got ehpriv ...PASSED\n"); \
   } else { \
       printf("FAILED\n"); \
   }\
})

#define test_tlbxx_one_operand(tlbinstr,masreg,masval,oper) ({ \
   unsigned int tmp = oper; \
   exception_type = -1; \
   printf("Testing "STR(tlb##tlbinstr)"..."); \
   if (masreg)  \
       mtspr(masreg, (masval)); \
   __asm__ volatile(""STR(tlb##tlbinstr)" 0, %0" \
                    : \
                    : "r" (tmp) \
                    : "memory" ); \
   if (exception_type == EXC_EHPRIV) { \
       printf("got ehpriv...PASSED\n"); \
   } else { \
       printf("FAILED\n"); \
   }\
})

#define test_msgxx(msginstr, val) ({ \
   unsigned int tmp = val; \
   exception_type = -1; \
   printf("Testing "STR(msg##msginstr)"..."); \
   __asm__ volatile(""STR(msg##msginstr)" %0" \
                    : \
                    : "r" (tmp)); \
   if (exception_type == EXC_EHPRIV) { \
       printf("got ehpriv...PASSED\n"); \
   } else { \
       printf("FAILED\n"); \
   } \
})

#define test_rfxx_interrupt(rfinstr,xxsrr,exception) ({ \
   unsigned int tmp; \
   exception_type = -1; \
   printf("Testing rf"STR(rfinstr)"..."); \
   __asm__ volatile("mfmsr %0" : "=r" (tmp)); \
   mtspr(SPR_##xxsrr##SRR1, tmp); \
   exception_type = -1; /* ignore exceptions from xSRR1 updates */ \
   __asm__ volatile("bl 1f \n \
                    1: \n \
                    mflr %0" \
                    : "=r" (tmp)); \
   mtspr(SPR_##xxsrr##SRR0, tmp+20); /* for rfi, return after it */ \
   exception_type = -1; /* ignore exceptions from xSRR0 updates */ \
   __asm__ volatile("rf"STR(rfinstr)"" ); \
   if ((exception_type == -1 && !exception) || \
       exception_type == EXC_EHPRIV) { \
        printf("PASSED, exception_type = %d\n", exception_type); \
   } else { \
        printf("FAILED\n"); \
   } \
})

void test_privileged_instructions(void)
{
	unsigned int tmp;
	printf("\nprivileged instruction tests...\n");

   /*
    * tlb management instructions
    */
   test_tlbxx_one_operand(ivax,0,0,0x04); /* tlbivax */
   test_tlbxx_no_operands(sync,0,0); /* tlbsync */

   /*
    * tlbre and copy-back MAS0-MAS3 via tlbwe
    */
   test_tlbxx_no_operands(re,SPR_MAS0,MAS0_TLBSEL1 | MAS0_ESEL(0));
   test_tlbxx_no_operands(we,0,0);

   /*
    * tlbsx 
    */
   test_tlbxx_one_operand(sx,SPR_MAS6,(mfspr(SPR_PID) << MAS6_SPID_SHIFT),0);

   /*
    * msgsnd & msgclr 
    */
   test_msgxx(snd,0);
   test_msgxx(clr,0);

   /*
    * test all types return from interrupt instructions 
    */
   test_rfxx_interrupt(i,G,0);
   test_rfxx_interrupt(ci,C,1);
   test_rfxx_interrupt(mci,MC,1);
   test_rfxx_interrupt(di,D,1);
}


