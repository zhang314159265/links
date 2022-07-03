/* menu.c
 * (c) 2002 Mikulas Patocka, Petr 'Brain' Kulhavy
 * This file is a part of the Links program, released under GPL.
 */

#include "links.h"


static struct history file_history = { 0, { &file_history.items, &file_history.items } };


static void remove_zeroes(unsigned char *string)
{
	int l = (int)strlen(cast_const_char string);

	while (l && string[l-1]=='0') {
		l--;
		string[l] = 0;
	}
}


static unsigned char * const version_texts[] = {
	TEXT_(T_LINKS_VERSION),
	TEXT_(T_OPERATING_SYSTEM_TYPE),
	TEXT_(T_OPERATING_SYSTEM_VERSION),
	TEXT_(T_COMPILER),
	TEXT_(T_WORD_SIZE),
	TEXT_(T_DEBUGGING_LEVEL),
	TEXT_(T_EVENT_HANDLER),
	TEXT_(T_IPV6),
	TEXT_(T_COMPRESSION_METHODS),
	TEXT_(T_ENCRYPTION),
	TEXT_(T_UTF8_TERMINAL),
#if defined(__linux__) || defined(__LINUX__) || defined(__SPAD__) || defined(USE_GPM)
	TEXT_(T_GPM_MOUSE_DRIVER),
#endif
#ifdef OS2
	TEXT_(T_XTERM_FOR_OS2),
#endif
#ifdef JS
	TEXT_(T_JAVASCRIPT),
#endif
	TEXT_(T_GRAPHICS_MODE),
#ifdef G
	TEXT_(T_FONT_RENDERING),
	TEXT_(T_IMAGE_LIBRARIES),
	TEXT_(T_OPENMP),
#endif
	TEXT_(T_CONFIGURATION_DIRECTORY),
	NULL,
};

static void add_and_pad(unsigned char **s, int *l, struct terminal *term, unsigned char *str, int maxlen)
{
	unsigned char *x = get_text_translation(str, term);
	int len = cp_len(term_charset(term), x);
	add_to_str(s, l, x);
	add_to_str(s, l, cast_uchar ":  ");
	while (len++ < maxlen) add_chr_to_str(s, l, ' ');
}

static void menu_version(void *term_)
{
	struct terminal *term = (struct terminal *)term_;
	int i;
	int maxlen = 0;
	unsigned char *s;
	int l;
	unsigned char * const * decc_volatile text_ptr;
	for (i = 0; version_texts[i]; i++) {
		unsigned char *t = get_text_translation(version_texts[i], term);
		int tl = cp_len(term_charset(term), t);
		if (tl > maxlen)
			maxlen = tl;
	}

	s = init_str();
	l = 0;
	text_ptr = version_texts;

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
	add_to_str(&s, &l, cast_uchar VERSION_STRING);
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
	add_to_str(&s, &l, cast_uchar SYSTEM_NAME);
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
	add_to_str(&s, &l, system_name);
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
	add_to_str(&s, &l, compiler_name);
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
	add_to_str(&s, &l, get_text_translation(TEXT_(T_MEMORY), term));
	add_chr_to_str(&s, &l, ' ');
	add_num_to_str(&s, &l, sizeof(void *) * 8);
	add_to_str(&s, &l, cast_uchar "-bit, ");
	add_to_str(&s, &l, get_text_translation(TEXT_(T_FILE_SIZE), term));
	add_chr_to_str(&s, &l, ' ');
	add_num_to_str(&s, &l, sizeof(off_t) * 8 /*- ((off_t)-1 < 0)*/);
	add_to_str(&s, &l, cast_uchar "-bit");
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
	add_num_to_str(&s, &l, DEBUGLEVEL);
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
	add_event_string(&s, &l, term);
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
#ifdef SUPPORT_IPV6
	if (!support_ipv6) add_to_str(&s, &l, get_text_translation(TEXT_(T_NOT_ENABLED_IN_SYSTEM), term));
	else if (!ipv6_full_access()) add_to_str(&s, &l, get_text_translation(TEXT_(T_LOCAL_NETWORK_ONLY), term));
	else add_to_str(&s, &l, get_text_translation(TEXT_(T_YES), term));
#else
	add_to_str(&s, &l, get_text_translation(TEXT_(T_NO), term));
#endif
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
#ifdef HAVE_ANY_COMPRESSION
	add_compress_methods(&s, &l);
#else
	add_to_str(&s, &l, get_text_translation(TEXT_(T_NO), term));
#endif
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
#ifdef HAVE_SSL
#ifdef OPENSSL_VERSION
	add_to_str(&s, &l, (unsigned char *)OpenSSL_version(OPENSSL_VERSION));
#else
	add_to_str(&s, &l, (unsigned char *)SSLeay_version(SSLEAY_VERSION));
#endif
#ifndef HAVE_SSL_CERTIFICATES
	add_to_str(&s, &l, cast_uchar " (");
	add_to_str(&s, &l, get_text_translation(TEXT_(T_NO_CERTIFICATE_VERIFICATION), term));
	add_chr_to_str(&s, &l, ')');
#endif
#else
	add_to_str(&s, &l, get_text_translation(TEXT_(T_NO), term));
#endif
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
#ifdef ENABLE_UTF8
	add_to_str(&s, &l, get_text_translation(TEXT_(T_YES), term));
#else
	add_to_str(&s, &l, get_text_translation(TEXT_(T_NO), term));
#endif
	add_to_str(&s, &l, cast_uchar "\n");

#if defined(__linux__) || defined(__LINUX__) || defined(__SPAD__) || defined(USE_GPM)
	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
#ifdef USE_GPM
	add_gpm_version(&s, &l);
#else
	add_to_str(&s, &l, get_text_translation(TEXT_(T_NO), term));
#endif
	add_to_str(&s, &l, cast_uchar "\n");
#endif

#ifdef OS2
	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
#ifdef X2
	add_to_str(&s, &l, get_text_translation(TEXT_(T_YES), term));
#else
	add_to_str(&s, &l, get_text_translation(TEXT_(T_NO), term));
#endif
	add_to_str(&s, &l, cast_uchar "\n");
#endif

#ifdef JS
	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
	add_to_str(&s, &l, get_text_translation(TEXT_(T_YES), term));
	add_to_str(&s, &l, cast_uchar "\n");
#endif

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
#ifdef G
	i = l;
	add_graphics_drivers(&s, &l);
	for (; s[i]; i++) if (s[i - 1] == ' ') s[i] = upcase(s[i]);
#else
	add_to_str(&s, &l, get_text_translation(TEXT_(T_NO), term));
#endif
	add_to_str(&s, &l, cast_uchar "\n");

#ifdef G
	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
#ifndef HAVE_FREETYPE
	add_to_str(&s, &l, get_text_translation(TEXT_(T_INTERNAL), term));
#else
	add_freetype_version(&s, &l);
	add_to_str(&s, &l, cast_uchar ", ");
	add_fontconfig_version(&s, &l);
#endif
	add_to_str(&s, &l, cast_uchar "\n");

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
	add_png_version(&s, &l);
#ifdef HAVE_JPEG
	add_to_str(&s, &l, cast_uchar ", ");
	add_jpeg_version(&s, &l);
#endif
#ifdef HAVE_TIFF
	add_to_str(&s, &l, cast_uchar ", ");
	add_tiff_version(&s, &l);
#endif
#ifdef HAVE_SVG
	add_to_str(&s, &l, cast_uchar ", ");
	add_svg_version(&s, &l);
#endif
#ifdef HAVE_WEBP
	add_to_str(&s, &l, cast_uchar ", ");
	add_webp_version(&s, &l);
#endif
	add_to_str(&s, &l, cast_uchar "\n");
#endif

#ifdef G
	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
#ifndef HAVE_OPENMP
	add_to_str(&s, &l, get_text_translation(TEXT_(T_NO), term));
#else
	if (!F) {
		add_to_str(&s, &l, get_text_translation(TEXT_(T_NOT_USED_IN_TEXT_MODE), term));
	} else if (disable_openmp) {
		add_to_str(&s, &l, get_text_translation(TEXT_(T_DISABLED), term));
	} else {
		int thr = omp_start();
		omp_end();
		add_num_to_str(&s, &l, thr);
		add_chr_to_str(&s, &l, ' ');
		if (thr == 1) add_to_str(&s, &l, get_text_translation(TEXT_(T_THREAD), term));
		else if (thr >= 2 && thr <= 4) add_to_str(&s, &l, get_text_translation(TEXT_(T_THREADS), term));
		else add_to_str(&s, &l, get_text_translation(TEXT_(T_THREADS5), term));
	}
#endif
	add_to_str(&s, &l, cast_uchar "\n");
#endif

	add_and_pad(&s, &l, term, *text_ptr++, maxlen);
	if (links_home) {
		unsigned char *native_home = os_conv_to_external_path(links_home, NULL);
		add_to_str(&s, &l, native_home);
		mem_free(native_home);
	} else {
		add_to_str(&s, &l, get_text_translation(TEXT_(T_NONE), term));
	}
	add_to_str(&s, &l, cast_uchar "\n");

	s[l - 1] = 0;
	if (*text_ptr)
		internal_error("menu_version: text mismatched");

	msg_box(term, getml(s, NULL), TEXT_(T_VERSION_INFORMATION), AL_LEFT | AL_MONO, s, MSG_BOX_END, NULL, 1, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC);
}

static void menu_about(struct terminal *term, void *d, void *ses_)
{
	msg_box(term, NULL, TEXT_(T_ABOUT), AL_CENTER, TEXT_(T_LINKS__LYNX_LIKE), MSG_BOX_END, (void *)term, 2, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC, TEXT_(T_VERSION), menu_version, 0);
}

static void menu_keys(struct terminal *term, void *d, void *ses_)
{
	if (!term->spec->braille)
		msg_box(term, NULL, TEXT_(T_KEYS), AL_LEFT | AL_MONO, TEXT_(T_KEYS_DESC), MSG_BOX_END, NULL, 1, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC);
	else
		msg_box(term, NULL, TEXT_(T_KEYS), AL_LEFT | AL_MONO, TEXT_(T_KEYS_DESC), cast_uchar "\n", TEXT_(T_KEYS_BRAILLE_DESC), MSG_BOX_END, NULL, 1, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC);
}

void activate_keys(struct session *ses)
{
	menu_keys(ses->term, NULL, ses);
}

static void menu_copying(struct terminal *term, void *d, void *ses_)
{
	msg_box(term, NULL, TEXT_(T_COPYING), AL_CENTER, TEXT_(T_COPYING_DESC), MSG_BOX_END, NULL, 1, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC);
}

static void menu_url(struct terminal *term, void *url_, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	unsigned char *url = get_text_translation((unsigned char *)url_, term);
	goto_url_utf8(ses, url);
}

static void menu_for_frame(struct terminal *term, void *f_, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	void (*f)(struct session *, struct f_data_c *, int) = *(void (* const *)(struct session *, struct f_data_c *, int))f_;
	do_for_frame(ses, f, 0);
}

static void menu_goto_url(struct terminal *term, void *d, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	dialog_goto_url(ses, cast_uchar "");
}

static void menu_save_url_as(struct terminal *term, void *d, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	dialog_save_url(ses);
}

static void menu_go_back(struct terminal *term, void *d, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	go_back(ses, 1);
}

static void menu_go_forward(struct terminal *term, void *d, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	go_back(ses, -1);
}

static void menu_reload(struct terminal *term, void *d, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	reload(ses, -1);
}

void really_exit_prog(void *ses_)
{
	struct session *ses = (struct session *)ses_;
	register_bottom_half(destroy_terminal, ses->term);
}

static void dont_exit_prog(void *ses_)
{
	struct session *ses = (struct session *)ses_;
	ses->exit_query = 0;
}

void query_exit(struct session *ses)
{
	int only_one_term = ses->term->list_entry.next == ses->term->list_entry.prev;
	ses->exit_query = 1;
	msg_box(ses->term, NULL, TEXT_(T_EXIT_LINKS), AL_CENTER,
		only_one_term && are_there_downloads() ? TEXT_(T_DO_YOU_REALLY_WANT_TO_EXIT_LINKS_AND_TERMINATE_ALL_DOWNLOADS) :
		!F || only_one_term ? TEXT_(T_DO_YOU_REALLY_WANT_TO_EXIT_LINKS) :
		TEXT_(T_DO_YOU_REALLY_WANT_TO_CLOSE_WINDOW),
		MSG_BOX_END,
		(void *)ses, 2, TEXT_(T_YES), really_exit_prog, B_ENTER, TEXT_(T_NO), dont_exit_prog, B_ESC);
}

void exit_prog(struct terminal *term, void *d, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	int only_one_term;
	if (!ses) {
		register_bottom_half(destroy_terminal, term);
		return;
	}
	only_one_term = ses->term->list_entry.next == ses->term->list_entry.prev;
	if (!ses->exit_query && (!d || (only_one_term && are_there_downloads()))) {
		query_exit(ses);
		return;
	}
	really_exit_prog(ses);
}

struct refresh {
	struct terminal *term;
	struct window *win;
	struct dialog *dlg;
	unsigned char *(*txt)(struct terminal *term);
	struct timer *timer;
};

static void refresh(void *r_)
{
	struct refresh *r = (struct refresh *)r_;
	unsigned char **udata = r->dlg->udata;
	unsigned char *txt = r->txt(r->win->term);
	if (strcmp(cast_const_char udata[0], cast_const_char txt)) {
		mem_free(udata[0]);
		udata[0] = txt;
		if (!F) redraw_below_window(r->win);
		redraw_window(r->win);
	} else {
		mem_free(txt);
	}
	r->timer = install_timer(RESOURCE_INFO_REFRESH, refresh, r);
}

static void refresh_abort(struct dialog_data *dlg)
{
	struct refresh *r = dlg->dlg->udata2;
	unsigned char **udata = r->dlg->udata;
	if (r->timer != NULL) kill_timer(r->timer);
	mem_free(udata[0]);
	mem_free(r);
}

static void refresh_dialog_box(struct terminal *term, unsigned char *(*txt)(struct terminal *))
{
	struct refresh *r = mem_alloc(sizeof(struct refresh));
	r->term = term;
	r->win = list_struct(term->windows.next, struct window);
	r->txt = txt;
	r->timer = install_timer(RESOURCE_INFO_REFRESH, refresh, r);
	r->dlg = ((struct dialog_data *)r->win->data)->dlg;
	r->dlg->udata2 = r;
	r->dlg->abort = refresh_abort;
}

static unsigned char *resource_info_msg(struct terminal *term)
{
	unsigned char *a = init_str();
	int l = 0;

	add_to_str(&a, &l, get_text_translation(TEXT_(T_RESOURCES), term));
	add_to_str(&a, &l, cast_uchar ": ");
	add_unsigned_long_num_to_str(&a, &l, select_info(CI_FILES));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_HANDLES), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, select_info(CI_TIMERS));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_TIMERS), term));
	add_to_str(&a, &l, cast_uchar ".\n");

	add_to_str(&a, &l, get_text_translation(TEXT_(T_CONNECTIONS), term));
	add_to_str(&a, &l, cast_uchar ": ");
	add_unsigned_long_num_to_str(&a, &l, connect_info(CI_FILES) - connect_info(CI_CONNECTING) - connect_info(CI_TRANSFER));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_WAITING), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, connect_info(CI_CONNECTING));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_CONNECTING), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, connect_info(CI_TRANSFER));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_tRANSFERRING), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, connect_info(CI_KEEP));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_KEEPALIVE), term));
	add_to_str(&a, &l, cast_uchar ".\n");

	add_to_str(&a, &l, get_text_translation(TEXT_(T_MEMORY_CACHE), term));
	add_to_str(&a, &l, cast_uchar ": ");
	add_unsigned_long_num_to_str(&a, &l, cache_info(CI_BYTES));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_BYTES), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, cache_info(CI_FILES));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_FILES), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, cache_info(CI_LOCKED));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_LOCKED), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, cache_info(CI_LOADING));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_LOADING), term));
	add_to_str(&a, &l, cast_uchar ".\n");

#ifdef HAVE_ANY_COMPRESSION
	add_to_str(&a, &l, get_text_translation(TEXT_(T_DECOMPRESSED_CACHE), term));
	add_to_str(&a, &l, cast_uchar ": ");
	add_unsigned_long_num_to_str(&a, &l, decompress_info(CI_BYTES));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_BYTES), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, decompress_info(CI_FILES));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_FILES), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, decompress_info(CI_LOCKED));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_LOCKED), term));
	add_to_str(&a, &l, cast_uchar ".\n");
#endif

#ifdef G
	if (F) {
		add_to_str(&a, &l, get_text_translation(TEXT_(T_IMAGE_CACHE), term));
		add_to_str(&a, &l, cast_uchar ": ");
		add_unsigned_long_num_to_str(&a, &l, imgcache_info(CI_BYTES));
		add_chr_to_str(&a, &l, ' ');
		add_to_str(&a, &l, get_text_translation(TEXT_(T_BYTES), term));
		add_to_str(&a, &l, cast_uchar ", ");
		add_unsigned_long_num_to_str(&a, &l, imgcache_info(CI_FILES));
		add_chr_to_str(&a, &l, ' ');
		add_to_str(&a, &l, get_text_translation(TEXT_(T_IMAGES), term));
		add_to_str(&a, &l, cast_uchar ", ");
		add_unsigned_long_num_to_str(&a, &l, imgcache_info(CI_LOCKED));
		add_chr_to_str(&a, &l, ' ');
		add_to_str(&a, &l, get_text_translation(TEXT_(T_LOCKED), term));
		add_to_str(&a, &l, cast_uchar ".\n");

		add_to_str(&a, &l, get_text_translation(TEXT_(T_FONT_CACHE), term));
		add_to_str(&a, &l, cast_uchar ": ");
		add_unsigned_long_num_to_str(&a, &l, fontcache_info(CI_BYTES));
		add_chr_to_str(&a, &l, ' ');
		add_to_str(&a, &l, get_text_translation(TEXT_(T_BYTES), term));
		add_to_str(&a, &l, cast_uchar ", ");
		add_unsigned_long_num_to_str(&a, &l, fontcache_info(CI_FILES));
		add_chr_to_str(&a, &l, ' ');
		add_to_str(&a, &l, get_text_translation(TEXT_(T_LETTERS), term));
		add_to_str(&a, &l, cast_uchar ".\n");
	}
#endif

	add_to_str(&a, &l, get_text_translation(TEXT_(T_FORMATTED_DOCUMENT_CACHE), term));
	add_to_str(&a, &l, cast_uchar ": ");
	add_unsigned_long_num_to_str(&a, &l, formatted_info(CI_FILES));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_DOCUMENTS), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, formatted_info(CI_LOCKED));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_LOCKED), term));
	add_to_str(&a, &l, cast_uchar ".\n");

	add_to_str(&a, &l, get_text_translation(TEXT_(T_DNS_CACHE), term));
	add_to_str(&a, &l, cast_uchar ": ");
	add_unsigned_long_num_to_str(&a, &l, dns_info(CI_FILES));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_SERVERS), term));
#ifdef SSL_SESSION_RESUME
	add_to_str(&a, &l, cast_uchar ", ");
	add_to_str(&a, &l, get_text_translation(TEXT_(T_TLS_SESSION_CACHE), term));
	add_to_str(&a, &l, cast_uchar ": ");
	add_unsigned_long_num_to_str(&a, &l, session_info(CI_FILES));
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_SERVERS), term));
#endif
	add_chr_to_str(&a, &l, '.');
	/*{
		int j = get_time() / 100 % 170;
		int i;
		add_chr_to_str(&a, &l, '\n');
		for (i = 0; i < j; i++)
			add_chr_to_str(&a, &l, '-');
	}*/

	return a;
}

static void resource_info_menu(struct terminal *term, void *d, void *ses_)
{
	unsigned char *a = resource_info_msg(term);
	msg_box(term, NULL, TEXT_(T_RESOURCES), AL_LEFT, a, MSG_BOX_END, NULL, 1, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC);
	refresh_dialog_box(term, resource_info_msg);
}

#ifdef LEAK_DEBUG

#if defined(LEAK_DEBUG_LIST)

static void top_blocks(void *r_, int mode, unsigned char *caption)
{
	struct refresh *r = (struct refresh *)r_;
	struct terminal *term = r->term;
	unsigned char *tm;
	int n_entries;

	if (!F) {
		n_entries = term->y - 7;
#ifdef G
	} else {
		n_entries = term->y / G_BFU_FONT_SIZE - 7;
#endif
	}

	if (n_entries < 3)
		n_entries = 3;

	tm = get_top_memory(mode, n_entries);
	msg_box(term, getml(tm, NULL), caption, AL_LEFT | AL_MONO, tm, MSG_BOX_END, (void *)r, 1, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC);
}

static void most_allocated(void *r_)
{
	struct refresh *r = (struct refresh *)r_;
	top_blocks(r, GTM_MOST_ALLOCATED, TEXT_(T_MOST_ALLOCATED));
}

static void largest_blocks(void *r_)
{
	struct refresh *r = (struct refresh *)r_;
	top_blocks(r, GTM_LARGEST_BLOCKS, TEXT_(T_LARGEST_BLOCKS));
}

#endif

static unsigned char *memory_info_msg(struct terminal *term)
{
	unsigned char *a = init_str();
	int l = 0;

	add_unsigned_long_num_to_str(&a, &l, mem_amount);
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_MEMORY_ALLOCATED), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, mem_blocks);
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_BLOCKS_ALLOCATED), term));
	add_chr_to_str(&a, &l, '.');

#ifdef MEMORY_BIGALLOC
	add_to_str(&a, &l, cast_uchar "\n");
	add_unsigned_long_num_to_str(&a, &l, mem_bigalloc);
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_MEMORY_LARGE_BLOCKS), term));
	add_to_str(&a, &l, cast_uchar ", ");
	add_unsigned_long_num_to_str(&a, &l, blocks_bigalloc);
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_BLOCKS_LARGE_BLOCKS), term));
	add_chr_to_str(&a, &l, '.');
#endif
#ifdef MEMORY_REQUESTED
	if (mem_requested && blocks_requested) {
		add_to_str(&a, &l, cast_uchar "\n");
		add_unsigned_long_num_to_str(&a, &l, mem_requested);
		add_chr_to_str(&a, &l, ' ');
		add_to_str(&a, &l, get_text_translation(TEXT_(T_MEMORY_REQUESTED), term));
		add_to_str(&a, &l, cast_uchar ", ");
		add_unsigned_long_num_to_str(&a, &l, blocks_requested);
		add_chr_to_str(&a, &l, ' ');
		add_to_str(&a, &l, get_text_translation(TEXT_(T_BLOCKS_REQUESTED), term));
		add_chr_to_str(&a, &l, '.');
	}
