/* view_gr.c
 * (c) 2002 Mikulas Patocka
 * This file is a part of the Links program, released under GPL.
 */

#include "cfg.h"

#ifdef G

#include "links.h"

static int *highlight_positions = NULL;
static int *highlight_lengths = NULL;
static int n_highlight_positions = 0;

static int root_x = 0;
static int root_y = 0;

static void get_object_pos(struct g_object *o, int *x, int *y);
static void g_get_search_data(struct f_data *f);
static struct g_object_text * g_find_nearest_object(struct f_data *f, int x, int y);

static int previous_link=-1;	/* for mouse event handlers */

void g_dummy_draw(struct f_data_c *fd, struct g_object *t, int x, int y)
{
}

void g_tag_destruct(struct g_object *t_)
{
	struct g_object_tag *t = get_struct(t_, struct g_object_tag,  go);
	mem_free(t);
}

void g_dummy_mouse(struct f_data_c *fd, struct g_object *a, int x, int y, int b)
{
}

static unsigned char print_all_textarea = 0;


/* returns byte index of x in t->text */
/* x is relative coordinate within the text (can be out of bounds) */
static int g_find_text_pos(struct g_object_text *t, int x)
{
	int i=0, p=0;
	unsigned char *text=t->text;
	int ox, oy;

	get_object_pos(&t->goti.go, &ox, &oy);
	x -= ox;

	if (x < 0) x = 0;
	if (x > t->goti.go.xw) x = t->goti.go.xw;

	while (1) {
		unsigned c;
		unsigned char *old_text;
		int w;

		old_text = text;
		if (!*text) break;
		GET_UTF_8(text, c);
		w = g_char_width(t->style, c);
		if (x < (p + (w >> 1))) break;
		p += w;
		i += (int)(text - old_text);
		if (p >= x) break;
	}
	return i;
}

static int g_text_no_search(struct f_data *f, struct g_object_text *t)
{
	struct link *l;
	if (t->goti.link_num < 0) return 0;
	l = f->links + t->goti.link_num;
	if (l->type == L_SELECT || l->type == L_FIELD || l->type == L_AREA) return 1;
	return 0;
}

static int prepare_input_field_char(unsigned char *p, unsigned char tx[7])
{
	unsigned char *pp = p;
	unsigned un;
	unsigned char *en;
	GET_UTF_8(p, un);
	if (!un) un = '*';
	if (un == 0xad) un = '-';
	en = encode_utf_8(un);
	strcpy(cast_char tx, cast_const_char en);
	return (int)(p - pp);
}

void g_text_draw(struct f_data_c *fd, struct g_object *t_, int x, int y)
{
	struct g_object_text *t = get_struct(t_, struct g_object_text, goti.go);
	struct form_control *form;
	struct form_state *fs;
	struct link *link;
	int l;
	int ll;
	int i, j;
	int yy;
	int cur;
	struct format_text_cache_entry *ftce;
	struct graphics_device *dev = fd->ses->term->dev;

	if (x + t->goti.go.xw <= fd->ses->term->dev->clip.x1)
		return;
	if (x >= fd->ses->term->dev->clip.x2)
		return;
	if (!print_all_textarea) {
		if (y + t->goti.go.yw <= fd->ses->term->dev->clip.y1)
			return;
		if (y >= fd->ses->term->dev->clip.y2)
			return;
	}

	link = t->goti.link_num >= 0 ? fd->f_data->links + t->goti.link_num : NULL;
	if (link && ((form = link->form))) {
		fs = find_form_state(fd, form);
		switch (form->type) {
			struct style *inv;
			int in, lid;
			int sl, td;
			case FC_RADIO:
				if (link && fd->active && fd->vs->g_display_link && fd->vs->current_link == link - fd->f_data->links) inv = g_invert_style(t->style), in = 1;
				else inv = t->style, in = 0;
				g_print_text(dev, x, y, inv, fs->state ? cast_uchar "[X]" : cast_uchar "[ ]", NULL);
				if (in) g_free_style(inv);
				return;
			case FC_CHECKBOX:
				if (link && fd->active && fd->vs->g_display_link && fd->vs->current_link == link - fd->f_data->links) inv = g_invert_style(t->style), in = 1;
				else inv = t->style, in = 0;
				g_print_text(dev, x, y, inv, fs->state ? cast_uchar "[X]" : cast_uchar "[ ]", NULL);
				if (in) g_free_style(inv);
				return;
			case FC_SELECT:
				if (link && fd->active && fd->vs->g_display_link && fd->vs->current_link == link - fd->f_data->links) inv = g_invert_style(t->style), in = 1;
				else inv = t->style, in = 0;
				fixup_select_state(form, fs);
				l = 0;
				if (fs->state < form->nvalues) g_print_text(dev, x, y, inv, form->labels[fs->state], &l);
				while (l < t->goti.go.xw) g_print_text(dev, x + l, y, inv, cast_uchar "_", &l);
				if (in) g_free_style(inv);
				return;
			case FC_TEXT:
			case FC_PASSWORD:
			case FC_FILE_UPLOAD:
				if ((size_t)fs->vpos > strlen(cast_const_char fs->string)) fs->vpos = (int)strlen(cast_const_char fs->string);
				sl = (int)strlen(cast_const_char fs->string);
				td = textptr_diff(fs->string + fs->state, fs->string + fs->vpos, fd->f_data->opt.cp);
				while (fs->vpos < sl && td >= form->size) {
					unsigned char *p = fs->string + fs->vpos;
					FWD_UTF_8(p);
					fs->vpos = (int)(p - fs->string);
					td--;
				}
				while (fs->vpos > fs->state) {
					unsigned char *p = fs->string + fs->vpos;
					BACK_UTF_8(p, fs->string);
					fs->vpos = (int)(p - fs->string);
				}
				l = 0;
				i = 0;
				ll = (int)strlen(cast_const_char fs->string);
				while (l < t->goti.go.xw) {
					struct style *st = t->style;
					int sm = 0;
					unsigned char tx[7];
					if (fs->state == fs->vpos + i && t->goti.link_num == fd->vs->current_link && fd->ses->locked_link) {
						st = g_invert_style(t->style);
						sm = 1;
					}
					if (fs->vpos + i >= ll) {
						tx[0] = '_', tx[1] = 0, i++;
					} else {
						i += prepare_input_field_char(fs->string + fs->vpos + i, tx);
						if (form->type == FC_PASSWORD) tx[0] = '*', tx[1] = 0;
					}
					g_print_text(dev, x + l, y, st, tx, &l);
					if (sm) g_free_style(st);
				}
				return;
			case FC_TEXTAREA:
				cur = area_cursor(fd, form, fs);
				ftce = format_text(fd, form, fs);

				yy = y - t->goti.link_order * t->style->height;
				lid = fs->vypos;
				for (j = 0; j < form->rows; j++) {
					unsigned char *pp, *en;
					int remaining_chars;
					int xx = fs->vpos;
					if (lid < ftce->n_lines) {
						pp = fs->string + ftce->ln[lid].st_offs;
						en = fs->string + ftce->ln[lid].en_offs;
						remaining_chars = ftce->ln[lid].chars;
					} else {
						pp = en = NULL;
						remaining_chars = 0;
					}
					while (pp && pp < en && xx > 0) {
						FWD_UTF_8(pp);
						xx--;
						remaining_chars--;
					}
					if (cur >= 0 && cur < form->cols && t->goti.link_num == fd->vs->current_link && fd->ses->locked_link && fd->active) {
						unsigned char tx[7];
						int xx = x;

						if (print_all_textarea || j == t->goti.link_order) while (xx < x + t->goti.go.xw) {
							struct style *st = t->style;
							if (pp && pp < en) {
								pp += prepare_input_field_char(pp, tx);
							} else {
								tx[0] = '_';
								tx[1] = 0;
							}
							if (!cur) {
								st = g_invert_style(t->style);
							}
							g_print_text(dev, xx, yy + j * t->style->height, st, tx, &xx);
							if (!cur) {
								g_free_style(st);
							}
							cur--;
						} else cur -= form->cols;
					} else {
						if (print_all_textarea || j == t->goti.link_order) {
							unsigned char *a;
							struct rect old;
							size_t text_size;
							if (remaining_chars <= form->cols)
								remaining_chars = form->cols - remaining_chars;
							else
								remaining_chars = 0;
							text_size = pp ? en - pp : 0;
							a = mem_alloc(text_size + remaining_chars + 1);
							if (text_size) memcpy(a, pp, text_size);
							memset(a + text_size, '_', remaining_chars);
							a[text_size + remaining_chars] = 0;
							restrict_clip_area(dev, &old, x, 0, x + t->goti.go.xw, dev->size.y2);
							g_print_text(dev, x, yy + j * t->style->height, t->style, a, NULL);
							set_clip_area(dev, &old);
							mem_free(a);
						}
						cur -= form->cols;
					}
					if (lid < ftce->n_lines) lid++;
				}
				return;
		}
	}
	if (link && fd->active && fd->vs->g_display_link && fd->vs->current_link == link - fd->f_data->links) {
		struct style *inv;
		inv = g_invert_style(t->style);
		g_print_text(dev, x, y, inv, t->text, NULL);
		g_free_style(inv);
	} else if ((!fd->f_data->hlt_len && (!highlight_positions || !n_highlight_positions)) || g_text_no_search(fd->f_data, t)) {
		prn:
		g_print_text(dev, x, y, t->style, t->text, NULL);
	} else {
		int tlen = (int)strlen(cast_const_char t->text);
		int found;
		int start = t->srch_pos;
		int end = t->srch_pos + tlen;
		int hl_start, hl_len;
		unsigned char *mask;
		unsigned char *tx;
		int txl;
		int pmask;
		int ii;
		struct style *inv;

		intersect(fd->f_data->hlt_pos, fd->f_data->hlt_len, start, tlen, &hl_start, &hl_len);

#define B_EQUAL(t, m) (highlight_positions[t] + highlight_lengths[t] > start && highlight_positions[t] < end)
#define B_ABOVE(t, m) (highlight_positions[t] >= end)
		BIN_SEARCH(n_highlight_positions, B_EQUAL, B_ABOVE, *, found);
		mask = mem_calloc(tlen);
		if (found != -1) {
			while (found > 0 && B_EQUAL(found - 1, *)) found--;
			while (found < n_highlight_positions && !B_ABOVE(found, *)) {
				int pos = highlight_positions[found] - t->srch_pos;
				for (ii = 0; ii < highlight_lengths[found]; ii++) {
					if (pos >= 0 && pos < tlen) mask[pos] = 1;
					pos++;
				}
				found++;
			}
			if (hl_len) goto hl;
		}
		else if (hl_len)
		{
			int x;
			hl:
			for (x = 0; x < hl_len; x++) mask[hl_start - t->srch_pos + x] ^= 1;
			/*memset(mask+hl_start-t->srch_pos, 1, hl_len);*/
		}
		else
		{
			mem_free(mask);
			goto prn;
		}

		inv = g_invert_style(t->style);
		tx = init_str();;
		txl = 0;
		pmask = -1;
		for (ii = 0; ii < tlen; ii++) {
			if (mask[ii] != pmask) {
				g_print_text(dev, x, y, pmask ? inv : t->style, tx, &x);
				mem_free(tx);
				tx = init_str();
				txl = 0;
			}
			add_chr_to_str(&tx, &txl, t->text[ii]);
			pmask = mask[ii];
		}
		g_print_text(dev, x, y, pmask ? inv : t->style, tx, &x);
		mem_free(tx);
		g_free_style(inv);
		mem_free(mask);
	}
}

