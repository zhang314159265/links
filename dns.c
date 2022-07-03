/* dns.c
 * (c) 2002 Mikulas Patocka
 * This file is a part of the Links program, released under GPL
 */

#include "links.h"

#ifdef SUPPORT_IPV6
int support_ipv6;
#endif

#if !defined(USE_GETADDRINFO) && (defined(HAVE_GETHOSTBYNAME_BUG) || !defined(HAVE_GETHOSTBYNAME))
#define EXTERNAL_LOOKUP
#endif

#if defined(WIN) || defined(INTERIX)
#define EXTRA_IPV6_LOOKUP
#endif

#ifdef OPENVMS_64BIT
/* 64-bit getaddrinfo with _SOCKADDR_LEN is broken and returns garbage */
#undef addrinfo
#undef getaddrinfo
#undef freeaddrinfo
#define addrinfo        __addrinfo32
static int my_getaddrinfo(const char *host, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
	int r;
#pragma __pointer_size	32
	char *host_32;
	struct addrinfo hints_32;
	struct addrinfo *res_32;
#pragma __pointer_size	64

	host_32 = _malloc32(strlen(host) + 1);
	if (!host_32) {
		errno = ENOMEM;
		return EAI_SYSTEM;
	}
	strcpy(host_32, host);
	memcpy(&hints_32, hints, sizeof(struct addrinfo));

	r = __getaddrinfo32(host_32, NULL, &hints_32, &res_32);

	free(host_32);

	if (!r)
		*res = res_32;

	return r;
}
static void my_freeaddrinfo(struct addrinfo *res)
{
#pragma __pointer_size	32
	struct addrinfo *res_32 = (struct addrinfo *)res;
#pragma __pointer_size	64
	__freeaddrinfo32(res_32);
}
#define getaddrinfo	my_getaddrinfo
#define freeaddrinfo	my_freeaddrinfo
#endif

struct dnsentry {
	list_entry_1st
	uttime absolute_time;
	uttime timeout;
	struct lookup_result addr;
	list_entry_last
	unsigned char name[1];
};

#ifndef THREAD_SAFE_LOOKUP
struct dnsquery *dns_queue = NULL;
#endif

static int dns_cache_addr_preference = -1;
static struct list_head dns_cache = {&dns_cache, &dns_cache};

static int shrink_dns_cache(int u);

static int get_addr_byte(unsigned char **ptr, unsigned char *res, unsigned char stp)
{
	unsigned u = 0;
	if (!(**ptr >= '0' && **ptr <= '9')) return -1;
	while (**ptr >= '0' && **ptr <= '9') {
		u = u * 10 + **ptr - '0';
		if (u >= 256) return -1;
		(*ptr)++;
	}
	if (stp != 255 && **ptr != stp) return -1;
	(*ptr)++;
	*res = (unsigned char)u;
	return 0;
}

int numeric_ip_address(unsigned char *name, unsigned char address[4])
{
	unsigned char dummy[4];
	if (!address) address = dummy;
	if (get_addr_byte(&name, address + 0, '.')) return -1;
	if (get_addr_byte(&name, address + 1, '.')) return -1;
	if (get_addr_byte(&name, address + 2, '.')) return -1;
	if (get_addr_byte(&name, address + 3, 0)) return -1;
	return 0;
}

#ifdef SUPPORT_IPV6

static int extract_ipv6_address(struct addrinfo *p, unsigned char address[16], unsigned *scope_id)
{
	/*{
		int i;
		for (i = 0; i < p->ai_addrlen; i++)
			fprintf(stderr, "%02x%c", ((unsigned char *)p->ai_addr)[i], i != p->ai_addrlen - 1 ? ':' : '\n');
	}*/
	if (p->ai_family == AF_INET6 && (socklen_t)p->ai_addrlen >= (socklen_t)sizeof(struct sockaddr_in6) && p->ai_addr->sa_family == AF_INET6) {
		memcpy(address, &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr, 16);
#ifdef SUPPORT_IPV6_SCOPE
		*scope_id = ((struct sockaddr_in6 *)p->ai_addr)->sin6_scope_id;
#else
		*scope_id = 0;
#endif
		return 0;
	}
	return -1;
}

