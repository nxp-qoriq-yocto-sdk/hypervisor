
#ifndef PLAT_H
#define PLAT_H

#define CCSR_BASE       0x01000000

/* MPIC defs */
#define MPIC 0x40000
#define GCR 0x1020
#define MPIC_IRQ_BASE 0x10000
#define IRQ_STRIDE 0x20
#define IIVPR 0x0
#define IIDR 0x10
#define IILR 0x18

typedef union {
	uint32_t data;
	struct {
		uint32_t msk:1;
		uint32_t A:1;
		uint32_t reserved1:6;
		uint32_t P:1;
		uint32_t reserved2:3;
		uint32_t priority:4;
		uint32_t vector:16;
	};
} iivpr_t;

typedef union {
	uint32_t data;
	struct {
		uint32_t reserved:24;
		uint32_t inttgt:8;
	};
} iilr_t;

#endif