void g_text_destruct(struct g_object *t_)
{
	struct g_object_text *t = get_struct(t_, struct g_object_text, goti.go);
	release_image_map(t->goti.map);
	g_free_style(t->style);
	mem_free(t);
}

void g_line_draw(struct f_data_c *fd, struct g_object *l_, int xx, int yy)
{
	struct g_object_line *l = get_struct(l_, struct g_object_line, go);
	struct graphics_device *dev = fd->ses->term->dev;
	int i;
	int x = 0;
	for (i = 0; i < l->n_entries; i++) {
		struct g_object *o = l->entries[i];
		if (o->x > x) g_draw_background(dev, l->bg, xx + x, yy, o->x - x, l->go.yw);
		if (o->y > 0) g_draw_background(dev, l->bg, xx + o->x, yy, o->xw, o->y);
		if (o->y + o->yw < l->go.yw) g_draw_background(dev, l->bg, xx + o->x, yy + o->y + o->yw, o->xw, l->go.yw - o->y - o->yw);
		o->draw(fd, o, xx + o->x, yy + o->y);
		x = o->x + o->xw;
	}
	if (x < l->go.xw) g_draw_background(dev, l->bg, xx + x, yy, l->go.xw - x, l->go.yw);
}

void g_line_destruct(struct g_object *l_)
{
	struct g_object_line *l = get_struct(l_, struct g_object_line, go);
	int i;
	for (i = 0; i < l->n_entries; i++) l->entries[i]->destruct(l->entries[i]);
	mem_free(l);
}

void g_line_bg_destruct(struct g_object *l_)
{
	struct g_object_line *l = get_struct(l_, struct g_object_line, go);
	g_release_background(l->bg);
	g_line_destruct(&l->go);
}

void g_line_get_list(struct g_object *l_, void (*f)(struct g_object *parent, struct g_object *child))
{
	struct g_object_line *l = get_struct(l_, struct g_object_line, go);
	int i;
	for (i = 0; i < l->n_entries; i++) f(&l->go, l->entries[i]);
}

#define OBJ_EQ(n, b)	(*a[n]).go.y <= (b) && (*a[n]).go.y + (*a[n]).go.yw > (b)
#define OBJ_ABOVE(n, b)	(*a[n]).go.y > (b)

static inline struct g_object_line **g_find_line(struct g_object_line **a, int n, int p)
{
	int res = -1;
	BIN_SEARCH(n, OBJ_EQ, OBJ_ABOVE, p, res);
	if (res == -1) return NULL;
	return &a[res];
}

#undef OBJ_EQ
#undef OBJ_ABOVE

void g_area_draw(struct f_data_c *fd, struct g_object *a_, int xx, int yy)
{
	struct g_object_area *a = get_struct(a_, struct g_object_area, go);
	struct g_object_line **i;
	int rx = root_x, ry = root_y;
	int y1 = fd->ses->term->dev->clip.y1 - yy;
	int y2 = fd->ses->term->dev->clip.y2 - yy - 1;
	struct g_object_line **l1;
	struct g_object_line **l2;
	if (fd->ses->term->dev->clip.y1 == fd->ses->term->dev->clip.y2 || fd->ses->term->dev->clip.x1 == fd->ses->term->dev->clip.x2) return;
	l1 = g_find_line(a->lines, a->n_lines, y1);
	l2 = g_find_line(a->lines, a->n_lines, y2);
	root_x = xx, root_y = yy;
	if (!l1) {
		if (y1 > a->go.yw) return;
		else l1 = &a->lines[0];
	}
	if (!l2) {
		if (y2 < 0) return;
		else l2 = &a->lines[a->n_lines - 1];
	}
	for (i = l1; i <= l2; i++) {
		struct g_object *o = &(*i)->go;
		o->draw(fd, o, xx + o->x, yy + o->y);
	}
	root_x = rx, root_y = ry;
}

void g_area_destruct(struct g_object *a_)
{
	struct g_object_area *a = get_struct(a_, struct g_object_area, go);
	int i;
	g_release_background(a->bg);
	for (i = 0; i < a->n_lines; i++) {
		struct g_object *o = &a->lines[i]->go;
		o->destruct(o);
	}
	mem_free(a);
}

void g_area_get_list(struct g_object *a_, void (*f)(struct g_object *parent, struct g_object *child))
{
	struct g_object_area *a = get_struct(a_, struct g_object_area, go);
	int i;
	for (i = 0; i < a->n_lines; i++) f(&a->go, &a->lines[i]->go);
}

/*
 * dsize - size of scrollbar
 * total - total data
 * vsize - visible data
 * vpos - position of visible data
 */

void get_scrollbar_pos(int dsize, int total, int vsize, int vpos, int *start, int *end)
{
	int ssize;
	if (!total) {
		*start = *end = 0;
		return;
	}
	ssize = (int)((double)dsize * vsize / total);
	if (ssize < G_SCROLL_BAR_MIN_SIZE) ssize = G_SCROLL_BAR_MIN_SIZE;
	if (total == vsize) {
		*start = 0; *end = dsize;
		return;
	}
	*start = (int)((double)(dsize - ssize) * vpos / (total - vsize) + 0.5);
	*end = *start + ssize;
	if (*start > dsize) *start = dsize;
	if (*start < 0) *start = 0;
	if (*end > dsize) *end = dsize;
	if (*end < 0) *end = 0;
	/*
	else {
		*start = (double)vpos * dsize / total;
		*end = (double)(vpos + vsize) * dsize / total;
	}
	if (*end > dsize) *end = dsize;
	*/
}

static long scroll_bar_frame_color;
static long scroll_bar_area_color;
static long scroll_bar_bar_color;