int numeric_ipv6_address(unsigned char *name, unsigned char address[16], unsigned *scope_id)
{
	unsigned char dummy_a[16];
	unsigned dummy_s;
	int r;
#ifdef HAVE_INET_PTON
	struct in6_addr i6a;
#endif
	struct addrinfo hints, *res;
	if (!address) address = dummy_a;
	if (!scope_id) scope_id = &dummy_s;

#ifdef HAVE_INET_PTON
	if (inet_pton(AF_INET6, cast_const_char name, &i6a) == 1) {
		memcpy(address, &i6a, 16);
		*scope_id = 0;
		return 0;
	}
	if (!strchr(cast_const_char name, '%'))
		return -1;
#endif

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET6;
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(cast_const_char name, NULL, &hints, &res))
		return -1;
	r = extract_ipv6_address(res, address, scope_id);
	freeaddrinfo(res);
	return r;
}

#endif

#ifdef EXTERNAL_LOOKUP

static int do_external_lookup(unsigned char *name, unsigned char *host)
{
	unsigned char buffer[1024];
	unsigned char sink[16];
	int rd;
	int pi[2];
	pid_t f;
	unsigned char *n;
	int rs;
	if (c_pipe(pi) == -1)
		return -1;
	EINTRLOOP(f, fork());
	if (f == -1) {
		EINTRLOOP(rs, close(pi[0]));
		EINTRLOOP(rs, close(pi[1]));
		return -1;
	}
	if (!f) {
#ifdef HAVE_SETSID
		/* without setsid it gets stuck when on background */
		EINTRLOOP(rs, setsid());
#endif
		EINTRLOOP(rs, close(pi[0]));
		EINTRLOOP(rs, dup2(pi[1], 1));
		if (rs == -1) _exit(1);
		EINTRLOOP(rs, dup2(pi[1], 2));
		if (rs == -1) _exit(1);
		EINTRLOOP(rs, close(pi[1]));
		EINTRLOOP(rs, execlp("host", "host", cast_const_char name, (char *)NULL));
		EINTRLOOP(rs, execl("/usr/sbin/host", "host", cast_const_char name, (char *)NULL));
		_exit(1);
	}
	EINTRLOOP(rs, close(pi[1]));
	rd = hard_read(pi[0], buffer, sizeof buffer - 1);
	if (rd >= 0) buffer[rd] = 0;
	if (rd > 0) {
		while (hard_read(pi[0], sink, sizeof sink) > 0);
	}
	EINTRLOOP(rs, close(pi[0]));
	/* Don't wait for the process, we already have sigchld handler that
	 * does cleanup.
	 * waitpid(f, NULL, 0); */
	if (rd < 0) return -1;
	/*fprintf(stderr, "query: '%s', result: %s\n", name, buffer);*/
	while ((n = strstr(buffer, name))) {
		memset(n, '-', strlen(cast_const_char name));
	}
	for (n = buffer; n < buffer + rd; n++) {
		if (*n >= '0' && *n <= '9') {
			if (get_addr_byte(&n, host + 0, '.')) goto skip_addr;
			if (get_addr_byte(&n, host + 1, '.')) goto skip_addr;
			if (get_addr_byte(&n, host + 2, '.')) goto skip_addr;
			if (get_addr_byte(&n, host + 3, 255)) goto skip_addr;
			return 0;
skip_addr:
			if (n >= buffer + rd) break;
		}
	}
	return -1;
}

#endif

static int memcmp_host_address(struct host_address *a, struct host_address *b)
{
	if (a->af != b->af || a->scope_id != b->scope_id)
		return 1;
	return memcmp(a->addr, b->addr, sizeof a->addr);
}

