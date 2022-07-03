/* string.c
 * (c) 2002 Mikulas Patocka
 * This file is a part of the Links program, released under GPL.
 */

#include "links.h"

int snprint(unsigned char *s, int n, my_uintptr_t num)
{
	my_uintptr_t q = 1;
	while (q <= num / 10) q *= 10;
	while (n-- > 1 && q) {
		*(s++) = (unsigned char)(num / q + '0');
		num %= q;
#ifdef __DECC_VER
		do_not_optimize_here(&q);	/* compiler bug */
#endif
		q /= 10;
	}
	*s = 0;
	return !!q;
}

int snzprint(unsigned char *s, int n, off_t num)
{
	off_t q = 1;
	if (n > 1 && num < 0) *(s++) = '-', num = -num, n--;
	while (q <= num / 10) q *= 10;
	while (n-- > 1 && q) {
		*(s++) = (unsigned char)(num / q + '0');
		num %= q;
#ifdef __DECC_VER
		do_not_optimize_here(&q);	/* compiler bug */
#endif
		q /= 10;
	}
	*s = 0;
	return !!q;
}

void add_to_strn(unsigned char **s, unsigned char *a)
{
	unsigned char *p;
	size_t l1 = strlen(cast_const_char *s), l2 = strlen(cast_const_char a);
	if (((l1 | l2) | (l1 + l2 + 1)) > MAXINT) overalloc();
	p = (unsigned char *)mem_realloc(*s, l1 + l2 + 1);
	strcat(cast_char p, cast_const_char a);
	*s = p;
}

void extend_str(unsigned char **s, int n)
{
	size_t l = strlen(cast_const_char *s);
	if (((l | n) | (l + n + 1)) > MAXINT) overalloc();
	*s = (unsigned char *)mem_realloc(*s, l + n + 1);
}

void add_bytes_to_str(unsigned char **s, int *l, unsigned char *a, size_t ll)
{
	unsigned char *p;
	size_t old_length;
	size_t new_length;
	size_t x;

	if (!ll)
		return;

	p = *s;
	old_length = (unsigned)*l;
	if (ll + old_length >= (unsigned)MAXINT / 2 || ll + old_length < ll) overalloc();
	new_length = old_length + ll;
	*l = (int)new_length;
	x = old_length ^ new_length;
	if (x >= old_length) {
		/* Need to realloc */
#ifdef HAVE___BUILTIN_CLZ
#if !(									\
	defined(__tune_i386__) ||					\
	defined(__tune_i486__) ||					\
	defined(__tune_i586__) ||					\
	defined(__tune_k6__) ||						\
	defined(__tune_lakemont__) ||					\
	(defined(__alpha__) && !defined(__alpha_cix__)) ||		\
	(defined(__mips) && __mips < 32) ||				\
	(defined(__ARM_ARCH) && __ARM_ARCH < 5) ||			\
	(defined(__sparc__) && (!defined(__VIS__) || __VIS__ < 0x300)) ||\
	defined(__hppa) ||						\
	defined(__riscv) ||						\
	defined(__sh__))
		if (!(sizeof(unsigned) & (sizeof(unsigned) - 1))) {
			new_length = 2U << ((sizeof(unsigned) * 8 - 1)
#ifdef __ICC
				-
#else
				^
#endif
				__builtin_clz(new_length));
		} else
#endif
#endif
		{
			new_length |= new_length >> 1;
			new_length |= new_length >> 2;
			new_length |= new_length >> 4;
			new_length |= new_length >> 8;
			new_length |= new_length >> 16;
			new_length++;
		}
		p = (unsigned char *)mem_realloc(p, new_length);
		*s = p;
	}
	p[*l] = 0;
	memcpy(p + old_length, a, ll);
}

void add_to_str(unsigned char **s, int *l, unsigned char *a)
{
	add_bytes_to_str(s, l, a, strlen(cast_const_char a));
}

void add_chr_to_str(unsigned char **s, int *l, unsigned char a)
{
	add_bytes_to_str(s, l, &a, 1);
}

void add_unsigned_long_num_to_str(unsigned char **s, int *l, my_uintptr_t n)
{
	unsigned char a[64];
	snprint(a, 64, n);
	add_to_str(s, l, a);
}

void add_num_to_str(unsigned char **s, int *l, off_t n)
{
	unsigned char a[64];
	if (n >= 0 && n < 1000) {
		unsigned sn = (unsigned)n;
		unsigned char *p = a;
		if (sn >= 100) {
			*p++ = '0' + sn / 100;
			sn %= 100;
			goto d10;
		}
		if (sn >= 10) {
			d10:
			*p++ = '0' + sn / 10;
			sn %= 10;
		}
		*p++ = '0' + sn;
		add_bytes_to_str(s, l, a, p - a);
	} else {
		snzprint(a, 64, n);
		add_to_str(s, l, a);
	}
}