void draw_vscroll_bar(struct graphics_device *dev, int x, int y, int yw, int total, int view, int pos)
{
	int spos, epos;
	drv->draw_hline(dev, x, y, x + G_SCROLL_BAR_WIDTH, scroll_bar_frame_color);
	drv->draw_vline(dev, x, y, y + yw, scroll_bar_frame_color);
	drv->draw_vline(dev, x + G_SCROLL_BAR_WIDTH - 1, y, y + yw, scroll_bar_frame_color);
	drv->draw_hline(dev, x, y + yw - 1, x + G_SCROLL_BAR_WIDTH, scroll_bar_frame_color);
	drv->draw_vline(dev, x + 1, y + 1, y + yw - 1, scroll_bar_area_color);
	drv->draw_vline(dev, x + G_SCROLL_BAR_WIDTH - 2, y + 1, y + yw - 1, scroll_bar_area_color);
	get_scrollbar_pos(yw - 4, total, view, pos, &spos, &epos);
	drv->fill_area(dev, x + 2, y + 1, x + G_SCROLL_BAR_WIDTH - 2, y + 2 + spos, scroll_bar_area_color);
	drv->fill_area(dev, x + 2, y + 2 + spos, x + G_SCROLL_BAR_WIDTH - 2, y + 2 + epos, scroll_bar_bar_color);
	drv->fill_area(dev, x + 2, y + 2 + epos, x + G_SCROLL_BAR_WIDTH - 2, y + yw - 1, scroll_bar_area_color);
}

void draw_hscroll_bar(struct graphics_device *dev, int x, int y, int xw, int total, int view, int pos)
{
	int spos, epos;
	drv->draw_vline(dev, x, y, y + G_SCROLL_BAR_WIDTH, scroll_bar_frame_color);
	drv->draw_hline(dev, x, y, x + xw, scroll_bar_frame_color);
	drv->draw_hline(dev, x, y + G_SCROLL_BAR_WIDTH - 1, x + xw, scroll_bar_frame_color);
	drv->draw_vline(dev, x + xw - 1, y, y + G_SCROLL_BAR_WIDTH, scroll_bar_frame_color);
	drv->draw_hline(dev, x + 1, y + 1, x + xw - 1, scroll_bar_area_color);
	drv->draw_hline(dev, x + 1, y + G_SCROLL_BAR_WIDTH - 2, x + xw - 1, scroll_bar_area_color);
	get_scrollbar_pos(xw - 4, total, view, pos, &spos, &epos);
	drv->fill_area(dev, x + 1, y + 2, x + 2 + spos, y + G_SCROLL_BAR_WIDTH - 2, scroll_bar_area_color);
	drv->fill_area(dev, x + 2 + spos, y + 2, x + 2 + epos, y + G_SCROLL_BAR_WIDTH - 2, scroll_bar_bar_color);
	drv->fill_area(dev, x + 2 + epos, y + 2, x + xw - 1, y + G_SCROLL_BAR_WIDTH - 2, scroll_bar_area_color);
}

static void g_get_search(struct f_data *f, unsigned char *s)
{
	int i;
	if (!s || !*s) return;
	if (f->last_search && !strcmp(cast_const_char f->last_search, cast_const_char s)) return;
	mem_free(f->search_positions);
	mem_free(f->search_lengths);
	f->search_positions = DUMMY, f->search_lengths = DUMMY, f->n_search_positions = 0;
	if (f->last_search) mem_free(f->last_search);
	if (!(f->last_search = stracpy(s))) return;
	for (i = 0; i < f->srch_string_size; i++) {
		int len;
		/*debug("%d: %d", i, f->srch_string[i]);*/
		if ((s[0] | f->srch_string[i]) < 0x80) {
			if ((f->srch_string[i] ^ s[0]) & 0xdf) continue;
			if (s[1] != 0 && (s[1] ^ f->srch_string[i + 1]) < 0x80) {
				if ((f->srch_string[i + 1] ^ s[1]) & 0xdf) continue;
			}
		}
		len = compare_case_utf8(f->srch_string + i, s);
		if (!len) continue;
		if (!(f->n_search_positions & (ALLOC_GR - 1))) {
			if ((unsigned)f->n_search_positions > MAXINT / sizeof(int) - ALLOC_GR) overalloc();
			f->search_positions = mem_realloc(f->search_positions, (f->n_search_positions + ALLOC_GR) * sizeof(int));
			f->search_lengths = mem_realloc(f->search_lengths, (f->n_search_positions + ALLOC_GR) * sizeof(int));
		}
		f->search_positions[f->n_search_positions] = i;
		f->search_lengths[f->n_search_positions] = len;
		f->n_search_positions++;
	}
}

static void draw_root(struct f_data_c *scr, int x, int y)
{
	scr->f_data->root->draw(scr, scr->f_data->root, x, y);
}

void draw_graphical_doc(struct terminal *t, struct f_data_c *scr, int active)
{
	struct rect old;
	struct view_state *vs = scr->vs;
	struct rect_set *rs;
	int xw = scr->xw;
	int yw = scr->yw;
	int vx, vy;
	int j;

	if (active) {
		if (scr->ses->search_word && scr->ses->search_word[0]) {
			g_get_search_data(scr->f_data);
			g_get_search(scr->f_data, scr->ses->search_word);
			highlight_positions = scr->f_data->search_positions;
			highlight_lengths = scr->f_data->search_lengths;
			n_highlight_positions = scr->f_data->n_search_positions;
		}
	}

	if (vs->view_pos > scr->f_data->y - scr->yw + scr->hsb * G_SCROLL_BAR_WIDTH) vs->view_pos = scr->f_data->y - scr->yw + scr->hsb * G_SCROLL_BAR_WIDTH;
	if (vs->view_pos < 0) vs->view_pos = 0;
	if (vs->view_posx > scr->f_data->x - scr->xw + scr->vsb * G_SCROLL_BAR_WIDTH) vs->view_posx = scr->f_data->x - scr->xw + scr->vsb * G_SCROLL_BAR_WIDTH;
	if (vs->view_posx < 0) vs->view_posx = 0;
	vx = vs->view_posx;
	vy = vs->view_pos;
	restrict_clip_area(t->dev, &old, scr->xp, scr->yp, scr->xp + xw, scr->yp + yw);
	if (scr->vsb) draw_vscroll_bar(t->dev, scr->xp + xw - G_SCROLL_BAR_WIDTH, scr->yp, yw - scr->hsb * G_SCROLL_BAR_WIDTH, scr->f_data->y, yw - scr->hsb * G_SCROLL_BAR_WIDTH, vs->view_pos);
	if (scr->hsb) draw_hscroll_bar(t->dev, scr->xp, scr->yp + yw - G_SCROLL_BAR_WIDTH, xw - scr->vsb * G_SCROLL_BAR_WIDTH, scr->f_data->x, xw - scr->vsb * G_SCROLL_BAR_WIDTH, vs->view_posx);
	if (scr->vsb && scr->hsb) drv->fill_area(t->dev, scr->xp + xw - G_SCROLL_BAR_WIDTH, scr->yp + yw - G_SCROLL_BAR_WIDTH, scr->xp + xw, scr->yp + yw, scroll_bar_frame_color);
	restrict_clip_area(t->dev, NULL, scr->xp, scr->yp, scr->xp + xw - scr->vsb * G_SCROLL_BAR_WIDTH, scr->yp + yw - scr->hsb * G_SCROLL_BAR_WIDTH);
	/*debug("buu: %d %d %d, %d %d %d", scr->xl, vx, xw, scr->yl, vy, yw);*/
	if (drv->flags & GD_DONT_USE_SCROLL && overwrite_instead_of_scroll) goto rrr;
	if (scr->xl == -1 || scr->yl == -1) goto rrr;
	if (is_rect_valid(&scr->ses->win->redr)) goto rrr;
	if (scr->xl - vx > xw || vx - scr->xl > xw ||
	    scr->yl - vy > yw || vy - scr->yl > yw) {
		goto rrr;
	}

	rs = g_scroll(t->dev, scr->xl - vx, scr->yl - vy);
	for (j = 0; j < rs->m; j++) {
		struct rect *r = &rs->r[j];
		struct rect clip1;
		restrict_clip_area(t->dev, &clip1, r->x1, r->y1, r->x2, r->y2);
		draw_root(scr, scr->xp - vs->view_posx, scr->yp - vs->view_pos);
		set_clip_area(t->dev, &clip1);
	}
	mem_free(rs);

	if (0) {
		rrr:
		draw_root(scr, scr->xp - vs->view_posx, scr->yp - vs->view_pos);
	}
	scr->xl = vx;
	scr->yl = vy;
	set_clip_area(t->dev, &old);

	highlight_positions = NULL;
	highlight_lengths = NULL;
	n_highlight_positions = 0;
}

struct draw_data {
	struct f_data_c *fd;
	struct g_object *o;
};