void add_address(struct lookup_result *host, int af, unsigned char *address, unsigned scope_id, int preference)
{
	struct host_address neww;
	struct host_address *e, *t;
	struct host_address *n;
	if (af != AF_INET && preference == ADDR_PREFERENCE_IPV4_ONLY)
		return;
#ifdef SUPPORT_IPV6
	if (af != AF_INET6 && preference == ADDR_PREFERENCE_IPV6_ONLY)
		return;
#endif
	if (host->n >= MAX_ADDRESSES)
		return;
	memset(&neww, 0, sizeof(struct host_address));
	neww.af = af;
	memcpy(neww.addr, address, af == AF_INET ? 4 : 16);
	neww.scope_id = scope_id;
	e = &host->a[host->n];
	t = e;
	for (n = host->a; n != e; n++) {
		if (!memcmp_host_address(n, &neww))
			return;
		if (preference == ADDR_PREFERENCE_IPV4 && af == AF_INET && n->af != AF_INET) {
			t = n;
			break;
		}
#ifdef SUPPORT_IPV6
		if (preference == ADDR_PREFERENCE_IPV6 && af == AF_INET6 && n->af != AF_INET6) {
			t = n;
			break;
		}
#endif
	}
	memmove(t + 1, t, (e - t) * sizeof(struct host_address));
	memcpy(t, &neww, sizeof(struct host_address));
	host->n++;
}

#ifdef USE_GETADDRINFO

static int use_getaddrinfo(unsigned char *name, struct addrinfo *hints, int preference, struct lookup_result *host)
{
	int gai_err;
	struct addrinfo *res, *p;
#ifdef OPENVMS
	struct addrinfo default_hints;
	if (!hints) {
		memset(&default_hints, 0, sizeof default_hints);
		default_hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
		hints = &default_hints;
	}
#endif
	gai_err = getaddrinfo(cast_const_char name, NULL, hints, &res);
	if (gai_err)
		return gai_err;
	for (p = res; p; p = p->ai_next) {
		if (p->ai_family == AF_INET && (socklen_t)p->ai_addrlen >= (socklen_t)sizeof(struct sockaddr_in) && p->ai_addr->sa_family == AF_INET) {
			add_address(host, AF_INET, (unsigned char *)&((struct sockaddr_in *)p->ai_addr)->sin_addr.s_addr, 0, preference);
			continue;
		}
#ifdef SUPPORT_IPV6
		{
			unsigned char address[16];
			unsigned scope_id;
			if (!extract_ipv6_address(p, address, &scope_id)) {
				add_address(host, AF_INET6, address, scope_id, preference);
				continue;
			}
		}
#endif
	}
	freeaddrinfo(res);
	return 0;
}

#endif

void rotate_addresses(struct lookup_result *host)
{
	int first_type, first_different, i;

	if (host->n <= 2)
		return;

	first_type = host->a[0].af;

	for (i = 1; i < host->n; i++) {
		if (host->a[i].af != first_type) {
			first_different = i;
			goto do_swap;
		}
	}
	return;

do_swap:
	if (first_different > 1) {
		struct host_address ha;
		memcpy(&ha, &host->a[first_different], sizeof(struct host_address));
		memmove(&host->a[2], &host->a[1], (first_different - 1) * sizeof(struct host_address));
		memcpy(&host->a[1], &ha, sizeof(struct host_address));
	}
}

