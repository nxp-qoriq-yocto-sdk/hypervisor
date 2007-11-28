// sprintf() and related functions.
//
// This software is copyright (c) 2007 Scott Wood <scott@buserror.net>.
// 
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors or contributors be held liable for any damages
// arising from the use of this software.
// 
// Permission is hereby granted to everyone, free of charge, to use, copy,
// modify, prepare derivative works of, publish, distribute, perform,
// sublicense, and/or sell copies of the Software, provided that the above
// copyright notice and disclaimer of warranty be included in all copies or
// substantial portions of this software.

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

enum {
	alt_form =        0x0001,
	zero_pad =        0x0002,
	neg_field =       0x0004,
	leave_blank =     0x0008,
	always_sign =     0x0010,
	group_thousands = 0x0020, // FIXME -- unimplemented
	long_arg =        0x0040,
	long_long_arg =   0x0080,
	short_arg =       0x0100,
	short_short_arg = 0x0200,
	intmax_arg =      0x0400,
	ptrdiff_arg =     0x0800,
	size_t_arg =      0x1000,
	capital_hex =     0x2000,
	num_signed =      0x4000,
	has_precision =   0x8000,
};

static void printf_string(char *buf, size_t *opos, size_t limit,
                          const char *src, size_t len)
{
	if (*opos < limit) {
		size_t olen = *opos + len <= limit ? len : limit - *opos;
		memcpy(buf + *opos, src, olen);
	}

	*opos += len;
}

static void printf_fill(char *buf, size_t *opos, size_t limit,
                        char ch, int len)
{
	if (*opos < limit) {
		size_t olen = *opos + len <= limit ? len : limit - *opos;
		memset(buf + *opos, ch, olen);
	}
	
	*opos += len;
}

static void printf_num(char *obuf, size_t *opos, size_t limit,
                       int64_t value, long radix, int fieldwidth,
                       int precision, int flags)
{
	char buf[65];
	int pos = 64;
	int letter = (flags & capital_hex) ? 'A' - 10 : 'a' - 10;
	uint64_t uval;
	int len, extralen;

	if (flags & num_signed)
		uval = value < 0 ? -value : value;
	else
		uval = value;
	
	// An explicit precision of 0 suppresses all output if the value
	// is zero.  Otherwise, the output size is not limited by precision
	// or field width.
	
	if (uval != 0 || !(flags & has_precision) || precision != 0) do {
		int ch = uval % radix;
		
		if (ch < 10)
			buf[pos] = ch + '0';
		else
			buf[pos] = ch + letter;
		
		uval /= radix;
		pos--;
	} while (uval);
	
	len = 64 - pos;

	// length which counts against fieldwidth but not precision
	extralen = 0; 
	
	if (flags & num_signed) {
		if (value < 0) {
			printf_fill(obuf, opos, limit, '-', 1);
			extralen += 1;
		} else if (flags & always_sign) {
			printf_fill(obuf, opos, limit, '+', 1);
			extralen += 1;
		} else if (flags & leave_blank) {
			printf_fill(obuf, opos, limit, ' ', 1);
			extralen += 1;
		}
	}
	
	if ((flags & alt_form) && value != 0) {
		if (radix == 8 && (!(flags & has_precision) || precision <= len)) {
			flags |= has_precision;
			precision = len + 1;
		}
		
		if (radix == 16) {
			printf_string(obuf, opos, limit, "0x", 2);
			extralen += 2;
		}
	}
	
	if ((flags & has_precision) && len < precision) {
		precision -= len;
		len += precision;
	} else {
		precision = 0;
	}
	
	len += extralen;
	
	if (!(flags & neg_field) && len < fieldwidth) {
		char padchar = (flags & zero_pad) ? '0' : ' ';
		printf_fill(obuf, opos, limit, padchar, fieldwidth - len);
		len = fieldwidth;
	}

	if (precision != 0) {
		printf_fill(obuf, opos, limit, '0', precision);
		len += precision;
	}
	
	printf_string(obuf, opos, limit, buf + pos + 1, 64 - pos);
	
	if ((flags & neg_field) && len < fieldwidth)
		printf_fill(obuf, opos, limit, ' ', fieldwidth - len);
}