static void draw_one_object_fn(struct terminal *t, void *d_)
{
	struct draw_data *d = (struct draw_data *)d_;
	struct rect clip;
	struct f_data_c *scr = d->fd;
	struct g_object *o = d->o;
	int x, y;
	restrict_clip_area(t->dev, &clip, scr->xp, scr->yp, scr->xp + scr->xw - scr->vsb * G_SCROLL_BAR_WIDTH, scr->yp + scr->yw - scr->hsb * G_SCROLL_BAR_WIDTH);
	get_object_pos(o, &x, &y);
	o->draw(scr, o, scr->xp - scr->vs->view_posx + x, scr->yp - scr->vs->view_pos + y);
	set_clip_area(t->dev, &clip);
}

void draw_one_object(struct f_data_c *scr, struct g_object *o)
{
	struct draw_data d;
	int *h1, *h2, h3;
	d.fd = scr;
	d.o = o;
	h1 = highlight_positions;
	h2 = highlight_lengths;
	h3 = n_highlight_positions;
	if (scr->ses->search_word && scr->ses->search_word[0]) {
		g_get_search_data(scr->f_data);
		g_get_search(scr->f_data, scr->ses->search_word);
		highlight_positions = scr->f_data->search_positions;
		highlight_lengths = scr->f_data->search_lengths;
		n_highlight_positions = scr->f_data->n_search_positions;
	}
	draw_to_window(scr->ses->win, draw_one_object_fn, &d);
	highlight_positions = h1;
	highlight_lengths = h2;
	n_highlight_positions = h3;
}

int g_forward_mouse(struct f_data_c *fd, struct g_object *a, int x, int y, int b)
{
	int r = 0;
	if (x < a->x) r |= 1;
	if (x >= a->x + a->xw) r |= 2;
	if (y < a->y) r |= 4;
	if (y >= a->y + a->yw) r |= 8;
	if (!r) {
		a->mouse_event(fd, a, x - a->x, y - a->y, b);
		return 0;
	}
	return r;
}

void g_area_mouse(struct f_data_c *fd, struct g_object *a_, int x, int y, int b)
{
	struct g_object_area *a = get_struct(a_, struct g_object_area, go);
	int found, g;
#define A_EQ(m, n)	((g = g_forward_mouse(fd, &a->lines[m]->go, x, y, b)), !g)
#define A_AB(m, n)	(g & 4)
	BIN_SEARCH(a->n_lines, A_EQ, A_AB, *, found);
	found = found + 1;	/* against warning */
#undef A_EQ
#undef A_AB
	/*int i;
	for (i = 0; i < a->n_lines; i++) if (!g_forward_mouse(fd, &a->lines[i]->go, x, y, b)) return;*/
}

void g_line_mouse(struct f_data_c *fd, struct g_object *a_, int x, int y, int b)
{
	struct g_object_line *a = get_struct(a_, struct g_object_line, go);
	int found, g;
#define A_EQ(m, n)	((g = g_forward_mouse(fd, a->entries[m], x, y, b)), !g)
#define A_AB(m, n)	(g & 1)
	BIN_SEARCH(a->n_entries, A_EQ, A_AB, *, found);
	found = found + 1;	/* against warning */
#undef A_EQ
#undef A_AB
	/*int i;
	for (i = 0; i < a->n_entries; i++) if (!g_forward_mouse(fd, a->entries[i], x, y, b)) return;*/
}

static struct f_data *ffff;

static void get_parents_sub(struct g_object *p, struct g_object *c)
{
	c->parent = p;
	if (c->get_list) c->get_list(c, get_parents_sub);
	if (c->destruct == g_tag_destruct) {
		struct g_object_tag *tg = get_struct(c, struct g_object_tag, go);
		int x = 0, y = 0;
		struct g_object *o;
		c->y -= c->parent->yw;
		for (o = c; o; o = o->parent) x += o->x, y += o->y;
		html_tag(ffff, tg->name, x, y);
	}
	if (c->mouse_event == g_text_mouse) {
		struct g_object_text_image *tc = get_struct(c, struct g_object_text_image, go);
		int l = tc->link_num;
		if (l >= 0) {
			struct link *link = &ffff->links[l];
			int x = 0, y = 0;
			struct g_object *o;
			for (o = c; o; o = o->parent) x += o->x, y += o->y;
			if (x < link->r.x1) link->r.x1 = x;
			if (y < link->r.y1) link->r.y1 = y;
			if (x + c->xw > link->r.x2) link->r.x2 = x + c->xw;
			if (y + c->yw > link->r.y2) link->r.y2 = y + c->yw;
			link->obj = c;
		}
	}
}

void get_parents(struct f_data *f, struct g_object *a)
{
	ffff = f;
	a->parent = NULL;
	if (a->get_list) a->get_list(a, get_parents_sub);
}

static void get_object_pos(struct g_object *o, int *x, int *y)
{
	*x = *y = 0;
	while (o) {
		*x += o->x;
		*y += o->y;
		o = o->parent;
	}
}

/* if set_position is 1 sets cursor position in FIELD/AREA elements */
static void g_set_current_link(struct f_data_c *fd, struct g_object_text_image *a, int x, int y, int set_position)
{
	if (a->map) {
		int i;
		for (i = 0; i < a->map->n_areas; i++) {
			if (is_in_area(&a->map->area[i], x, y) && a->map->area[i].link_num >= 0) {
				fd->vs->current_link = a->map->area[i].link_num;
				fd->vs->orig_link = fd->vs->current_link;
				return;
			}
		}
	}
	fd->vs->current_link = -1;
	fd->vs->orig_link = fd->vs->current_link;
	if (a->link_num >= 0) {
		fd->vs->current_link = a->link_num;
		fd->vs->orig_link = fd->vs->current_link;
		/* if link is a field, set cursor position */
		if (set_position && a->link_num >= 0 && a->link_num < fd->f_data->nlinks) { /* valid link */
			struct link *l = &fd->f_data->links[a->link_num];
			struct form_state *fs;
			int xx, yy;

			if (!l->form) return;
			if (l->type == L_AREA) {
				struct g_object_text *at = get_struct(a, struct g_object_text, goti);
				struct format_text_cache_entry *ftce;
				fs = find_form_state(fd,l->form);

				if (g_char_width(at->style, ' '))
					xx = x / g_char_width(at->style, ' ');
				else
					xx = x;
				xx += fs->vpos;
				xx = xx < 0 ? 0 : xx;
				yy = a->link_order;
				yy += fs->vypos;
				ftce = format_text(fd, l->form, fs);
				if (yy >= ftce->n_lines)
					yy = ftce->n_lines - 1;
				if (yy >= 0) {
					int bla = textptr_diff(fs->string + ftce->ln[yy].en_offs, fs->string + ftce->ln[yy].st_offs, fd->f_data->opt.cp);

					fs->state = ftce->ln[yy].st_offs;
					fs->state = (int)(textptr_add(fs->string + fs->state, xx < bla ? xx : bla, fd->f_data->opt.cp) - fs->string);
				}
				return;
			}
			if (l->type == L_FIELD) {
				struct g_object_text *at = get_struct(a, struct g_object_text, goti);
				fs = find_form_state(fd, l->form);
				if (g_char_width(at->style, ' '))
					xx = x / g_char_width(at->style, ' ');
				else
					xx = x;
				fs->state = (int)(textptr_add(fs->string + ((size_t)fs->vpos > strlen(cast_const_char fs->string) ? strlen(cast_const_char fs->string) : (size_t)fs->vpos), (xx < 0 ? 0 : xx), fd->f_data->opt.cp) - fs->string);
			}
		}
	}
}

void g_text_mouse(struct f_data_c *fd, struct g_object *a_, int x, int y, int b)
{
	struct g_object_text_image *a = get_struct(a_, struct g_object_text_image, go);
	int e;
	g_set_current_link(fd, a, x, y, (b == (B_UP | B_LEFT)));

#ifdef JS
	if (fd->vs && fd->f_data && fd->vs->current_link >= 0 && fd->vs->current_link < fd->f_data->nlinks) {
		/* fd->vs->current links is a valid link */

		struct link *l = &fd->f_data->links[fd->vs->current_link];

		if (l->js_event && l->js_event->up_code && (b & BM_ACT) == B_UP)
			jsint_execute_code(fd, l->js_event->up_code, strlen(cast_const_char l->js_event->up_code), -1, -1, -1, NULL);

		if (l->js_event && l->js_event->down_code && (b & BM_ACT) == B_DOWN)
			jsint_execute_code(fd, l->js_event->down_code, strlen(cast_const_char l->js_event->down_code), -1, -1, -1, NULL);
	}
#endif

	if (b == (B_UP | B_LEFT)) {
		int ix = ismap_x, iy = ismap_y, il = ismap_link;
		ismap_x = x;
		ismap_y = y;
		ismap_link = a->ismap;
		e = enter(fd->ses, fd, 1);
		ismap_x = ix;
		ismap_y = iy;
		ismap_link = il;
		if (e) {
			print_all_textarea = 1;
			draw_one_object(fd, &a->go);
			print_all_textarea = 0;
		}
		if (e == 2) fd->f_data->locked_on = &a->go;
		return;
	}
	if (b == (B_UP | B_RIGHT)) {
		if (fd->vs->current_link != -1) link_menu(fd->ses->term, NULL, fd->ses);
	}
}