void do_real_lookup(unsigned char *name, int preference, struct lookup_result *host)
{
	unsigned char address[16];
#ifdef SUPPORT_IPV6
	size_t nl;
#endif

	memset(host, 0, sizeof(struct lookup_result));

	if (strlen(cast_const_char name) >= 6 && !casestrcmp(name + strlen(cast_const_char name) - 6, cast_uchar ".onion"))
		goto ret;

	if (!support_ipv6) preference = ADDR_PREFERENCE_IPV4_ONLY;

	if (!numeric_ip_address(name, address)) {
		add_address(host, AF_INET, address, 0, preference);
		goto ret;
	}
#ifdef SUPPORT_IPV6
	nl = strlen(cast_const_char name);
	if (name[0] == '[' && name[nl - 1] == ']') {
		unsigned char *n2 = cast_uchar strdup(cast_const_char(name + 1));
		if (n2) {
			unsigned scope_id;
			n2[nl - 2] = 0;
			if (!numeric_ipv6_address(n2, address, &scope_id)) {
				free(n2);
				add_address(host, AF_INET6, address, scope_id, preference);
				goto ret;
			}
			free(n2);
		}
	} else {
		unsigned scope_id;
		if (!numeric_ipv6_address(name, address, &scope_id)) {
			add_address(host, AF_INET6, address, scope_id, preference);
			goto ret;
		}
	}
#endif

#if defined(USE_GETADDRINFO)
	use_getaddrinfo(name, NULL, preference, host);
#if defined(SUPPORT_IPV6) && defined(EXTRA_IPV6_LOOKUP)
	if ((preference == ADDR_PREFERENCE_IPV4 && !host->n) ||
	    preference == ADDR_PREFERENCE_IPV6 ||
	    preference == ADDR_PREFERENCE_IPV6_ONLY) {
		struct addrinfo hints;
		int i;
		for (i = 0; i < host->n; i++)
			if (host->a[i].af == AF_INET6)
				goto already_have_inet6;
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_INET6;
		hints.ai_flags = 0;
		use_getaddrinfo(name, &hints, preference, host);
	}
	already_have_inet6:;
#endif
#elif defined(HAVE_GETHOSTBYNAME)
	{
		int i;
		struct hostent *hst;
		if ((hst = gethostbyname(cast_const_char name))) {
			if (hst->h_addrtype != AF_INET || hst->h_length != 4 || !hst->h_addr)
				goto ret;
#ifdef h_addr
			for (i = 0; hst->h_addr_list[i]; i++) {
				add_address(host, AF_INET, cast_uchar hst->h_addr_list[i], 0, preference);
			}
#else
			add_address(host, AF_INET, cast_uchar hst->h_addr, 0, preference);
#endif
			goto ret;
		}
	}
#endif

#ifdef EXTERNAL_LOOKUP
	if (!do_external_lookup(name, address)) {
		add_address(host, AF_INET, address, 0, preference);
		goto ret;
	}
#endif

ret:
	return;
}

#ifndef NO_ASYNC_LOOKUP
static void lookup_fn(void *q_, int h)
{
	struct dnsquery *q = (struct dnsquery *)q_;
	struct lookup_result host;
	do_real_lookup(q->name, q->addr_preference, &host);
	/*{
		int i;
		for (i = 0; i < sizeof(struct lookup_result); i++) {
			if (i == 1) portable_sleep(1000);
			hard_write(h, (unsigned char *)&host + i, 1);
		}
	}*/
	hard_write(h, (unsigned char *)&host, sizeof(struct lookup_result));
}

static void end_real_lookup(void *q_)
{
	struct dnsquery *q = (struct dnsquery *)q_;
	int r = 1;
	int rs;
	if (!q->addr || hard_read(q->h, (unsigned char *)q->addr, sizeof(struct lookup_result)) != sizeof(struct lookup_result) || !q->addr->n) goto end;
	r = 0;

	end:
	set_handlers(q->h, NULL, NULL, NULL);
	EINTRLOOP(rs, close(q->h));
	end_dns_lookup(q, r, -1);
}
#endif

static void do_lookup(struct dnsquery *q, int force_async)
{
	/*debug("starting lookup for %s", q->name);*/
#ifndef NO_ASYNC_LOOKUP
	if (!async_lookup && !force_async) {
#endif
#ifndef NO_ASYNC_LOOKUP
		sync_lookup:
#endif
		do_real_lookup(q->name, q->addr_preference, q->addr);
		end_dns_lookup(q, !q->addr->n, -1);
#ifndef NO_ASYNC_LOOKUP
	} else {
		q->h = start_thread(lookup_fn, q, (int)((unsigned char *)strchr(cast_const_char q->name, 0) + 1 - (unsigned char *)q), 1);
		if (q->h == -1) goto sync_lookup;
		set_handlers(q->h, end_real_lookup, NULL, q);
	}
#endif
}

