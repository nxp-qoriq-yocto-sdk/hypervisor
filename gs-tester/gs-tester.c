
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


void  branch_to_guest(uint32_t vaddr);
void guest_main(void);

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

  
   /* GIVOR4 */
   mtspr(SPR_GIVOR4,0x4);


   /* 
    * start the guest 
    */

   branch_to_guest((uint32_t)&guest_main);

}

volatile int exception_type;

void guest_main(void)
{
   unsigned int tmp;
   unsigned int tmp2;

   printf("MSR[GS=0]\n");
   printf("Starting tests...\n");

   /*
    * tlbivax
    */
   printf("Testing tlbivax...");
   exception_type = -1;
   tmp=0x04;
   __asm __volatile("tlbivax 0, %0"
                    :
                    : "r"(tmp));
   if (exception_type == EXC_EHPRIV) {
       printf("got ehpriv...PASSED\n");
   } else {
       printf("FAILED\n");
   }

   /*
    * GIOVR4 read
    */
   printf("Testing GIVIOR4 read...");
   exception_type = -1;
   tmp = mfspr(SPR_GIVOR4);
   if (tmp == 0x4 && exception_type == -1) {
       printf("got %x...PASSED\n",tmp);
   } else {
       printf("FAILED...value=%04x, exception_type=%d\n",tmp,exception_type);
   }


   /*
    * SRR0/GSRR0 read
    */
   printf("Testing SRR0/GSRR0 read/write...");
   exception_type = -1;
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
    __asm__ volatile("mr 2, 2");

}
