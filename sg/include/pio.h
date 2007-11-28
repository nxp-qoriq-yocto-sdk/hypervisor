/*-
 * Copyright (c) 1997 Per Fogelstrom, Opsycon AB and RTMX Inc, USA.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed under OpenBSD by
 *	Per Fogelstrom Opsycon AB for RTMX Inc, North Carolina, USA.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _MACHINE_PIO_H_
#define	_MACHINE_PIO_H_

#include <uv.h>

/*
 * I/O macros.
 */

static __inline void
__outb(volatile uint8_t *a, uint8_t v)
{
	__asm__ volatile("sync");
	*a = v;
}

static __inline void
__outw(volatile uint16_t *a, uint16_t v)
{
	__asm__ volatile("sync");
	*a = v;
}

static __inline void
__outl(volatile uint32_t *a, uint32_t v)
{
	__asm__ volatile("sync");
	*a = v;
}

static __inline uint8_t
__inb(volatile uint8_t *a)
{
	uint8_t _v_;

	__asm__ volatile("sync");
	_v_ = *a;
	__asm__ volatile("isync");

	return _v_;
}

static __inline uint16_t
__inw(volatile uint16_t *a)
{
	uint16_t _v_;

	__asm__ volatile("sync");
	_v_ = *a;
	__asm__ volatile("isync");

	return _v_;
}

static __inline uint32_t
__inl(volatile uint32_t *a)
{
	uint32_t _v_;

	__asm__ volatile("sync");
	_v_ = *a;
	__asm__ volatile("isync");

	return _v_;
}

#define	out8(a,v)	(__outb((volatile uint8_t *)(a), v))
#define	out16(a,v)	(__outw((volatile uint16_t *)(a), v))
#define	out32(a,v)	(__outl((volatile uint32_t *)(a), v))
#define	in8(a)		(__inb((volatile uint8_t *)(a)))
#define	in16(a)		(__inw((volatile uint16_t *)(a)))
#define	in32(a)		(__inl((volatile uint32_t *)(a)))

#endif /*_MACHINE_PIO_H_*/