static void do_queued_lookup(struct dnsquery *q)
{
#ifndef THREAD_SAFE_LOOKUP
	if (!dns_queue) {
		dns_queue = q;
		/*debug("direct lookup");*/
#endif
		do_lookup(q, 0);
#ifndef THREAD_SAFE_LOOKUP
	} else {
		/*debug("queuing lookup for %s", q->name);*/
		if (dns_queue->next_in_queue) internal_error("DNS queue corrupted");
		dns_queue->next_in_queue = q;
		dns_queue = q;
	}
#endif
}

static void check_dns_cache_addr_preference(void)
{
	if (dns_cache_addr_preference != ipv6_options.addr_preference) {
		shrink_dns_cache(SH_FREE_ALL);
		dns_cache_addr_preference = ipv6_options.addr_preference;
	}
}

static int find_in_dns_cache(unsigned char *name, struct dnsentry **dnsentry)
{
	struct dnsentry *e;
	struct list_head *le;
	check_dns_cache_addr_preference();
	foreach(struct dnsentry, e, le, dns_cache)
		if (!casestrcmp(e->name, name)) {
			del_from_list(e);
			add_to_list(dns_cache, e);
			*dnsentry = e;
			return 0;
		}
	return -1;
}

static void free_dns_entry(struct dnsentry *dnsentry)
{
	del_from_list(dnsentry);
	mem_free(dnsentry);
}

void end_dns_lookup(struct dnsquery *q, int a, uttime timeout)
{
	struct dnsentry *dnsentry;
	size_t sl;
	void (*fn)(void *, int);
	void *data;
	/*debug("end lookup %s: %d, %lu", q->name, a, (unsigned long)timeout);*/
	if (timeout > DNS_TIMEOUT)
		timeout = DNS_TIMEOUT;
#ifndef THREAD_SAFE_LOOKUP
	if (q->next_in_queue) {
		/*debug("processing next in queue: %s", q->next_in_queue->name);*/
		do_lookup(q->next_in_queue, 1);
	} else dns_queue = NULL;
#endif
	if (!q->fn || !q->addr) {
		free(q);
		return;
	}
	if (!find_in_dns_cache(q->name, &dnsentry)) {
		if (a) {
			memcpy(q->addr, &dnsentry->addr, sizeof(struct lookup_result));
			a = 0;
			goto e;
		}
		free_dns_entry(dnsentry);
	}
	if (a) goto e;
	if (q->addr_preference != ipv6_options.addr_preference) goto e;
	check_dns_cache_addr_preference();
	sl = strlen(cast_const_char q->name);
	if (sl > MAXINT - sizeof(struct dnsentry)) overalloc();
	dnsentry = mem_alloc(sizeof(struct dnsentry) + sl);
	strcpy(cast_char dnsentry->name, cast_const_char q->name);
	memcpy(&dnsentry->addr, q->addr, sizeof(struct lookup_result));
	dnsentry->absolute_time = get_absolute_time();
	dnsentry->timeout = timeout;
	add_to_list(dns_cache, dnsentry);
	e:
	if (q->s) *q->s = NULL;
	fn = q->fn;
	data = q->data;
	free(q);
	fn(data, a);
}

void find_host_no_cache(unsigned char *name, int no_doh, struct lookup_result *addr, void **qp, void (*fn)(void *, int), void *data)
{
	struct dnsquery *q;
	retry:
	q = (struct dnsquery *)malloc(sizeof(struct dnsquery) + strlen(cast_const_char name));
	if (!q) {
		if (out_of_memory(0, NULL, 0))
			goto retry;
		fn(data, 1);
	}
#ifndef THREAD_SAFE_LOOKUP
	q->next_in_queue = NULL;
#endif
	q->fn = fn;
	q->data = data;
	q->s = qp;
	q->doh = NULL;
	q->addr = addr;
	q->addr_preference = ipv6_options.addr_preference;
	strcpy(cast_char q->name, cast_const_char name);
	if (qp) *qp = q;
	if (is_noproxy_host(name))
		no_doh = 1;
	if (!numeric_ip_address(name, NULL))
		no_doh = 1;
#ifdef SUPPORT_IPV6
	if (!numeric_ipv6_address(name, NULL, NULL))
		no_doh = 1;
#endif
	if (!no_doh && *dns_over_https) {
		do_doh_lookup(q);
	} else {
		do_queued_lookup(q);
	}
}