static int horizontal_page_jump(struct f_data_c *fd)
{
	int j = fd->xw - fd->vsb * G_SCROLL_BAR_WIDTH;
	if (j <= 0) j = 1;
	return j;
}

static int vertical_page_jump(struct f_data_c *fd)
{
	int j = fd->yw - fd->hsb * G_SCROLL_BAR_WIDTH;
	if (j >= fd->ses->ds.font_size * 2) j -= fd->ses->ds.font_size;
	if (j <= 0) j = 1;
	return j;
}

static void process_sb_event(struct f_data_c *fd, int off, int h)
{
	int spos, epos;
	int w = h ? fd->hsbsize : fd->vsbsize;
	get_scrollbar_pos(w - 4, h ? fd->f_data->x : fd->f_data->y, w, h ? fd->vs->view_posx : fd->vs->view_pos, &spos, &epos);
	spos += 2;
	epos += 2;
	/*debug("%d %d %d", spos, epos, off);*/
	if (off >= spos && off < epos) {
		fd->ses->scrolling = 1;
		fd->ses->scrolltype = h;
		fd->ses->scrolloff = off - spos;
		return;
	}
	if (off < spos) {
		if (h) fd->vs->view_posx -= horizontal_page_jump(fd);
		else fd->vs->view_pos -= vertical_page_jump(fd);
	} else {
		if (h) fd->vs->view_posx += horizontal_page_jump(fd);
		else fd->vs->view_pos += vertical_page_jump(fd);
	}
	fd->vs->orig_view_pos = fd->vs->view_pos;
	fd->vs->orig_view_posx = fd->vs->view_posx;
	draw_graphical_doc(fd->ses->term, fd, 1);
}

static void process_sb_move(struct f_data_c *fd, int off)
{
	int h = fd->ses->scrolltype;
	int w = h ? fd->hsbsize : fd->vsbsize;
	int rpos = off - 2 - fd->ses->scrolloff;
	int st, en;
	int new_val;
	get_scrollbar_pos(w - 4, h ? fd->f_data->x : fd->f_data->y, w, h ? fd->vs->view_posx : fd->vs->view_pos, &st, &en);
	if (en - st >= w - 4) return;
	/*
	*(h ? &fd->vs->view_posx : &fd->vs->view_pos) = rpos * (h ? fd->f_data->x : fd->f_data->y) / (w - 4);
	*/
	if (w - 4 - (en - st) <= 0) return;
	new_val = (int)(rpos * (double)(h ? fd->f_data->x - w : fd->f_data->y - w) / (w - 4 - (en - st)) + 0.5);
	*(h ? &fd->vs->view_posx : &fd->vs->view_pos) = new_val;
	fd->vs->orig_view_pos = fd->vs->view_pos;
	fd->vs->orig_view_posx = fd->vs->view_posx;
	draw_graphical_doc(fd->ses->term, fd, 1);
}

static inline int ev_in_rect(struct links_event *ev, int x1, int y1, int x2, int y2)
{
	return ev->x >= x1 && ev->y >= y1 && ev->x < x2 && ev->y < y2;
}

int is_link_in_view(struct f_data_c *fd, int nl)
{
	struct link *l = &fd->f_data->links[nl];
	return fd->vs->view_pos < l->r.y2 && fd->vs->view_pos + fd->yw - fd->hsb * G_SCROLL_BAR_WIDTH > l->r.y1;
}

static int skip_link(struct f_data_c *fd, int nl)
{
	struct link *l = &fd->f_data->links[nl];
	return !l->where && !l->form;
}

static void redraw_link(struct f_data_c *fd, int nl)
{
	struct link *l = &fd->f_data->links[nl];
	struct rect r;
	memcpy(&r, &l->r, sizeof(struct rect));
	r.x1 += fd->xp - fd->vs->view_posx;
	r.x2 += fd->xp - fd->vs->view_posx;
	r.y1 += fd->yp - fd->vs->view_pos;
	r.y2 += fd->yp - fd->vs->view_pos;
	t_redraw(fd->ses->term->dev, &r);
}

static int lr_link(struct f_data_c *fd, int nl)
{
	struct link *l = &fd->f_data->links[nl];
	int xx = fd->vs->view_posx;
	if (l->r.x2 > fd->vs->view_posx + fd->xw - fd->vsb * G_SCROLL_BAR_WIDTH) fd->vs->view_posx = l->r.x2 - (fd->xw - fd->vsb * G_SCROLL_BAR_WIDTH);
	if (l->r.x1 < fd->vs->view_posx) fd->vs->view_posx = l->r.x1;
	fd->vs->orig_view_posx = fd->vs->view_posx;
	return xx != fd->vs->view_posx;
}

int g_next_link(struct f_data_c *fd, int dir, int do_scroll)
{
	int orig_link = -1;
	int r = 2;
	int n, pn;
	if (fd->vs->current_link >= 0 && fd->vs->current_link < fd->f_data->nlinks) {
		orig_link = fd->vs->current_link;
		n = (pn = fd->vs->current_link) + dir;
	} else retry: n = dir > 0 ? 0 : fd->f_data->nlinks - 1, pn = -1;
	again:
	if (n < 0 || n >= fd->f_data->nlinks) {
		if (!do_scroll)
			return 0;
		if (r == 1) {
			fd->vs->current_link = -1;
			if (fd->vs->view_pos > fd->f_data->y - fd->yw + fd->hsb * G_SCROLL_BAR_WIDTH) fd->vs->view_pos = fd->f_data->y - fd->yw + fd->hsb * G_SCROLL_BAR_WIDTH;
			if (fd->vs->view_pos < 0) fd->vs->view_pos = 0;
			if (orig_link != -1 && is_link_in_view(fd, orig_link)) fd->vs->current_link = orig_link;
			fd->vs->orig_link = fd->vs->current_link;
			if (fd->vs->current_link == -1) fd->ses->locked_link = 0;
			return 1;
		}
		if (dir < 0) {
			if (!fd->vs->view_pos) {
				fd->vs->orig_view_pos = fd->vs->view_pos;
				return 0;
			}
			fd->vs->view_pos -= vertical_page_jump(fd);
			fd->vs->orig_view_pos = fd->vs->view_pos;
		} else {
			if (fd->vs->view_pos >= fd->f_data->y - fd->yw + fd->hsb * G_SCROLL_BAR_WIDTH) return 0;
			fd->vs->view_pos += vertical_page_jump(fd);
			fd->vs->orig_view_pos = fd->vs->view_pos;
		}
		r = 1;
		goto retry;
	}
	if (!is_link_in_view(fd, n) || skip_link(fd, n)) {
		n += dir;
		goto again;
	}
	if (fd->vs->current_link >= 0 && fd->vs->current_link < fd->f_data->nlinks) {
		redraw_link(fd, fd->vs->current_link);
	}
	fd->vs->current_link = n;
	fd->vs->orig_link = fd->vs->current_link;
	fd->vs->g_display_link = 1;
	redraw_link(fd, n);
	fd->ses->locked_link = 0;
	if (fd->f_data->links[fd->vs->current_link].type == L_FIELD || fd->f_data->links[fd->vs->current_link].type == L_AREA) {
		if ((fd->f_data->locked_on = fd->f_data->links[fd->vs->current_link].obj)) fd->ses->locked_link = 1;
	}
	set_textarea(fd->ses, fd, -dir);
	change_screen_status(fd->ses);
	print_screen_status(fd->ses);
	if (lr_link(fd, fd->vs->current_link)) r = 1;
	return r;
}

static void unset_link(struct f_data_c *fd)
{
	int n = fd->vs->current_link;
	fd->vs->current_link = -1;
	fd->vs->orig_link = fd->vs->current_link;
	fd->vs->g_display_link = 0;
	fd->ses->locked_link = 0;
	if (n >= 0 && n < fd->f_data->nlinks) {
		redraw_link(fd, n);
	}
}

static int scroll_vh(int *vp, int *ovp, int *sc, int d, int limit)
{
	int o = *vp;
	*vp += d;
	if (*vp > limit)
		*vp = limit;
	if (*vp < 0)
		*vp = 0;
	*ovp = *vp;
	o -= *vp;
	if (sc) *sc -= o;
	return o ? 3 : 0;
}

