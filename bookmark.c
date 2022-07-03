/* bookmark.c
 * (c) 2002 Petr 'Brain' Kulhavy, Karel 'Clock' Kulhavy
 * This file is a part of the Links program, released under GPL.
 */

#include "links.h"

#define SEARCH_IN_URL

#ifdef SEARCH_IN_URL
#define SHOW_URL
#endif

static struct stat bookmarks_st;

static struct list *bookmark_new_item(void *);
static unsigned char *bookmark_type_item(struct terminal *, struct list *, int);
static void bookmark_delete_item(struct list *);
static void bookmark_edit_item(struct dialog_data *, struct list *, void (*)(struct dialog_data *, struct list *, struct list *, struct list_description *), struct list *, unsigned char);
static void bookmark_copy_item(struct list *, struct list *);
static void bookmark_goto_item(struct session *, struct list *);
static void *bookmark_default_value(struct session *, unsigned char);
static struct list *bookmark_find_item(struct list *start, unsigned char *str, int direction);
static void save_bookmarks(struct session *ses);

struct list bookmarks = { init_list_1st(&bookmarks.list_entry) 0, -1, NULL, init_list_last(&bookmarks.list_entry) };

static struct history bookmark_search_history = { 0, { &bookmark_search_history.items, &bookmark_search_history.items } };

/* when you change anything, don't forget to change it in reinit_bookmarks too !*/

struct bookmark_ok_struct {
	void (*fn)(struct dialog_data *, struct list *, struct list *, struct list_description *);
	struct list *data;
	struct dialog_data *dlg;
};


struct bookmark_list {
	list_head_1st
	/* bookmark specific */
	unsigned char *title;
	unsigned char *url;
	list_head_last
};


static struct list_description bookmark_ld = {
	1,  /* 0= flat; 1=tree */
	&bookmarks,  /* list */
	bookmark_new_item,	/* no codepage translations */
	bookmark_edit_item,	/* translate when create dialog and translate back when ok is pressed */
	bookmark_default_value,	/* codepage translation from current_page_encoding to UTF8 */
	bookmark_delete_item,	/* no codepage translations */
	bookmark_copy_item,	/* no codepage translations */
	bookmark_type_item,	/* no codepage translations (bookmarks are internally in UTF8) */
	bookmark_find_item,
	&bookmark_search_history,
	0,		/* this is set in init_bookmarks function */
	15,  /* # of items in main window */
	T_BOOKMARK,
	T_BOOKMARKS_ALREADY_IN_USE,
	T_BOOKMARK_MANAGER,
	T_DELETE_BOOKMARK,
	T_GOTO,
	bookmark_goto_item,	/* FIXME: should work (URL in UTF8), but who knows? */
	save_bookmarks,

	NULL,NULL,0,0,  /* internal vars */
	0, /* modified */
	NULL,
	NULL,
	0,
};


struct kawasaki {
	unsigned char *title;
	unsigned char *url;
};


/* clears the bookmark list */
static void free_bookmarks(void)
{
	struct list *b;
	struct list_head *lb;

	foreach(struct list, b, lb, bookmarks.list_entry) {
		struct bookmark_list *bm = get_struct(b, struct bookmark_list, head);
		mem_free(bm->title);
		mem_free(bm->url);
		lb = lb->prev;
		del_from_list(b);
		mem_free(bm);
	}
}


/* called before exiting the links */
void finalize_bookmarks(void)
{
	free_bookmarks();
	free_history(bookmark_search_history);
}