int find_host_in_cache(unsigned char *name, struct lookup_result *addr, void **qp, void (*fn)(void *, int), void *data)
{
	struct dnsentry *dnsentry;
	if (qp) *qp = NULL;
	if (!find_in_dns_cache(name, &dnsentry)) {
		if (get_absolute_time() - dnsentry->absolute_time >= dnsentry->timeout) goto timeout;
		memcpy(addr, &dnsentry->addr, sizeof(struct lookup_result));
		fn(data, 0);
		return 0;
	}
	timeout:
	return -1;
}

void kill_dns_request(void **qp)
{
	struct dnsquery *q = *qp;
	q->fn = NULL;
	q->addr = NULL;
	*qp = NULL;
}

#ifndef NO_ASYNC_LOOKUP
static void dns_prefetch_end(void *addr_, int status)
{
	struct lookup_result *addr = (struct lookup_result *)addr_;
	free(addr);
}
#endif

void dns_prefetch(unsigned char *name)
{
#ifndef NO_ASYNC_LOOKUP
	struct lookup_result *addr;
	if (!async_lookup)
		return;
	addr = (struct lookup_result *)malloc(sizeof(struct lookup_result));
	if (!addr)
		return;
	if (find_host_in_cache(name, addr, NULL, dns_prefetch_end, addr))
		find_host_no_cache(name, 0, addr, NULL, dns_prefetch_end, addr);
#endif
}

void dns_set_priority(unsigned char *name, struct host_address *address, int prefer)
{
	int i;
	struct dnsentry *dnsentry;
	if (find_in_dns_cache(name, &dnsentry))
		return;
	for (i = 0; i < dnsentry->addr.n; i++)
		if (!memcmp_host_address(&dnsentry->addr.a[i], address)) goto found_it;
	return;
	found_it:
	if (prefer) {
		memmove(&dnsentry->addr.a[1], &dnsentry->addr.a[0], i * sizeof(struct host_address));
		memcpy(&dnsentry->addr.a[0], address, sizeof(struct host_address));
	} else {
		memmove(&dnsentry->addr.a[i], &dnsentry->addr.a[i + 1], (dnsentry->addr.n - i - 1) * sizeof(struct host_address));
		memcpy(&dnsentry->addr.a[dnsentry->addr.n - 1], address, sizeof(struct host_address));
	}
}

void dns_clear_host(unsigned char *name)
{
	struct dnsentry *dnsentry;
	if (find_in_dns_cache(name, &dnsentry))
		return;
	free_dns_entry(dnsentry);
}

unsigned long dns_info(int type)
{
	switch (type) {
		case CI_FILES:
			shrink_dns_cache(SH_CHECK_QUOTA);
			return list_size(&dns_cache);
		default:
			internal_error("dns_info: bad request");
	}
	return 0;
}

static int shrink_dns_cache(int u)
{
	uttime now = get_absolute_time();
	struct dnsentry *d;
	struct list_head *ld;
	int f = 0;
	if (u == SH_FREE_SOMETHING && !list_empty(dns_cache)) {
		d = list_struct(dns_cache.prev, struct dnsentry);
		goto delete_last;
	}
	foreach(struct dnsentry, d, ld, dns_cache) if (u == SH_FREE_ALL || now - d->absolute_time >= d->timeout) {
delete_last:
		ld = d->list_entry.prev;
		free_dns_entry(d);
		f = ST_SOMETHING_FREED;
	}
	return f | (list_empty(dns_cache) ? ST_CACHE_EMPTY : 0);
}