static int scroll_v(struct f_data_c *fd, int y)
{
	return scroll_vh(&fd->vs->view_pos, &fd->vs->orig_view_pos, fd->ses->scrolling == 2 ? &fd->ses->scrolloff : NULL, y, fd->f_data->y - fd->yw + fd->hsb * G_SCROLL_BAR_WIDTH);
}

static int scroll_h(struct f_data_c *fd, int x)
{
	return scroll_vh(&fd->vs->view_posx, &fd->vs->orig_view_posx, fd->ses->scrolling == 2 ? &fd->ses->scrolltype : NULL, x, fd->f_data->x - fd->xw + fd->vsb * G_SCROLL_BAR_WIDTH);
}

int g_frame_ev(struct session *ses, struct f_data_c *fd, struct links_event *ev)
{
	if (!fd->f_data) return 0;
	switch ((int)ev->ev) {
		case EV_MOUSE:
			if (BM_IS_WHEEL(ev->b) && ses->locked_link) {
				if (fd->vs->current_link >= 0 && fd->vs->current_link < fd->f_data->nlinks && fd->f_data->links[fd->vs->current_link].type == L_AREA) {
					if (field_op(ses, fd, &fd->f_data->links[fd->vs->current_link], ev)) {
						if (fd->f_data->locked_on) {
							print_all_textarea = 1;
							draw_one_object(fd, fd->f_data->locked_on);
							print_all_textarea = 0;
							return 2;
						}
					}
				}
				return 1;
			}
			if ((ev->b & BM_BUTT) == B_WHEELUP)
				return scroll_v(fd, -64);
			if ((ev->b & BM_BUTT) == B_WHEELDOWN)
				return scroll_v(fd, 64);
			if ((ev->b & BM_BUTT) == B_WHEELUP1)
				return scroll_v(fd, -16);
			if ((ev->b & BM_BUTT) == B_WHEELDOWN1)
				return scroll_v(fd, 16);
			if ((ev->b & BM_BUTT) == B_WHEELLEFT)
				return scroll_h(fd, -64);
			if ((ev->b & BM_BUTT) == B_WHEELRIGHT)
				return scroll_h(fd, 64);
			if ((ev->b & BM_BUTT) == B_WHEELLEFT1)
				return scroll_h(fd, -16);
			if ((ev->b & BM_BUTT) == B_WHEELRIGHT1)
				return scroll_h(fd, 16);
			if ((ev->b & BM_ACT) == B_MOVE) ses->scrolling = 0;
			if (ses->scrolling == 1) process_sb_move(fd, ses->scrolltype ? ev->x : ev->y);
			if (ses->scrolling == 2) {
				fd->vs->view_pos = -ev->y + ses->scrolloff;
				fd->vs->view_posx = -ev->x + ses->scrolltype;
				fd->vs->orig_view_pos = fd->vs->view_pos;
				fd->vs->orig_view_posx = fd->vs->view_posx;
				draw_graphical_doc(ses->term, fd, 1);
				if ((ev->b & BM_ACT) == B_UP) {
					ses->scrolling = 0;
				}
				break;
			}
			if (ses->scrolling) {
				if ((ev->b & BM_ACT) == B_UP) {
					ses->scrolling = 0;
				}
				break;
			}

			if ((ev->b & BM_ACT) == B_DOWN && fd->vsb && ev_in_rect(ev, fd->xw - G_SCROLL_BAR_WIDTH, 0, fd->xw, fd->yw - fd->hsb * G_SCROLL_BAR_WIDTH)) {
				process_sb_event(fd, ev->y, 0);
				break;
			}
			if ((ev->b & BM_ACT) == B_DOWN && fd->hsb && ev_in_rect(ev, 0, fd->yw - G_SCROLL_BAR_WIDTH, fd->xw - fd->vsb * G_SCROLL_BAR_WIDTH, fd->yw)) {
				process_sb_event(fd, ev->x, 1);
				break;
			}
			if (fd->vsb && ev_in_rect(ev, fd->xw - G_SCROLL_BAR_WIDTH, 0, fd->xw, fd->yw)) return 0;
			if (fd->hsb && ev_in_rect(ev, 0, fd->yw - G_SCROLL_BAR_WIDTH, fd->xw, fd->yw)) return 0;

			if ((ev->b & BM_ACT) == B_DOWN && (ev->b & BM_BUTT) == B_MIDDLE) {
				scrll:
				ses->scrolltype = ev->x + fd->vs->view_posx;
				ses->scrolloff = ev->y + fd->vs->view_pos;
				ses->scrolling = 2;
				break;
			}

			previous_link=fd->vs->current_link;
			if (fd->vs->g_display_link) {
				fd->vs->g_display_link = 0;
				if (fd->vs->current_link >= 0 && fd->vs->current_link < fd->f_data->nlinks) redraw_link(fd, fd->vs->current_link);
			}
			if (!(ev->b == (B_LEFT | B_UP) && fd->f_data->hlt_len && fd->f_data->start_highlight_x == -1)) {
				fd->vs->current_link = -1;
				fd->vs->orig_link = fd->vs->current_link;
				fd->f_data->root->mouse_event(fd, fd->f_data->root, ev->x + fd->vs->view_posx, ev->y + fd->vs->view_pos, (int)ev->b);
				if (previous_link!=fd->vs->current_link)
					change_screen_status(ses);
				print_screen_status(ses);
			}

			/* highlight text */
			if ((ev->b & BM_ACT) == B_DOWN && (ev->b & BM_BUTT) == B_LEFT) {   /* start highlighting */
				int need_redraw = !!fd->f_data->hlt_len;
				fd->f_data->start_highlight_x = ev->x;
				fd->f_data->start_highlight_y = ev->y;
				fd->f_data->hlt_len = 0;
				fd->f_data->hlt_pos = -1;
				return need_redraw;
			}
			if (((ev->b & BM_ACT) == B_DRAG || (ev->b & BM_ACT) == B_UP) && (ev->b & BM_BUTT) == B_LEFT) {	/* stop highlighting */
				struct g_object_text *t;
				if (fd->f_data->start_highlight_x != -1) {
					if (abs(ev->x - fd->f_data->start_highlight_x) < 8 && abs(ev->y - fd->f_data->start_highlight_y) < 8) goto skip_hl;
					t = g_find_nearest_object(fd->f_data, fd->f_data->start_highlight_x + fd->vs->view_posx, fd->f_data->start_highlight_y + fd->vs->view_pos);

					if (t) {
						g_get_search_data(fd->f_data);
						fd->f_data->hlt_pos = t->srch_pos+g_find_text_pos(t, fd->f_data->start_highlight_x+fd->vs->view_posx);
						fd->f_data->hlt_len=0;
					}
					fd->f_data->start_highlight_x = -1;
					fd->f_data->start_highlight_y = -1;
				}
				if (fd->f_data->hlt_pos == -1) goto skip_hl;
				t = g_find_nearest_object(fd->f_data, ev->x + fd->vs->view_posx, ev->y + fd->vs->view_pos);

				if (t) {
					int end;
					g_get_search_data(fd->f_data);
					end = t->srch_pos + g_find_text_pos(t, ev->x+fd->vs->view_posx);
					fd->f_data->hlt_len = end-fd->f_data->hlt_pos;
					if ((ev->b & BM_ACT) == B_UP || (ev->b & BM_ACT) == B_DRAG) {
						unsigned char *m = memacpy(fd->f_data->srch_string + fd->f_data->hlt_pos + (fd->f_data->hlt_len > 0 ? 0 : fd->f_data->hlt_len), fd->f_data->hlt_len > 0 ? fd->f_data->hlt_len : -fd->f_data->hlt_len);
						if (m) {
							unsigned char *p = m;
							while ((p = cast_uchar strchr(cast_const_char p, 1))) *p++ = ' ';
							p = m;
							while ((p = cast_uchar strstr(cast_const_char p, "\302\255"))) memmove(p, p + 2, strlen(cast_const_char(p + 2)) + 1);
							if (*m) set_clipboard_text(ses->term, m);
							mem_free(m);
						}
					}
					return 1;
				}
			}
			skip_hl:
			if (((ev->b & BM_ACT) == B_MOVE || (ev->b & BM_ACT) == B_UP) && (ev->b & BM_BUTT) == B_LEFT) {	/* stop highlighting */
				fd->f_data->start_highlight_x = -1;
				fd->f_data->start_highlight_y = -1;
			}

#ifdef JS
			/* process onmouseover/onmouseout handlers */
			if (previous_link!=fd->vs->current_link)
			{
				struct link* lnk=NULL;

			if (previous_link>=0&&previous_link<fd->f_data->nlinks)lnk=&(fd->f_data->links[previous_link]);
				if (lnk&&lnk->js_event&&lnk->js_event->out_code)
					jsint_execute_code(fd,lnk->js_event->out_code,strlen(cast_const_char lnk->js_event->out_code),-1,-1,-1, NULL);
				lnk=NULL;
				if (fd->vs->current_link>=0&&fd->vs->current_link<fd->f_data->nlinks)lnk=&(fd->f_data->links[fd->vs->current_link]);
				if (lnk&&lnk->js_event&&lnk->js_event->over_code)
					jsint_execute_code(fd,lnk->js_event->over_code,strlen(cast_const_char lnk->js_event->over_code),-1,-1,-1, NULL);
			}
#endif

			if ((ev->b & BM_ACT) == B_DOWN && (ev->b & BM_BUTT) == B_RIGHT && fd->vs->current_link == -1) goto scrll;
			break;
		case EV_KBD:
			if (ses->locked_link && fd->vs->current_link >= 0 && fd->vs->current_link < fd->f_data->nlinks && (fd->f_data->links[fd->vs->current_link].type == L_FIELD || fd->f_data->links[fd->vs->current_link].type == L_AREA)) {
				if (field_op(ses, fd, &fd->f_data->links[fd->vs->current_link], ev)) {
					if (fd->f_data->locked_on) {
						print_all_textarea = 1;
						draw_one_object(fd, fd->f_data->locked_on);
						print_all_textarea = 0;
						return 2;
					}
					return 1;
				}
				if (ev->x == KBD_ENTER && !(ev->y & KBD_PASTING)) {
					return enter(ses, fd, 0);
				}
			}
			if (ev->y & KBD_PASTING)
				return 0;
			if (ev->x == KBD_ENTER && fd->f_data->opt.plain == 2) {
				ses->ds.porn_enable ^= 1;
				html_interpret_recursive(ses->screen);
				return 1;
			}
			if (ev->x == KBD_RIGHT || ev->x == KBD_ENTER) {
				struct link *l;
				if (fd->vs->current_link >= 0 && fd->vs->current_link < fd->f_data->nlinks) {
					l = &fd->f_data->links[fd->vs->current_link];
					set_window_ptr(ses->win, fd->xp + l->r.x1 - fd->vs->view_posx, fd->yp + l->r.y1 - fd->vs->view_pos);
				} else {
					set_window_ptr(ses->win, fd->xp, fd->yp);
				}
				return enter(ses, fd, 0);
			}
			if (ev->x == '*') {
				ses->ds.display_images ^= 1;
				html_interpret_recursive(ses->screen);
				return 1;
			}
			if (ev->x == KBD_PAGE_DOWN || (ev->x == ' ' && !(ev->y & KBD_ALT)) || (upcase(ev->x) == 'F' && ev->y & KBD_CTRL)) {
				unset_link(fd);
				return scroll_v(fd, vertical_page_jump(fd));
			}
			if (ev->x == KBD_PAGE_UP || (upcase(ev->x) == 'B' && !(ev->y & KBD_ALT))) {
				unset_link(fd);
				return scroll_v(fd, -vertical_page_jump(fd));
			}
			if (ev->x == KBD_DEL || (upcase(ev->x) == 'N' && ev->y & KBD_CTRL) || (ev->x == 'l' && !(ev->y & (KBD_CTRL | KBD_ALT)))) {
				return scroll_v(fd, 32);
			}
			if (ev->x == KBD_INS || (upcase(ev->x) == 'P' && ev->y & KBD_CTRL) || (ev->x == 'p' && !(ev->y & (KBD_CTRL | KBD_ALT)))) {
				return scroll_v(fd, -32);
			}
			if (ev->x == KBD_DOWN) {
				return g_next_link(fd, 1, 1);
			}
			if (ev->x == KBD_UP) {
				return g_next_link(fd, -1, 1);
			}
			if (ev->x == 'H' && !(ev->y & (KBD_CTRL | KBD_ALT))) {
				unset_link(fd);
				return g_next_link(fd, 1, 0);
			}
			if (ev->x == 'L' && !(ev->y & (KBD_CTRL | KBD_ALT))) {
				unset_link(fd);
				return g_next_link(fd, -1, 0);
			}
			if (ev->x == '[') {
				return scroll_h(fd, -64);
			}
			if (ev->x == ']') {
				return scroll_h(fd, 64);
			}
			if (ev->x == KBD_HOME || (upcase(ev->x) == 'A' && ev->y & KBD_CTRL)) {
				fd->vs->view_pos = 0;
				fd->vs->orig_view_pos = fd->vs->view_pos;
				unset_link(fd);
				return 3;
			}
			if (ev->x == KBD_END || (upcase(ev->x) == 'E' && ev->y & KBD_CTRL)) {
				fd->vs->view_pos = fd->f_data->y;
				fd->vs->orig_view_pos = fd->vs->view_pos;
				unset_link(fd);
				return 3;
			}
			if ((upcase(ev->x) == 'F' && !(ev->y & (KBD_ALT | KBD_CTRL))) || ev->x == KBD_FRONT) {
				set_frame(ses, fd, 0);
				return 2;
			}
			if (ev->x == '#') {
				ses->ds.images ^= 1;
				html_interpret_recursive(fd);
				ses->ds.images ^= 1;
				return 1;
			}
			if (ev->x == 'i' && !(ev->y & KBD_ALT)) {
				if (!F || fd->f_data->opt.plain != 2) frm_view_image(ses, fd);
				return 2;
			}
			if (ev->x == 'I' && !(ev->y & KBD_ALT)) {
				if (!anonymous) frm_download_image(ses, fd);
				return 2;
			}
			if (upcase(ev->x) == 'D' && !(ev->y & KBD_ALT)) {
				if (!anonymous) frm_download(ses, fd);
				return 2;
			}
			if (ev->x == '/' || (ev->x == KBD_FIND && !(ev->y & (KBD_SHIFT | KBD_CTRL | KBD_ALT)))) {
				search_dlg(ses, fd, 0);
				return 2;
			}
			if (ev->x == '?' || (ev->x == KBD_FIND && ev->y & (KBD_SHIFT | KBD_CTRL | KBD_ALT))) {
				search_back_dlg(ses, fd, 0);
				return 2;
			}
			if ((ev->x == 'n' && !(ev->y & KBD_ALT)) || ev->x == KBD_REDO) {
				find_next(ses, fd, 0);
				return 2;
			}
			if ((ev->x == 'N' && !(ev->y & KBD_ALT)) || ev->x == KBD_UNDO) {
				find_next_back(ses, fd, 0);
				return 2;
			}
			if (ev->x == KBD_MENU) {
				if (fd->vs->current_link >= 0 && fd->vs->current_link < fd->f_data->nlinks) {
					ses->win->xp = fd->f_data->links[fd->vs->current_link].r.x1 + fd->xp - fd->vs->view_posx;
					ses->win->yp = fd->f_data->links[fd->vs->current_link].r.y2 + fd->yp - fd->vs->view_pos;
					link_menu(ses->term, NULL, ses);
				}
			}
			break;
	}
	return 0;
}