#endif
#ifdef JS
	add_to_str(&a, &l, cast_uchar "\n");
	add_unsigned_long_num_to_str(&a, &l, js_zaflaknuto_pameti);
	add_chr_to_str(&a, &l, ' ');
	add_to_str(&a, &l, get_text_translation(TEXT_(T_JS_MEMORY_ALLOCATED), term));
	add_chr_to_str(&a, &l, '.');
#endif

	return a;
}

static void memory_info_menu(struct terminal *term, void *d, void *ses_)
{
	unsigned char *a = memory_info_msg(term);

#if defined(LEAK_DEBUG_LIST)
	msg_box(term, NULL, TEXT_(T_MEMORY_INFO), AL_CENTER, a, MSG_BOX_END, NULL, 3, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC, TEXT_(T_MOST_ALLOCATED), most_allocated, 0, TEXT_(T_LARGEST_BLOCKS), largest_blocks, 0);
#else
	msg_box(term, NULL, TEXT_(T_MEMORY_INFO), AL_CENTER, a, MSG_BOX_END, NULL, 1, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC);
#endif
	refresh_dialog_box(term, memory_info_msg);
}

#endif

static void flush_caches(struct terminal *term, void *d, void *e)
{
	abort_background_connections();
	shrink_memory(SH_FREE_ALL, 0);
}

/* jde v historii na polozku id_ptr */
void go_backwards(struct terminal *term, void *id_ptr, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	unsigned want_id = (unsigned)(my_intptr_t)id_ptr;
	struct location *l;
	struct list_head *ll;
	int n = 0;
	foreach(struct location, l, ll, ses->history) {
		if (l->location_id == want_id) {
			goto have_it;
		}
		n++;
	}
	n = -1;
	foreach(struct location, l, ll, ses->forward_history) {
		if (l->location_id == want_id) {
			goto have_it;
		}
		n--;
	}
	return;

	have_it:
	go_back(ses, n);
}