size_t vsnprintf(char *buf, size_t size,
                 const char *str, va_list args)
{
	size_t opos = 0; // position in the output string
	unsigned int flags = 0;
	int radix = 10;
	int state = 0;
	int fieldwidth = 0;
	int precision = 0;

	for (size_t pos = 0; str[pos]; pos++) switch (state) {
		case 0:
			if (str[pos] == '%') {
				flags = 0;
				radix = 10;
				state = 1;
				fieldwidth = 0;
				precision = 0;
				break;
			}
		
			if (opos < size - 1)
				buf[opos] = str[pos];

			opos++;
			break;
		
		case 1: // A percent has been seen; read in format characters
			switch (str[pos]) {
				case '#':
					flags |= alt_form;
					break;
				
				case '0':
					if (!(flags & has_precision)) {
						flags |= zero_pad;
						break;
					}
					
					// else fall through
				
				case '1' ... '9':
					if (flags & has_precision)
						goto default_case;
					
					do {
						fieldwidth *= 10;
						fieldwidth += str[pos++] - '0';
					} while (str[pos] >= '0' && str[pos] <= '9');
					
					pos--;
					break;
				
				case '*':
					if (fieldwidth || (flags & has_precision))
						goto default_case;
					
					fieldwidth = va_arg(args, int);
					break;
				
				case '.':
					flags |= has_precision;
					
					if (str[pos + 1] == '*') {
						pos++;
						precision = va_arg(args, int);
					} else while (str[pos + 1] >= '0' && str[pos + 1] <= '9') {
						precision *= 10;
						precision += str[++pos] - '0';
					}
					
					break;
						
				case '-':
					flags |= neg_field;
					break;
				
				case ' ':
					flags |= leave_blank;
					break;
				
				case '+':
					flags |= always_sign;
					break;
				
				case '\'':
					flags |= group_thousands;
					break;

				case 'l':
					if (flags & long_arg)
						flags |= long_long_arg;
					else
						flags |= long_arg;
	
					break;
				
				case 'h':
					if (flags & short_arg)
						flags |= short_short_arg;
					else
						flags |= short_arg;

					break;
				
				case 'j':
					flags |= intmax_arg;
					break;
				
				case 't':
					flags |= ptrdiff_arg;
					break;
				
				// Note that %z and other such "new" format characters are
				// basically useless because some GCC coder actually went out
				// of their way to make the compiler reject C99 format
				// strings in C++ code, with no way of overriding it that I
				// can find (the source code comments suggest the checking is
				// only when using -pedantic, but I wasn't using -pedantic).
				//
				// Thus, we have the choice of either avoiding %z and friends
				// (and possibly needing to insert hackish casts to silence
				// the compiler's warnings if different architectures define
				// types like size_t in different ways), or not using the
				// format warnings at all.
				//
				// To mitigate this, 32-bit architectures should define
				// pointer-sized special types as "long" rather than "int",
				// so that %lx/%ld can always be used with them.  Fixed-size
				// 32-bit types should be declared as "int" rather than
				// "long" for the same reason.
				
				case 'z':
					flags |= size_t_arg;
					break;
				
				case 'd':
				case 'i': {
					int64_t arg;
				
					if ((flags & intmax_arg) || (flags & long_long_arg))
						arg = va_arg(args, long long);
					else if (flags & size_t_arg)
						arg = va_arg(args, ssize_t);
					else if (flags & ptrdiff_arg)
						arg = va_arg(args, ptrdiff_t);
					else if (flags & long_arg)
						arg = va_arg(args, long);
					else if (flags & short_short_arg)
						arg = (signed char)va_arg(args, int);
					else if (flags & short_arg)
						arg = (short)va_arg(args, int);
					else
						arg = va_arg(args, int);
					
					flags |= num_signed;
					printf_num(buf, &opos, size - 1, arg, 10,
					           fieldwidth, precision, flags);
					state = 0;
					break;
				}

				case 'X':
					flags |= capital_hex;
					// fall-through

				case 'x':
					radix = 18;
					// fall-through
					
				case 'o':
					radix -= 2;
					// fall-through
				
				case 'u': {
					uint64_t arg;
				
					if ((flags & intmax_arg) || (flags & long_long_arg))
						arg = va_arg(args, unsigned long long);
					else if (flags & size_t_arg)
						arg = va_arg(args, size_t);
					else if (flags & ptrdiff_arg)
						arg = va_arg(args, intptr_t);
					else if (flags & long_arg)
						arg = va_arg(args, unsigned long);
					else if (flags & short_short_arg)
						arg = (unsigned char)va_arg(args, unsigned int);
					else if (flags & short_arg)
						arg = (unsigned short)va_arg(args, unsigned int);
					else if (flags & short_short_arg)
						arg = (signed char)va_arg(args, int);
					else if (flags & short_arg)
						arg = (short)va_arg(args, int);
					else
						arg = va_arg(args, unsigned int);
					
					printf_num(buf, &opos, size - 1, arg, radix,
					           fieldwidth, precision, flags);
					state = 0;
					break;
				}
				
				case 'c':
					if (opos < size - 1)
						buf[opos] = va_arg(args, int);
	
					opos++;
					state = 0;
					break;
				
				case 's': {
					const char *arg = va_arg(args, const char *);
					
					if (!arg)
						arg = "(null)";
					
					size_t len = strlen(arg);
					printf_string(buf, &opos, size - 1, arg, len);
					state = 0;
					break;
				}
				
				case 'p': {
					const void *arg = va_arg(args, const void *);

					printf_num(buf, &opos, size - 1, (unsigned long)arg, 16,
					           fieldwidth, precision, flags);
					
					state = 0;
					break;
				}
				
				case 'n': {
					if ((flags & intmax_arg) || (flags & long_long_arg))
						*va_arg(args, unsigned long long *) = opos;
					else if (flags & size_t_arg)
						*va_arg(args, ssize_t *) = opos;
					else if (flags & ptrdiff_arg)
						*va_arg(args, ptrdiff_t *) = opos;
					else if (flags & long_arg)
						*va_arg(args, long *) = opos;
					else if (flags & short_short_arg)
						*va_arg(args, signed char *) = opos;
					else if (flags & short_arg)
						*va_arg(args, short *) = opos;
					else
						*va_arg(args, int *) = opos;
						
					state = 0;
					break;
				}

				default_case: // label for goto
				default:
					if (opos < size - 1)
						buf[opos] = str[pos];
					
					opos++;
					state = 0;
					break;
			}
	}

	if (opos < size)
		buf[opos] = 0;
	else if (size > 0)
		buf[size - 1] = 0;
	
	return opos;
}

size_t snprintf(char *buf, size_t size, const char *str, ...)
{
	va_list args;
	va_start(args, str);
	int ret = vsnprintf(buf, size, str, args);
	va_end(args);
	return ret;
}

size_t sprintf(char *buf, const char *str, ...)
{
	va_list args;
	va_start(args, str);
	int ret = vsnprintf(buf, ULONG_MAX, str, args);
	va_end(args);
	return ret;
}