void draw_title(struct f_data_c *f)
{
	int b, z, w;
	struct graphics_device *dev = f->ses->term->dev;
	unsigned char *title = stracpy(!drv->set_title && f->f_data && f->f_data->title && f->f_data->title[0] ? f->f_data->title : NULL);
	if (!title) {
		if (f->rq && f->rq->url)
			title = display_url(f->ses->term, f->rq->url, 1);
		else
			title = stracpy((unsigned char *)"");
	}
	w = g_text_width(bfu_style_bw, title);
	z = 0;
	g_print_text(dev, 0, 0, !proxies.only_proxies ? bfu_style_bw : bfu_style_wb, cast_uchar " " G_LEFT_ARROW " ", &z);
	f->ses->back_size = z;
	b = (dev->size.x2 - w) - 16;
	if (b < z) b = z;
	drv->fill_area(dev, z, 0, b, G_BFU_FONT_SIZE, !proxies.only_proxies ? bfu_bg_color : bfu_fg_color);
	g_print_text(dev, b, 0, !proxies.only_proxies ? bfu_style_bw : bfu_style_wb, title, &b);
	drv->fill_area(dev, b, 0, dev->size.x2, G_BFU_FONT_SIZE, !proxies.only_proxies ? bfu_bg_color : bfu_fg_color);
	mem_free(title);
}

static struct f_data *srch_f_data;

static void get_searched_sub(struct g_object *p, struct g_object *c)
{
	if (c->draw == g_text_draw && !g_text_no_search(srch_f_data, get_struct(c, struct g_object_text, goti.go))) {
		struct g_object_text *t = get_struct(c, struct g_object_text, goti.go);
		int pos = srch_f_data->srch_string_size;
		t->srch_pos = pos;
		add_to_str(&srch_f_data->srch_string, &srch_f_data->srch_string_size, t->text);
	}
	if (c->get_list) c->get_list(c, get_searched_sub);
	if (c->draw == g_line_draw) {
		if (srch_f_data->srch_string_size && srch_f_data->srch_string[srch_f_data->srch_string_size - 1] != ' ')
			add_chr_to_str(&srch_f_data->srch_string, &srch_f_data->srch_string_size, ' ');
	}
}