static_const struct menu_item no_hist_menu[] = {
	{ TEXT_(T_NO_HISTORY), cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

static void add_history_menu_entry(struct terminal *term, struct menu_item **mi, int *n, struct location *l)
{
	unsigned char *url;
	if (!*mi) *mi = new_menu(MENU_FREE_ITEMS | MENU_FREE_TEXT);
	url = display_url(term, l->url, 1);
	add_to_menu(mi, url, cast_uchar "", cast_uchar "", go_backwards, (void *)(my_intptr_t)l->location_id, 0, *n);
	(*n)++;
	if (*n == MAXINT) overalloc();
}

static void history_menu(struct terminal *term, void *ddd, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	struct location *l;
	struct list_head *ll;
	struct menu_item *mi = NULL;
	int n = 0;
	int selected = 0;
	foreachback(struct location, l, ll, ses->forward_history) {
		add_history_menu_entry(term, &mi, &n, l);
	}
	selected = n;
	foreach(struct location, l, ll, ses->history) {
		add_history_menu_entry(term, &mi, &n, l);
	}
	if (!mi) do_menu(term, (struct menu_item *)no_hist_menu, ses);
	else do_menu_selected(term, mi, ses, selected, NULL, NULL);
}

static_const struct menu_item no_downloads_menu[] = {
	{ TEXT_(T_NO_DOWNLOADS), cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

static void downloads_menu(struct terminal *term, void *ddd, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	struct download *d;
	struct list_head *ld;
	struct menu_item *mi = NULL;
	int n = 0;
	foreachback(struct download, d, ld, downloads) {
		unsigned char *f, *ff;
		if (!mi) mi = new_menu(MENU_FREE_ITEMS | MENU_FREE_TEXT | MENU_FREE_RTEXT);
		f = !d->prog ? d->orig_file : d->url;
		for (ff = f; *ff; ff++)
			if ((dir_sep(ff[0])
#if defined(DOS_FS) || defined(SPAD)
			  || (!d->prog && ff[0] == ':')
#endif
			  ) && ff[1])
				f = ff + 1;
		if (!d->prog)
			f = stracpy(f);
		else
			f = display_url(term, f, 1);
		add_to_menu(&mi, f, download_percentage(d, 0), cast_uchar "", display_download, d, 0, n);
		n++;
	}
	if (!n) do_menu(term, (struct menu_item *)no_downloads_menu, ses);
	else do_menu(term, mi, ses);
}

#ifndef GRDRV_VIRTUAL_DEVICES

#define have_windows_menu	0

#else

#define have_windows_menu	(F && drv->init_device == init_virtual_device)

static void window_switch(struct terminal *term, void *nump, void *ses)
{
	int n = (int)(my_intptr_t)nump;
	switch_virtual_device(n);
}

static void windows_menu(struct terminal *term, void *xxx, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	struct menu_item *mi = new_menu(MENU_FREE_ITEMS | MENU_FREE_TEXT | MENU_FREE_RTEXT | MENU_FREE_HOTKEY);
	int i;
	int selected = 0;
	int pos = 0;
	int have_free_slot = 0;
	int o;
	for (i = 0; i < n_virtual_devices; i++) {
		if (virtual_devices[i]) {
			struct session *xs;
			struct list_head *ls;
			unsigned char *l = init_str(), *r = init_str(), *h = init_str();
			int ll = 0, rr = 0, hh = 0;
			add_num_to_str(&l, &ll, i + 1);
			add_chr_to_str(&l, &ll, '.');
			foreach(struct session, xs, ls, sessions) if ((void *)xs->term == virtual_devices[i]->user_data) {
				if (xs->screen && xs->screen->f_data && xs->screen->f_data->title) {
					add_chr_to_str(&l, &ll, ' ');
					if (xs->screen->f_data->title[0]) {
						add_to_str(&l, &ll, xs->screen->f_data->title);
					} else if (xs->screen->rq && xs->screen->rq->url) {
						unsigned char *url = display_url(term, xs->screen->rq->url, 1);
						add_to_str(&l, &ll, url);
						mem_free(url);
					}
				}
				break;
			}
			if (n_virtual_devices > 10) {
				add_to_str(&r, &rr, cast_uchar "Alt-F");
				add_num_to_str(&r, &rr, i + 1);
			} else {
				add_to_str(&r, &rr, cast_uchar "Alt-");
				add_chr_to_str(&r, &rr, (i + 1) % 10 + '0');
			}
			if (i < 10) {
				add_chr_to_str(&h, &hh, (i + 1) % 10 + '0');
			}
			if (current_virtual_device == virtual_devices[i])
				selected = pos;
			add_to_menu(&mi, l, r, h, window_switch, (void *)(my_intptr_t)i, 0, pos++);
		} else {
			have_free_slot = 1;
		}
	}
	if ((o = can_open_in_new(term)) && have_free_slot) {
		add_to_menu(&mi, cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, pos++);
		mi[pos - 1].free_i = MENU_FREE_ITEMS;
		add_to_menu(&mi, TEXT_(T_NEW_WINDOW), cast_uchar "", TEXT_(T_HK_NEW_WINDOW), open_in_new_window, (void *)&send_open_new_xterm_ptr, o - 1, pos++);
		mi[pos - 1].free_i = MENU_FREE_ITEMS;
	}
	do_menu_selected(term, mi, ses, selected, NULL, NULL);
}

#endif

static void menu_doc_info(struct terminal *term, void *ddd, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	state_msg(ses);
}

static void menu_head_info(struct terminal *term, void *ddd, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	head_msg(ses);
}

static unsigned char *get_clipboard_test_empty(struct terminal *term)
{
	unsigned char *clip = get_clipboard_text(term);
	if (clip && !*clip) {
		mem_free(clip);
		clip = NULL;
	}
	if (!clip) {
		msg_box(
			term,
			NULL,
			TEXT_(T_SAVE_CLIPBOARD_TO_A_FILE),
			AL_CENTER,
			TEXT_(T_THE_CLIPBOARD_IS_EMPTY), MSG_BOX_END,
			NULL,
			1,
			TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC
		);
		return NULL;
	}
	return clip;
}

static void save_clipboard(struct session *ses, unsigned char *file, int mode)
{
	unsigned char *clip;
	int h, rs, l;
	int download_mode = mode == DOWNLOAD_DEFAULT ? CDF_EXCL : 0;
	clip = get_clipboard_test_empty(ses->term);
	if (!clip)
		return;
	if ((h = create_download_file(ses, ses->term->cwd, file, download_mode, 0)) < 0) goto ret;
	l = (int)strlen(cast_const_char clip);
	if (hard_write(h, clip, l) != l) {
		msg_box(ses->term, NULL, TEXT_(T_SAVE_ERROR), AL_CENTER, TEXT_(T_ERROR_WRITING_TO_FILE), MSG_BOX_END, NULL, 1, TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC);
	}
	EINTRLOOP(rs, close(h));
ret:
	mem_free(clip);
}

static void menu_save_clipboard(struct terminal *term, void *ddd, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	unsigned char *clip;
	clip = get_clipboard_test_empty(term);
	if (!clip)
		return;
	mem_free(clip);
	query_file(ses, cast_uchar "", NULL, save_clipboard, NULL, DOWNLOAD_OVERWRITE);
}

static void load_clipboard(void *ses_, unsigned char *file)
{
	struct session *ses = (struct session *)ses_;
	unsigned char *wd, *f, *c;
	if (!*file)
		return;
	wd = get_cwd();
	set_cwd(ses->term->cwd);
	f = translate_download_file(file);
	c = read_config_file(f);
	if (!c) {
		unsigned char *emsg = strerror_alloc(errno, ses->term);
		if (wd) set_cwd(wd), mem_free(wd);
		msg_box(
			ses->term,
			getml(emsg, f, NULL),
			TEXT_(T_LOAD_CLIPBOARD_FROM_A_FILE),
			AL_CENTER,
			TEXT_(T_ERROR_READING_THE_FILE), cast_uchar " ", f, cast_uchar ": ", emsg, MSG_BOX_END,
			NULL,
			1,
			TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC
		);
		return;
	}
	if (wd) set_cwd(wd), mem_free(wd);
	set_clipboard_text(ses->term, c);
	mem_free(f);
	mem_free(c);
}

static void menu_load_clipboard(struct terminal *term, void *ddd, void *ses_)
{
	input_field(term, NULL, TEXT_(T_LOAD_CLIPBOARD_FROM_A_FILE), TEXT_(T_FILE), ses_, &file_history, MAX_INPUT_URL_LEN, cast_uchar "", 0, 0, NULL, 2, TEXT_(T_OK), load_clipboard, TEXT_(T_CANCEL), input_field_null);
}

static void menu_toggle(struct terminal *term, void *ddd, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	toggle(ses, ses->screen, 0);
}

static void set_display_codepage(struct terminal *term, void *pcp, void *ptr)
{
	int cp = (int)(my_intptr_t)pcp;
	struct term_spec *t = new_term_spec(term->term);
	t->character_set = cp;
	cls_redraw_all_terminals();
}

static void set_val(struct terminal *term, void *ip, void *d)
{
	*(int *)d = (int)(my_intptr_t)ip;
}

static void charset_sel_list(struct terminal *term, int ini, void (*set)(struct terminal *term, void *ip, void *ptr), void *ptr, int utf, int def)
{
	int i;
	unsigned char *n;
	struct menu_item *mi;
#ifdef OS_NO_SYSTEM_CHARSET
	def = 0;
#endif
	mi = new_menu(MENU_FREE_ITEMS | MENU_FREE_RTEXT);
	for (i = -def; (n = get_cp_name(i)); i++) {
		unsigned char *n, *r, *p;
		if (!utf && i == utf8_table) continue;
		if (i == -1) {
			n = TEXT_(T_DEFAULT_CHARSET);
			r = stracpy(get_cp_name(term->default_character_set));
			p = cast_uchar strstr(cast_const_char r, " (");
			if (p) *p = 0;
		} else {
			n = get_cp_name(i);
			r = stracpy(cast_uchar "");
		}
		add_to_menu(&mi, n, r, cast_uchar "", set, (void *)(my_intptr_t)i, 0, i + def);
	}
	ini += def;
	if (ini < 0)
		ini = term->default_character_set;
	do_menu_selected(term, mi, ptr, ini, NULL, NULL);
}

static void charset_list(struct terminal *term, void *xxx, void *ses_)
{
	charset_sel_list(term, term->spec->character_set, set_display_codepage, NULL,
#ifdef ENABLE_UTF8
		1
#else
		0
#endif
		, 1);
}

static void terminal_options_ok(void *p)
{
	cls_redraw_all_terminals();
}

static unsigned char * const td_labels[] = {
	TEXT_(T_NO_FRAMES),
	TEXT_(T_VT_100_FRAMES),
	TEXT_(T_LINUX_OR_OS2_FRAMES),
	TEXT_(T_KOI8R_FRAMES),
	TEXT_(T_FREEBSD_FRAMES),
#ifdef ENABLE_UTF8
	TEXT_(T_UTF8_FRAMES),
#endif
	TEXT_(T_USE_11M),
	TEXT_(T_RESTRICT_FRAMES_IN_CP850_852),
	TEXT_(T_BLOCK_CURSOR),
	TEXT_(T_COLOR),
	TEXT_(T_BRAILLE_TERMINAL),
	NULL
};

static void terminal_options(struct terminal *term, void *xxx, void *ses_)
{
	int a;
	struct dialog *d;
	struct term_spec *ts = new_term_spec(term->term);
	d = mem_calloc(sizeof(struct dialog) + 13 * sizeof(struct dialog_item));
	d->title = TEXT_(T_TERMINAL_OPTIONS);
	d->fn = checkbox_list_fn;
	d->udata = (void *)td_labels;
	d->refresh = terminal_options_ok;
	a = 0;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = TERM_DUMB;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->mode;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = TERM_VT100;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->mode;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = TERM_LINUX;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->mode;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = TERM_KOI8;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->mode;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = TERM_FREEBSD;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->mode;
#ifdef ENABLE_UTF8
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = TERM_UTF8;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->mode;
#endif
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->m11_hack;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->restrict_852;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->block_cursor;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->col;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&ts->braille;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a++].text = TEXT_(T_OK);
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a++].text = TEXT_(T_CANCEL);
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

static unsigned char left_margin_str[5];
static unsigned char right_margin_str[5];
static unsigned char top_margin_str[5];
static unsigned char bottom_margin_str[5];

static void margins_ok(void *xxx)
{
	struct terminal *term = xxx;
	int left, right, top, bottom;
	left = atoi(cast_const_char left_margin_str);
	right = atoi(cast_const_char right_margin_str);
	top = atoi(cast_const_char top_margin_str);
	bottom = atoi(cast_const_char bottom_margin_str);
	if (!F) {
		struct term_spec *ts = new_term_spec(term->term);
		if (left + right >= term->real_x ||
		    top + bottom >= term->real_y) {
			goto error;
		}
		ts->left_margin = left;
		ts->right_margin = right;
		ts->top_margin = top;
		ts->bottom_margin = bottom;
		cls_redraw_all_terminals();
#ifdef G
	} else {
		if (drv->set_margin(left, right, top, bottom))
			goto error;
#endif
	}
	return;

error:
	msg_box(
		term,
		NULL,
		TEXT_(T_MARGINS_TOO_LARGE),
		AL_CENTER,
		TEXT_(T_THE_ENTERED_VALUES_ARE_TOO_LARGE_FOR_THE_CURRENT_SCREEN), MSG_BOX_END,
		NULL,
		1,
		TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC
	);
}

static unsigned char * const margins_labels[] = {
	TEXT_(T_LEFT_MARGIN),
	TEXT_(T_RIGHT_MARGIN),
	TEXT_(T_TOP_MARGIN),
	TEXT_(T_BOTTOM_MARGIN),
};

static void screen_margins(struct terminal *term, void *xxx, void *ses_)
{
	struct dialog *d;
	struct term_spec *ts = term->spec;
	int string_len = !F ? 4 : 5;
	int max_value = !F ? 999 : 9999;
	int l, r, t, b;
	if (!F) {
		l = ts->left_margin;
		r = ts->right_margin;
		t = ts->top_margin;
		b = ts->bottom_margin;
#ifdef G
	} else {
		drv->get_margin(&l, &r, &t, &b);
#endif
	}
	snprint(left_margin_str, string_len, l);
	snprint(right_margin_str, string_len, r);
	snprint(top_margin_str, string_len, t);
	snprint(bottom_margin_str, string_len, b);
	d = mem_calloc(sizeof(struct dialog) + 6 * sizeof(struct dialog_item));
	d->title = TEXT_(T_SCREEN_MARGINS);
	d->fn = group_fn;
	d->udata = (void *)margins_labels;
	d->refresh = margins_ok;
	d->refresh_data = term;
	d->items[0].type = D_FIELD;
	d->items[0].dlen = string_len;
	d->items[0].data = left_margin_str;
	d->items[0].fn = check_number;
	d->items[0].gid = 0;
	d->items[0].gnum = max_value;
	d->items[1].type = D_FIELD;
	d->items[1].dlen = string_len;
	d->items[1].data = right_margin_str;
	d->items[1].fn = check_number;
	d->items[1].gid = 0;
	d->items[1].gnum = max_value;
	d->items[2].type = D_FIELD;
	d->items[2].dlen = string_len;
	d->items[2].data = top_margin_str;
	d->items[2].fn = check_number;
	d->items[2].gid = 0;
	d->items[2].gnum = max_value;
	d->items[3].type = D_FIELD;
	d->items[3].dlen = string_len;
	d->items[3].data = bottom_margin_str;
	d->items[3].fn = check_number;
	d->items[3].gid = 0;
	d->items[3].gnum = max_value;
	d->items[4].type = D_BUTTON;
	d->items[4].gid = B_ENTER;
	d->items[4].fn = ok_dialog;
	d->items[4].text = TEXT_(T_OK);
	d->items[5].type = D_BUTTON;
	d->items[5].gid = B_ESC;
	d->items[5].fn = cancel_dialog;
	d->items[5].text = TEXT_(T_CANCEL);
	d->items[6].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#ifdef JS

static unsigned char * const jsopt_labels[] = { TEXT_(T_KILL_ALL_SCRIPTS), TEXT_(T_ENABLE_JAVASCRIPT), TEXT_(T_VERBOSE_JS_ERRORS), TEXT_(T_VERBOSE_JS_WARNINGS), TEXT_(T_ENABLE_ALL_CONVERSIONS), TEXT_(T_ENABLE_GLOBAL_NAME_RESOLUTION), TEXT_(T_MANUAL_JS_CONTROL), TEXT_(T_JS_RECURSION_DEPTH), TEXT_(T_JS_MEMORY_LIMIT_KB), NULL };

static int kill_script_opt;
static unsigned char js_fun_depth_str[7];
static unsigned char js_memory_limit_str[7];


static inline void kill_js_recursively(struct f_data_c *fd)
{
	struct f_data_c *f;
	struct list_head *lf;

	if (fd->js) js_downcall_game_over(fd->js->ctx);
	foreach(struct f_data_c, f, lf, fd->subframes) kill_js_recursively(f);
}


static inline void quiet_kill_js_recursively(struct f_data_c *fd)
{
	struct f_data_c *f;
	struct list_head *lf;

	if (fd->js) js_downcall_game_over(fd->js->ctx);
	foreach(struct f_data_c, f, lf, fd->subframes) quiet_kill_js_recursively(f);
}


static void refresh_javascript(void *ses_)
{
	struct session *ses = (struct session *)ses_;
	if (ses->screen->f_data)jsint_scan_script_tags(ses->screen);
	if (kill_script_opt)
		kill_js_recursively(ses->screen);
	if (!js_enable) /* vypnuli jsme skribt */
	{
		if (ses->default_status)mem_free(ses->default_status),ses->default_status=NULL;
		quiet_kill_js_recursively(ses->screen);
	}

	js_fun_depth=strtol(cast_const_char js_fun_depth_str,0,10);
	js_memory_limit=strtol(cast_const_char js_memory_limit_str,0,10);

	/* reparse document (muze se zmenit hodne veci) */
	html_interpret_recursive(ses->screen);
	draw_formatted(ses);
}


static void javascript_options(struct terminal *term, void *xxx, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	struct dialog *d;
	kill_script_opt=0;
	snprintf(cast_char js_fun_depth_str,7,"%d",js_fun_depth);
	snprintf(cast_char js_memory_limit_str,7,"%d",js_memory_limit);
	d = mem_calloc(sizeof(struct dialog) + 11 * sizeof(struct dialog_item));
	d->title = TEXT_(T_JAVASCRIPT_OPTIONS);
	d->fn = group_fn;
	d->refresh = refresh_javascript;
	d->refresh_data=ses;
	d->udata = (void *)jsopt_labels;
	d->items[0].type = D_CHECKBOX;
	d->items[0].gid = 0;
	d->items[0].dlen = sizeof(int);
	d->items[0].data = (void *)&kill_script_opt;
	d->items[1].type = D_CHECKBOX;
	d->items[1].gid = 0;
	d->items[1].dlen = sizeof(int);
	d->items[1].data = (void *)&js_enable;
	d->items[2].type = D_CHECKBOX;
	d->items[2].gid = 0;
	d->items[2].dlen = sizeof(int);
	d->items[2].data = (void *)&js_verbose_errors;
	d->items[3].type = D_CHECKBOX;
	d->items[3].gid = 0;
	d->items[3].dlen = sizeof(int);
	d->items[3].data = (void *)&js_verbose_warnings;
	d->items[4].type = D_CHECKBOX;
	d->items[4].gid = 0;
	d->items[4].dlen = sizeof(int);
	d->items[4].data = (void *)&js_all_conversions;
	d->items[5].type = D_CHECKBOX;
	d->items[5].gid = 0;
	d->items[5].dlen = sizeof(int);
	d->items[5].data = (void *)&js_global_resolve;
	d->items[6].type = D_CHECKBOX;
	d->items[6].gid = 0;
	d->items[6].dlen = sizeof(int);
	d->items[6].data = (void *)&js_manual_confirmation;
	d->items[7].type = D_FIELD;
	d->items[7].dlen = 7;
	d->items[7].data = js_fun_depth_str;
	d->items[7].fn = check_number;
	d->items[7].gid = 1;
	d->items[7].gnum = 999999;
	d->items[8].type = D_FIELD;
	d->items[8].dlen = 7;
	d->items[8].data = js_memory_limit_str;
	d->items[8].fn = check_number;
	d->items[8].gid = 1024;
	d->items[8].gnum = 30*1024;
	d->items[9].type = D_BUTTON;
	d->items[9].gid = B_ENTER;
	d->items[9].fn = ok_dialog;
	d->items[9].text = TEXT_(T_OK);
	d->items[10].type = D_BUTTON;
	d->items[10].gid = B_ESC;
	d->items[10].fn = cancel_dialog;
	d->items[10].text = TEXT_(T_CANCEL);
	d->items[11].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#endif

#ifndef G

static inline void reinit_video(void) { }

#else

#define VO_GAMMA_LEN 9
static unsigned char disp_red_g[VO_GAMMA_LEN];
static unsigned char disp_green_g[VO_GAMMA_LEN];
static unsigned char disp_blue_g[VO_GAMMA_LEN];
static unsigned char user_g[VO_GAMMA_LEN];
static unsigned char aspect_str[VO_GAMMA_LEN];

static void reinit_video(void);

static void refresh_video(void *xxx)
{
	display_red_gamma = atof(cast_const_char disp_red_g);
	display_green_gamma = atof(cast_const_char disp_green_g);
	display_blue_gamma = atof(cast_const_char disp_blue_g);
	user_gamma = atof(cast_const_char user_g);
	bfu_aspect = atof(cast_const_char aspect_str);
	if (F && drv->flags & (GD_SELECT_PALETTE | GD_SWITCH_PALETTE) && drv->set_palette)
		drv->set_palette();
	reinit_video();
}

static void reinit_video(void)
{
	if (!F)
		return;

	/* Flush font cache */
	update_aspect();

	/* Recompute dithering tables for the new gamma */
	init_dither(drv->depth);

	shutdown_bfu();
	freetype_reload();
	init_bfu();
	init_grview();

	/* Redraw all terminals */
	cls_redraw_all_terminals();

	shrink_format_cache(SH_FREE_ALL);
}

#define video_msg_0	TEXT_(T_VIDEO_OPTIONS_TEXT)

static unsigned char * const video_msg_1[] = {
	TEXT_(T_RED_DISPLAY_GAMMA),
	TEXT_(T_GREEN_DISPLAY_GAMMA),
	TEXT_(T_BLUE_DISPLAY_GAMMA),
	TEXT_(T_USER_GAMMA),
	TEXT_(T_ASPECT_RATIO),
};

static unsigned char * const video_msg_2[] = {
	TEXT_(T_DISPLAY_OPTIMIZATION_CRT),
	TEXT_(T_DISPLAY_OPTIMIZATION_LCD_RGB),
	TEXT_(T_DISPLAY_OPTIMIZATION_LCD_BGR),
	TEXT_(T_DITHER_LETTERS),
	TEXT_(T_DITHER_IMAGES),
	TEXT_(T_8_BIT_GAMMA_CORRECTION),
	TEXT_(T_16_BIT_GAMMA_CORRECTION),
	TEXT_(T_AUTO_GAMMA_CORRECTION),
};

#define video_option_select_palette	(drv->flags & GD_SELECT_PALETTE)
#define video_option_switch_palette	(drv->flags & GD_SWITCH_PALETTE)
#define video_option_scrolling		(drv->flags & GD_DONT_USE_SCROLL)

static unsigned char * const video_msg_select_palette[] = {
	TEXT_(T_RGB_PALETTE_6x6x6),
	TEXT_(T_RGB_PALETTE_8x8x4),
};

static unsigned char * const video_msg_switch_palette[] = {
	TEXT_(T_SWITCH_PALETTE),
};

static unsigned char * const video_msg_scrolling[] = {
	TEXT_(T_OVERWRITE_SCREEN_INSTEAD_OF_SCROLLING_IT),
};

static void videoopt_fn(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	struct dialog_item_data *did;
	int max = 0, min = 0;
	int w, rw;
	int y = gf_val(-1, -G_BFU_FONT_SIZE);
	max_text_width(term, video_msg_0, &max, AL_LEFT);
	min_text_width(term, video_msg_0, &min, AL_LEFT);
	max_group_width(term, video_msg_1, dlg->items, array_elements(video_msg_1), &max);
	min_group_width(term, video_msg_1, dlg->items, array_elements(video_msg_1), &min);
	checkboxes_width(term, video_msg_2, array_elements(video_msg_2), &max, max_text_width);
	checkboxes_width(term, video_msg_2, array_elements(video_msg_2), &min, min_text_width);
	if (video_option_select_palette) {
		checkboxes_width(term, video_msg_select_palette, array_elements(video_msg_select_palette), &max, max_text_width);
		checkboxes_width(term, video_msg_select_palette, array_elements(video_msg_select_palette), &min, min_text_width);
	}
	if (video_option_switch_palette) {
		checkboxes_width(term, video_msg_switch_palette, array_elements(video_msg_switch_palette), &max, max_text_width);
		checkboxes_width(term, video_msg_switch_palette, array_elements(video_msg_switch_palette), &min, min_text_width);
	}
	if (video_option_scrolling) {
		checkboxes_width(term, video_msg_scrolling, array_elements(video_msg_scrolling), &max, max_text_width);
		checkboxes_width(term, video_msg_scrolling, array_elements(video_msg_scrolling), &min, min_text_width);
	}
	max_buttons_width(term, dlg->items + dlg->n-2, 2, &max);
	min_buttons_width(term, dlg->items + dlg->n-2, 2, &min);
	w = dlg->win->term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w > max) w = max;
	if (w < min) w = min;
	if (w > dlg->win->term->x - 2 * DIALOG_LB) w = dlg->win->term->x - 2 * DIALOG_LB;
	if (w < 1) w = 1;
	rw = 0;
	dlg_format_text(dlg, NULL, video_msg_0, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	y += LL;
	did = dlg->items;
	dlg_format_group(dlg, NULL, video_msg_1, did, array_elements(video_msg_1), 0, &y, w, &rw);
	did += array_elements(video_msg_1);
	y += LL;
	dlg_format_checkboxes(dlg, NULL, did, array_elements(video_msg_2), dlg->x + DIALOG_LB, &y, w, &rw, video_msg_2);
	did += array_elements(video_msg_2);
	if (video_option_select_palette) {
		dlg_format_checkboxes(dlg, NULL, did, array_elements(video_msg_select_palette), dlg->x + DIALOG_LB, &y, w, &rw, video_msg_select_palette);
		did += array_elements(video_msg_select_palette);
	}
	if (video_option_switch_palette) {
		dlg_format_checkboxes(dlg, NULL, did, array_elements(video_msg_switch_palette), dlg->x + DIALOG_LB, &y, w, &rw, video_msg_switch_palette);
		did += array_elements(video_msg_switch_palette);
	}
	if (video_option_scrolling) {
		dlg_format_checkboxes(dlg, NULL, did, array_elements(video_msg_scrolling), dlg->x + DIALOG_LB, &y, w, &rw, video_msg_scrolling);
		did += array_elements(video_msg_scrolling);
	}
	y += LL;
	dlg_format_buttons(dlg, NULL, did, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = w + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB;
	dlg_format_text(dlg, term, video_msg_0, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	y += LL;
	did = dlg->items;
	dlg_format_group(dlg, term, video_msg_1, did, array_elements(video_msg_1), dlg->x + DIALOG_LB, &y, w, NULL);
	did += array_elements(video_msg_1);
	y += LL;
	dlg_format_checkboxes(dlg, term, did, array_elements(video_msg_2), dlg->x + DIALOG_LB, &y, w, NULL, video_msg_2);
	did += array_elements(video_msg_2);
	if (video_option_select_palette) {
		dlg_format_checkboxes(dlg, term, did, array_elements(video_msg_select_palette), dlg->x + DIALOG_LB, &y, w, NULL, video_msg_select_palette);
		did += array_elements(video_msg_select_palette);
	}
	if (video_option_switch_palette) {
		dlg_format_checkboxes(dlg, term, did, array_elements(video_msg_switch_palette), dlg->x + DIALOG_LB, &y, w, NULL, video_msg_switch_palette);
		did += array_elements(video_msg_switch_palette);
	}
	if (video_option_scrolling) {
		dlg_format_checkboxes(dlg, term, did, array_elements(video_msg_scrolling), dlg->x + DIALOG_LB, &y, w, NULL, video_msg_scrolling);
		did += array_elements(video_msg_scrolling);
	}
	y += LL;
	dlg_format_buttons(dlg, term, dlg->items+dlg->n-2, 2, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
}

static void video_options(struct terminal *term, void *xxx, void *ses_)
{
	struct dialog *d;
	int a;
	snprintf(cast_char disp_red_g, VO_GAMMA_LEN, "%f", display_red_gamma);
	remove_zeroes(disp_red_g);
	snprintf(cast_char disp_green_g, VO_GAMMA_LEN, "%f", display_green_gamma);
	remove_zeroes(disp_green_g);
	snprintf(cast_char disp_blue_g, VO_GAMMA_LEN, "%f", display_blue_gamma);
	remove_zeroes(disp_blue_g);
	snprintf(cast_char user_g, VO_GAMMA_LEN, "%f", user_gamma);
	remove_zeroes(user_g);
	snprintf(cast_char aspect_str, VO_GAMMA_LEN, "%f", bfu_aspect);
	remove_zeroes(aspect_str);
	d = mem_calloc(sizeof(struct dialog) + 18 * sizeof(struct dialog_item));
	d->title = TEXT_(T_VIDEO_OPTIONS);
	d->fn = videoopt_fn;
	d->refresh = refresh_video;
	a=0;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = VO_GAMMA_LEN;
	d->items[a].data = disp_red_g;
	d->items[a].fn = check_float;
	d->items[a].gid = 1;
	d->items[a++].gnum = 10000;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = VO_GAMMA_LEN;
	d->items[a].data = disp_green_g;
	d->items[a].fn = check_float;
	d->items[a].gid = 1;
	d->items[a++].gnum = 10000;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = VO_GAMMA_LEN;
	d->items[a].data = disp_blue_g;
	d->items[a].fn = check_float;
	d->items[a].gid = 1;
	d->items[a++].gnum = 10000;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = VO_GAMMA_LEN;
	d->items[a].data = user_g;
	d->items[a].fn = check_float;
	d->items[a].gid = 1;
	d->items[a++].gnum = 10000;

	d->items[a].type = D_FIELD;
	d->items[a].dlen = VO_GAMMA_LEN;
	d->items[a].data = aspect_str;
	d->items[a].fn = check_float;
	d->items[a].gid = 25;
	d->items[a++].gnum = 400;

	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = 0;	/* CRT */
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&display_optimize;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = 1;	/* LCD RGB */
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&display_optimize;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = 2;	/* LCD BGR*/
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&display_optimize;

	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&dither_letters;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&dither_images;
	d->items[a].type = D_CHECKBOX;

	d->items[a].gid = 2;
	d->items[a].gnum = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&gamma_bits;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 2;
	d->items[a].gnum = 1;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&gamma_bits;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 2;
	d->items[a].gnum = 2;
	d->items[a].dlen = sizeof(int);
	d->items[a++].data = (void *)&gamma_bits;

	if (video_option_select_palette) {
		int *palette_mode_p = &drv->param->palette_mode;
		d->items[a].type = D_CHECKBOX;
		d->items[a].gid = 3;
		d->items[a].gnum = 0;
		d->items[a].dlen = sizeof(int);
		d->items[a++].data = (void *)palette_mode_p;
		d->items[a].type = D_CHECKBOX;
		d->items[a].gid = 3;
		d->items[a].gnum = 1;
		d->items[a].dlen = sizeof(int);
		d->items[a++].data = (void *)palette_mode_p;
	}

	if (video_option_switch_palette) {
		int *palette_mode_p = &drv->param->palette_mode;
		d->items[a].type = D_CHECKBOX;
		d->items[a].gid = 0;
		d->items[a].dlen = sizeof(int);
		d->items[a++].data = (void *)palette_mode_p;
	}

	if (video_option_scrolling) {
		d->items[a].type = D_CHECKBOX;
		d->items[a].gid = 0;
		d->items[a].dlen = sizeof(int);
		d->items[a++].data = (void *)&overwrite_instead_of_scroll;
	}

	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a++].text = TEXT_(T_OK);
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a++].text = TEXT_(T_CANCEL);
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#endif

static void refresh_network(void *xxx)
{
	abort_background_connections();
	register_bottom_half(check_queue, NULL);
}

static unsigned char max_c_str[3];
static unsigned char max_cth_str[3];
static unsigned char max_t_str[3];
static unsigned char time_str[5];
static unsigned char unrtime_str[5];
static unsigned char addrtime_str[4];

static void refresh_connections(void *xxx)
{
	netcfg_stamp++;
	max_connections = atoi(cast_const_char max_c_str);
	max_connections_to_host = atoi(cast_const_char max_cth_str);
	max_tries = atoi(cast_const_char max_t_str);
	receive_timeout = atoi(cast_const_char time_str);
	unrestartable_receive_timeout = atoi(cast_const_char unrtime_str);
	timeout_multiple_addresses = atoi(cast_const_char addrtime_str);
	refresh_network(xxx);
}

static unsigned char *net_msg[10];

static void dlg_net_options(struct terminal *term, void *xxx, void *yyy)
{
	struct dialog *d;
	int a;
	snprint(max_c_str, 3, max_connections);
	snprint(max_cth_str, 3, max_connections_to_host);
	snprint(max_t_str, 3, max_tries);
	snprint(time_str, 5, receive_timeout);
	snprint(unrtime_str, 5, unrestartable_receive_timeout);
	snprint(addrtime_str, 4, timeout_multiple_addresses);
	d = mem_calloc(sizeof(struct dialog) + 11 * sizeof(struct dialog_item));
	d->title = TEXT_(T_NETWORK_OPTIONS);
	d->fn = group_fn;
	d->udata = (void *)net_msg;
	d->refresh = refresh_connections;
	a = 0;
	net_msg[a] = TEXT_(T_MAX_CONNECTIONS);
	d->items[a].type = D_FIELD;
	d->items[a].data = max_c_str;
	d->items[a].dlen = 3;
	d->items[a].fn = check_number;
	d->items[a].gid = 1;
	d->items[a++].gnum = 99;
	net_msg[a] = TEXT_(T_MAX_CONNECTIONS_TO_ONE_HOST);
	d->items[a].type = D_FIELD;
	d->items[a].data = max_cth_str;
	d->items[a].dlen = 3;
	d->items[a].fn = check_number;
	d->items[a].gid = 1;
	d->items[a++].gnum = 99;
	net_msg[a] = TEXT_(T_RETRIES);
	d->items[a].type = D_FIELD;
	d->items[a].data = max_t_str;
	d->items[a].dlen = 3;
	d->items[a].fn = check_number;
	d->items[a].gid = 0;
	d->items[a++].gnum = 16;
	net_msg[a] = TEXT_(T_RECEIVE_TIMEOUT_SEC);
	d->items[a].type = D_FIELD;
	d->items[a].data = time_str;
	d->items[a].dlen = 5;
	d->items[a].fn = check_number;
	d->items[a].gid = 1;
	d->items[a++].gnum = 9999;
	net_msg[a] = TEXT_(T_TIMEOUT_WHEN_UNRESTARTABLE);
	d->items[a].type = D_FIELD;
	d->items[a].data = unrtime_str;
	d->items[a].dlen = 5;
	d->items[a].fn = check_number;
	d->items[a].gid = 1;
	d->items[a++].gnum = 9999;
#ifdef USE_GETADDRINFO
	net_msg[a] = TEXT_(T_TIMEOUT_WHEN_TRYING_MULTIPLE_ADDRESSES);
#else
	net_msg[a] = TEXT_(T_TIMEOUT_WHEN_TRYING_KEEPALIVE_CONNECTION);
#endif
	d->items[a].type = D_FIELD;
	d->items[a].data = addrtime_str;
	d->items[a].dlen = 4;
	d->items[a].fn = check_number;
	d->items[a].gid = 1;
	d->items[a++].gnum = 999;
	net_msg[a] = TEXT_(T_BIND_TO_LOCAL_IP_ADDRESS);
	d->items[a].type = D_FIELD;
	d->items[a].data = bind_ip_address;
	d->items[a].dlen = sizeof(bind_ip_address);
	d->items[a++].fn = check_local_ip_address;
#ifdef SUPPORT_IPV6
	if (support_ipv6) {
		net_msg[a] = TEXT_(T_BIND_TO_LOCAL_IPV6_ADDRESS);
		d->items[a].type = D_FIELD;
		d->items[a].data = bind_ipv6_address;
		d->items[a].dlen = sizeof(bind_ipv6_address);
		d->items[a++].fn = check_local_ipv6_address;
	}
#endif
	net_msg[a] = TEXT_(T_SET_TIME_OF_DOWNLOADED_FILES);
	d->items[a].type = D_CHECKBOX;
	d->items[a].data = (unsigned char *)&download_utime;
	d->items[a++].dlen = sizeof(int);
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a++].text = TEXT_(T_OK);
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a++].text = TEXT_(T_CANCEL);
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#ifdef SUPPORT_IPV6

static unsigned char * const ipv6_labels[] = { TEXT_(T_IPV6_DEFAULT), TEXT_(T_IPV6_PREFER_IPV4), TEXT_(T_IPV6_PREFER_IPV6), TEXT_(T_IPV6_USE_ONLY_IPV4), TEXT_(T_IPV6_USE_ONLY_IPV6), NULL };

static void dlg_ipv6_options(struct terminal *term, void *xxx, void *yyy)
{
	struct dialog *d;
	d = mem_calloc(sizeof(struct dialog) + 7 * sizeof(struct dialog_item));
	d->title = TEXT_(T_IPV6_OPTIONS);
	d->fn = checkbox_list_fn;
	d->udata = (void *)ipv6_labels;
	d->refresh = refresh_network;
	d->items[0].type = D_CHECKBOX;
	d->items[0].gid = 1;
	d->items[0].gnum = ADDR_PREFERENCE_DEFAULT;
	d->items[0].dlen = sizeof(int);
	d->items[0].data = (void *)&ipv6_options.addr_preference;
	d->items[1].type = D_CHECKBOX;
	d->items[1].gid = 1;
	d->items[1].gnum = ADDR_PREFERENCE_IPV4;
	d->items[1].dlen = sizeof(int);
	d->items[1].data = (void *)&ipv6_options.addr_preference;
	d->items[2].type = D_CHECKBOX;
	d->items[2].gid = 1;
	d->items[2].gnum = ADDR_PREFERENCE_IPV6;
	d->items[2].dlen = sizeof(int);
	d->items[2].data = (void *)&ipv6_options.addr_preference;
	d->items[3].type = D_CHECKBOX;
	d->items[3].gid = 1;
	d->items[3].gnum = ADDR_PREFERENCE_IPV4_ONLY;
	d->items[3].dlen = sizeof(int);
	d->items[3].data = (void *)&ipv6_options.addr_preference;
	d->items[4].type = D_CHECKBOX;
	d->items[4].gid = 1;
	d->items[4].gnum = ADDR_PREFERENCE_IPV6_ONLY;
	d->items[4].dlen = sizeof(int);
	d->items[4].data = (void *)&ipv6_options.addr_preference;
	d->items[5].type = D_BUTTON;
	d->items[5].gid = B_ENTER;
	d->items[5].fn = ok_dialog;
	d->items[5].text = TEXT_(T_OK);
	d->items[6].type = D_BUTTON;
	d->items[6].gid = B_ESC;
	d->items[6].fn = cancel_dialog;
	d->items[6].text = TEXT_(T_CANCEL);
	d->items[7].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#endif

#ifdef HAVE_SSL
#define N_N	6
#else
#define N_N	5
#endif

static unsigned char * const proxy_msg[] = {
	TEXT_(T_HTTP_PROXY__HOST_PORT),
	TEXT_(T_FTP_PROXY__HOST_PORT),
#ifdef HAVE_SSL
	TEXT_(T_HTTPS_PROXY__HOST_PORT),
#endif
	TEXT_(T_SOCKS_4A_PROXY__USER_HOST_PORT),
	TEXT_(T_APPEND_TEXT_TO_SOCKS_LOOKUPS),
	TEXT_(T_NOPROXY_LIST),
	TEXT_(T_ONLY_PROXIES),
};

static void proxy_fn(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	int max = 0, min = 0;
	int w, rw;
	int i;
	int y = gf_val(-1, -G_BFU_FONT_SIZE);
	if (dlg->win->term->spec->braille) y += LL;
	for (i = 0; i < N_N; i++) {
		max_text_width(term, proxy_msg[i], &max, AL_LEFT);
		min_text_width(term, proxy_msg[i], &min, AL_LEFT);
	}
	max_group_width(term, proxy_msg + N_N, dlg->items + N_N, dlg->n - 2 - N_N, &max);
	min_group_width(term, proxy_msg + N_N, dlg->items + N_N, dlg->n - 2 - N_N, &min);
	max_buttons_width(term, dlg->items + dlg->n - 2, 2, &max);
	min_buttons_width(term, dlg->items + dlg->n - 2, 2, &min);
	w = dlg->win->term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w > max) w = max;
	if (w < min) w = min;
	if (w > dlg->win->term->x - 2 * DIALOG_LB) w = dlg->win->term->x - 2 * DIALOG_LB;
	if (w < 1) w = 1;
	rw = 0;
	for (i = 0; i < N_N; i++) {
		dlg_format_text_and_field(dlg, NULL, proxy_msg[i], &dlg->items[i], 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
		if (!dlg->win->term->spec->braille) y += LL;
	}
	dlg_format_group(dlg, NULL, proxy_msg + N_N, dlg->items + N_N, dlg->n - 2 - N_N, 0, &y, w, &rw);
	y += LL;
	dlg_format_buttons(dlg, NULL, dlg->items + dlg->n - 2, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = w + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB;
	if (dlg->win->term->spec->braille) y += LL;
	for (i = 0; i < N_N; i++) {
		dlg_format_text_and_field(dlg, term, proxy_msg[i], &dlg->items[i], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
		if (!dlg->win->term->spec->braille) y += LL;
	}
	dlg_format_group(dlg, term, proxy_msg + N_N, &dlg->items[N_N], dlg->n - 2 - N_N, dlg->x + DIALOG_LB, &y, w, NULL);
	y += LL;
	dlg_format_buttons(dlg, term, &dlg->items[dlg->n - 2], 2, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
}

void reset_settings_for_tor(void)
{
#ifdef DOS
	max_connections = 3;
	max_connections_to_host = 2;
#else
	max_connections = 10;
	max_connections_to_host = 8;
#endif
	max_tries = 3;
	receive_timeout = 120;
	unrestartable_receive_timeout = 600;

	max_cookie_age = 0;

	max_format_cache_entries = 5;
	memory_cache_size = 4194304;
	image_cache_size = 1048576;
	font_cache_size = 2097152;
	aggressive_cache = 1;

	http_options.http10 = 0;
	http_options.allow_blacklist = 1;
	http_options.no_accept_charset = 0;
	http_options.no_compression = 0;
	http_options.retry_internal_errors = 0;
	http_options.header.do_not_track = 0;
	http_options.header.referer = proxies.only_proxies ? REFERER_NONE : REFERER_REAL_SAME_SERVER;
	http_options.header.extra_header[0] = 0;

	ftp_options.eprt_epsv = 0;

	dither_letters = 1;
	dither_images = 1;

	dds.tables = 1;
	dds.frames = 1;
	dds.auto_refresh = 0;
	dds.display_images = 1;
}

static void data_cleanup(void)
{
	struct session *ses;
	struct list_head *lses;
	reset_settings_for_tor();
	foreach(struct session, ses, lses, sessions) {
		ses->ds.tables = dds.tables;
		ses->ds.frames = dds.frames;
		ses->ds.auto_refresh = dds.auto_refresh;
		ses->ds.display_images = dds.display_images;
		cleanup_session(ses);
		draw_formatted(ses);
	}
	reinit_video();
	free_blacklist();
	free_cookies();
	free_auth();
	abort_all_connections();
	shrink_memory(SH_FREE_ALL, 0);
}

static unsigned char http_proxy[MAX_STR_LEN];
static unsigned char ftp_proxy[MAX_STR_LEN];
static unsigned char https_proxy[MAX_STR_LEN];
static unsigned char socks_proxy[MAX_STR_LEN];
static unsigned char no_proxy[MAX_STR_LEN];

static void display_proxy(struct terminal *term, unsigned char *result, unsigned char *proxy)
{
	unsigned char *url, *res;
	int sl;

	if (!proxy[0]) {
		result[0] = 0;
		return;
	}

	url = stracpy(cast_uchar "proxy://");
	add_to_strn(&url, proxy);
	add_to_strn(&url, cast_uchar "/");

	res = display_url(term, url, 0);

	sl = (int)strlen(cast_const_char res);
	if (sl < 9 || strncmp(cast_const_char res, "proxy://", 8) || res[sl - 1] != '/') {
		result[0] = 0;
	} else {
		res[sl - 1] = 0;
		safe_strncpy(result, res + 8, MAX_STR_LEN);
	}

	mem_free(url);
	mem_free(res);
}

static void display_noproxy_list(struct terminal *term, unsigned char *result, unsigned char *noproxy_list)
{
	unsigned char *res;
	res = display_host_list(term, noproxy_list);
	if (!res) {
		result[0] = 0;
	} else {
		safe_strncpy(result, res, MAX_STR_LEN);
	}
	mem_free(res);
}

int save_proxy(int charset, unsigned char *result, unsigned char *proxy)
{
	unsigned char *url, *res;
	int sl;
	int retval;

	if (!proxy[0]) {
		result[0] = 0;
		return 0;
	}

	proxy = convert(charset, utf8_table, proxy, NULL);

	url = stracpy(cast_uchar "proxy://");
	add_to_strn(&url, proxy);
	add_to_strn(&url, cast_uchar "/");

	mem_free(proxy);

	if (parse_url(url, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL)) {
		mem_free(url);
		result[0] = 0;
		return -1;
	}

	res = idn_encode_url(url, 0);

	mem_free(url);

	if (!res) {
		result[0] = 0;
		return -1;
	}
	sl = (int)strlen(cast_const_char res);
	if (sl < 9 || strncmp(cast_const_char res, "proxy://", 8) || res[sl - 1] != '/') {
		result[0] = 0;
		retval = -1;
	} else {
		res[sl - 1] = 0;
		safe_strncpy(result, res + 8, MAX_STR_LEN);
		retval = strlen(cast_const_char (res + 8)) >= MAX_STR_LEN ? -1 : 0;
	}

	mem_free(res);

	return retval;
}

int save_noproxy_list(int charset, unsigned char *result, unsigned char *noproxy_list)
{
	unsigned char *res;

	noproxy_list = convert(charset, utf8_table, noproxy_list, NULL);
	res = idn_encode_host(noproxy_list, (int)strlen(cast_const_char noproxy_list), cast_uchar ".,", 0);
	mem_free(noproxy_list);
	if (!res) {
		result[0] = 0;
		return -1;
	} else {
		safe_strncpy(result, res, MAX_STR_LEN);
		retval = strlen(cast_const_char res) >= MAX_STR_LEN ? -1 : 0;
	}
	mem_free(res);
	return retval;
}

static int check_proxy_noproxy(struct dialog_data *dlg, struct dialog_item_data *di, int (*save)(int, unsigned char *, unsigned char *))
{
	unsigned char *result = mem_alloc(MAX_STR_LEN);
	if (save(term_charset(dlg->win->term), result, di->cdata)) {
		mem_free(result);
		msg_box(dlg->win->term, NULL, TEXT_(T_BAD_STRING), AL_CENTER, TEXT_(T_BAD_PROXY_SYNTAX), MSG_BOX_END, NULL, 1, TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC);
		return 1;
	}
	mem_free(result);
	return 0;
}

static int check_proxy(struct dialog_data *dlg, struct dialog_item_data *di)
{
	return check_proxy_noproxy(dlg, di, save_proxy);
}

static int check_noproxy_list(struct dialog_data *dlg, struct dialog_item_data *di)
{
	return check_proxy_noproxy(dlg, di, save_noproxy_list);
}

static int proxy_ok_dialog(struct dialog_data *dlg, struct dialog_item_data *di)
{
	struct terminal *term = dlg->win->term;
	int charset = term_charset(term);
	int op = proxies.only_proxies;
	int r = ok_dialog(dlg, di);
	if (r) return r;
	save_proxy(charset, proxies.http_proxy, http_proxy);
	save_proxy(charset, proxies.ftp_proxy, ftp_proxy);
	save_proxy(charset, proxies.https_proxy, https_proxy);
	save_proxy(charset, proxies.socks_proxy, socks_proxy);
	save_noproxy_list(charset, proxies.no_proxy, no_proxy);

	if (!proxies.only_proxies) {
		/* parsing duplicated in make_connection */
		long lp;
		char *end;
		unsigned char *p = cast_uchar strchr(cast_const_char proxies.socks_proxy, '@');
		if (!p) p = proxies.socks_proxy;
		else p++;
		p = cast_uchar strchr(cast_const_char p, ':');
		if (p) {
			p++;
			lp = strtol(cast_const_char p, &end, 10);
			if (!*end && lp == 9050) {
				proxies.only_proxies = 1;
				msg_box(term, NULL, TEXT_(T_PROXIES), AL_LEFT, TEXT_(T_TOR_MODE_ENABLED), MSG_BOX_END, NULL, 1, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC);
			}
		}
	}

	if (op != proxies.only_proxies) {
		data_cleanup();
	}
	refresh_network(NULL);
	return 0;
}

static void dlg_proxy_options(struct terminal *term, void *xxx, void *yyy)
{
	struct dialog *d;
	int a = 0;
	display_proxy(term, http_proxy, proxies.http_proxy);
	display_proxy(term, ftp_proxy, proxies.ftp_proxy);
	display_proxy(term, https_proxy, proxies.https_proxy);
	display_proxy(term, socks_proxy, proxies.socks_proxy);
	display_noproxy_list(term, no_proxy, proxies.no_proxy);
	d = mem_calloc(sizeof(struct dialog) + (N_N + 3) * sizeof(struct dialog_item));
	d->title = TEXT_(T_PROXIES);
	d->fn = proxy_fn;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = http_proxy;
	d->items[a].fn = check_proxy;
	a++;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = ftp_proxy;
	d->items[a].fn = check_proxy;
	a++;
#ifdef HAVE_SSL
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = https_proxy;
	d->items[a].fn = check_proxy;
	a++;
#endif
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = socks_proxy;
	d->items[a].fn = check_proxy;
	a++;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = proxies.dns_append;
	a++;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = no_proxy;
	d->items[a].fn = check_noproxy_list;
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].data = (unsigned char *)&proxies.only_proxies;
	d->items[a].dlen = sizeof(int);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = proxy_ok_dialog;
	d->items[a].text = TEXT_(T_OK);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a].text = TEXT_(T_CANCEL);
	a++;
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#undef N_N

#ifdef HAVE_SSL_CERTIFICATES

static int check_file(struct dialog_data *dlg, struct dialog_item_data *di, int type)
{
	unsigned char *p = di->cdata;
	int r;
	struct stat st;
	links_ssl *ssl;
	if (!p[0]) return 0;
	EINTRLOOP(r, stat(cast_const_char p, &st));
	if (r || !S_ISREG(st.st_mode)) {
		msg_box(dlg->win->term, NULL, TEXT_(T_BAD_FILE), AL_CENTER, TEXT_(T_THE_FILE_DOES_NOT_EXIST), MSG_BOX_END, NULL, 1, TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC);
		return 1;
	}
	ssl = getSSL();
	if (!ssl)
		return 0;
#if !defined(OPENSSL_NO_STDIO)
	if (!type) {
		ssl_asked_for_password = 0;
		r = SSL_use_PrivateKey_file(ssl->ssl, cast_const_char p, SSL_FILETYPE_PEM);
		if (!r && ssl_asked_for_password) r = 1;
		r = r != 1;
	} else {
		r = SSL_use_certificate_file(ssl->ssl, cast_const_char p, SSL_FILETYPE_PEM);
		r = r != 1;
	}
#else
	r = 0;
#endif
	if (r)
		msg_box(dlg->win->term, NULL, TEXT_(T_BAD_FILE), AL_CENTER, TEXT_(T_THE_FILE_HAS_INVALID_FORMAT), MSG_BOX_END, NULL, 1, TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC);
	freeSSL(ssl);
	return r;
}

static int check_file_key(struct dialog_data *dlg, struct dialog_item_data *di)
{
	return check_file(dlg, di, 0);
}

static int check_file_crt(struct dialog_data *dlg, struct dialog_item_data *di)
{
	return check_file(dlg, di, 1);
}

static unsigned char * const ssl_labels[] = {
	TEXT_(T_ACCEPT_INVALID_CERTIFICATES),
	TEXT_(T_WARN_ON_INVALID_CERTIFICATES),
	TEXT_(T_REJECT_INVALID_CERTIFICATES),
#ifdef HAVE_BUILTIN_SSL_CERTIFICATES
	TEXT_(T_USE_BUILT_IN_CERTIFICATES),
#endif
	TEXT_(T_CLIENT_CERTIFICATE_KEY_FILE),
	TEXT_(T_CLIENT_CERTIFICATE_FILE),
	TEXT_(T_CLIENT_CERTIFICATE_KEY_PASSWORD),
	NULL
};

static void ssl_options_fn(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	int max = 0, min = 0;
	int w, rw;
	int y = 0;
	checkboxes_width(term, dlg->dlg->udata, dlg->n - 4, &max, max_text_width);
	checkboxes_width(term, dlg->dlg->udata, dlg->n - 4, &min, min_text_width);
	max_text_width(term, ssl_labels[dlg->n - 4], &max, AL_LEFT);
	min_text_width(term, ssl_labels[dlg->n - 4], &min, AL_LEFT);
	max_text_width(term, ssl_labels[dlg->n - 3], &max, AL_LEFT);
	min_text_width(term, ssl_labels[dlg->n - 3], &min, AL_LEFT);
	max_buttons_width(term, dlg->items + dlg->n - 2, 2, &max);
	min_buttons_width(term, dlg->items + dlg->n - 2, 2, &min);
	w = term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w > max) w = max;
	if (w < min) w = min;
	if (w > term->x - 2 * DIALOG_LB) w = term->x - 2 * DIALOG_LB;
	if (w < 5) w = 5;
	rw = 0;
	dlg_format_checkboxes(dlg, NULL, dlg->items, dlg->n - 5, 0, &y, w, &rw, dlg->dlg->udata);
	y += LL;
	dlg_format_text_and_field(dlg, NULL, ssl_labels[dlg->n - 5], dlg->items + dlg->n - 5, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	if (!dlg->win->term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, NULL, ssl_labels[dlg->n - 4], dlg->items + dlg->n - 4, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	if (!dlg->win->term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, NULL, ssl_labels[dlg->n - 3], dlg->items + dlg->n - 3, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	y += LL;
	dlg_format_buttons(dlg, NULL, dlg->items + dlg->n - 2, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = rw + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB + LL;
	dlg_format_checkboxes(dlg, term, dlg->items, dlg->n - 5, dlg->x + DIALOG_LB, &y, w, NULL, dlg->dlg->udata);
	y += LL;
	dlg_format_text_and_field(dlg, term, ssl_labels[dlg->n - 5], dlg->items + dlg->n - 5, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	if (!dlg->win->term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, term, ssl_labels[dlg->n - 4], dlg->items + dlg->n - 4, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	if (!dlg->win->term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, term, ssl_labels[dlg->n - 3], dlg->items + dlg->n - 3, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	y += LL;
	dlg_format_buttons(dlg, term, dlg->items + dlg->n - 2, 2, dlg->x + DIALOG_LB, &y, w, &rw, AL_CENTER);
}

static void dlg_ssl_options(struct terminal *term, void *xxx, void *yyy)
{
	struct dialog *d;
	int a = 0;
	const int items = 8
#ifdef HAVE_BUILTIN_SSL_CERTIFICATES
		+ 1
#endif
		;
	d = mem_calloc(sizeof(struct dialog) + items * sizeof(struct dialog_item));
	d->title = TEXT_(T_SSL_OPTIONS);
	d->fn = ssl_options_fn;
	d->udata = (void *)ssl_labels;
	d->refresh = refresh_network;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = SSL_ACCEPT_INVALID_CERTIFICATE;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&ssl_options.certificates;
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = SSL_WARN_ON_INVALID_CERTIFICATE;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&ssl_options.certificates;
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 1;
	d->items[a].gnum = SSL_REJECT_INVALID_CERTIFICATE;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&ssl_options.certificates;
	a++;
#ifdef HAVE_BUILTIN_SSL_CERTIFICATES
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&ssl_options.built_in_certificates;
	a++;
#endif
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = ssl_options.client_cert_key;
	d->items[a].fn = check_file_key;
	a++;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = ssl_options.client_cert_crt;
	d->items[a].fn = check_file_crt;
	a++;
	d->items[a].type = D_FIELD_PASS;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = ssl_options.client_cert_password;
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a].text = TEXT_(T_OK);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a].text = TEXT_(T_CANCEL);
	a++;
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#endif

static unsigned char * const dns_msg[] = {
#ifndef NO_ASYNC_LOOKUP
	TEXT_(T_ASYNC_DNS_LOOKUP),
#endif
	TEXT_(T_DNS_OVER_HTTPS_URL),
};

static void dns_options_fn(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	int max = 0, min = 0;
	int w, rw;
	int i;
	int y = 0;
	i = 0;
#ifndef NO_ASYNC_LOOKUP
	max_group_width(term, dns_msg + i, dlg->items, 1, &max);
	min_group_width(term, dns_msg + i, dlg->items, 1, &min);
	i++;
#endif
	for (; i < dlg->n - 2; i++) {
		max_text_width(term, dns_msg[i], &max, AL_LEFT);
		min_text_width(term, dns_msg[i], &min, AL_LEFT);
	}
	max_buttons_width(term, dlg->items + dlg->n - 2, 2, &max);
	min_buttons_width(term, dlg->items + dlg->n - 2, 2, &min);
	w = dlg->win->term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w > max) w = max;
	if (w < min) w = min;
	if (w > dlg->win->term->x - 2 * DIALOG_LB) w = dlg->win->term->x - 2 * DIALOG_LB;
	if (w < 1) w = 1;
	rw = 0;
	i = 0;
#ifndef NO_ASYNC_LOOKUP
	dlg_format_group(dlg, NULL, dns_msg, dlg->items, 1, 0, &y, w, &rw);
	y += LL;
	i++;
#else
	if (!dlg->win->term->spec->braille) y -= LL;
#endif
	for (; i < dlg->n - 2; i++) {
		dlg_format_text_and_field(dlg, NULL, dns_msg[i], &dlg->items[i], 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
		if (!dlg->win->term->spec->braille) y += LL;
	}
	dlg_format_buttons(dlg, NULL, dlg->items + dlg->n - 2, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = w + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB;
	y += LL;
	i = 0;
#ifndef NO_ASYNC_LOOKUP
	dlg_format_group(dlg, term, dns_msg, dlg->items, 1, dlg->x + DIALOG_LB, &y, w, &rw);
	y += LL;
	i++;
#else
	if (!dlg->win->term->spec->braille) y -= LL;
#endif
	for (; i < dlg->n - 2; i++) {
		dlg_format_text_and_field(dlg, term, dns_msg[i], &dlg->items[i], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
		y += LL;
	}
	dlg_format_buttons(dlg, term, &dlg->items[dlg->n - 2], 2, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
}

static void dlg_dns_options(struct terminal *term, void *xxx, void *yyy)
{
	struct dialog *d;
	int a = 0;
	d = mem_calloc(sizeof(struct dialog) + 5 * sizeof(struct dialog_item));
	d->title = TEXT_(T_DNS_OPTIONS);
	d->fn = dns_options_fn;
	d->refresh = refresh_network;
#ifndef NO_ASYNC_LOOKUP
	d->items[a].type = D_CHECKBOX;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (unsigned char *)&async_lookup;
	a++;
#endif
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = dns_over_https;
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a].text = TEXT_(T_OK);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a].text = TEXT_(T_CANCEL);
	a++;
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

static unsigned char * const http_labels[] = { TEXT_(T_USE_HTTP_10), TEXT_(T_ALLOW_SERVER_BLACKLIST), TEXT_(T_DO_NOT_SEND_ACCEPT_CHARSET),
#ifdef HAVE_ANY_COMPRESSION
	TEXT_(T_DO_NOT_ADVERTISE_COMPRESSION_SUPPORT),
#endif
	TEXT_(T_RETRY_ON_INTERNAL_ERRORS), NULL };

static unsigned char * const http_header_labels[] = { TEXT_(T_FAKE_FIREFOX), TEXT_(T_DO_NOT_TRACK), TEXT_(T_REFERER_NONE), TEXT_(T_REFERER_SAME_URL), TEXT_(T_REFERER_FAKE), TEXT_(T_REFERER_REAL_SAME_SERVER), TEXT_(T_REFERER_REAL), TEXT_(T_FAKE_REFERER), TEXT_(T_FAKE_USERAGENT), TEXT_(T_EXTRA_HEADER), NULL };

static void httpheadopt_fn(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	int max = 0, min = 0;
	int w, rw;
	int y = 0;
	checkboxes_width(term, dlg->dlg->udata, dlg->n - 5, &max, max_text_width);
	checkboxes_width(term, dlg->dlg->udata, dlg->n - 5, &min, min_text_width);
	max_text_width(term, http_header_labels[dlg->n - 5], &max, AL_LEFT);
	min_text_width(term, http_header_labels[dlg->n - 5], &min, AL_LEFT);
	max_text_width(term, http_header_labels[dlg->n - 4], &max, AL_LEFT);
	min_text_width(term, http_header_labels[dlg->n - 4], &min, AL_LEFT);
	max_text_width(term, http_header_labels[dlg->n - 3], &max, AL_LEFT);
	min_text_width(term, http_header_labels[dlg->n - 3], &min, AL_LEFT);
	max_buttons_width(term, dlg->items + dlg->n - 2, 2, &max);
	min_buttons_width(term, dlg->items + dlg->n - 2, 2, &min);
	w = term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w > max) w = max;
	if (w < min) w = min;
	if (w > term->x - 2 * DIALOG_LB) w = term->x - 2 * DIALOG_LB;
	if (w < 5) w = 5;
	rw = 0;
	dlg_format_checkboxes(dlg, NULL, dlg->items, dlg->n - 5, 0, &y, w, &rw, dlg->dlg->udata);
	y += LL;
	dlg_format_text_and_field(dlg, NULL, http_header_labels[dlg->n - 5], dlg->items + dlg->n - 5, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	if (!dlg->win->term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, NULL, http_header_labels[dlg->n - 4], dlg->items + dlg->n - 4, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	if (!dlg->win->term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, NULL, http_header_labels[dlg->n - 3], dlg->items + dlg->n - 3, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	y += LL;
	dlg_format_buttons(dlg, NULL, dlg->items + dlg->n - 2, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = rw + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB + LL;
	dlg_format_checkboxes(dlg, term, dlg->items, dlg->n - 5, dlg->x + DIALOG_LB, &y, w, NULL, dlg->dlg->udata);
	y += LL;
	dlg_format_text_and_field(dlg, term, http_header_labels[dlg->n - 5], dlg->items + dlg->n - 5, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	if (!dlg->win->term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, term, http_header_labels[dlg->n - 4], dlg->items + dlg->n - 4, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	if (!dlg->win->term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, term, http_header_labels[dlg->n - 3], dlg->items + dlg->n - 3, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	y += LL;
	dlg_format_buttons(dlg, term, dlg->items + dlg->n - 2, 2, dlg->x + DIALOG_LB, &y, w, &rw, AL_CENTER);
}

static int dlg_http_header_options(struct dialog_data *dlg, struct dialog_item_data *di)
{
	struct http_header_options *header = (struct http_header_options *)di->cdata;
	struct dialog *d;
	d = mem_calloc(sizeof(struct dialog) + 12 * sizeof(struct dialog_item));
	d->title = TEXT_(T_HTTP_HEADER_OPTIONS);
	d->fn = httpheadopt_fn;
	d->udata = (void *)http_header_labels;
	d->items[0].type = D_CHECKBOX;
	d->items[0].gid = 0;
	d->items[0].dlen = sizeof(int);
	d->items[0].data = (void *)&header->fake_firefox;
	d->items[1].type = D_CHECKBOX;
	d->items[1].gid = 0;
	d->items[1].dlen = sizeof(int);
	d->items[1].data = (void *)&header->do_not_track;
	d->items[2].type = D_CHECKBOX;
	d->items[2].gid = 1;
	d->items[2].gnum = REFERER_NONE;
	d->items[2].dlen = sizeof(int);
	d->items[2].data = (void *)&header->referer;
	d->items[3].type = D_CHECKBOX;
	d->items[3].gid = 1;
	d->items[3].gnum = REFERER_SAME_URL;
	d->items[3].dlen = sizeof(int);
	d->items[3].data = (void *)&header->referer;
	d->items[4].type = D_CHECKBOX;
	d->items[4].gid = 1;
	d->items[4].gnum = REFERER_FAKE;
	d->items[4].dlen = sizeof(int);
	d->items[4].data = (void *)&header->referer;
	d->items[5].type = D_CHECKBOX;
	d->items[5].gid = 1;
	d->items[5].gnum = REFERER_REAL_SAME_SERVER;
	d->items[5].dlen = sizeof(int);
	d->items[5].data = (void *)&header->referer;
	d->items[6].type = D_CHECKBOX;
	d->items[6].gid = 1;
	d->items[6].gnum = REFERER_REAL;
	d->items[6].dlen = sizeof(int);
	d->items[6].data = (void *)&header->referer;

	d->items[7].type = D_FIELD;
	d->items[7].dlen = MAX_STR_LEN;
	d->items[7].data = header->fake_referer;
	d->items[8].type = D_FIELD;
	d->items[8].dlen = MAX_STR_LEN;
	d->items[8].data = header->fake_useragent;
	d->items[9].type = D_FIELD;
	d->items[9].dlen = MAX_STR_LEN;
	d->items[9].data = header->extra_header;
	d->items[10].type = D_BUTTON;
	d->items[10].gid = B_ENTER;
	d->items[10].fn = ok_dialog;
	d->items[10].text = TEXT_(T_OK);
	d->items[11].type = D_BUTTON;
	d->items[11].gid = B_ESC;
	d->items[11].fn = cancel_dialog;
	d->items[11].text = TEXT_(T_CANCEL);
	d->items[12].type = D_END;
	do_dialog(dlg->win->term, d, getml(d, NULL));
	return 0;
}

static void dlg_http_options(struct terminal *term, void *xxx, void *yyy)
{
	struct dialog *d;
	int a = 0;
	d = mem_calloc(sizeof(struct dialog) + 8 * sizeof(struct dialog_item));
	d->title = TEXT_(T_HTTP_BUG_WORKAROUNDS);
	d->fn = checkbox_list_fn;
	d->udata = (void *)http_labels;
	d->refresh = refresh_network;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&http_options.http10;
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&http_options.allow_blacklist;
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&http_options.no_accept_charset;
	a++;
#ifdef HAVE_ANY_COMPRESSION
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&http_options.no_compression;
	a++;
#endif
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&http_options.retry_internal_errors;
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].fn = dlg_http_header_options;
	d->items[a].text = TEXT_(T_HEADER_OPTIONS);
	d->items[a].data = (void *)&http_options.header;
	d->items[a].dlen = sizeof(struct http_header_options);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a].text = TEXT_(T_OK);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a].text = TEXT_(T_CANCEL);
	a++;
	d->items[a].type = D_END;
	a++;
	do_dialog(term, d, getml(d, NULL));
}

static unsigned char * const ftp_texts[] = { TEXT_(T_PASSWORD_FOR_ANONYMOUS_LOGIN), TEXT_(T_USE_PASSIVE_FTP), TEXT_(T_USE_EPRT_EPSV), TEXT_(T_SET_TYPE_OF_SERVICE), NULL };

static void ftpopt_fn(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	int max = 0, min = 0;
	int w, rw;
	int y = 0;
	if (dlg->win->term->spec->braille) y += LL;
	max_text_width(term, ftp_texts[0], &max, AL_LEFT);
	min_text_width(term, ftp_texts[0], &min, AL_LEFT);
	checkboxes_width(term, ftp_texts + 1, dlg->n - 3, &max, max_text_width);
	checkboxes_width(term, ftp_texts + 1, dlg->n - 3, &min, min_text_width);
	max_buttons_width(term, dlg->items + dlg->n - 2, 2, &max);
	min_buttons_width(term, dlg->items + dlg->n - 2, 2, &min);
	w = term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w > max) w = max;
	if (w < min) w = min;
	if (w > term->x - 2 * DIALOG_LB) w = term->x - 2 * DIALOG_LB;
	if (w < 5) w = 5;
	rw = 0;
	dlg_format_text_and_field(dlg, NULL, ftp_texts[0], dlg->items, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	dlg_format_checkboxes(dlg, NULL, dlg->items + 1, dlg->n - 3, 0, &y, w, &rw, ftp_texts + 1);
	y += LL;
	dlg_format_buttons(dlg, NULL, dlg->items + dlg->n - 2, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = rw + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB;
	if (dlg->win->term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, term, ftp_texts[0], dlg->items, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	y += LL;
	dlg_format_checkboxes(dlg, term, dlg->items + 1, dlg->n - 3, dlg->x + DIALOG_LB, &y, w, NULL, ftp_texts + 1);
	y += LL;
	dlg_format_buttons(dlg, term, dlg->items + dlg->n - 2, 2, dlg->x + DIALOG_LB, &y, w, &rw, AL_CENTER);
}

static void dlg_ftp_options(struct terminal *term, void *xxx, void *yyy)
{
	int a;
	struct dialog *d;
	d = mem_calloc(sizeof(struct dialog) + 6 * sizeof(struct dialog_item));
	d->title = TEXT_(T_FTP_OPTIONS);
	d->fn = ftpopt_fn;
	d->refresh = refresh_network;
	a=0;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a].data = ftp_options.anon_pass;
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void*)&ftp_options.passive_ftp;
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void*)&ftp_options.eprt_epsv;
	a++;
#ifdef HAVE_IPTOS
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void*)&ftp_options.set_tos;
	a++;
#endif
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a].text = TEXT_(T_OK);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a].text = TEXT_(T_CANCEL);
	a++;
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#ifndef DISABLE_SMB

static unsigned char * const smb_labels[] = { TEXT_(T_ALLOW_HYPERLINKS_TO_SMB), NULL };

static void dlg_smb_options(struct terminal *term, void *xxx, void *yyy)
{
	int a;
	struct dialog *d;
	d = mem_calloc(sizeof(struct dialog) + 3 * sizeof(struct dialog_item));
	d->title = TEXT_(T_SMB_OPTIONS);
	d->fn = checkbox_list_fn;
	d->udata = (void *)smb_labels;
	a=0;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void*)&smb_options.allow_hyperlinks_to_smb;
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a].text = TEXT_(T_OK);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a].text = TEXT_(T_CANCEL);
	a++;
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#endif

static unsigned char * const prg_msg[] = {
	TEXT_(T_MAILTO_PROG),
	TEXT_(T_TELNET_PROG),
	TEXT_(T_TN3270_PROG),
	TEXT_(T_MMS_PROG),
	TEXT_(T_MAGNET_PROG),
	TEXT_(T_GOPHER_PROG),
	TEXT_(T_SHELL_PROG),
	cast_uchar ""
};

static void netprog_fn(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	int max = 0, min = 0;
	int w, rw;
	int y = gf_val(-1, -G_BFU_FONT_SIZE);
	int a;
	a=0;
	max_text_width(term, prg_msg[a], &max, AL_LEFT);
	min_text_width(term, prg_msg[a++], &min, AL_LEFT);
	max_text_width(term, prg_msg[a], &max, AL_LEFT);
	min_text_width(term, prg_msg[a++], &min, AL_LEFT);
	max_text_width(term, prg_msg[a], &max, AL_LEFT);
	min_text_width(term, prg_msg[a++], &min, AL_LEFT);
	max_text_width(term, prg_msg[a], &max, AL_LEFT);
	min_text_width(term, prg_msg[a++], &min, AL_LEFT);
	max_text_width(term, prg_msg[a], &max, AL_LEFT);
	min_text_width(term, prg_msg[a++], &min, AL_LEFT);
	max_text_width(term, prg_msg[a], &max, AL_LEFT);
	min_text_width(term, prg_msg[a++], &min, AL_LEFT);
#ifdef G
	if (have_extra_exec()) {
		max_text_width(term, prg_msg[a], &max, AL_LEFT);
		min_text_width(term, prg_msg[a++], &min, AL_LEFT);
	}
#endif
	max_buttons_width(term, dlg->items + a, 2, &max);
	min_buttons_width(term, dlg->items + a, 2, &min);
	w = dlg->win->term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w > max) w = max;
	if (w < min) w = min;
	if (w > dlg->win->term->x - 2 * DIALOG_LB) w = dlg->win->term->x - 2 * DIALOG_LB;
	if (w < 1) w = 1;
	rw = 0;
	a=0;
	if (term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, NULL, prg_msg[a], &dlg->items[a], 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, NULL, prg_msg[a], &dlg->items[a], 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, NULL, prg_msg[a], &dlg->items[a], 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, NULL, prg_msg[a], &dlg->items[a], 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, NULL, prg_msg[a], &dlg->items[a], 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, NULL, prg_msg[a], &dlg->items[a], 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
#ifdef G
	if (have_extra_exec()) {
		dlg_format_text_and_field(dlg, NULL, prg_msg[a], &dlg->items[a], 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
		a++;
		if (!term->spec->braille) y += LL;
	}
#endif
	if (term->spec->braille) y += LL;
	dlg_format_buttons(dlg, NULL, dlg->items + a, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = w + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB;
	if (term->spec->braille) y += LL;
	a=0;
	dlg_format_text_and_field(dlg, term, prg_msg[a], &dlg->items[a], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, term, prg_msg[a], &dlg->items[a], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, term, prg_msg[a], &dlg->items[a], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, term, prg_msg[a], &dlg->items[a], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, term, prg_msg[a], &dlg->items[a], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
	dlg_format_text_and_field(dlg, term, prg_msg[a], &dlg->items[a], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	a++;
	if (!term->spec->braille) y += LL;
#ifdef G
	if (have_extra_exec()) {
		dlg_format_text_and_field(dlg, term, prg_msg[a], &dlg->items[a], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
		a++;
		if (!term->spec->braille) y += LL;
	}
#endif
	if (term->spec->braille) y += LL;
	dlg_format_buttons(dlg, term, &dlg->items[a], 2, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
}

static void net_programs(struct terminal *term, void *xxx, void *yyy)
{
	struct dialog *d;
	int a;
	d = mem_calloc(sizeof(struct dialog) + 9 * sizeof(struct dialog_item));
#ifdef G
	if (have_extra_exec()) d->title = TEXT_(T_MAIL_TELNET_AND_SHELL_PROGRAMS);
	else
#endif
		d->title = TEXT_(T_MAIL_AND_TELNET_PROGRAMS);

	d->fn = netprog_fn;
	a=0;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a++].data = get_prog(&mailto_prog);
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a++].data = get_prog(&telnet_prog);
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a++].data = get_prog(&tn3270_prog);
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a++].data = get_prog(&mms_prog);
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a++].data = get_prog(&magnet_prog);
	d->items[a].type = D_FIELD;
	d->items[a].dlen = MAX_STR_LEN;
	d->items[a++].data = get_prog(&gopher_prog);
#ifdef G
	if (have_extra_exec()) {
		d->items[a].type = D_FIELD;
		d->items[a].dlen = MAX_STR_LEN;
		d->items[a++].data = drv->param->shell_term;
	}
#endif
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a++].text = TEXT_(T_OK);
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a++].text = TEXT_(T_CANCEL);
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

static unsigned char mc_str[8];
#ifdef G
static unsigned char ic_str[8];
static unsigned char fc_str[8];
#endif
static unsigned char doc_str[4];

static void cache_refresh(void *xxx)
{
	memory_cache_size = atoi(cast_const_char mc_str) * 1024;
#ifdef G
	if (F) {
		image_cache_size = atoi(cast_const_char ic_str) * 1024;
		font_cache_size = atoi(cast_const_char fc_str) * 1024;
	}
#endif
	max_format_cache_entries = atoi(cast_const_char doc_str);
	shrink_memory(SH_CHECK_QUOTA, 0);
}

static unsigned char * const cache_texts[] = { TEXT_(T_MEMORY_CACHE_SIZE__KB), TEXT_(T_NUMBER_OF_FORMATTED_DOCUMENTS), TEXT_(T_AGGRESSIVE_CACHE) };
#ifdef G
static unsigned char * const g_cache_texts[] = { TEXT_(T_MEMORY_CACHE_SIZE__KB), TEXT_(T_IMAGE_CACHE_SIZE__KB), TEXT_(T_FONT_CACHE_SIZE__KB), TEXT_(T_NUMBER_OF_FORMATTED_DOCUMENTS), TEXT_(T_AGGRESSIVE_CACHE) };
#endif

static void cache_opt(struct terminal *term, void *xxx, void *yyy)
{
	struct dialog *d;
	int a;
	snprint(mc_str, 8, memory_cache_size / 1024);
#ifdef G
	if (F) {
		snprint(ic_str, 8, image_cache_size / 1024);
		snprint(fc_str, 8, font_cache_size / 1024);
	}
#endif
	snprint(doc_str, 4, max_format_cache_entries);
#ifdef G
	if (F) {
		d = mem_calloc(sizeof(struct dialog) + 7 * sizeof(struct dialog_item));
	} else
#endif
	{
		d = mem_calloc(sizeof(struct dialog) + 5 * sizeof(struct dialog_item));
	}
	a=0;
	d->title = TEXT_(T_CACHE_OPTIONS);
	d->fn = group_fn;
#ifdef G
	if (F) d->udata = (void *)g_cache_texts;
	else
#endif
	d->udata = (void *)cache_texts;
	d->refresh = cache_refresh;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = 8;
	d->items[a].data = mc_str;
	d->items[a].fn = check_number;
	d->items[a].gid = 0;
	d->items[a].gnum = MAXINT / 1024;
	a++;
#ifdef G
	if (F)
	{
		d->items[a].type = D_FIELD;
		d->items[a].dlen = 8;
		d->items[a].data = ic_str;
		d->items[a].fn = check_number;
		d->items[a].gid = 0;
		d->items[a].gnum = MAXINT / 1024;
		a++;
		d->items[a].type = D_FIELD;
		d->items[a].dlen = 8;
		d->items[a].data = fc_str;
		d->items[a].fn = check_number;
		d->items[a].gid = 0;
		d->items[a].gnum = MAXINT / 1024;
		a++;
	}
#endif
	d->items[a].type = D_FIELD;
	d->items[a].dlen = 4;
	d->items[a].data = doc_str;
	d->items[a].fn = check_number;
	d->items[a].gid = 0;
	d->items[a].gnum = 999;
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&aggressive_cache;
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a].text = TEXT_(T_OK);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a].text = TEXT_(T_CANCEL);
	a++;
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

static void menu_shell(struct terminal *term, void *xxx, void *yyy)
{
	unsigned char *sh;
	if (!(sh = cast_uchar GETSHELL)) sh = cast_uchar DEFAULT_SHELL;
	exec_on_terminal(term, sh, cast_uchar "", 1);
}

static void menu_kill_background_connections(struct terminal *term, void *xxx, void *yyy)
{
	abort_background_connections();
}

static void menu_kill_all_connections(struct terminal *term, void *xxx, void *yyy)
{
	abort_all_connections();
}

static void menu_save_html_options(struct terminal *term, void *xxx, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	memcpy(&dds, &ses->ds, sizeof(struct document_setup));
	write_html_config(term);
}

static unsigned char marg_str[2];
#ifdef G
static unsigned char html_font_str[4];
static unsigned char image_scale_str[6];
#endif

static void session_refresh(struct session *ses)
{
	html_interpret_recursive(ses->screen);
	draw_formatted(ses);
}

static void html_refresh(void *ses_)
{
	struct session *ses = (struct session *)ses_;
	ses->ds.margin = atoi(cast_const_char marg_str);
#ifdef G
	if (F) {
		ses->ds.font_size = atoi(cast_const_char html_font_str);
		ses->ds.image_scale = atoi(cast_const_char image_scale_str);
	}
#endif
	session_refresh(ses);
}

#ifdef G
static unsigned char * const html_texts_g[] = {
	TEXT_(T_DISPLAY_TABLES),
	TEXT_(T_DISPLAY_FRAMES),
	TEXT_(T_BREAK_LONG_LINES),
	TEXT_(T_DISPLAY_LINKS_TO_IMAGES),
	TEXT_(T_DISPLAY_IMAGE_FILENAMES),
	TEXT_(T_DISPLAY_IMAGES),
	TEXT_(T_AUTO_REFRESH),
	TEXT_(T_TARGET_IN_NEW_WINDOW),
	TEXT_(T_TEXT_MARGIN),
	cast_uchar "",
	TEXT_(T_IGNORE_CHARSET_INFO_SENT_BY_SERVER),
	TEXT_(T_USER_FONT_SIZE),
	TEXT_(T_SCALE_ALL_IMAGES_BY),
	TEXT_(T_PORN_ENABLE)
};
#endif

static unsigned char * const html_texts[] = {
	TEXT_(T_DISPLAY_TABLES),
	TEXT_(T_DISPLAY_FRAMES),
	TEXT_(T_BREAK_LONG_LINES),
	TEXT_(T_DISPLAY_LINKS_TO_IMAGES),
	TEXT_(T_DISPLAY_IMAGE_FILENAMES),
	TEXT_(T_LINK_ORDER_BY_COLUMNS),
	TEXT_(T_NUMBERED_LINKS),
	TEXT_(T_AUTO_REFRESH),
	TEXT_(T_TARGET_IN_NEW_WINDOW),
	TEXT_(T_TEXT_MARGIN),
	cast_uchar "",
	TEXT_(T_IGNORE_CHARSET_INFO_SENT_BY_SERVER)
};

static int dlg_assume_cp(struct dialog_data *dlg, struct dialog_item_data *di)
{
	charset_sel_list(dlg->win->term, *(int *)di->cdata, set_val, (void *)di->cdata, 1, 0);
	return 0;
}

#ifdef G
static int dlg_kb_cp(struct dialog_data *dlg, struct dialog_item_data *di)
{
	charset_sel_list(dlg->win->term, *(int *)di->cdata, set_val, (void *)di->cdata,
#ifdef DOS
		0
#else
		1
#endif
		, 1);
	return 0;
}
#endif

void dialog_html_options(struct session *ses)
{
	struct dialog *d;
	int a;

	snprint(marg_str, 2, ses->ds.margin);
	if (!F) {
		d = mem_calloc(sizeof(struct dialog) + 14 * sizeof(struct dialog_item));
#ifdef G
	} else {
		d = mem_calloc(sizeof(struct dialog) + 16 * sizeof(struct dialog_item));
		snprintf(cast_char html_font_str,4,"%d",ses->ds.font_size);
		snprintf(cast_char image_scale_str,6,"%d",ses->ds.image_scale);
#endif
	}
	d->title = TEXT_(T_HTML_OPTIONS);
	d->fn = group_fn;
	d->udata = (void *)gf_val(html_texts, html_texts_g);
	d->udata2 = ses;
	d->refresh = html_refresh;
	d->refresh_data = ses;
	a=0;
	d->items[a].type = D_CHECKBOX;
	d->items[a].data = (unsigned char *) &ses->ds.tables;
	d->items[a].dlen = sizeof(int);
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].data = (unsigned char *) &ses->ds.frames;
	d->items[a].dlen = sizeof(int);
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].data = (unsigned char *) &ses->ds.break_long_lines;
	d->items[a].dlen = sizeof(int);
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].data = (unsigned char *) &ses->ds.images;
	d->items[a].dlen = sizeof(int);
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].data = (unsigned char *) &ses->ds.image_names;
	d->items[a].dlen = sizeof(int);
	a++;
#ifdef G
	if (F) {
		d->items[a].type = D_CHECKBOX;
		d->items[a].data = (unsigned char *) &ses->ds.display_images;
		d->items[a].dlen = sizeof(int);
		a++;
	}
#endif
	if (!F) {
		d->items[a].type = D_CHECKBOX;
		d->items[a].data = (unsigned char *) &ses->ds.table_order;
		d->items[a].dlen = sizeof(int);
		a++;
		d->items[a].type = D_CHECKBOX;
		d->items[a].data = (unsigned char *) &ses->ds.num_links;
		d->items[a].dlen = sizeof(int);
		a++;
	}
	d->items[a].type = D_CHECKBOX;
	d->items[a].data = (unsigned char *) &ses->ds.auto_refresh;
	d->items[a].dlen = sizeof(int);
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].data = (unsigned char *) &ses->ds.target_in_new_window;
	d->items[a].dlen = sizeof(int);
	a++;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = 2;
	d->items[a].data = marg_str;
	d->items[a].fn = check_number;
	d->items[a].gid = 0;
	d->items[a].gnum = 9;
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].fn = dlg_assume_cp;
	d->items[a].text = TEXT_(T_DEFAULT_CODEPAGE);
	d->items[a].data = (unsigned char *) &ses->ds.assume_cp;
	d->items[a].dlen = sizeof(int);
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].data = (unsigned char *) &ses->ds.hard_assume;
	d->items[a].dlen = sizeof(int);
	a++;
#ifdef G
	if (F) {
		d->items[a].type = D_FIELD;
		d->items[a].dlen = 4;
		d->items[a].data = html_font_str;
		d->items[a].fn = check_number;
		d->items[a].gid = 1;
		d->items[a].gnum = MAX_FONT_SIZE;
		a++;
		d->items[a].type = D_FIELD;
		d->items[a].dlen = 4;
		d->items[a].data = image_scale_str;
		d->items[a].fn = check_number;
		d->items[a].gid = 1;
		d->items[a].gnum = 999;
		a++;
		d->items[a].type = D_CHECKBOX;
		d->items[a].data = (unsigned char *) &ses->ds.porn_enable;
		d->items[a].dlen = sizeof(int);
		a++;
	}
#endif
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a].text = TEXT_(T_OK);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a].text = TEXT_(T_CANCEL);
	a++;
	d->items[a].type = D_END;
	do_dialog(ses->term, d, getml(d, NULL));
}

static void menu_html_options(struct terminal *term, void *xxx, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	dialog_html_options(ses);
}

static unsigned char * const color_texts[] = { cast_uchar "", cast_uchar "", cast_uchar "", TEXT_(T_IGNORE_DOCUMENT_COLOR) };

#ifdef G
static unsigned char * const color_texts_g[] = { TEXT_(T_TEXT_COLOR), TEXT_(T_LINK_COLOR), TEXT_(T_BACKGROUND_COLOR), TEXT_(T_IGNORE_DOCUMENT_COLOR) };

static unsigned char g_text_color_str[7];
static unsigned char g_link_color_str[7];
static unsigned char g_background_color_str[7];
#endif

static void html_color_refresh(void *ses_)
{
	struct session *ses = (struct session *)ses_;
#ifdef G
	if (F) {
		ses->ds.g_text_color = (int)strtol(cast_const_char g_text_color_str, NULL, 16);
		ses->ds.g_link_color = (int)strtol(cast_const_char g_link_color_str, NULL, 16);
		ses->ds.g_background_color = (int)strtol(cast_const_char g_background_color_str, NULL, 16);
	}
#endif
	html_interpret_recursive(ses->screen);
	draw_formatted(ses);
}

static void select_color(struct terminal *term, int n, int *ptr)
{
	int i;
	struct menu_item *mi;
	mi = new_menu(MENU_FREE_ITEMS);
	for (i = 0; i < n; i++) {
		add_to_menu(&mi, TEXT_(T_COLOR_0 + i), cast_uchar "", cast_uchar "", set_val, (void *)(unsigned long)i, 0, i);
	}
	do_menu_selected(term, mi, ptr, *ptr, NULL, NULL);
}

static int select_color_8(struct dialog_data *dlg, struct dialog_item_data *di)
{
	select_color(dlg->win->term, 8, (int *)di->cdata);
	return 0;
}

static int select_color_16(struct dialog_data *dlg, struct dialog_item_data *di)
{
	select_color(dlg->win->term, 16, (int *)di->cdata);
	return 0;
}

static void menu_color(struct terminal *term, void *xxx, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	struct dialog *d;

#ifdef G
	if (F) {
		snprintf(cast_char g_text_color_str, 7, "%06x", (unsigned)ses->ds.g_text_color);
		snprintf(cast_char g_link_color_str, 7, "%06x", (unsigned)ses->ds.g_link_color);
		snprintf(cast_char g_background_color_str,7,"%06x", (unsigned)ses->ds.g_background_color);
	}
#endif

	d = mem_calloc(sizeof(struct dialog) + 6 * sizeof(struct dialog_item));
	d->title = TEXT_(T_COLOR);
	d->fn = group_fn;
	d->udata = (void *)gf_val(color_texts, color_texts_g);
	d->udata2 = ses;
	d->refresh = html_color_refresh;
	d->refresh_data = ses;

	if (!F) {
		d->items[0].type = D_BUTTON;
		d->items[0].gid = 0;
		d->items[0].text = TEXT_(T_TEXT_COLOR);
		d->items[0].fn = select_color_16;
		d->items[0].data = (unsigned char *)&ses->ds.t_text_color;
		d->items[0].dlen = sizeof(int);

		d->items[1].type = D_BUTTON;
		d->items[1].gid = 0;
		d->items[1].text = TEXT_(T_LINK_COLOR);
		d->items[1].fn = select_color_16;
		d->items[1].data = (unsigned char *)&ses->ds.t_link_color;
		d->items[1].dlen = sizeof(int);

		d->items[2].type = D_BUTTON;
		d->items[2].gid = 0;
		d->items[2].text = TEXT_(T_BACKGROUND_COLOR);
		d->items[2].fn = select_color_8;
		d->items[2].data = (unsigned char *)&ses->ds.t_background_color;
		d->items[2].dlen = sizeof(int);
	}
#ifdef G
	else {
		d->items[0].type = D_FIELD;
		d->items[0].dlen = 7;
		d->items[0].data = g_text_color_str;
		d->items[0].fn = check_hex_number;
		d->items[0].gid = 0;
		d->items[0].gnum = 0xffffff;

		d->items[1].type = D_FIELD;
		d->items[1].dlen = 7;
		d->items[1].data = g_link_color_str;
		d->items[1].fn = check_hex_number;
		d->items[1].gid = 0;
		d->items[1].gnum = 0xffffff;

		d->items[2].type = D_FIELD;
		d->items[2].dlen = 7;
		d->items[2].data = g_background_color_str;
		d->items[2].fn = check_hex_number;
		d->items[2].gid = 0;
		d->items[2].gnum = 0xffffff;
	}
#endif

	d->items[3].type = D_CHECKBOX;
	d->items[3].data = (unsigned char *) gf_val(&ses->ds.t_ignore_document_color, &ses->ds.g_ignore_document_color);
	d->items[3].dlen = sizeof(int);

	d->items[4].type = D_BUTTON;
	d->items[4].gid = B_ENTER;
	d->items[4].fn = ok_dialog;
	d->items[4].text = TEXT_(T_OK);

	d->items[5].type = D_BUTTON;
	d->items[5].gid = B_ESC;
	d->items[5].fn = cancel_dialog;
	d->items[5].text = TEXT_(T_CANCEL);

	d->items[6].type = D_END;

	do_dialog(term, d, getml(d, NULL));
}

static unsigned char new_bookmarks_file[MAX_STR_LEN];
static int new_bookmarks_codepage;

#ifdef G
static unsigned char menu_font_str[4];
static unsigned char bg_color_str[7];
static unsigned char fg_color_str[7];
static unsigned char scroll_area_color_str[7];
static unsigned char scroll_bar_color_str[7];
static unsigned char scroll_frame_color_str[7];
#endif

static void refresh_misc(void *ses_)
{
	struct session *ses = (struct session *)ses_;
#ifdef G
	if (F) {
		menu_font_size = (int)strtol(cast_const_char menu_font_str, NULL, 10);
		G_BFU_FG_COLOR = (int)strtol(cast_const_char fg_color_str, NULL, 16);
		G_BFU_BG_COLOR = (int)strtol(cast_const_char bg_color_str, NULL, 16);
		G_SCROLL_BAR_AREA_COLOR = (int)strtol(cast_const_char scroll_area_color_str, NULL, 16);
		G_SCROLL_BAR_BAR_COLOR = (int)strtol(cast_const_char scroll_bar_color_str, NULL, 16);
		G_SCROLL_BAR_FRAME_COLOR = (int)strtol(cast_const_char scroll_frame_color_str, NULL, 16);
		shutdown_bfu();
		init_bfu();
		init_grview();
		cls_redraw_all_terminals();
	}
#endif
	if (strcmp(cast_const_char new_bookmarks_file, cast_const_char bookmarks_file) || new_bookmarks_codepage != bookmarks_codepage) {
		reinit_bookmarks(ses, new_bookmarks_file, new_bookmarks_codepage);
	}
}

#ifdef G
static unsigned char * const miscopt_labels_g[] = { TEXT_(T_MENU_FONT_SIZE), TEXT_(T_ENTER_COLORS_AS_RGB_TRIPLETS), TEXT_(T_MENU_FOREGROUND_COLOR), TEXT_(T_MENU_BACKGROUND_COLOR), TEXT_(T_SCROLL_BAR_AREA_COLOR), TEXT_(T_SCROLL_BAR_BAR_COLOR), TEXT_(T_SCROLL_BAR_FRAME_COLOR), TEXT_(T_BOOKMARKS_FILE), NULL };
#endif
static unsigned char * const miscopt_labels[] = { TEXT_(T_BOOKMARKS_FILE), NULL };
static unsigned char * const miscopt_checkbox_labels[] = { TEXT_(T_SAVE_URL_HISTORY_ON_EXIT), NULL };

static void miscopt_fn(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	unsigned char **labels=dlg->dlg->udata;
	int max = 0, min = 0;
	int w, rw;
	int y = 0;
	int a=0;
	int bmk=!anonymous;

#ifdef G
	if (F&&((drv->flags)&GD_NEED_CODEPAGE))a=1;
#endif

	max_text_width(term, labels[F?7:0], &max, AL_LEFT);
	min_text_width(term, labels[F?7:0], &min, AL_LEFT);
#ifdef G
	if (F)
	{
		max_text_width(term, labels[1], &max, AL_LEFT);
		min_text_width(term, labels[1], &min, AL_LEFT);
		max_group_width(term, labels, dlg->items, 1, &max);
		min_group_width(term, labels, dlg->items, 1, &min);
		max_group_width(term, labels + 2, dlg->items+1, 5, &max);
		min_group_width(term, labels + 2, dlg->items+1, 5, &min);
	}
#endif
	if (bmk)
	{
		max_buttons_width(term, dlg->items + dlg->n - 3 - a - bmk, 1, &max);
		min_buttons_width(term, dlg->items + dlg->n - 3 - a - bmk, 1, &min);
	}
	if (a)
	{
		max_buttons_width(term, dlg->items + dlg->n - 3 - bmk, 1, &max);
		min_buttons_width(term, dlg->items + dlg->n - 3 - bmk, 1, &min);
	}
	if (bmk) {
		checkboxes_width(term, miscopt_checkbox_labels, 1, &max, max_text_width);
		checkboxes_width(term, miscopt_checkbox_labels, 1, &min, min_text_width);
	}
	max_buttons_width(term, dlg->items + dlg->n - 2, 2, &max);
	min_buttons_width(term, dlg->items + dlg->n - 2, 2, &min);
	w = term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w > max) w = max;
	if (w < min) w = min;
	if (w > term->x - 2 * DIALOG_LB) w = term->x - 2 * DIALOG_LB;
	if (w < 5) w = 5;
	rw = 0;

#ifdef G
	if (F)
	{
		dlg_format_group(dlg, NULL, labels, dlg->items,1,dlg->x + DIALOG_LB, &y, w, &rw);
		y += LL;
		dlg_format_text(dlg, NULL, labels[1], dlg->x + DIALOG_LB, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
		y += LL;
		dlg_format_group(dlg, NULL, labels+2, dlg->items+1,5,dlg->x + DIALOG_LB, &y, w, &rw);
		y += LL;
	}
#endif
	if (bmk)
	{
		dlg_format_text_and_field(dlg, NULL, labels[F?7:0], dlg->items + dlg->n - 4 - a - bmk, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
		y += LL;
	}
	if (bmk) {
		y += LL;
		dlg_format_buttons(dlg, NULL, dlg->items + dlg->n - 3 - a - bmk, 1, 0, &y, w, &rw, AL_LEFT);
	}
	if (a) dlg_format_buttons(dlg, NULL, dlg->items + dlg->n - 3 - bmk, 1, 0, &y, w, &rw, AL_LEFT);
	if (bmk) dlg_format_checkboxes(dlg, NULL, dlg->items + dlg->n - 3, 1, 0, &y, w, &rw, miscopt_checkbox_labels);
	dlg_format_buttons(dlg, NULL, dlg->items + dlg->n - 2, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = w + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB;
#ifdef G
	if (F)
	{
		y += LL;
		dlg_format_group(dlg, term, labels, dlg->items,1,dlg->x + DIALOG_LB, &y, w, NULL);
		y += LL;
		dlg_format_text(dlg, term, labels[1], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
		y += LL;
		dlg_format_group(dlg, term, labels+2, dlg->items+1,5,dlg->x + DIALOG_LB, &y, w, NULL);
		y += LL;
	} else
#endif
	{
		y += LL;
	}
	if (bmk)
	{
		dlg_format_text_and_field(dlg, term, labels[F?7:0], dlg->items + dlg->n - 4 - a - bmk, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
		y += LL;
		dlg_format_buttons(dlg, term, dlg->items + dlg->n - 3 - a - bmk, 1, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
	}
	if (a) dlg_format_buttons(dlg, term, dlg->items + dlg->n - 3 - bmk, 1, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
	if (bmk) {
		dlg_format_checkboxes(dlg, term, dlg->items + dlg->n - 3, 1, dlg->x + DIALOG_LB, &y, w, NULL, miscopt_checkbox_labels);
		y += LL;
	}
	dlg_format_buttons(dlg, term, dlg->items+dlg->n-2, 2, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
}

static void miscelaneous_options(struct terminal *term, void *xxx, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	struct dialog *d;
	int a=0;

	if (anonymous&&!F) return;	/* if you add something into text mode (or both text and graphics), remove this (and enable also miscelaneous_options in do_setup_menu) */

	safe_strncpy(new_bookmarks_file,bookmarks_file,MAX_STR_LEN);
	new_bookmarks_codepage=bookmarks_codepage;
	if (!F) {
		d = mem_calloc(sizeof(struct dialog) + 5 * sizeof(struct dialog_item));
	}
#ifdef G
	else {
		d = mem_calloc(sizeof(struct dialog) + 12 * sizeof(struct dialog_item));
		snprintf(cast_char menu_font_str,4,"%d",menu_font_size);
		snprintf(cast_char fg_color_str,7,"%06x",(unsigned) G_BFU_FG_COLOR);
		snprintf(cast_char bg_color_str,7,"%06x",(unsigned) G_BFU_BG_COLOR);
		snprintf(cast_char scroll_bar_color_str,7,"%06x",(unsigned) G_SCROLL_BAR_BAR_COLOR);
		snprintf(cast_char scroll_area_color_str,7,"%06x",(unsigned) G_SCROLL_BAR_AREA_COLOR);
		snprintf(cast_char scroll_frame_color_str,7,"%06x",(unsigned) G_SCROLL_BAR_FRAME_COLOR);
	}
#endif
	d->title = TEXT_(T_MISCELANEOUS_OPTIONS);
	d->refresh = refresh_misc;
	d->refresh_data = ses;
	d->fn=miscopt_fn;
	if (!F)
		d->udata = (void *)miscopt_labels;
#ifdef G
	else
		d->udata = (void *)miscopt_labels_g;
#endif
	d->udata2 = ses;
#ifdef G
	if (F) {
		d->items[a].type = D_FIELD;
		d->items[a].dlen = 4;
		d->items[a].data = menu_font_str;
		d->items[a].fn = check_number;
		d->items[a].gid = 1;
		d->items[a].gnum = MAX_FONT_SIZE;
		a++;
		d->items[a].type = D_FIELD;
		d->items[a].dlen = 7;
		d->items[a].data = fg_color_str;
		d->items[a].fn = check_hex_number;
		d->items[a].gid = 0;
		d->items[a].gnum = 0xffffff;
		a++;
		d->items[a].type = D_FIELD;
		d->items[a].dlen = 7;
		d->items[a].data = bg_color_str;
		d->items[a].fn = check_hex_number;
		d->items[a].gid = 0;
		d->items[a].gnum = 0xffffff;
		a++;
		d->items[a].type = D_FIELD;
		d->items[a].dlen = 7;
		d->items[a].data = scroll_area_color_str;
		d->items[a].fn = check_hex_number;
		d->items[a].gid = 0;
		d->items[a].gnum = 0xffffff;
		a++;
		d->items[a].type = D_FIELD;
		d->items[a].dlen = 7;
		d->items[a].data = scroll_bar_color_str;
		d->items[a].fn = check_hex_number;
		d->items[a].gid = 0;
		d->items[a].gnum = 0xffffff;
		a++;
		d->items[a].type = D_FIELD;
		d->items[a].dlen = 7;
		d->items[a].data = scroll_frame_color_str;
		d->items[a].fn = check_hex_number;
		d->items[a].gid = 0;
		d->items[a].gnum = 0xffffff;
		a++;
	}
#endif
	if (!anonymous) {
		d->items[a].type = D_FIELD;
		d->items[a].dlen = MAX_STR_LEN;
		d->items[a].data = new_bookmarks_file;
		a++;
		d->items[a].type = D_BUTTON;
		d->items[a].gid = 0;
		d->items[a].fn = dlg_assume_cp;
		d->items[a].text = TEXT_(T_BOOKMARKS_ENCODING);
		d->items[a].data = (unsigned char *) &new_bookmarks_codepage;
		d->items[a].dlen = sizeof(int);
		a++;
	}
#ifdef G
	if (F && (drv->flags & GD_NEED_CODEPAGE)) {
		d->items[a].type = D_BUTTON;
		d->items[a].gid = 0;
		d->items[a].fn = dlg_kb_cp;
		d->items[a].text = TEXT_(T_KEYBOARD_CODEPAGE);
		d->items[a].data = (unsigned char *)&drv->param->kbd_codepage;
		d->items[a].dlen = sizeof(int);
		a++;
	}
#endif
	if (!anonymous) {
		d->items[a].type = D_CHECKBOX;
		d->items[a].gid = 0;
		d->items[a].dlen = sizeof(int);
		d->items[a].data = (void *)&save_history;
		a++;
	}
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a].text = TEXT_(T_OK);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a].text = TEXT_(T_CANCEL);
	a++;
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#define C_LEN	9
static unsigned char max_cookie_age_str[C_LEN];

static void refresh_cookies(void *xxx)
{
	max_cookie_age = atof(cast_const_char max_cookie_age_str);
}

static int clear_cookies(struct dialog_data *dlg, struct dialog_item_data *di)
{
	unsigned char *s = init_str();
	int l = 0;
	unsigned long cnt = free_cookies();
	clear_cookies_file();
	add_num_to_str(&s, &l, cnt);
	msg_box(
		dlg->win->term,
		getml(s, NULL),
		TEXT_(T_CLEAR_COOKIES),
		AL_CENTER,
		s, cast_uchar " ", TEXT_(T_COOKIES_WERE_CLEARED), MSG_BOX_END,
		(void *)NULL,
		1,
		TEXT_(T_OK), msg_box_null, B_ENTER
	);
	return 0;
}

static unsigned char * const cookies_texts[] = { TEXT_(T_ENABLE_COOKIES), TEXT_(T_SAVE_COOKIES), TEXT_(T_MAX_COOKIE_AGE), NULL };

static void cookiesopt_fn(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	int max = 0, min = 0;
	int w, rw;
	int y = 0;
	if (dlg->win->term->spec->braille) y += LL;
	checkboxes_width(term, cookies_texts, 2, &max, max_text_width);
	checkboxes_width(term, cookies_texts, 2, &min, min_text_width);
	max_text_width(term, cookies_texts[2], &max, AL_LEFT);
	min_text_width(term, cookies_texts[2], &min, AL_LEFT);
	max_buttons_width(term, dlg->items + dlg->n - 3, 3, &max);
	min_buttons_width(term, dlg->items + dlg->n - 3, 3, &min);
	w = term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w > max) w = max;
	if (w < min) w = min;
	if (w > term->x - 2 * DIALOG_LB) w = term->x - 2 * DIALOG_LB;
	if (w < 5) w = 5;
	rw = 0;
	y += LL;
	dlg_format_checkboxes(dlg, NULL, dlg->items, 2, 0, &y, w, &rw, cookies_texts);
	dlg_format_text_and_field(dlg, NULL, cookies_texts[2], dlg->items + 2, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	y += LL;
	dlg_format_buttons(dlg, NULL, dlg->items + dlg->n - 3, 3, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = rw + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB;
	y += LL;
	dlg_format_checkboxes(dlg, term, dlg->items, 2, dlg->x + DIALOG_LB, &y, w, NULL, cookies_texts);
	y += LL;
	dlg_format_text_and_field(dlg, term, cookies_texts[2], dlg->items + 2, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	y += LL;
	dlg_format_buttons(dlg, term, dlg->items + dlg->n - 3, 3, dlg->x + DIALOG_LB, &y, w, &rw, AL_CENTER);
}

static void cookies_options(struct terminal *term, void *xxx, void *yyy)
{
	int a;
	struct dialog *d;

	snprintf(cast_char max_cookie_age_str, C_LEN, "%f", max_cookie_age);
	remove_zeroes(max_cookie_age_str);

	d = mem_calloc(sizeof(struct dialog) + 6 * sizeof(struct dialog_item));
	d->title = TEXT_(T_COOKIES);
	d->fn = cookiesopt_fn;
	d->refresh = refresh_cookies;
	a=0;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&enable_cookies;
	a++;
	d->items[a].type = D_CHECKBOX;
	d->items[a].gid = 0;
	d->items[a].dlen = sizeof(int);
	d->items[a].data = (void *)&save_cookies;
	a++;
	d->items[a].type = D_FIELD;
	d->items[a].dlen = C_LEN;
	d->items[a].data = max_cookie_age_str;
	d->items[a].fn = check_float;
	d->items[a].gid = 0;
	d->items[a].gnum = 999999900;
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].fn = clear_cookies;
	d->items[a].text = TEXT_(T_CLEAR_COOKIES);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a].text = TEXT_(T_OK);
	a++;
	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a].text = TEXT_(T_CANCEL);
	a++;
	d->items[a].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#ifdef HAVE_FREETYPE

static void refresh_fonts(void *x)
{
	reinit_video();
}

static void font_selected(struct terminal *term, void *ip, void *d)
{
	safe_strncpy(d, ip, MAX_STR_LEN);
}

static void free_fonts(void *fonts_)
{
	fontconfig_free_fonts((struct list_of_fonts *)fonts_);
}

static int select_font(struct dialog_data *dlg, struct dialog_item_data *di)
{
	unsigned char * decc_volatile empty = cast_uchar "";
	struct list_of_fonts *fonts;
	int n_fonts;
	int i;
	struct menu_item *mi;
	int monospaced = di->item->data == font_file_m || di->item->data == font_file_m_b
#ifdef USE_ITALIC
		|| di->item->data == font_file_i_m || di->item->data == font_file_i_m_b
#endif
		;
	int bold = di->item->data == font_file_b || di->item->data == font_file_m_b
#ifdef USE_ITALIC
		|| di->item->data == font_file_i_b || di->item->data == font_file_i_m_b
#endif
		;
	int current_font = 0;

	/*debug("select font: %s", di->cdata);*/

	fontconfig_list_fonts(&fonts, &n_fonts, monospaced);

	mi = new_menu(MENU_FREE_ITEMS | MENU_FONT_LIST | (bold * MENU_FONT_LIST_BOLD) | (monospaced * MENU_FONT_LIST_MONO));

	add_to_menu(&mi, TEXT_(T_BUILT_IN_FONT), empty, empty, font_selected, empty, 0, 0);
	for (i = 0; i < n_fonts; i++) {
		if (!strcmp(cast_const_char fonts[i].file, cast_const_char di->cdata))
			current_font = i + 1;
		add_to_menu(&mi, fonts[i].name, empty, empty, font_selected, fonts[i].file, 0, i + 1);
	}

	do_menu_selected(dlg->win->term, mi, di->cdata, current_font, free_fonts, fonts);

	return 0;
}

static unsigned char * const font_labels[] = { cast_uchar "", cast_uchar "", cast_uchar "", cast_uchar "", cast_uchar "", cast_uchar "", cast_uchar "", cast_uchar "" };

static void fonts_fn(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	int step = (dlg->n - 2) / 2;
	int max = 0, min = 0;
	int w, rw;
	int y = 0;
	max_buttons_width(term, dlg->items, step, &max);
	min_buttons_width(term, dlg->items, step, &min);
	max_buttons_width(term, dlg->items + step, step, &max);
	min_buttons_width(term, dlg->items + step, step, &min);
	max_buttons_width(term, dlg->items + dlg->n - 2, 2, &max);
	min_buttons_width(term, dlg->items + dlg->n - 2, 2, &min);
	w = term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w > max) w = max;
	if (w < min) w = min;
	if (w > term->x - 2 * DIALOG_LB) w = term->x - 2 * DIALOG_LB;
	if (w < 1) w = 1;
	rw = 0;
	dlg_format_buttons(dlg, NULL, dlg->items, step, 0, &y, w, &rw, AL_CENTER);
	dlg_format_buttons(dlg, NULL, dlg->items + step, step, 0, &y, w, &rw, AL_CENTER);
	dlg_format_buttons(dlg, NULL, dlg->items + dlg->n - 2, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = rw + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB + LL;
	dlg_format_buttons(dlg, term, dlg->items, step, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
	dlg_format_buttons(dlg, term, dlg->items + step, step, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
	dlg_format_buttons(dlg, term, dlg->items + dlg->n - 2, 2, dlg->x + DIALOG_LB, &y, w, &rw, AL_CENTER);
}

static void font_options(struct terminal *term, void *xxx, void *ses_)
{
	struct dialog *d;
	int a;

	d = mem_calloc(sizeof(struct dialog) + (2 + FF_SHAPES) * sizeof(struct dialog_item));
	d->title = TEXT_(T_FONTS);
	d->fn = fonts_fn;
	d->udata = (void *)font_labels;
	d->refresh = refresh_fonts;

	a = 0;

	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].text = TEXT_(T_REGULAR_FONT);
	d->items[a].fn = select_font;
	d->items[a].data = font_file;
	d->items[a++].dlen = MAX_STR_LEN;

	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].text = TEXT_(T_BOLD_FONT);
	d->items[a].fn = select_font;
	d->items[a].data = font_file_b;
	d->items[a++].dlen = MAX_STR_LEN;

	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].text = TEXT_(T_MONOSPACED_FONT);
	d->items[a].fn = select_font;
	d->items[a].data = font_file_m;
	d->items[a++].dlen = MAX_STR_LEN;

	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].text = TEXT_(T_MONOSPACED_BOLD_FONT);
	d->items[a].fn = select_font;
	d->items[a].data = font_file_m_b;
	d->items[a++].dlen = MAX_STR_LEN;

#ifdef USE_ITALIC
	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].text = TEXT_(T_ITALIC_FONT);
	d->items[a].fn = select_font;
	d->items[a].data = font_file_i;
	d->items[a++].dlen = MAX_STR_LEN;

	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].text = TEXT_(T_ITALIC_BOLD_FONT);
	d->items[a].fn = select_font;
	d->items[a].data = font_file_i_b;
	d->items[a++].dlen = MAX_STR_LEN;

	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].text = TEXT_(T_MONOSPACED_ITALIC_FONT);
	d->items[a].fn = select_font;
	d->items[a].data = font_file_i_m;
	d->items[a++].dlen = MAX_STR_LEN;

	d->items[a].type = D_BUTTON;
	d->items[a].gid = 0;
	d->items[a].text = TEXT_(T_MONOSPACED_ITALIC_BOLD_FONT);
	d->items[a].fn = select_font;
	d->items[a].data = font_file_i_m_b;
	d->items[a++].dlen = MAX_STR_LEN;
#endif

	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ENTER;
	d->items[a].fn = ok_dialog;
	d->items[a++].text = TEXT_(T_OK);

	d->items[a].type = D_BUTTON;
	d->items[a].gid = B_ESC;
	d->items[a].fn = cancel_dialog;
	d->items[a++].text = TEXT_(T_CANCEL);

	d->items[a].type = D_END;

	do_dialog(term, d, getml(d, NULL));
}

#endif

static void menu_set_language(struct terminal *term, void *pcp, void *ptr)
{
	set_language((int)(my_intptr_t)pcp);
	cls_redraw_all_terminals();
}

static void menu_language_list(struct terminal *term, void *xxx, void *ses_)
{
#ifdef OS_NO_SYSTEM_LANGUAGE
	const int def = 0;
#else
	const int def = 1;
#endif
	int i, sel;
	struct menu_item *mi;
	mi = new_menu(MENU_FREE_ITEMS);
	for (i = -def; i < n_languages(); i++) {
		unsigned char *n, *r;
		if (i == -1) {
			n = TEXT_(T_DEFAULT_LANG);
			r = language_name(get_default_language());
		} else {
			n = language_name(i);
			r = cast_uchar "";
		}
		add_to_menu(&mi, n, r, cast_uchar "", menu_set_language, (void *)(my_intptr_t)i, 0, i + def);
	}
	sel = current_language + def;
	if (sel < 0)
		sel = get_default_language();
	do_menu_selected(term, mi, NULL, sel, NULL, NULL);
}

static unsigned char * const resize_texts[] = { TEXT_(T_COLUMNS), TEXT_(T_ROWS) };

static unsigned char x_str[4];
static unsigned char y_str[4];

static void do_resize_terminal(void *term_)
{
	struct terminal *term = (struct terminal *)term_;
	unsigned char str[8];
	strcpy(cast_char str, cast_const_char x_str);
	strcat(cast_char str, ",");
	strcat(cast_char str, cast_const_char y_str);
	do_terminal_function(term, TERM_FN_RESIZE, str);
}

static void dlg_resize_terminal(struct terminal *term, void *xxx, void *ses_)
{
	struct dialog *d;
	unsigned x = (unsigned)term->x > 999 ? 999 : term->x;
	unsigned y = (unsigned)term->y > 999 ? 999 : term->y;
	sprintf(cast_char x_str, "%u", x);
	sprintf(cast_char y_str, "%u", y);
	d = mem_calloc(sizeof(struct dialog) + 4 * sizeof(struct dialog_item));
	d->title = TEXT_(T_RESIZE_TERMINAL);
	d->fn = group_fn;
	d->udata = (void *)resize_texts;
	d->refresh = do_resize_terminal;
	d->refresh_data = term;
	d->items[0].type = D_FIELD;
	d->items[0].dlen = 4;
	d->items[0].data = x_str;
	d->items[0].fn = check_number;
	d->items[0].gid = 1;
	d->items[0].gnum = 999;
	d->items[1].type = D_FIELD;
	d->items[1].dlen = 4;
	d->items[1].data = y_str;
	d->items[1].fn = check_number;
	d->items[1].gid = 1;
	d->items[1].gnum = 999;
	d->items[2].type = D_BUTTON;
	d->items[2].gid = B_ENTER;
	d->items[2].fn = ok_dialog;
	d->items[2].text = TEXT_(T_OK);
	d->items[3].type = D_BUTTON;
	d->items[3].gid = B_ESC;
	d->items[3].fn = cancel_dialog;
	d->items[3].text = TEXT_(T_CANCEL);
	d->items[4].type = D_END;
	do_dialog(term, d, getml(d, NULL));

}

static_const struct menu_item file_menu11[] = {
	{ TEXT_(T_GOTO_URL), cast_uchar "g", TEXT_(T_HK_GOTO_URL), menu_goto_url, NULL, 0, 1 },
	{ TEXT_(T_GO_BACK), cast_uchar "z", TEXT_(T_HK_GO_BACK), menu_go_back, NULL, 0, 1 },
	{ TEXT_(T_GO_FORWARD), cast_uchar "x", TEXT_(T_HK_GO_FORWARD), menu_go_forward, NULL, 0, 1 },
	{ TEXT_(T_HISTORY), cast_uchar ">", TEXT_(T_HK_HISTORY), history_menu, NULL, 1, 1 },
	{ TEXT_(T_RELOAD), cast_uchar "Ctrl-R", TEXT_(T_HK_RELOAD), menu_reload, NULL, 0, 1 },
};

#ifdef G
static_const struct menu_item file_menu111[] = {
	{ TEXT_(T_GOTO_URL), cast_uchar "g", TEXT_(T_HK_GOTO_URL), menu_goto_url, NULL, 0, 1 },
	{ TEXT_(T_GO_BACK), cast_uchar "z", TEXT_(T_HK_GO_BACK), menu_go_back, NULL, 0, 1 },
	{ TEXT_(T_GO_FORWARD), cast_uchar "x", TEXT_(T_HK_GO_FORWARD), menu_go_forward, NULL, 0, 1 },
	{ TEXT_(T_HISTORY), cast_uchar ">", TEXT_(T_HK_HISTORY), history_menu, NULL, 1, 1 },
	{ TEXT_(T_RELOAD), cast_uchar "Ctrl-R", TEXT_(T_HK_RELOAD), menu_reload, NULL, 0, 1 },
};
#endif

static_const struct menu_item file_menu12[] = {
	{ TEXT_(T_BOOKMARKS), cast_uchar "s", TEXT_(T_HK_BOOKMARKS), menu_bookmark_manager, NULL, 0, 1 },
};

static_const struct menu_item file_menu21[] = {
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 1 },
	{ TEXT_(T_SAVE_AS), cast_uchar "", TEXT_(T_HK_SAVE_AS), save_as, NULL, 0, 1 },
	{ TEXT_(T_SAVE_URL_AS), cast_uchar "", TEXT_(T_HK_SAVE_URL_AS), menu_save_url_as, NULL, 0, 1 },
	{ TEXT_(T_SAVE_FORMATTED_DOCUMENT), cast_uchar "", TEXT_(T_HK_SAVE_FORMATTED_DOCUMENT), menu_save_formatted, NULL, 0, 1 },
};

#ifdef G
static_const struct menu_item file_menu211[] = {
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 1 },
	{ TEXT_(T_SAVE_AS), cast_uchar "", TEXT_(T_HK_SAVE_AS), save_as, NULL, 0, 1 },
	{ TEXT_(T_SAVE_URL_AS), cast_uchar "", TEXT_(T_HK_SAVE_URL_AS), menu_save_url_as, NULL, 0, 1 },
};
#endif

#ifdef G
static_const struct menu_item file_menu211_clipb[] = {
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 1 },
	{ TEXT_(T_SAVE_AS), cast_uchar "", TEXT_(T_HK_SAVE_AS), save_as, NULL, 0, 1 },
	{ TEXT_(T_SAVE_URL_AS), cast_uchar "", TEXT_(T_HK_SAVE_URL_AS), menu_save_url_as, NULL, 0, 1 },
	{ TEXT_(T_COPY_URL_LOCATION), cast_uchar "", TEXT_(T_HK_COPY_URL_LOCATION), copy_url_location, NULL, 0, 1 },
};
#endif

static_const struct menu_item file_menu22[] = {
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 1},
	{ TEXT_(T_KILL_BACKGROUND_CONNECTIONS), cast_uchar "", TEXT_(T_HK_KILL_BACKGROUND_CONNECTIONS), menu_kill_background_connections, NULL, 0, 1 },
	{ TEXT_(T_KILL_ALL_CONNECTIONS), cast_uchar "", TEXT_(T_HK_KILL_ALL_CONNECTIONS), menu_kill_all_connections, NULL, 0, 1 },
	{ TEXT_(T_FLUSH_ALL_CACHES), cast_uchar "", TEXT_(T_HK_FLUSH_ALL_CACHES), flush_caches, NULL, 0, 1 },
	{ TEXT_(T_RESOURCE_INFO), cast_uchar "", TEXT_(T_HK_RESOURCE_INFO), resource_info_menu, NULL, 0, 1 },
#ifdef LEAK_DEBUG
	{ TEXT_(T_MEMORY_INFO), cast_uchar "", TEXT_(T_HK_MEMORY_INFO), memory_info_menu, NULL, 0, 1 },
#endif
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 1 },
};