unsigned char *print_address(struct host_address *a)
{
#define SCOPE_ID_LEN	11
#ifdef SUPPORT_IPV6
	static unsigned char buffer[INET6_ADDRSTRLEN + SCOPE_ID_LEN];
#else
	static unsigned char buffer[INET_ADDRSTRLEN + SCOPE_ID_LEN];
#endif
#ifdef HAVE_INET_NTOP
	union {
		struct in_addr in;
#ifdef SUPPORT_IPV6
		struct in6_addr in6;
#endif
		char pad[16];
	} u;
	memcpy(&u, a->addr, 16);
	if (!inet_ntop(a->af, &u, cast_char buffer, sizeof buffer - SCOPE_ID_LEN))
		return NULL;
#else
	if (a->af == AF_INET)
		snprintf(cast_char buffer, sizeof buffer, "%d.%d.%d.%d", a->addr[0], a->addr[1], a->addr[2], a->addr[3]);
#ifdef SUPPORT_IPV6
	else if (a->af == AF_INET6)
		snprintf(cast_char buffer, sizeof buffer, "%x:%x:%x:%x:%x:%x:%x:%x",
			(a->addr[0] << 8) | a->addr[1],
			(a->addr[2] << 8) | a->addr[3],
			(a->addr[4] << 8) | a->addr[5],
			(a->addr[6] << 8) | a->addr[7],
			(a->addr[8] << 8) | a->addr[9],
			(a->addr[10] << 8) | a->addr[11],
			(a->addr[12] << 8) | a->addr[13],
			(a->addr[14] << 8) | a->addr[15]);
#endif
	else
		return NULL;
#endif
	if (a->scope_id) {
		unsigned char *end = cast_uchar strchr(cast_const_char buffer, 0);
		snprintf(cast_char end, buffer + sizeof(buffer) - end, "%%%u", a->scope_id);
	}
	return buffer;
}

int ipv6_full_access(void)
{
#ifdef SUPPORT_IPV6
	/*
	 * Test if we can access global IPv6 address space.
	 * This doesn't send anything anywhere, it just creates an UDP socket,
	 * connects it and closes it.
	 */
	struct sockaddr_in6 sin6;
	int h, c, rs;
	if (!support_ipv6) return 0;
	h = c_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (h == -1) return 0;
	memset(&sin6, 0, sizeof sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = htons(1024);
	memcpy(&sin6.sin6_addr.s6_addr, "\052\001\004\060\000\015\000\000\002\314\236\377\376\044\176\032", 16);
	EINTRLOOP(c, connect(h, (struct sockaddr *)(void *)&sin6, sizeof sin6));
	EINTRLOOP(rs, close(h));
	if (!c) return 1;
#endif
	return 0;
}

#ifdef FLOOD_MEMORY

void flood_memory(void)
{
	struct list_head list;
	size_t s = 32768 * 32;
	struct dnsentry *de;
	dns_cache_addr_preference = ipv6_options.addr_preference;
#if defined(HAVE__HEAPMIN)
	_heapmin();
#endif
	init_list(list);
	while (!list_empty(dns_cache)) {
		de = list_struct(dns_cache.prev, struct dnsentry);
		del_from_list(de);
		add_to_list(list, de);
	}
	while (1) {
		while ((de = mem_alloc_mayfail(s))) {
			de->absolute_time = get_absolute_time();
			de->timeout = DNS_TIMEOUT;
			memset(&de->addr, 0, sizeof de->addr);
			de->name[0] = 0;
			add_to_list(list, de);
		}
		if (s == sizeof(struct dnsentry)) break;
		s = s / 2;
		if (s < sizeof(struct dnsentry)) s = sizeof(struct dnsentry);
	}
	while (!list_empty(list)) {
		de = list_struct(list.prev, struct dnsentry);
		del_from_list(de);
		add_to_list(dns_cache, de);
	}
}

#endif

void init_dns(void)
{
	register_cache_upcall(shrink_dns_cache, 0, cast_uchar "dns");
#ifdef FLOOD_MEMORY
	flood_memory();
#endif
#ifdef SUPPORT_IPV6
	{
		int h, rs;
		h = c_socket(AF_INET6, SOCK_STREAM, 0);
		if (h == -1) {
			support_ipv6 = 0;
		} else {
			EINTRLOOP(rs, close(h));
			support_ipv6 = 1;
		}
	}
#endif
}