void add_knum_to_str(unsigned char **s, int *l, off_t n)
{
	unsigned char a[13];
	if (n && n / (1024 * 1024) * (1024 * 1024) == n) snzprint(a, 12, n / (1024 * 1024)), a[strlen(cast_const_char a) + 1] = 0, a[strlen(cast_const_char a)] = 'M';
	else if (n && n / 1024 * 1024 == n) snzprint(a, 12, n / 1024), a[strlen(cast_const_char a) + 1] = 0, a[strlen(cast_const_char a)] = 'k';
	else snzprint(a, 13, n);
	add_to_str(s, l, a);
}

long strtolx(unsigned char *c, unsigned char **end)
{
	char *end_c;
	long l;
	if (c[0] == '0' && upcase(c[1]) == 'X' && c[2]) l = strtol(cast_const_char(c + 2), &end_c, 16);
	else l = strtol(cast_const_char c, &end_c, 10);
	*end = cast_uchar end_c;
	if (upcase(**end) == 'K') {
		(*end)++;
		if (l < -MAXINT / 1024) return -MAXINT;
		if (l > MAXINT / 1024) return MAXINT;
		return l * 1024;
	}
	if (upcase(**end) == 'M') {
		(*end)++;
		if (l < -MAXINT / (1024 * 1024)) return -MAXINT;
		if (l > MAXINT / (1024 * 1024)) return MAXINT;
		return l * (1024 * 1024);
	}
	return l;
}

my_strtoll_t my_strtoll(unsigned char *string, unsigned char **end)
{
	char *end_c;
	my_strtoll_t f;
	errno = 0;
#if defined(HAVE_STRTOLL)
	f = strtoll(cast_const_char string, &end_c, 10);
#elif defined(HAVE_STRTOQ)
	f = strtoq(cast_const_char string, &end_c, 10);
#elif defined(HAVE_STRTOIMAX)
	f = strtoimax(cast_const_char string, &end_c, 10);
#else
	f = strtol(cast_const_char string, &end_c, 10);
#endif
	if (end)
		*end = cast_uchar end_c;
	if (f < 0 || errno) return -1;
	return f;
}

/* Copies at most dst_size chars into dst. Ensures null termination of dst. */
void safe_strncpy(unsigned char *dst, const unsigned char *src, size_t dst_size)
{
	dst[dst_size - 1] = 0;
	strncpy(cast_char dst, cast_const_char src, dst_size - 1);
}

#ifdef JS
/* deletes all nonprintable characters from string */
void skip_nonprintable(unsigned char *txt)
{
	unsigned char *txt1=txt;

	if (!txt||!*txt)return;
	for (;*txt;txt++)
		switch(*txt)
		{
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
			case 8:
			case 11:
			case 12:
			case 13:
			case 14:
			case 15:
			case 16:
			case 17:
			case 18:
			case 19:
			case 20:
			case 21:
			case 22:
			case 23:
			case 24:
			case 25:
			case 26:
			case 27:
			case 28:
			case 29:
			case 30:
			case 31:
			break;

			case 9:
			*txt1=' ';
			txt1++;
			break;

			default:
			*txt1=*txt;
			txt1++;
			break;
		}
	*txt1=0;
}
#endif

/* don't use strcasecmp because it depends on locale */
int casestrcmp(const unsigned char *s1, const unsigned char *s2)
{
	while (1) {
		unsigned char c1 = *s1;
		unsigned char c2 = *s2;
		c1 = locase(c1);
		c2 = locase(c2);
		if (c1 != c2) {
			return (int)c1 - (int)c2;
		}
		if (!*s1) break;
		s1++, s2++;
	}
	return 0;
}

/* case insensitive compare of 2 strings */
/* comparison ends after len (or less) characters */
/* return value: 1=strings differ, 0=strings are same */
int casecmp(const unsigned char *c1, const unsigned char *c2, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++) if (srch_cmp(c1[i], c2[i])) return 1;
	return 0;
}

int casestrstr(const unsigned char *h, const unsigned char *n)
{
	const unsigned char *p;

	for (p=h;*p;p++)
	{
		if (!srch_cmp(*p,*n))  /* same */
		{
			const unsigned char *q, *r;
			for (q=n, r=p;*r&&*q;)
			{
				if (!srch_cmp(*q,*r)) r++,q++;    /* same */
				else break;
			}
			if (!*q) return 1;
		}
	}

	return 0;
}

unsigned hash_string(unsigned char *u)
{
	unsigned hash = 0;
	for (; *u; u++)
		hash = hash * 0x11 + *u;
	return hash;
}