static_const struct menu_item file_menu3[] = {
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 1 },
	{ TEXT_(T_EXIT), cast_uchar "q", TEXT_(T_HK_EXIT), exit_prog, NULL, 0, 1 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

static void do_file_menu(struct terminal *term, void *xxx, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	int x;
	int o;
	struct menu_item *file_menu, *e;
	file_menu = mem_alloc(sizeof(file_menu11) + sizeof(file_menu12) + sizeof(file_menu21) + sizeof(file_menu22) + sizeof(file_menu3) + 3 * sizeof(struct menu_item));
	e = file_menu;
	if (!F) {
		memcpy(e, file_menu11, sizeof(file_menu11));
		e += sizeof(file_menu11) / sizeof(struct menu_item);
#ifdef G
	} else {
		memcpy(e, file_menu111, sizeof(file_menu111));
		e += sizeof(file_menu111) / sizeof(struct menu_item);
#endif
	}
	if (!anonymous) {
		memcpy(e, file_menu12, sizeof(file_menu12));
		e += sizeof(file_menu12) / sizeof(struct menu_item);
	}
	if (!have_windows_menu && (o = can_open_in_new(term))) {
		e->text = TEXT_(T_NEW_WINDOW);
		e->rtext = o - 1 ? cast_uchar ">" : cast_uchar "";
		e->hotkey = TEXT_(T_HK_NEW_WINDOW);
		e->func = open_in_new_window;
		e->data = (void *)&send_open_new_xterm_ptr;
		e->in_m = o - 1;
		e->free_i = 0;
		e++;
	}
	if (!anonymous) {
		if (!F) {
			memcpy(e, file_menu21, sizeof(file_menu21));
			e += sizeof(file_menu21) / sizeof(struct menu_item);
#ifdef G
		} else {
			if (clipboard_support(term))
			{
				memcpy(e, file_menu211_clipb, sizeof(file_menu211_clipb));
				e += sizeof(file_menu211_clipb) / sizeof(struct menu_item);
			}
			else
			{
				memcpy(e, file_menu211, sizeof(file_menu211));
				e += sizeof(file_menu211) / sizeof(struct menu_item);
			}
#endif
		}
	}
	memcpy(e, file_menu22, sizeof(file_menu22));
	e += sizeof(file_menu22) / sizeof(struct menu_item);
	/*cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0,
	TEXT_(T_OS_SHELL), cast_uchar "", TEXT_(T_HK_OS_SHELL), menu_shell, NULL, 0, 0,*/
	x = 1;
	if (!anonymous && can_open_os_shell(term->environment)) {
		e->text = TEXT_(T_OS_SHELL);
		e->rtext = cast_uchar "";
		e->hotkey = TEXT_(T_HK_OS_SHELL);
		e->func = menu_shell;
		e->data = NULL;
		e->in_m = 0;
		e->free_i = 0;
		e++;
		x = 0;
	}
	if (can_resize_window(term)) {
		e->text = TEXT_(T_RESIZE_TERMINAL);
		e->rtext = cast_uchar "";
		e->hotkey = TEXT_(T_HK_RESIZE_TERMINAL);
		e->func = dlg_resize_terminal;
		e->data = NULL;
		e->in_m = 0;
		e->free_i = 0;
		e++;
		x = 0;
	}
	memcpy(e, file_menu3 + x, sizeof(file_menu3) - x * sizeof(struct menu_item));
	e += sizeof(file_menu3) / sizeof(struct menu_item);
	do_menu(term, file_menu, ses);
}

static void (* const search_dlg_ptr)(struct session *ses, struct f_data_c *f, int a) = search_dlg;
static void (* const search_back_dlg_ptr)(struct session *ses, struct f_data_c *f, int a) = search_back_dlg;
static void (* const find_next_ptr)(struct session *ses, struct f_data_c *f, int a) = find_next;
static void (* const find_next_back_ptr)(struct session *ses, struct f_data_c *f, int a) = find_next_back;
static void (* const set_frame_ptr)(struct session *ses, struct f_data_c *f, int a) = set_frame;

static_const struct menu_item view_menu[] = {
	{ TEXT_(T_SEARCH), cast_uchar "/", TEXT_(T_HK_SEARCH), menu_for_frame, (void *)&search_dlg_ptr, 0, 0 },
	{ TEXT_(T_SEARCH_BACK), cast_uchar "?", TEXT_(T_HK_SEARCH_BACK), menu_for_frame, (void *)&search_back_dlg_ptr, 0, 0 },
	{ TEXT_(T_FIND_NEXT), cast_uchar "n", TEXT_(T_HK_FIND_NEXT), menu_for_frame, (void *)&find_next_ptr, 0, 0 },
	{ TEXT_(T_FIND_PREVIOUS), cast_uchar "N", TEXT_(T_HK_FIND_PREVIOUS), menu_for_frame, (void *)&find_next_back_ptr, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ TEXT_(T_TOGGLE_HTML_PLAIN), cast_uchar "\\", TEXT_(T_HK_TOGGLE_HTML_PLAIN), menu_toggle, NULL, 0, 0 },
	{ TEXT_(T_DOCUMENT_INFO), cast_uchar "=", TEXT_(T_HK_DOCUMENT_INFO), menu_doc_info, NULL, 0, 0 },
	{ TEXT_(T_HEADER_INFO), cast_uchar "|", TEXT_(T_HK_HEADER_INFO), menu_head_info, NULL, 0, 0 },
	{ TEXT_(T_FRAME_AT_FULL_SCREEN), cast_uchar "f", TEXT_(T_HK_FRAME_AT_FULL_SCREEN), menu_for_frame, (void *)&set_frame_ptr, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ TEXT_(T_SAVE_CLIPBOARD_TO_A_FILE), cast_uchar "", TEXT_(T_HK_SAVE_CLIPBOARD_TO_A_FILE), menu_save_clipboard, NULL, 0, 0 },
	{ TEXT_(T_LOAD_CLIPBOARD_FROM_A_FILE), cast_uchar "", TEXT_(T_HK_LOAD_CLIPBOARD_FROM_A_FILE), menu_load_clipboard, NULL, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ TEXT_(T_HTML_OPTIONS), cast_uchar "", TEXT_(T_HK_HTML_OPTIONS), menu_html_options, NULL, 0, 0 },
	{ TEXT_(T_SAVE_HTML_OPTIONS), cast_uchar "", TEXT_(T_HK_SAVE_HTML_OPTIONS), menu_save_html_options, NULL, 0, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

static_const struct menu_item view_menu_anon[] = {
	{ TEXT_(T_SEARCH), cast_uchar "/", TEXT_(T_HK_SEARCH), menu_for_frame, (void *)&search_dlg_ptr, 0, 0 },
	{ TEXT_(T_SEARCH_BACK), cast_uchar "?", TEXT_(T_HK_SEARCH_BACK), menu_for_frame, (void *)&search_back_dlg_ptr, 0, 0 },
	{ TEXT_(T_FIND_NEXT), cast_uchar "n", TEXT_(T_HK_FIND_NEXT), menu_for_frame, (void *)&find_next_ptr, 0, 0 },
	{ TEXT_(T_FIND_PREVIOUS), cast_uchar "N", TEXT_(T_HK_FIND_PREVIOUS), menu_for_frame, (void *)&find_next_back_ptr, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ TEXT_(T_TOGGLE_HTML_PLAIN), cast_uchar "\\", TEXT_(T_HK_TOGGLE_HTML_PLAIN), menu_toggle, NULL, 0, 0 },
	{ TEXT_(T_DOCUMENT_INFO), cast_uchar "=", TEXT_(T_HK_DOCUMENT_INFO), menu_doc_info, NULL, 0, 0 },
	{ TEXT_(T_FRAME_AT_FULL_SCREEN), cast_uchar "f", TEXT_(T_HK_FRAME_AT_FULL_SCREEN), menu_for_frame, (void *)&set_frame_ptr, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ TEXT_(T_HTML_OPTIONS), cast_uchar "", TEXT_(T_HK_HTML_OPTIONS), menu_html_options, NULL, 0, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

static_const struct menu_item view_menu_color[] = {
	{ TEXT_(T_SEARCH), cast_uchar "/", TEXT_(T_HK_SEARCH), menu_for_frame, (void *)&search_dlg_ptr, 0, 0 },
	{ TEXT_(T_SEARCH_BACK), cast_uchar "?", TEXT_(T_HK_SEARCH_BACK), menu_for_frame, (void *)&search_back_dlg_ptr, 0, 0 },
	{ TEXT_(T_FIND_NEXT), cast_uchar "n", TEXT_(T_HK_FIND_NEXT), menu_for_frame, (void *)&find_next_ptr, 0, 0 },
	{ TEXT_(T_FIND_PREVIOUS), cast_uchar "N", TEXT_(T_HK_FIND_PREVIOUS), menu_for_frame, (void *)&find_next_back_ptr, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ TEXT_(T_TOGGLE_HTML_PLAIN), cast_uchar "\\", TEXT_(T_HK_TOGGLE_HTML_PLAIN), menu_toggle, NULL, 0, 0 },
	{ TEXT_(T_DOCUMENT_INFO), cast_uchar "=", TEXT_(T_HK_DOCUMENT_INFO), menu_doc_info, NULL, 0, 0 },
	{ TEXT_(T_HEADER_INFO), cast_uchar "|", TEXT_(T_HK_HEADER_INFO), menu_head_info, NULL, 0, 0 },
	{ TEXT_(T_FRAME_AT_FULL_SCREEN), cast_uchar "f", TEXT_(T_HK_FRAME_AT_FULL_SCREEN), menu_for_frame, (void *)&set_frame_ptr, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ TEXT_(T_SAVE_CLIPBOARD_TO_A_FILE), cast_uchar "", TEXT_(T_HK_SAVE_CLIPBOARD_TO_A_FILE), menu_save_clipboard, NULL, 0, 0 },
	{ TEXT_(T_LOAD_CLIPBOARD_FROM_A_FILE), cast_uchar "", TEXT_(T_HK_LOAD_CLIPBOARD_FROM_A_FILE), menu_load_clipboard, NULL, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ TEXT_(T_HTML_OPTIONS), cast_uchar "", TEXT_(T_HK_HTML_OPTIONS), menu_html_options, NULL, 0, 0 },
	{ TEXT_(T_COLOR), cast_uchar "", TEXT_(T_HK_COLOR), menu_color, NULL, 0, 0 },
	{ TEXT_(T_SAVE_HTML_OPTIONS), cast_uchar "", TEXT_(T_HK_SAVE_HTML_OPTIONS), menu_save_html_options, NULL, 0, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

static_const struct menu_item view_menu_anon_color[] = {
	{ TEXT_(T_SEARCH), cast_uchar "/", TEXT_(T_HK_SEARCH), menu_for_frame, (void *)&search_dlg_ptr, 0, 0 },
	{ TEXT_(T_SEARCH_BACK), cast_uchar "?", TEXT_(T_HK_SEARCH_BACK), menu_for_frame, (void *)&search_back_dlg_ptr, 0, 0 },
	{ TEXT_(T_FIND_NEXT), cast_uchar "n", TEXT_(T_HK_FIND_NEXT), menu_for_frame, (void *)&find_next_ptr, 0, 0 },
	{ TEXT_(T_FIND_PREVIOUS), cast_uchar "N", TEXT_(T_HK_FIND_PREVIOUS), menu_for_frame, (void *)&find_next_back_ptr, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ TEXT_(T_TOGGLE_HTML_PLAIN), cast_uchar "\\", TEXT_(T_HK_TOGGLE_HTML_PLAIN), menu_toggle, NULL, 0, 0 },
	{ TEXT_(T_DOCUMENT_INFO), cast_uchar "=", TEXT_(T_HK_DOCUMENT_INFO), menu_doc_info, NULL, 0, 0 },
	{ TEXT_(T_FRAME_AT_FULL_SCREEN), cast_uchar "f", TEXT_(T_HK_FRAME_AT_FULL_SCREEN), menu_for_frame, (void *)&set_frame_ptr, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 0 },
	{ TEXT_(T_HTML_OPTIONS), cast_uchar "", TEXT_(T_HK_HTML_OPTIONS), menu_html_options, NULL, 0, 0 },
	{ TEXT_(T_COLOR), cast_uchar "", TEXT_(T_HK_COLOR), menu_color, NULL, 0, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

static void do_view_menu(struct terminal *term, void *xxx, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	if (F || term->spec->col) {
		if (!anonymous) do_menu(term, (struct menu_item *)view_menu_color, ses);
		else do_menu(term, (struct menu_item *)view_menu_anon_color, ses);
	} else {
		if (!anonymous) do_menu(term, (struct menu_item *)view_menu, ses);
		else do_menu(term, (struct menu_item *)view_menu_anon, ses);
	}
}


static_const struct menu_item help_menu[] = {
	{ TEXT_(T_ABOUT), cast_uchar "", TEXT_(T_HK_ABOUT), menu_about, NULL, 0, 0 },
	{ TEXT_(T_KEYS), cast_uchar "F1", TEXT_(T_HK_KEYS), menu_keys, NULL, 0, 0 },
	{ TEXT_(T_MANUAL), cast_uchar "", TEXT_(T_HK_MANUAL), menu_url, TEXT_(T_URL_MANUAL), 0, 0 },
	{ TEXT_(T_HOMEPAGE), cast_uchar "", TEXT_(T_HK_HOMEPAGE), menu_url, TEXT_(T_URL_HOMEPAGE), 0, 0 },
	{ TEXT_(T_COPYING), cast_uchar "", TEXT_(T_HK_COPYING), menu_copying, NULL, 0, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

#ifdef G
static_const struct menu_item help_menu_g[] = {
	{ TEXT_(T_ABOUT), cast_uchar "", TEXT_(T_HK_ABOUT), menu_about, NULL, 0, 0 },
	{ TEXT_(T_KEYS), cast_uchar "F1", TEXT_(T_HK_KEYS), menu_keys, NULL, 0, 0 },
	{ TEXT_(T_MANUAL), cast_uchar "", TEXT_(T_HK_MANUAL), menu_url, TEXT_(T_URL_MANUAL), 0, 0 },
	{ TEXT_(T_HOMEPAGE), cast_uchar "", TEXT_(T_HK_HOMEPAGE), menu_url, TEXT_(T_URL_HOMEPAGE), 0, 0 },
	{ TEXT_(T_CALIBRATION), cast_uchar "", TEXT_(T_HK_CALIBRATION), menu_url, TEXT_(T_URL_CALIBRATION), 0, 0 },
	{ TEXT_(T_COPYING), cast_uchar "", TEXT_(T_HK_COPYING), menu_copying, NULL, 0, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};
#endif

static_const struct menu_item net_options_menu[] = {
	{ TEXT_(T_CONNECTIONS), cast_uchar "", TEXT_(T_HK_CONNECTIONS), dlg_net_options, NULL, 0, 0 },
	{ TEXT_(T_PROXIES), cast_uchar "", TEXT_(T_HK_PROXIES), dlg_proxy_options, NULL, 0, 0 },
#ifdef HAVE_SSL_CERTIFICATES
	{ TEXT_(T_SSL_OPTIONS), cast_uchar "", TEXT_(T_HK_SSL_OPTIONS), dlg_ssl_options, NULL, 0, 0 },
#endif
	{ TEXT_(T_DNS_OPTIONS), cast_uchar "", TEXT_(T_HK_DNS_OPTIONS), dlg_dns_options, NULL, 0, 0 },
	{ TEXT_(T_HTTP_OPTIONS), cast_uchar "", TEXT_(T_HK_HTTP_OPTIONS), dlg_http_options, NULL, 0, 0 },
	{ TEXT_(T_FTP_OPTIONS), cast_uchar "", TEXT_(T_HK_FTP_OPTIONS), dlg_ftp_options, NULL, 0, 0 },
#ifndef DISABLE_SMB
	{ TEXT_(T_SMB_OPTIONS), cast_uchar "", TEXT_(T_HK_SMB_OPTIONS), dlg_smb_options, NULL, 0, 0 },
#endif
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

#ifdef SUPPORT_IPV6
static_const struct menu_item net_options_ipv6_menu[] = {
	{ TEXT_(T_CONNECTIONS), cast_uchar "", TEXT_(T_HK_CONNECTIONS), dlg_net_options, NULL, 0, 0 },
	{ TEXT_(T_IPV6_OPTIONS), cast_uchar "", TEXT_(T_HK_IPV6_OPTIONS), dlg_ipv6_options, NULL, 0, 0 },
	{ TEXT_(T_PROXIES), cast_uchar "", TEXT_(T_HK_PROXIES), dlg_proxy_options, NULL, 0, 0 },
#ifdef HAVE_SSL_CERTIFICATES
	{ TEXT_(T_SSL_OPTIONS), cast_uchar "", TEXT_(T_HK_SSL_OPTIONS), dlg_ssl_options, NULL, 0, 0 },
#endif
	{ TEXT_(T_DNS_OPTIONS), cast_uchar "", TEXT_(T_HK_DNS_OPTIONS), dlg_dns_options, NULL, 0, 0 },
	{ TEXT_(T_HTTP_OPTIONS), cast_uchar "", TEXT_(T_HK_HTTP_OPTIONS), dlg_http_options, NULL, 0, 0 },
	{ TEXT_(T_FTP_OPTIONS), cast_uchar "", TEXT_(T_HK_FTP_OPTIONS), dlg_ftp_options, NULL, 0, 0 },
#ifndef DISABLE_SMB
	{ TEXT_(T_SMB_OPTIONS), cast_uchar "", TEXT_(T_HK_SMB_OPTIONS), dlg_smb_options, NULL, 0, 0 },
#endif
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};
#endif

static void network_menu(struct terminal *term, void *xxx, void *yyy)
{
#ifdef SUPPORT_IPV6
	if (support_ipv6)
		do_menu(term, (struct menu_item *)net_options_ipv6_menu, NULL);
	else
#endif
		do_menu(term, (struct menu_item *)net_options_menu, NULL);
}

static void menu_write_config(struct terminal *term, void *xxx, void *yyy)
{
	write_config(term);
}

static_const struct menu_item setup_menu_1[] = {
	{ TEXT_(T_LANGUAGE), cast_uchar ">", TEXT_(T_HK_LANGUAGE), menu_language_list, NULL, 1, 1 },
};

static_const struct menu_item setup_menu_2[] = {
	{ TEXT_(T_CHARACTER_SET), cast_uchar ">", TEXT_(T_HK_CHARACTER_SET), charset_list, NULL, 1, 1 },
	{ TEXT_(T_TERMINAL_OPTIONS), cast_uchar "", TEXT_(T_HK_TERMINAL_OPTIONS), terminal_options, NULL, 0, 1 },
};

#ifdef G
static_const struct menu_item setup_menu_3[] = {
	{ TEXT_(T_VIDEO_OPTIONS), cast_uchar "", TEXT_(T_HK_VIDEO_OPTIONS), video_options, NULL, 0, 1 },
};
#endif

static_const struct menu_item setup_menu_4[] = {
	{ TEXT_(T_SCREEN_MARGINS), cast_uchar "", TEXT_(T_HK_SCREEN_MARGINS), screen_margins, NULL, 0, 1 },
};

static_const struct menu_item setup_menu_5[] = {
	{ TEXT_(T_NETWORK_OPTIONS), cast_uchar ">", TEXT_(T_HK_NETWORK_OPTIONS), network_menu, NULL, 1, 1 },
};

static_const struct menu_item setup_menu_6[] = {
	{ TEXT_(T_COOKIES), cast_uchar "", TEXT_(T_HK_COOKIES), cookies_options, NULL, 0, 1 },
	{ TEXT_(T_MISCELANEOUS_OPTIONS), cast_uchar "", TEXT_(T_HK_MISCELANEOUS_OPTIONS), miscelaneous_options, NULL, 0, 1 },
};

#ifdef HAVE_FREETYPE
static_const struct menu_item setup_menu_7[] = {
	{ TEXT_(T_FONTS), cast_uchar "", TEXT_(T_HK_FONTS), font_options, NULL, 0, 1 },
};
#endif

static_const struct menu_item setup_menu_8[] = {
#ifdef JS
	{ TEXT_(T_JAVASCRIPT_OPTIONS), cast_uchar "", TEXT_(T_HK_JAVASCRIPT_OPTIONS), javascript_options, NULL, 0, 1 },
#endif
	{ TEXT_(T_CACHE), cast_uchar "", TEXT_(T_HK_CACHE), cache_opt, NULL, 0, 1 },
	{ TEXT_(T_MAIL_AND_TELNEL), cast_uchar "", TEXT_(T_HK_MAIL_AND_TELNEL), net_programs, NULL, 0, 1 },
	{ TEXT_(T_ASSOCIATIONS), cast_uchar "", TEXT_(T_HK_ASSOCIATIONS), menu_assoc_manager, NULL, 0, 1 },
	{ TEXT_(T_FILE_EXTENSIONS), cast_uchar "", TEXT_(T_HK_FILE_EXTENSIONS), menu_ext_manager, NULL, 0, 1 },
	{ TEXT_(T_BLOCK_LIST), cast_uchar "", TEXT_(T_HK_BLOCK_LIST), block_manager, NULL, 0, 0 },
	{ cast_uchar "", cast_uchar "", M_BAR, NULL, NULL, 0, 1 },
	{ TEXT_(T_SAVE_OPTIONS), cast_uchar "", TEXT_(T_HK_SAVE_OPTIONS), menu_write_config, NULL, 0, 1 },
};

static_const struct menu_item setup_menu_9[] = {
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

static void do_setup_menu(struct terminal *term, void *xxx, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	struct menu_item *setup_menu, *e;
	int size =
		sizeof(setup_menu_1) +
		sizeof(setup_menu_2) +
#ifdef G
		sizeof(setup_menu_3) +
#endif
		sizeof(setup_menu_4) +
		sizeof(setup_menu_5) +
		sizeof(setup_menu_6) +
#ifdef HAVE_FREETYPE
		sizeof(setup_menu_7) +
#endif
		sizeof(setup_menu_8) +
		sizeof(setup_menu_9);
	setup_menu = mem_alloc(size);
	e = setup_menu;
	memcpy(e, setup_menu_1, sizeof(setup_menu_1));
	e += sizeof(setup_menu_1) / sizeof(struct menu_item);
	if (!F) {
		memcpy(e, setup_menu_2, sizeof(setup_menu_2));
		e += sizeof(setup_menu_2) / sizeof(struct menu_item);
#ifdef G
	} else {
		memcpy(e, setup_menu_3, sizeof(setup_menu_3));
		e += sizeof(setup_menu_3) / sizeof(struct menu_item);
#endif
	}
	if (!F
#ifdef G
	    || (drv->get_margin && drv->set_margin)
#endif
	      ) {
		memcpy(e, setup_menu_4, sizeof(setup_menu_4));
		e += sizeof(setup_menu_4) / sizeof(struct menu_item);
	}
	if (!anonymous) {
		memcpy(e, setup_menu_5, sizeof(setup_menu_5));
		e += sizeof(setup_menu_5) / sizeof(struct menu_item);
	}
	if (!anonymous || F) {
		memcpy(e, setup_menu_6, sizeof(setup_menu_6));
		e += sizeof(setup_menu_6) / sizeof(struct menu_item);
	}
#ifdef HAVE_FREETYPE
	if (F) {
		memcpy(e, setup_menu_7, sizeof(setup_menu_7));
		e += sizeof(setup_menu_7) / sizeof(struct menu_item);
	}
#endif
	if (!anonymous) {
		memcpy(e, setup_menu_8, sizeof(setup_menu_8));
		e += sizeof(setup_menu_8) / sizeof(struct menu_item);
	}
	memcpy(e, setup_menu_9, sizeof(setup_menu_9));
	e += sizeof(setup_menu_9) / sizeof(struct menu_item);
	do_menu(term, setup_menu, ses);
}

static void do_help_menu(struct terminal *term, void *xxx, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	do_menu(term, (struct menu_item *)gf_val(help_menu, help_menu_g), ses);
}


static_const struct menu_item main_menu[] = {
	{ TEXT_(T_FILE), cast_uchar "", TEXT_(T_HK_FILE), do_file_menu, NULL, 1, 1 },
	{ TEXT_(T_VIEW), cast_uchar "", TEXT_(T_HK_VIEW), do_view_menu, NULL, 1, 1 },
	{ TEXT_(T_LINK), cast_uchar "", TEXT_(T_HK_LINK), link_menu, NULL, 1, 1 },
	{ TEXT_(T_DOWNLOADS), cast_uchar "", TEXT_(T_HK_DOWNLOADS), downloads_menu, NULL, 1, 1 },
	{ TEXT_(T_SETUP), cast_uchar "", TEXT_(T_HK_SETUP), do_setup_menu, NULL, 1, 1 },
	{ TEXT_(T_HELP), cast_uchar "", TEXT_(T_HK_HELP), do_help_menu, NULL, 1, 1 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

#ifdef G

#ifdef GRDRV_VIRTUAL_DEVICES

static_const struct menu_item main_menu_g_windows[] = {
	{ TEXT_(T_FILE), cast_uchar "", TEXT_(T_HK_FILE), do_file_menu, NULL, 1, 1 },
	{ TEXT_(T_VIEW), cast_uchar "", TEXT_(T_HK_VIEW), do_view_menu, NULL, 1, 1 },
	{ TEXT_(T_LINK), cast_uchar "", TEXT_(T_HK_LINK), link_menu, NULL, 1, 1 },
	{ TEXT_(T_DOWNLOADS), cast_uchar "", TEXT_(T_HK_DOWNLOADS), downloads_menu, NULL, 1, 1 },
	{ TEXT_(T_WINDOWS), cast_uchar "", TEXT_(T_HK_WINDOWS), windows_menu, NULL, 1, 1 },
	{ TEXT_(T_SETUP), cast_uchar "", TEXT_(T_HK_SETUP), do_setup_menu, NULL, 1, 1 },
	{ TEXT_(T_HELP), cast_uchar "", TEXT_(T_HK_HELP), do_help_menu, NULL, 1, 1 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 }
};

#endif

#endif


/* lame technology rulez ! */

void activate_bfu_technology(struct session *ses, int item)
{
	struct terminal *term = ses->term;
	/* decc_volatile to avoid compiler bug */
	struct menu_item * decc_volatile m = (struct menu_item *)main_menu;
#ifdef G
	struct menu_item * decc_volatile mg = m;
#ifdef GRDRV_VIRTUAL_DEVICES
	if (have_windows_menu)
		mg = (struct menu_item *)main_menu_g_windows;
#endif
#endif
	do_mainmenu(term, gf_val(m, mg), ses, item);
}

struct history goto_url_history = { 0, { &goto_url_history.items, &goto_url_history.items } };

void dialog_goto_url(struct session *ses, unsigned char *url)
{
	input_field(ses->term, NULL, TEXT_(T_GOTO_URL), TEXT_(T_ENTER_URL), ses, &goto_url_history, MAX_INPUT_URL_LEN, url, 0, 0, NULL, 2, TEXT_(T_OK), goto_url, TEXT_(T_CANCEL), input_field_null);
}

void dialog_save_url(struct session *ses)
{
	input_field(ses->term, NULL, TEXT_(T_SAVE_URL), TEXT_(T_ENTER_URL), ses, &goto_url_history, MAX_INPUT_URL_LEN, cast_uchar "", 0, 0, NULL, 2, TEXT_(T_OK), save_url, TEXT_(T_CANCEL), input_field_null);
}


struct does_file_exist_s {
	void (*fn)(struct session *, unsigned char *, int);
	void (*cancel)(void *);
	int flags;
	struct session *ses;
	unsigned char *file;
	unsigned char *url;
	unsigned char *head;
};

static void does_file_exist_ok(struct does_file_exist_s *h, int mode)
{
	if (h->fn) {
		unsigned char *d = h->file;
		unsigned char *dd;
		for (dd = h->file; *dd; dd++) if (dir_sep(*dd)) d = dd + 1;
		if (d - h->file < MAX_STR_LEN) {
			memcpy(download_dir, h->file, d - h->file);
			download_dir[d - h->file] = 0;
		}
		h->fn(h->ses, h->file, mode);
	}
}


static void does_file_exist_continue(void *data)
{
	does_file_exist_ok(data, DOWNLOAD_CONTINUE);
}

static void does_file_exist_overwrite(void *data)
{
	does_file_exist_ok(data, DOWNLOAD_OVERWRITE);
}

static void does_file_exist_cancel(void *data)
{
	struct does_file_exist_s *h=(struct does_file_exist_s *)data;
	if (h->cancel) h->cancel(h->ses);
}

static void does_file_exist_rename(void *data)
{
	struct does_file_exist_s *h=(struct does_file_exist_s *)data;
	query_file(h->ses, h->url, h->head, h->fn, h->cancel, h->flags);
}

static void does_file_exist(void *d_, unsigned char *file)
{
	struct does_file_exist_s *d = (struct does_file_exist_s *)d_;
	unsigned char *f;
	unsigned char *wd;
	struct session *ses = d->ses;
	struct stat st;
	int r;
	struct does_file_exist_s *h;
	unsigned char *msg;
	int file_type = 0;

	h = mem_alloc(sizeof(struct does_file_exist_s));
	h->fn = d->fn;
	h->cancel = d->cancel;
	h->flags = d->flags;
	h->ses = ses;
	h->file = stracpy(file);
	h->url = stracpy(d->url);
	h->head = stracpy(d->head);

	if (!*file) {
		does_file_exist_rename(h);
		goto free_h_ret;
	}

	if (test_abort_downloads_to_file(file, ses->term->cwd, 0)) {
		msg = TEXT_(T_ALREADY_EXISTS_AS_DOWNLOAD);
		goto display_msgbox;
	}

	wd = get_cwd();
	set_cwd(ses->term->cwd);
	f = translate_download_file(file);
	EINTRLOOP(r, stat(cast_const_char f, &st));
	mem_free(f);
	if (wd) set_cwd(wd), mem_free(wd);
	if (r) {
		does_file_exist_ok(h, DOWNLOAD_DEFAULT);
free_h_ret:
		if (h->head) mem_free(h->head);
		mem_free(h->file);
		mem_free(h->url);
		mem_free(h);
		return;
	}

	if (!S_ISREG(st.st_mode)) {
		if (S_ISDIR(st.st_mode))
			file_type = 2;
		else
			file_type = 1;
	}

	msg = TEXT_(T_ALREADY_EXISTS);
	display_msgbox:
	if (file_type == 2) {
		msg_box(
			ses->term,
			getml(h, h->file, h->url, h->head, NULL),
			TEXT_(T_FILE_ALREADY_EXISTS),
			AL_CENTER,
			TEXT_(T_DIRECTORY), cast_uchar " ", h->file, cast_uchar " ", TEXT_(T_ALREADY_EXISTS), MSG_BOX_END,
			(void *)h,
			2,
			TEXT_(T_RENAME), does_file_exist_rename, B_ENTER,
			TEXT_(T_CANCEL), does_file_exist_cancel, B_ESC
		);
	} else if (file_type || h->flags != DOWNLOAD_CONTINUE) {
		msg_box(
			ses->term,
			getml(h, h->file, h->url, h->head, NULL),
			TEXT_(T_FILE_ALREADY_EXISTS),
			AL_CENTER,
			TEXT_(T_FILE), cast_uchar " ", h->file, cast_uchar " ", msg, cast_uchar " ", TEXT_(T_DO_YOU_WISH_TO_OVERWRITE), MSG_BOX_END,
			(void *)h,
			3,
			TEXT_(T_OVERWRITE), does_file_exist_overwrite, B_ENTER,
			TEXT_(T_RENAME), does_file_exist_rename, 0,
			TEXT_(T_CANCEL), does_file_exist_cancel, B_ESC
		);
	} else {
		msg_box(
			ses->term,
			getml(h, h->file, h->url, h->head, NULL),
			TEXT_(T_FILE_ALREADY_EXISTS),
			AL_CENTER,
			TEXT_(T_FILE), cast_uchar " ", h->file, cast_uchar " ", msg, cast_uchar " ", TEXT_(T_DO_YOU_WISH_TO_CONTINUE), MSG_BOX_END,
			(void *)h,
			4,
			TEXT_(T_CONTINUE), does_file_exist_continue, B_ENTER,
			TEXT_(T_OVERWRITE), does_file_exist_overwrite, 0,
			TEXT_(T_RENAME), does_file_exist_rename, 0,
			TEXT_(T_CANCEL), does_file_exist_cancel, B_ESC
		);
	}
}


static void query_file_cancel(void *d_, unsigned char *s_)
{
	struct does_file_exist_s *d = (struct does_file_exist_s *)d_;
	if (d->cancel) d->cancel(d->ses);
}


void query_file(struct session *ses, unsigned char *url, unsigned char *head, void (*fn)(struct session *, unsigned char *, int), void (*cancel)(void *), int flags)
{
	unsigned char *fc, *file, *def;
	int dfl = 0;
	struct does_file_exist_s *h;

	h = mem_alloc(sizeof(struct does_file_exist_s));

	fc = get_filename_from_url(url, head, 0);
	file = convert(utf8_table, 0, fc, NULL);
	mem_free(fc);
	check_filename(&file);

	def = init_str();
	add_to_str(&def, &dfl, download_dir);
	if (*def && !dir_sep(def[strlen(cast_const_char def) - 1])) add_chr_to_str(&def, &dfl, '/');
	add_to_str(&def, &dfl, file);
	mem_free(file);

	h->fn = fn;
	h->cancel = cancel;
	h->flags = flags;
	h->ses = ses;
	h->file = NULL;
	h->url = stracpy(url);
	h->head = stracpy(head);

	input_field(ses->term, getml(h, h->url, h->head, NULL), TEXT_(T_DOWNLOAD), TEXT_(T_SAVE_TO_FILE), h, &file_history, MAX_INPUT_URL_LEN, def, 0, 0, NULL, 2, TEXT_(T_OK), does_file_exist, TEXT_(T_CANCEL), query_file_cancel);
	mem_free(def);
}

static struct history search_history = { 0, { &search_history.items, &search_history.items } };

void search_back_dlg(struct session *ses, struct f_data_c *f, int a)
{
	if (list_empty(ses->history) || !f->f_data || !f->vs) {
		msg_box(ses->term, NULL, TEXT_(T_SEARCH), AL_LEFT, TEXT_(T_YOU_ARE_NOWHERE), MSG_BOX_END, NULL, 1, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC);
		return;
	}
	input_field(ses->term, NULL, TEXT_(T_SEARCH_BACK), TEXT_(T_SEARCH_FOR_TEXT), ses, &search_history, MAX_INPUT_URL_LEN, cast_uchar "", 0, 0, NULL, 2, TEXT_(T_OK), search_for_back, TEXT_(T_CANCEL), input_field_null);
}

void search_dlg(struct session *ses, struct f_data_c *f, int a)
{
	if (list_empty(ses->history) || !f->f_data || !f->vs) {
		msg_box(ses->term, NULL, TEXT_(T_SEARCH_FOR_TEXT), AL_LEFT, TEXT_(T_YOU_ARE_NOWHERE), MSG_BOX_END, NULL, 1, TEXT_(T_OK), msg_box_null, B_ENTER | B_ESC);
		return;
	}
	input_field(ses->term, NULL, TEXT_(T_SEARCH), TEXT_(T_SEARCH_FOR_TEXT), ses, &search_history, MAX_INPUT_URL_LEN, cast_uchar "", 0, 0, NULL, 2, TEXT_(T_OK), search_for, TEXT_(T_CANCEL), input_field_null);
}

void free_history_lists(void)
{
	free_history(goto_url_history);
	free_history(file_history);
	free_history(search_history);
#ifdef JS
	free_history(js_get_string_history);   /* is in jsint.c */
#endif
}