/* allocates struct kawasaki and puts current page title and url */
/* type: 0=item, 1=directory */
/* on error returns NULL */
static void *bookmark_default_value(struct session *ses, unsigned char type)
{
	struct kawasaki *zelena;
	unsigned char *txt;

	txt=mem_alloc(MAX_STR_LEN);

	zelena=mem_alloc(sizeof(struct kawasaki));

	zelena->url=NULL;
	zelena->title=NULL;
	if (get_current_url(ses,txt,MAX_STR_LEN))
	{
		if (ses->screen->f_data)
		{
			zelena->url=convert(term_charset(ses->term),bookmark_ld.codepage,txt,NULL);
			clr_white(zelena->url);
		}
		else
			zelena->url=stracpy(txt);
	}
	if (get_current_title(ses->screen,txt,MAX_STR_LEN))  /* ses->screen->f_data must exist here */
	{
		zelena->title=convert(term_charset(ses->term),bookmark_ld.codepage,txt,NULL);
		clr_white(zelena->title);
	}

	mem_free(txt);

	return zelena;
}


static void bookmark_copy_item(struct list *in, struct list *out)
{
	struct bookmark_list *item_in = get_struct(in, struct bookmark_list, head);
	struct bookmark_list *item_out = get_struct(out, struct bookmark_list, head);

	item_out->head.type = item_in->head.type;
	item_out->head.depth = item_in->head.depth;

	mem_free(item_out->title);
	item_out->title = stracpy(item_in->title);
	mem_free(item_out->url);
	item_out->url = stracpy(item_in->url);
}


static unsigned char * const bm_add_msg[] = {
	TEXT_(T_NNAME),
	TEXT_(T_URL),
};

