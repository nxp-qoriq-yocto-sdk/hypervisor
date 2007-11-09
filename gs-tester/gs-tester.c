
/*
 * Initial hack:
 *   hypervisor is at 0x10000000 physical
 *   guest is at 0x20000000 physical
 *
 */

#include "tlb.h"
#include "console.h"
#include "trap_booke.h"
#include "spr.h"

void branch_to_guest(register_t r3, register_t r4, register_t r5,
                     register_t r6, register_t r7, uint32_t vaddr);

void guest_main(void);
void test_privileged_instructions(void);
void test_givors_rw(void);

unsigned int srr0;
unsigned int srr1;

void start_guest(void)
{
   unsigned int tmp;

#define GUEST_PA              0x10000000
#define GUEST_VA              0x00000000
#define GUEST_SIZE            0x01000000
#define GUEST_TID             0x0
#define GUEST_GS              0x1
    
    /* set up a tlb mapping for the guest */
    __tlb1_set_entry(0, GUEST_VA, GUEST_PA, GUEST_SIZE, _TLB_ENTRY_MEM, UV_TID, 0, GUEST_GS);

/* hardcoded hack for now */
#define CCSRBAR_PA              0xfe000000
#define CCSRBAR_VA              0xf0000000
#define CCSRBAR_SIZE            0x01000000

    __tlb1_set_entry(1, CCSRBAR_VA, CCSRBAR_PA, CCSRBAR_SIZE, _TLB_ENTRY_IO, UV_TID, 0, GUEST_GS);

   printf("gs-tester...\n");
   printf("MSR[GS=1]\n");

   /*
    * do some initialization of registers for the test
    */

  
   /* GIVOR init */
   mtspr(SPR_GIVOR2,0x20);
   mtspr(SPR_GIVOR3,0x30);
   mtspr(SPR_GIVOR4,0x40);
   mtspr(SPR_GIVOR8,0x80);
   mtspr(SPR_GIVOR13,0x130);
   mtspr(SPR_GIVOR14,0x140);


   /* 
    * start the guest 
    */

   printf("Switching to guest state...\n");
   branch_to_guest(0,0,0,0,0,(uint32_t)&guest_main);

}

volatile int exception_type;

void guest_main(void)
{
   unsigned int tmp;
   unsigned int tmp2;

   printf("MSR[GS=0]\n");
   printf("Starting tests...\n\n");


   test_privileged_instructions();

   test_givors_rw();


   /*
    * SRR0/GSRR0 read
    */
   printf("Testing SRR0/GSRR0 read/write...");
   exception_type = -1; /* reinit the exception type */
   tmp = mfspr(SPR_GSRR0);
   tmp2 = mfspr(SPR_SRR0);
   if (tmp != tmp2) {
       printf("FAILED...GSRR0 != SRR0\n");
   } else {
       mtspr(SPR_SRR0,0x1234);
       tmp = mfspr(SPR_GSRR0);
       if (tmp != 0x1234) {
           printf("FAILED...write failed\n");
       } else if (exception_type != -1) {
           printf("FAILED...got exception\n");
       } else {
           printf("PASSED\n");
       }
   }


   /* end of simulation */
    __asm__ volatile("mr 22, 22");

}

void test_privileged_instructions(void)
{
	unsigned int tmp;
   /*
    * tlbivax
    */
   printf("Testing tlbivax...");
   exception_type = -1;/* reinit the exception type */
   tmp=0x04;
   __asm __volatile("tlbivax 0, %0"
                    :
                    : "r"(tmp));
   if (exception_type == EXC_EHPRIV) {
       printf("got ehpriv...PASSED\n");
   } else {
       printf("FAILED\n");
   }

}

#define GIVOR_TST(str,num,expected) do { \
	unsigned int tmp; \
	printf("Testing %s read...",str); \
	exception_type = -1; \
	tmp = mfspr(num); \
	if (tmp == expected && exception_type == -1) { \
	    printf("got 0x%x...PASSED\n",tmp); \
	} else { \
	    printf("FAILED...value=%04x, exception_type=%d\n",tmp,exception_type); \
	} \
	printf("Testing %s write...",str); \
	exception_type = -1; \
	mtspr(num,tmp); \
	if (exception_type == EXC_EHPRIV) { \
		printf("got ehpriv...PASSED\n"); \
	} else { \
		printf("FAILED got %d\n",exception_type); \
	} \
    } while (0)


void test_givors_rw(void)
{

    GIVOR_TST("givor2",SPR_GIVOR2,0x20);
    GIVOR_TST("givor3",SPR_GIVOR3,0x30);
    GIVOR_TST("givor4",SPR_GIVOR4,0x40);
    GIVOR_TST("givor8",SPR_GIVOR8,0x80);
    GIVOR_TST("givor13",SPR_GIVOR13,0x130);
    GIVOR_TST("givor14",SPR_GIVOR14,0x140);

}