static void g_get_search_data(struct f_data *f)
{
	int i;
	srch_f_data = f;
	if (f->srch_string) return;
	f->srch_string = init_str();
	f->srch_string_size = 0;
	if (f->root && f->root->get_list) f->root->get_list(f->root, get_searched_sub);
	while (f->srch_string_size && f->srch_string[f->srch_string_size - 1] == ' ') {
		f->srch_string[--f->srch_string_size] = 0;
	}
	for (i = 0; i < f->srch_string_size; i++) if (f->srch_string[i] == 1) f->srch_string[i] = ' ';
}

struct f_data *fnd_f;
static struct g_object_text *fnd_obj;
static int fnd_x, fnd_y;
static int fnd_obj_dist;

/*
#define dist(a,b) (a<b?b-a:a-b)
*/

static inline int dist_to_rect(int x, int y, int x1, int y1, int x2, int y2)
{
	int w;
	if (x < x1) w = x1 - x;
	else if (x > x2) w = x - x2;
	else w = 0;
	if (y < y1) w += y1 - y;
	else if (y > y2) w += y - y2;
	return w;
}

static void find_nearest_sub(struct g_object *p, struct g_object *c)
{
	int tx, ty, a;

	if (!fnd_obj_dist) return;

	get_object_pos(c, &tx, &ty);

	a = dist_to_rect(fnd_x, fnd_y, tx, ty, tx+c->xw, ty+c->yw);

	if (a >= fnd_obj_dist) return;

	if (c->draw == g_text_draw && !g_text_no_search(fnd_f, get_struct(c, struct g_object_text, goti.go))) {

		fnd_obj = get_struct(c, struct g_object_text, goti.go);
		fnd_obj_dist = a;
	}
	if (c->get_list == g_area_get_list) {
		struct g_object_area *ar = get_struct(c, struct g_object_area, go);
		struct g_object_line **ln;
		int idx, i, dist;
		if (!ar->n_lines) return;
		ln = g_find_line(ar->lines, ar->n_lines, fnd_y - ty);
		if (!ln) {
			if (fnd_y < ty) ln = &ar->lines[0];
			else ln = &ar->lines[ar->n_lines - 1];
		}
		idx = (int)(ln - &ar->lines[0]);
		for (i = idx; i < ar->n_lines; i++) {
			dist = dist_to_rect(0, fnd_y, 0, ty + ar->lines[i]->go.y, 0, ty + ar->lines[i]->go.y + ar->lines[i]->go.yw);
			if (dist >= fnd_obj_dist) break;
			find_nearest_sub(NULL, &ar->lines[i]->go);
		}
		for (i = idx - 1; i >= 0; i--) {
			dist = dist_to_rect(0, fnd_y, 0, ty + ar->lines[i]->go.y, 0, ty + ar->lines[i]->go.y + ar->lines[i]->go.yw);
			if (dist >= fnd_obj_dist) break;
			find_nearest_sub(NULL, &ar->lines[i]->go);
		}
		return;
	}
	if (c->get_list) {
		c->get_list(c, find_nearest_sub);
	}
}

static struct g_object_text * g_find_nearest_object(struct f_data *f, int x, int y)
{
	fnd_f = f;
	fnd_obj = NULL;
	fnd_x = x;
	fnd_y = y;
	fnd_obj_dist = MAXINT;

	if (f->root) find_nearest_sub(NULL, f->root);
	return fnd_obj;
}

static unsigned char *search_word;

static int find_refline;
static int find_direction;

static int find_opt_yy;
static int find_opt_y;
static int find_opt_yw;
static int find_opt_x;
static int find_opt_xw;
static struct f_data *find_opt_f_data;

static void find_next_sub(struct g_object *p, struct g_object *c)
{
	if (c->draw == g_text_draw && !g_text_no_search(find_opt_f_data, get_struct(c, struct g_object_text, goti.go))) {
		struct g_object_text *t = get_struct(c, struct g_object_text, goti.go);
		int start = t->srch_pos;
		int end = t->srch_pos + (int)strlen(cast_const_char t->text);
		int found;
		BIN_SEARCH(n_highlight_positions, B_EQUAL, B_ABOVE, *, found);
		if (found != -1) {
			int x, y, yy;
			get_object_pos(c, &x, &y);
			y += t->goti.go.yw / 2;
			yy = y;
			if (yy < find_refline) yy += MAXINT / 2;
			if (find_direction < 0) yy = MAXINT - yy;
			if (find_opt_yy == -1 || yy > find_opt_yy) {
				int sx, ex;
				unsigned char *tt;
				while (found > 0) {
					found--;
					if (B_EQUAL(found, *)) continue;
					found++;
					break;
				}
				find_opt_yy = yy;
				find_opt_y = y;
				find_opt_yw = t->style->height;
				find_opt_x = x;
				find_opt_xw = t->goti.go.xw;
				if (highlight_positions[found] < start) sx = 0;
				else sx = highlight_positions[found] - start;
				if (highlight_positions[found] + highlight_lengths[found] > end) ex = end - start;
				else ex = highlight_positions[found] + highlight_lengths[found] - start;

				tt = memacpy(t->text, sx);
				find_opt_x += g_text_width(t->style, tt);
				mem_free(tt);
				tt = memacpy(t->text + sx, ex - sx);
				find_opt_xw = g_text_width(t->style, tt);
				mem_free(tt);
			}
		}
	}
	if (c->get_list) c->get_list(c, find_next_sub);
}

static void g_find_next_str(struct f_data *f)
{
	find_opt_yy = -1;
	find_opt_f_data = f;
	if (f->root && f->root->get_list) f->root->get_list(f->root, find_next_sub);
}

void g_find_next(struct f_data_c *f, int a)
{
	g_get_search_data(f->f_data);
	g_get_search(f->f_data, f->ses->search_word);
	search_word = f->ses->search_word;
	if (!f->f_data->n_search_positions) msg_box(f->ses->term, NULL, TEXT_(T_SEARCH), AL_CENTER, TEXT_(T_SEARCH_STRING_NOT_FOUND), MSG_BOX_END, NULL, 1, TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC);

	highlight_positions = f->f_data->search_positions;
	highlight_lengths = f->f_data->search_lengths;
	n_highlight_positions = f->f_data->n_search_positions;

	if ((!a && f->ses->search_direction == -1) ||
	     (a && f->ses->search_direction == 1)) find_refline = f->vs->view_pos;
	else find_refline = f->vs->view_pos + f->yw - f->hsb * G_SCROLL_BAR_WIDTH;
	find_direction = -f->ses->search_direction;

	g_find_next_str(f->f_data);

	highlight_positions = NULL;
	highlight_lengths = NULL;
	n_highlight_positions = 0;

	if (find_opt_yy == -1) goto d;
	if (!a || find_opt_y < f->vs->view_pos || find_opt_y + find_opt_yw >= f->vs->view_pos + f->yw - f->hsb * G_SCROLL_BAR_WIDTH) {
		f->vs->view_pos = find_opt_y - (f->yw - f->hsb * G_SCROLL_BAR_WIDTH) / 2;
		f->vs->orig_view_pos = f->vs->view_pos;
	}
	if (find_opt_x < f->vs->view_posx || find_opt_x + find_opt_xw >= f->vs->view_posx + f->xw - f->vsb * G_SCROLL_BAR_WIDTH) {
		f->vs->view_posx = find_opt_x + find_opt_xw / 2 - (f->xw - f->vsb * G_SCROLL_BAR_WIDTH) / 2;
		f->vs->orig_view_posx = f->vs->view_posx;
	}

	d:draw_fd(f);
}

void init_grview(void)
{
#ifdef DEBUG
	int i, w = g_text_width(bfu_style_wb_mono, cast_uchar " ");
	for (i = 32; i < 128; i++) {
		unsigned char a[2];
		a[0] = (unsigned char)i, a[1] = 0;
		if (g_text_width(bfu_style_wb_mono, a) != w) internal_error("Monospaced font is not monospaced (error at char %d, width %d, wanted width %d)", i, (int)g_text_width(bfu_style_wb_mono, a), w);
	}
#endif
	scroll_bar_frame_color = dip_get_color_sRGB(G_SCROLL_BAR_FRAME_COLOR);
	scroll_bar_area_color = dip_get_color_sRGB(G_SCROLL_BAR_AREA_COLOR);
	scroll_bar_bar_color = dip_get_color_sRGB(G_SCROLL_BAR_BAR_COLOR);
}

#endif