/* Called to setup the add bookmark dialog */
static void bookmark_edit_item_fn(struct dialog_data *dlg)
{
	int max = 0, min = 0;
	int w, rw;
	int y = gf_val(-1, -1 * G_BFU_FONT_SIZE);
	struct terminal *term;
	int a;
	if (dlg->win->term->spec->braille) y += gf_val(1, G_BFU_FONT_SIZE);

	term = dlg->win->term;

	for (a=0;a<dlg->n-2;a++)
	{
		max_text_width(term, bm_add_msg[a], &max, AL_LEFT);
		min_text_width(term, bm_add_msg[a], &min, AL_LEFT);
	}
	max_buttons_width(term, dlg->items + dlg->n-2, 2, &max);
	min_buttons_width(term, dlg->items + dlg->n-2, 2, &min);
	w = term->x * 9 / 10 - 2 * DIALOG_LB;

	if (w < min) w = min;

	rw = w;

	for (a = 0; a < dlg->n - 2; a++) {
		dlg_format_text_and_field(dlg, NULL, bm_add_msg[a], &dlg->items[a], 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
		y += gf_val(1, 1 * G_BFU_FONT_SIZE);
	}
	dlg_format_buttons(dlg, NULL, dlg->items+dlg->n-2, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = w + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB;
	if (dlg->win->term->spec->braille) y += gf_val(1, G_BFU_FONT_SIZE);
	for (a = 0; a < dlg->n - 2; a++) {
		dlg_format_text_and_field(dlg, term, bm_add_msg[a], &dlg->items[a], dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
		y += gf_val(1, G_BFU_FONT_SIZE);
	}
	dlg_format_buttons(dlg, term, &dlg->items[dlg->n-2], 2, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
}


/* Puts url and title into the bookmark item */
static void bookmark_edit_done(void *data)
{
	struct dialog *d = (struct dialog *)data;
	struct bookmark_list *item = (struct bookmark_list *)d->udata;
	unsigned char *title, *url;
	struct bookmark_ok_struct *s = (struct bookmark_ok_struct *)d->udata2;
	int a;

	if (item->head.type & 1) a = 4; /* folder */
	else a = 5;
	title = (unsigned char *)&d->items[a];
	url = title + MAX_STR_LEN;

	mem_free(item->title);
	item->title = convert(term_charset(s->dlg->win->term), bookmark_ld.codepage, title, NULL);
	clr_white(item->title);

	mem_free(item->url);
	item->url = convert(term_charset(s->dlg->win->term), bookmark_ld.codepage, url, NULL);
	clr_white(item->url);

	s->fn(s->dlg, s->data, &item->head, &bookmark_ld);
	d->udata = NULL;  /* for abort function */
}


/* destroys an item, this function is called when edit window is aborted */
static void bookmark_edit_abort(struct dialog_data *data)
{
	struct bookmark_list *item = (struct bookmark_list *)data->dlg->udata;
	struct dialog *dlg = data->dlg;

	mem_free(dlg->udata2);
	if (item) bookmark_delete_item(&item->head);
}


/* dlg_title is TITLE_EDIT or TITLE_ADD */
/* edit item function */
static void bookmark_edit_item(struct dialog_data *dlg, struct list *data, void (*ok_fn)(struct dialog_data *, struct list *, struct list *, struct list_description *), struct list *ok_arg, unsigned char dlg_title)
{
	struct bookmark_list *item = get_struct(data, struct bookmark_list, head);
	unsigned char *title, *url;
	struct dialog *d;
	struct bookmark_ok_struct *s;
	int a;

	/* Create the dialog */
	s = mem_alloc(sizeof(struct bookmark_ok_struct));
	s->fn = ok_fn;
	s->data = ok_arg;
	s->dlg = dlg;

	if (item->head.type & 1) a = 4; /* folder */
	else a = 5;
	d = mem_calloc(sizeof(struct dialog) + a * sizeof(struct dialog_item) + 2 * MAX_STR_LEN);

	title = (unsigned char *)&d->items[a];
	url = title + MAX_STR_LEN;

	{
		unsigned char *txt;

		txt = convert(bookmark_ld.codepage, term_charset(dlg->win->term), item->title, NULL);
		clr_white(txt);
		safe_strncpy(title, txt, MAX_STR_LEN);
		mem_free(txt);

		txt = convert(bookmark_ld.codepage, term_charset(dlg->win->term), item->url, NULL);
		clr_white(txt);
		safe_strncpy(url, txt, MAX_STR_LEN);
		mem_free(txt);
	}

	switch (dlg_title)
	{
		case TITLE_EDIT:
			if (item->head.type & 1) d->title = TEXT_(T_EDIT_FOLDER);
			else d->title = TEXT_(T_EDIT_BOOKMARK);
			break;

		case TITLE_ADD:
			if (item->head.type & 1) d->title = TEXT_(T_ADD_FOLDER);
			else d->title = TEXT_(T_ADD_BOOKMARK);
			break;

		default:
			internal_error("Unsupported dialog title.\n");
	}
	d->fn = bookmark_edit_item_fn;
	d->udata = item;  /* item */
	d->udata2 = s;
	d->refresh = bookmark_edit_done;
	d->abort = bookmark_edit_abort;
	d->refresh_data = d;

	d->items[0].type = D_FIELD;
	d->items[0].dlen = MAX_STR_LEN;
	d->items[0].data = title;
	d->items[0].fn = check_nonempty;

	a = 0;
	if (!(item->head.type & 1))  /* item */
	{
		d->items[1].type = D_FIELD;
		d->items[1].dlen = MAX_STR_LEN;
		d->items[1].data = url;
		d->items[1].fn = check_nonempty;
		a++;
	}

	d->items[a+1].type = D_BUTTON;
	d->items[a+1].gid = B_ENTER;
	d->items[a+1].fn = ok_dialog;
	d->items[a+1].text = TEXT_(T_OK);

	d->items[a+2].type = D_BUTTON;
	d->items[a+2].gid = B_ESC;
	d->items[a+2].text = TEXT_(T_CANCEL);
	d->items[a+2].fn = cancel_dialog;

	d->items[a+3].type = D_END;

	do_dialog(dlg->win->term, d, getml(d, NULL));
}


/* create new bookmark item and returns pointer to it, on error returns 0*/
/* bookmark is filled with given data, data are deallocated afterwards */
static struct list *bookmark_new_item(void *data)
{
	struct bookmark_list *b;
	struct kawasaki *zelena = (struct kawasaki *)data;

	b = mem_alloc(sizeof(struct bookmark_list));

	if (zelena && zelena->title)
		b->title = zelena->title;
	else
		b->title = stracpy(cast_uchar "");

	if (zelena && zelena->url)
		b->url = zelena->url;
	else
		b->url = stracpy(cast_uchar "");

	if (zelena)
		mem_free(zelena);

	return &b->head;
}


/* allocate string and print bookmark into it */
/* x: 0=type all, 1=type title only */
static unsigned char *bookmark_type_item(struct terminal *term, struct list *data, int x)
{
	unsigned char *txt, *txt1;
	struct bookmark_list *item;

	if (data == &bookmarks)   /* head */
		return stracpy(get_text_translation(TEXT_(T_BOOKMARKS), term));

	item = get_struct(data, struct bookmark_list, head);
	txt = stracpy(item->title);

#ifdef SHOW_URL
	x = 0;
#endif
	if (!x && !(item->head.type & 1)) {
		add_to_strn(&txt, cast_uchar "   (");
		add_to_strn(&txt, item->url);
		add_to_strn(&txt, cast_uchar ")");
	}

	txt1 = convert(bookmark_ld.codepage, term_charset(term), txt, NULL);
	clr_white(txt1);
	mem_free(txt);
	return txt1;
}


/* goto bookmark (called when goto button is pressed) */
static void bookmark_goto_item(struct session *ses, struct list *i)
{
	struct bookmark_list *item = get_struct(i, struct bookmark_list, head);

	goto_url_utf8(ses, item->url);
}


/* delete bookmark from list */
static void bookmark_delete_item(struct list *data)
{
	struct bookmark_list *item = get_struct(data, struct bookmark_list, head);

	if (item->head.list_entry.next) del_from_list(&item->head);
	mem_free(item->url);
	mem_free(item->title);
	mem_free(item);
}

static int substr_utf8(unsigned char *string, unsigned char *substr)
{
	int r;
	string = unicode_upcase_string(string);
	substr = unicode_upcase_string(substr);
	r = !!strstr(cast_const_char string, cast_const_char substr);
	mem_free(string);
	mem_free(substr);
	return r;
}

static int test_entry(struct list *e, unsigned char *str)
{
	struct bookmark_list *list = get_struct(e, struct bookmark_list, head);
	if (substr_utf8(list->title, str))
		return 1;
#ifdef SEARCH_IN_URL
	return casestrstr(list->url, str);
#else
	return 0;
#endif
}

static struct list *bookmark_find_item(struct list *s, unsigned char *str, int direction)
{
	struct list *e;

	if (direction >= 0) {
		for (e = list_next(s); e != s; e = list_next(e)) {
			if (e->depth >= 0 && test_entry(e, str))
				return e;
		}
	} else {
		for (e = list_prev(s); e != s; e = list_prev(e)) {
			if (e->depth >= 0 && test_entry(e, str))
				return e;
		}
	}

	if (e->depth >= 0 && test_entry(e, str))
		return e;

	return NULL;
}


/* returns previous item in the same folder and with same the depth, or father if there's no previous item */
/* we suppose that previous items have correct pointer fotr */
static struct list *previous_on_this_level(struct list *item)
{
	struct list *p;

	for (p = list_prev(item); p->depth > item->depth; p = p->fotr)
		;
	return p;
}


/* create new bookmark at the end of the list */
/* if url is NULL, create folder */
/* both strings are null terminated */
static void add_bookmark(unsigned char *title, unsigned char *url, int depth)
{
	struct bookmark_list *b;
	struct list *p;
	struct document_options *dop;

	if (!title) return;

	b = mem_alloc(sizeof(struct bookmark_list));

	dop = mem_calloc(sizeof(struct document_options));
	dop->cp = bookmark_ld.codepage;

	b->title = convert(bookmarks_codepage, bookmark_ld.codepage, title, dop);
	clr_white(b->title);

	if (url) {
		b->url = convert(bookmarks_codepage, bookmark_ld.codepage, url, NULL);
		clr_white(b->url);
		b->head.type = 0;
	} else {
		b->url = stracpy(cast_uchar "");
		b->head.type = 1;
	}

	b->head.depth = depth;

	add_to_list_end(bookmarks.list_entry, &b->head);

	p = previous_on_this_level(&b->head);
	if (p->depth < b->head.depth) b->head.fotr = p;   /* directory b belongs into */
	else b->head.fotr = p->fotr;

	mem_free(dop);
}

/* Created pre-cooked bookmarks */
static void create_initial_bookmarks(void)
{
	add_bookmark(cast_uchar "Links", NULL, 0);
	add_bookmark(cast_uchar "English", NULL, 1);
	add_bookmark(cast_uchar "Calibration Procedure", cast_uchar "http://links.twibright.com/calibration.html", 2);
	add_bookmark(cast_uchar "Links Homepage", cast_uchar "http://links.twibright.com/", 2);
	add_bookmark(cast_uchar "Links Manual", cast_uchar "http://links.twibright.com/user_en.html", 2);
	add_bookmark(cast_uchar "Cesky", NULL, 1);
	add_bookmark(cast_uchar "Kalibracni procedura", cast_uchar "http://links.twibright.com/kalibrace.html", 2);
	add_bookmark(cast_uchar "Links: domaci stranka", cast_uchar "http://links.twibright.com/index_cz.php", 2);
	add_bookmark(cast_uchar "Manual k Linksu", cast_uchar "http://links.twibright.com/user.html", 2);
}

static void load_bookmarks(struct session *ses)
{
	unsigned char *buf;
	long len;

	unsigned char *p, *end;
	unsigned char *name, *attr;
	int namelen;
	int status;
	unsigned char *title = 0;
	unsigned char *url = 0;
	int depth;

	struct document_options dop;
	int rs;

	memset(&dop, 0, sizeof(dop));
	dop.plain = 1;

	/* status:
	 *		0 = find <dt> or </dl> element
	 *		1 = find <a> or <h3> element
	 *		2 = reading bookmark, find </a> element, title is pointer
	 *		    behind the leading <a> element
	 *		3 = reading folder name, find </h3> element, title is
	 *		pointer behind leading <h3> element
	 */

	buf = read_config_file(bookmarks_file);
	if (!buf) {
		create_initial_bookmarks();
		bookmark_ld.modified = 1;
		save_bookmarks(ses);
		return;
	}

	len = strlen(cast_const_char buf);

	p = buf;
	end = buf + len;

	status = 0;  /* find bookmark */
	depth = 0;

	d_opt = &dop;
	while (1) {
		unsigned char *s;

		while (p < end && *p != '<') p++;  /* find start of html tag */
		if (p >= end) break;   /* parse end */
		s = p;
		if (end - p >= 2 && (p[1] == '!' || p[1]== '?')) { p = skip_comment(p, end); continue; }
		if (parse_element(p, end, &name, &namelen, &attr, &p)) { p++; continue; }

		switch (status) {
			case 0:  /* <dt> or </dl> */
			if (namelen == 2 && !casecmp(name, cast_uchar "dt", 2))
				status = 1;
			else if (namelen == 3 && !casecmp(name, cast_uchar "/dl", 3)) {
				depth--;
				if (depth == -1) goto smitec;
			}
			continue;

			case 1:   /* find "a" element */
			if (namelen == 1 && !casecmp(name, cast_uchar "a", 1)) {
				if (!(url = get_attr_val(attr, cast_uchar "href"))) continue;
				status = 2;
				title = p;
			} else if (namelen == 2 && !casecmp(name, cast_uchar "h3", 1)) {
				status = 3;
				title = p;
			}
			continue;

			case 2:   /* find "/a" element */
			if (namelen != 2 || casecmp(name, cast_uchar "/a", 2)) continue;   /* ignore all other elements */
			*s = 0;
			add_bookmark(title, url, depth);
			mem_free(url);
			status = 0;
			continue;

			case 3:   /* find "/h3" element */
			if (namelen !=3 || casecmp(name, cast_uchar "/h3", 2)) continue;   /* ignore all other elements */
			*s = 0;
			add_bookmark(title, NULL, depth);
			status = 0;
			depth++;
			continue;
		}
	}
	if (status == 2) mem_free(url);
smitec:
	mem_free(buf);
	d_opt = &dd_opt;
	bookmark_ld.modified = 0;

	EINTRLOOP(rs, stat(cast_const_char bookmarks_file, &bookmarks_st));
	if (rs)
		memset(&bookmarks_st, -1, sizeof bookmarks_st);
}

void init_bookmarks(void)
{
	memset(&bookmarks_st, -1, sizeof bookmarks_st);
	if (!*bookmarks_file) {
		unsigned char *e;
		safe_strncpy(bookmarks_file, links_home ? links_home : (unsigned char*)"", MAX_STR_LEN);
		e = cast_uchar strchr(cast_const_char bookmarks_file, 0);
#ifdef DOS_FS_8_3
		safe_strncpy(e, cast_uchar "bookmark.htm", MAX_STR_LEN - (e - bookmarks_file));
#else
		safe_strncpy(e, cast_uchar "bookmarks.html", MAX_STR_LEN - (e - bookmarks_file));
#endif
	}

	bookmark_ld.codepage = utf8_table;
	load_bookmarks(NULL);
}

void reinit_bookmarks(struct session *ses, unsigned char *new_bookmarks_file, int new_bookmarks_codepage)
{
	unsigned char *buf;
	if (test_list_window_in_use(&bookmark_ld, ses->term))
		return;

	if (!strcmp(cast_const_char bookmarks_file, cast_const_char new_bookmarks_file)) {
		goto save_only;
	}

	buf = read_config_file(new_bookmarks_file);
	if (buf) {
		mem_free(buf);
		free_bookmarks();
		safe_strncpy(bookmarks_file, new_bookmarks_file, MAX_STR_LEN);
		bookmarks_codepage = new_bookmarks_codepage;
		load_bookmarks(ses);
		reinit_list_window(&bookmark_ld);
	} else {
		save_only:
		safe_strncpy(bookmarks_file, new_bookmarks_file, MAX_STR_LEN);
		bookmarks_codepage = new_bookmarks_codepage;
		bookmark_ld.modified = 1;
		save_bookmarks(ses);
	}
}


/* gets str, converts all < = > & to appropriate entity
 * returns allocated string with result
 */
static unsigned char *convert_to_entity_string(unsigned char *str)
{
	unsigned char *dst, *p;
	int dstl;

	dst = init_str();
	dstl = 0;

	for (p = str; *p; p++) {
		switch(*p) {
			case '<':
				add_to_str(&dst, &dstl, cast_uchar "&lt;");
				break;
			case '>':
				add_to_str(&dst, &dstl, cast_uchar "&gt;");
				break;

			case '=':
				add_to_str(&dst, &dstl, cast_uchar "&equals;");
				break;

			case '&':
				add_to_str(&dst, &dstl, cast_uchar "&amp;");
				break;

			case '"':
				add_to_str(&dst, &dstl, cast_uchar "&quot;");
				break;

			default:
				add_chr_to_str(&dst, &dstl, *p);
				break;
		}
	}
	return dst;
}

/* writes bookmarks to disk */
static void save_bookmarks(struct session *ses)
{
	struct list *li;
	struct list_head *lli;
	int depth;
	int a;
	unsigned char *data;
	int l;
	int err;
	int rs;

	if (!bookmark_ld.modified)return;
	data = init_str();
	l = 0;
	add_to_str(&data, &l, cast_uchar
	"<HTML>\n"
	"<HEAD>\n"
	"<!-- This is an automatically generated file.\n"
	"It will be read and overwritten.\n"
	"Do Not Edit! -->\n"
	"<TITLE>Links bookmarks</TITLE>\n"
	"</HEAD>\n"
	"<H1>Links bookmarks</H1>\n\n"
	"<DL><P>\n"
	);
	depth = 0;
	foreach(struct list, li, lli, bookmarks.list_entry) {
		struct bookmark_list *b = get_struct(li, struct bookmark_list, head);
		for (a = b->head.depth; a < depth; a++) add_to_str(&data, &l, cast_uchar "</DL>\n");
		depth = b->head.depth;

		if (b->head.type & 1) {
			unsigned char *txt, *txt1;
			txt = convert(bookmark_ld.codepage, bookmarks_codepage, b->title, NULL);
			clr_white(txt);
			txt1 = convert_to_entity_string(txt);
			add_to_str(&data, &l, cast_uchar "    <DT><H3>");
			add_to_str(&data, &l, txt1);
			add_to_str(&data, &l, cast_uchar "</H3>\n<DL>\n");
			mem_free(txt);
			mem_free(txt1);
			depth++;
		} else {
			unsigned char *txt1, *txt2, *txt11;
			txt1 = convert(bookmark_ld.codepage, bookmarks_codepage, b->title, NULL);
			clr_white(txt1);
			txt2 = convert(bookmark_ld.codepage, bookmarks_codepage, b->url, NULL);
			clr_white(txt2);
			txt11 = convert_to_entity_string(txt1);
			add_to_str(&data, &l, cast_uchar "    <DT><A HREF=\"");
			add_to_str(&data, &l, txt2);
			add_to_str(&data, &l, cast_uchar "\">");
			add_to_str(&data, &l, txt11);
			add_to_str(&data, &l, cast_uchar "</A>\n");
			mem_free(txt1);
			mem_free(txt2);
			mem_free(txt11);
		}
	}
	for (a = 0; a <depth; a++) add_to_str(&data, &l, cast_uchar "</DL>\n");
	add_to_str(&data, &l, cast_uchar
	"</DL><P>\n"
	"</HTML>\n"
	);
	err = write_to_config_file(bookmarks_file, data, 1);
	mem_free(data);
	if (!err) {
		bookmark_ld.modified = 0;
	} else {
		if (ses) {
			unsigned char *f = stracpy(bookmarks_file);
			msg_box(ses->term, getml(f, NULL), TEXT_(T_BOOKMARK_ERROR), AL_CENTER, TEXT_(T_UNABLE_TO_WRITE_TO_BOOKMARK_FILE), cast_uchar " ", f, cast_uchar ": ", get_err_msg(err, ses->term), MSG_BOX_END, NULL, 1, TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC);
		}
	}

	EINTRLOOP(rs, stat(cast_const_char bookmarks_file, &bookmarks_st));
	if (rs)
		memset(&bookmarks_st, -1, sizeof bookmarks_st);
}

void menu_bookmark_manager(struct terminal *term, void *fcp, void *ses_)
{
	struct session *ses = (struct session *)ses_;
	struct stat st;
	int rs;
	EINTRLOOP(rs, stat(cast_const_char bookmarks_file, &st));
	if (!rs &&
	    (st.st_ctime != bookmarks_st.st_ctime ||
	     st.st_mtime != bookmarks_st.st_mtime ||
	     st.st_size != bookmarks_st.st_size)) {
		if (!test_list_window_in_use(&bookmark_ld, NULL)) {
			free_bookmarks();
			load_bookmarks(ses);
			reinit_list_window(&bookmark_ld);
		}
	}
	create_list_window(&bookmark_ld, &bookmarks, term, ses);
}
