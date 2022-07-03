/* html_gr.c
 * HTML parser in graphics mode
 * (c) 2002 Mikulas Patocka
 * This file is a part of the Links program, released under GPL.
 */

#include "cfg.h"

#ifdef G

#include "links.h"

static int g_nobreak;

static int get_real_font_size(int size)
{
	int fs=d_opt->font_size;

	if (size < 1) size = 1;
	if (size > 7) size = 7;
	switch (size) {
		case 1:	return (14*fs)>>4;
		case 2: return (15*fs)>>4;
		case 3: return (16*fs)>>4;
		case 4: return (19*fs)>>4;
		case 5: return (22*fs)>>4;
		case 6: return (25*fs)>>4;
		case 7: return (28*fs)>>4;
	}
	return 0;
}

struct background *g_get_background(unsigned char *bg, unsigned char *bgcolor)
{
	struct background *b;
	struct rgb r;
	b = mem_alloc(sizeof(struct background));
	b->color = 0;
	b->gamma_stamp = gamma_stamp - 1;
	if (bgcolor && !decode_color(bgcolor, &r)) {
		b->sRGB = (r.r << 16) + (r.g << 8) + r.b;
	} else {
		b->sRGB = (d_opt->default_bg.r << 16) + (d_opt->default_bg.g << 8) + d_opt->default_bg.b;
	}
	return b;
}

void g_release_background(struct background *bg)
{
	mem_free(bg);
}

void g_draw_background(struct graphics_device *dev, struct background *bg, int x, int y, int xw, int yw)
{
	int x2, y2;
	long color;
	if (bg->gamma_stamp == gamma_stamp) {
		color = bg->color;
	} else {
		bg->gamma_stamp = gamma_stamp;
		color = bg->color = dip_get_color_sRGB(bg->sRGB);
	}
	if (test_int_overflow(x, xw, &x2)) x2 = MAXINT;
	if (test_int_overflow(y, yw, &y2)) y2 = MAXINT;
	drv->fill_area(dev, x, y, x2, y2, color);
}

static void g_put_chars(void *, unsigned char *, int);

/* Returns 0 to 2550 */
static int gray (int r, int g, int b)
{
	return r*3+g*6+b;
}

/* Tells if two colors are too near to be legible one on another */
/* 1=too near 0=not too near */
static int too_near(int r1, int g1, int b1, int r2, int g2, int b2)
{
	int diff = abs(r1-r2) * 3 + abs(g1-g2) * 6 + abs(b1-b2);

	return diff < 0x17f;
}

/* Fixes foreground based on background */
static void separate_fg_bg(int *fgr, int *fgg, int *fgb
	, int bgr, int bgg, int bgb)
{
	if (too_near(*fgr, *fgg, *fgb, bgr, bgg, bgb)) {
		*fgr = 255 - bgr;
		*fgg = 255 - bgg;
		*fgb = 255 - bgb;
	} else return;
	if (too_near(*fgr, *fgg, *fgb, bgr, bgg, bgb)) {
		if (gray(bgr, bgg, bgb) <= 1275)
			*fgr = *fgg = *fgb = 255;
		else
			*fgr = *fgg = *fgb = 0;
	}
}

static struct style *get_style_by_ta(struct text_attrib *ta)
{
	int fg_r, fg_g, fg_b; /* sRGB 0-255 values */
	int fs = get_real_font_size(ta->fontsize);
	struct style *stl;
	unsigned fflags;

	fg_r = ta->fg.r;
	fg_g = ta->fg.g;
	fg_b = ta->fg.b;
	separate_fg_bg(&fg_r, &fg_g, &fg_b, ta->bg.r, ta->bg.g, ta->bg.b);

	fflags = 0;
	if (ta->attr & AT_UNDERLINE) fflags |= FF_UNDERLINE;
	if (ta->attr & AT_BOLD) fflags |= FF_BOLD;
	if (ta->attr & AT_ITALIC) fflags |= FF_ITALIC;
	if (ta->attr & AT_FIXED) fflags |= FF_MONOSPACED;

	stl = g_get_style((fg_r << 16) + (fg_g << 8) + fg_b, (ta->bg.r << 16) +
			(ta->bg.g << 8) + ta->bg.b, fs, fflags);
	return stl;
}

#define rm(x) ((x).width - (x).rightmargin * G_HTML_MARGIN > 0 ? (x).width - (x).rightmargin * G_HTML_MARGIN : 0)

void flush_pending_line_to_obj(struct g_part *p, int minheight)
{
	int i, pp, pos, w, lbl;
	struct g_object_line *l = p->line;
	struct g_object_area *a;
	if (!l) {
		return;
	}
	for (i = l->n_entries - 1; i >= 0; i--) {
		struct g_object *go = l->entries[i];
		if (go->draw == g_text_draw) {
			struct g_object_text *got = get_struct(go, struct g_object_text, goti.go);
			int l = (int)strlen(cast_const_char got->text);
			while (l && got->text[l - 1] == ' ') got->text[--l] = 0, got->goti.go.xw -= g_char_width(got->style, ' ');
			if (got->goti.go.xw < 0) internal_error("xw(%d) < 0", got->goti.go.xw);
		}
		if (!go->xw) continue;
		break;
	}
	scan_again:
	pp = 0;
	w = minheight;
	lbl = 0;
	for (i = 0; i < l->n_entries; i++) {
		int yy = l->entries[i]->y;
		if (yy >= G_OBJ_ALIGN_SPECIAL) yy = 0;
		pp = safe_add(pp, l->entries[i]->xw);
		if (l->entries[i]->xw && safe_add(l->entries[i]->yw, yy) > w) w = safe_add(l->entries[i]->yw, yy);
		if (yy < lbl) lbl = yy;
	}
	if (lbl < 0) {
		for (i = 0; i < l->n_entries; i++) {
			if (l->entries[i]->y < G_OBJ_ALIGN_SPECIAL) l->entries[i]->y = safe_add(l->entries[i]->y, -lbl);
		}
		goto scan_again;
	}
	if (par_format.align == AL_CENTER) pos = (safe_add(rm(par_format), par_format.leftmargin * G_HTML_MARGIN) - pp) / 2;
	else if (par_format.align == AL_RIGHT) pos = rm(par_format) - pp;
	else pos = par_format.leftmargin * G_HTML_MARGIN;
	if (pos < par_format.leftmargin * G_HTML_MARGIN) pos = par_format.leftmargin * G_HTML_MARGIN;
	pp = pos;
	for (i = 0; i < l->n_entries; i++) {
		l->entries[i]->x = pp;
		pp = safe_add(pp, l->entries[i]->xw);
		if (l->entries[i]->y < G_OBJ_ALIGN_SPECIAL) {
			l->entries[i]->y = w - l->entries[i]->yw - l->entries[i]->y;
		} else if (l->entries[i]->y == G_OBJ_ALIGN_TOP) {
			l->entries[i]->y = 0;
		} else if (l->entries[i]->y == G_OBJ_ALIGN_MIDDLE) {
			l->entries[i]->y = (w - safe_add(l->entries[i]->yw, 1)) / 2;
		}
	}
	l->go.x = 0;
	l->go.xw = par_format.width;
	l->go.yw = w;
	l->go.y = p->cy;
	a = p->root;
	if (l->go.xw > a->go.xw) a->go.xw = l->go.xw;
	p->cy = safe_add(p->cy, w);
	a->go.yw = p->cy;
	if (!(a->n_lines & (a->n_lines + 1))) {
		if ((unsigned)a->n_lines > ((MAXINT - sizeof(struct g_object_area)) / sizeof(struct g_object_text *) - 1) / 2) overalloc();
		a = mem_realloc(a, sizeof(struct g_object_area) + sizeof(struct g_object_text *) * (a->n_lines * 2 + 1));
		p->root = a;
	}
	a->lines[a->n_lines++] = l;
	p->line = NULL;
	p->w.pos = 0;
	p->w.last_wrap = NULL;
	p->w.last_wrap_obj = NULL;
}

void add_object_to_line(struct g_part *pp, struct g_object_line **lp, struct g_object *go)
{
	struct g_object_line *l;
	if (go && (go->xw < 0 || go->yw < 0)) {
		internal_error("object has negative size: %d,%d", go->xw, go->yw);
		return;
	}
	if (!*lp) {
		l = mem_calloc(sizeof(struct g_object_line) + sizeof(struct g_object_text *));
		l->go.mouse_event = g_line_mouse;
		l->go.draw = g_line_draw;
		l->go.destruct = g_line_destruct;
		l->go.get_list = g_line_get_list;
		/*l->go.x = 0;
		l->go.y = 0;
		l->go.xw = 0;
		l->go.yw = 0;*/
		l->bg = pp->root->bg;
		if (!go) {
			*lp = l;
			return;
		}
		l->n_entries = 1;
	} else {
		if (!go) return;
		(*lp)->n_entries++;
		if ((unsigned)(*lp)->n_entries > (MAXINT - sizeof(struct g_object_line)) / sizeof(struct g_object *)) overalloc();
		l = mem_realloc(*lp, sizeof(struct g_object_line) + sizeof(struct g_object *) * (*lp)->n_entries);
		*lp = l;
	}
	l->entries[l->n_entries - 1] = go;
	*lp = l;
	if (pp->cx == -1) pp->cx = par_format.leftmargin * G_HTML_MARGIN;
	if (go->xw) {
		pp->cx = safe_add(pp->cx, pp->cx_w);
		pp->cx_w = 0;
		pp->cx = safe_add(pp->cx, go->xw);
	}
}

void flush_pending_text_to_line(struct g_part *p)
{
	struct g_object_text *t = p->text;
	if (!t) return;
	add_object_to_line(p, &p->line, &t->goti.go);
	p->text = NULL;
}

static void split_line_object(struct g_part *p, struct g_object *text_go, unsigned char *ptr)
{
	struct g_object_text *text_t = NULL;
	struct g_object_text *t2;
	struct g_object_line *l2;
	size_t sl;
	int n;
	if (!ptr) {
		if (p->line && p->line->n_entries && text_go == p->line->entries[p->line->n_entries - 1]) {
			flush_pending_line_to_obj(p, 0);
			goto wwww;
		}
		t2 = NULL;
		goto nt2;
	}
	text_t = get_struct(text_go, struct g_object_text, goti.go);
	if (par_format.align == AL_NO_BREAKABLE && text_t == p->text && !ptr[strspn(cast_const_char ptr, cast_const_char " ")]) {
		return;
	}
#ifdef DEBUG
	if (ptr < text_t->text || ptr >= text_t->text + strlen(cast_const_char text_t->text))
		internal_error("split_line_object: split point (%p) pointing out of object (%p,%lx)", ptr, text_t->text, (unsigned long)strlen(cast_const_char text_t->text));
#endif
	sl = strlen(cast_const_char ptr);
	if (sl > MAXINT - sizeof(struct g_object_text)) overalloc();
	t2 = mem_calloc(sizeof(struct g_object_text) + sl);
	t2->goti.go.mouse_event = g_text_mouse;
	t2->goti.go.draw = g_text_draw;
	t2->goti.go.destruct = g_text_destruct;
	t2->goti.go.get_list = NULL;
	if (*ptr == ' ') {
		memcpy(t2->text, ptr + 1, sl);
		*ptr = 0;
		/*debug("split: (%s)(%s)", text_t->text, ptr + 1);*/
	} else {
		memcpy(t2->text, ptr, sl + 1);
		ptr[0] = '-';
		ptr[1] = 0;
	}
	t2->goti.go.y = text_t->goti.go.y;
	t2->style = g_clone_style(text_t->style);
	t2->goti.link_num = text_t->goti.link_num;
	t2->goti.link_order = text_t->goti.link_order;
	text_t->goti.go.xw = g_text_width(text_t->style, text_t->text);
	nt2:
	if (p->line) for (n = 0; n < p->line->n_entries; n++) if (p->line->entries[n] == text_go) goto found;
	if (!text_t || text_t != p->text) {
		internal_error("split_line_object: bad wrap");
		return;
	}
	if (0) {
		int nn;
		found:
		n = safe_add(n, !ptr);
		l2 = mem_calloc(sizeof(struct g_object_line) + (p->line->n_entries - n) * sizeof(struct g_object_text *));
		l2->go.mouse_event = g_line_mouse;
		l2->go.draw = g_line_draw;
		l2->go.destruct = g_line_destruct;
		l2->go.get_list = g_line_get_list;
		l2->bg = p->root->bg;
		l2->n_entries = p->line->n_entries - n;
		if (ptr)
			l2->entries[0] = &t2->goti.go;
		memcpy(&l2->entries[!!ptr], p->line->entries + safe_add(n, !!ptr), (l2->n_entries - !!ptr) * sizeof(struct g_object_text *));
		p->line->n_entries = safe_add(n, !!ptr);
		flush_pending_line_to_obj(p, 0);
		p->line = l2;
		if (ptr) {
			t2->goti.go.xw = g_text_width(t2->style, t2->text);
			t2->goti.go.yw = text_t->goti.go.yw;
			p->w.pos = 0;
		}
		for (nn = 0; nn < l2->n_entries; nn++) {
			p->w.pos = safe_add(p->w.pos, l2->entries[nn]->xw);	/* !!! FIXME: nastav last_wrap */
			/*debug("a1: %d (%s)", l2->entries[nn]->xw, tt->text);*/
		}
		wwww:
		if (p->text) {
			int qw = g_text_width(p->text->style, p->text->text);
			p->w.pos = safe_add(p->w.pos, qw);
		}
		/*debug("%d", p->w.pos);*/
	} else {
		flush_pending_text_to_line(p);
		flush_pending_line_to_obj(p, 0);
		p->line = NULL;
		t2->goti.go.xw = g_text_width(t2->style, t2->text);
		t2->goti.go.yw = text_t->goti.go.yw;
		p->text = t2;
		p->pending_text_len = -1;
		p->w.pos = t2->goti.go.xw;
		p->cx_w = g_char_width(t2->style, ' ');
	}
	p->w.last_wrap = NULL;
	p->w.last_wrap_obj = NULL;
	t2 = p->text;
	if (t2) {
		int sl = (int)strlen(cast_const_char t2->text);
		if (sl >= 1 && t2->text[sl - 1] == ' ') {
			p->w.last_wrap = &t2->text[sl - 1];
			p->w.last_wrap_obj = &t2->goti.go;
		} else if (sl >= 2 && t2->text[sl - 2] == 0xc2 && t2->text[sl - 1] == 0xad) {
			p->w.last_wrap = &t2->text[sl - 2];
			p->w.last_wrap_obj = &t2->goti.go;
		}
	}
}

void add_object(struct g_part *p, struct g_object *o)
{
	g_nobreak = 0;
	flush_pending_text_to_line(p);
	p->w.width = rm(par_format) - par_format.leftmargin * G_HTML_MARGIN;
	if (safe_add(p->w.pos, o->xw) > p->w.width) flush_pending_line_to_obj(p, 0);
	add_object_to_line(p, &p->line, o);
	p->w.last_wrap = NULL;
	p->w.last_wrap_obj = o;
	p->w.pos = safe_add(p->w.pos, o->xw);
}

static void g_line_break(void *p_)
{
	struct g_part *p = p_;
	if (g_nobreak) {
		g_nobreak = 0;
		return;
	}
	flush_pending_text_to_line(p);
	if (!p->line || par_format.align == AL_NO || par_format.align == AL_NO_BREAKABLE) {
		add_object_to_line(p, &p->line, NULL);
		empty_line:
		flush_pending_line_to_obj(p, get_real_font_size(format_.fontsize));
	} else {
		int i;
		for (i = 0; i < p->line->n_entries; i++) {
			struct g_object *go = p->line->entries[i];
			if (go->destruct != g_tag_destruct)
				goto flush;
		}
		goto empty_line;
		flush:
		flush_pending_line_to_obj(p, 0);
	}
	if (p->cx > p->xmax) p->xmax = p->cx;
	p->cx = -1;
	p->cx_w = 0;
}

/* SHADOWED IN html_form_control */
static void g_html_form_control(struct g_part *p, struct form_control *fc)
{
	if (!p->data) {
		/*destroy_fc(fc);
		mem_free(fc);*/
		add_to_list(p->uf, fc);
		return;
	}
	fc->g_ctrl_num = g_ctrl_num;
	g_ctrl_num = safe_add(g_ctrl_num, 1);
	if (fc->type == FC_TEXT || fc->type == FC_PASSWORD || fc->type == FC_TEXTAREA) {
		unsigned char *dv = convert_string(convert_table, fc->default_value, (int)strlen(cast_const_char fc->default_value), d_opt);
		if (dv) {
			mem_free(fc->default_value);
			fc->default_value = dv;
		}
		/*
		for (i = 0; i < fc->nvalues; i++) if ((dv = convert_string(convert_table, fc->values[i], strlen(cast_const_char fc->values[i]), d_opt))) {
			mem_free(fc->values[i]);
			fc->values[i] = dv;
		}
		*/
	}
	if (fc->type == FC_TEXTAREA) {
		unsigned char *p;
		for (p = fc->default_value; p[0]; p++) if (p[0] == '\r') {
			if (p[1] == '\n') memmove(p, p + 1, strlen(cast_const_char p)), p--;
			else p[0] = '\n';
		}
	}
	add_to_list(p->data->forms, fc);
}

static struct link **putchars_link_ptr = NULL;

/* Probably releases clickable map */
void release_image_map(struct image_map *map)
{
	int i;
	if (!map) return;
	for (i = 0; i < map->n_areas; i++) mem_free(map->area[i].coords);
	mem_free(map);
}

int is_in_area(struct map_area *a, int x, int y)
{
	int i;
	int over;
	switch (a->shape) {
		case SHAPE_DEFAULT:
			return 1;
		case SHAPE_RECT:
			return a->ncoords >= 4 && x >= a->coords[0] && y >= a->coords[1] && x < a->coords[2] && y < a->coords[3];
		case SHAPE_CIRCLE:
			return a->ncoords >= 3 && (a->coords[0]-x)*(a->coords[0]-x)+(a->coords[1]-y)*(a->coords[1]-y) <= a->coords[2]*a->coords[2];
		case SHAPE_POLY:
			over = 0;
			if (a->ncoords >= 4) for (i = 0; a->ncoords - i > 1; i += 2) {
				int x1, x2, y1, y2;
				x1 = a->coords[i];
				y1 = a->coords[i + 1];
				x2 = a->coords[0];
				y2 = a->coords[1];
				if (a->ncoords - i > 3) {
					x2 = a->coords[i + 2];
					y2 = a->coords[i + 3];
				}
				if (y1 > y2) {
					int sw;
					sw = x1; x1 = x2; x2 = sw;
					sw = y1; y1 = y2; y2 = sw;
				}
				if (y >= y1 && y < y2) {
					int po = 10000 * (y - y1) / (y2 - y1);
					int xs = x1 + (x2 - x1) * po / 10000;
					if (xs >= x) over++;
				}
			}
			return over & 1;
		default:
			internal_error("is_in_area: bad shape: %d", a->shape);
	}
	return 0;
}

/* The size is requested in im->xsize and im->ysize. <0 means
 * not specified. Autoscale is requested in im->autoscale.
 * If autoscale is specified, im->xsize and im->ysize must
 * be >0. */
static void do_image(struct g_part *p, struct image_description *im)
{
	struct g_object_image *io;
	struct link *link;
	link = NULL;
	putchars_link_ptr = &link;
	g_put_chars(p, NULL, 0);
	putchars_link_ptr = NULL;
	if (!link) im->link_num = -1;
	else {
		im->link_num = (int)(link - p->data->links);
		im->link_order = link->obj_order;
		link->obj_order = safe_add(link->obj_order, 1);
		if (link->img_alt) mem_free(link->img_alt);
		link->img_alt = stracpy(im->alt);
	}
	io = insert_image(p, im);
	if (!io) goto ab;
	io->goti.ismap = im->ismap;
	add_object(p, &io->goti.go);
	if (im->usemap && p->data) {
		unsigned char *tag = extract_position(im->usemap);
		struct additional_file *af = request_additional_file(current_f_data, im->usemap);
		af->need_reparse = 1;
		if (af->rq && (af->rq->state == O_LOADING || af->rq->state == O_INCOMPLETE || af->rq->state == O_OK) && af->rq->ce) {
			struct memory_list *ml;
			struct menu_item *menu;
			struct cache_entry *ce = af->rq->ce;
			unsigned char *start;
			size_t len;
			int i;
			struct image_map *map;
			if (get_file(af->rq, &start, &len)) goto ft;
			if (!len) goto ft;
			if (len > MAXINT) len = MAXINT;
			if (get_image_map(ce->head, start, start + len, tag, &menu, &ml, format_.href_base, format_.target_base, 0, 0, 0, 1)) goto ft;
			map = mem_alloc(sizeof(struct image_map));
			map->n_areas = 0;
			for (i = 0; menu[i].text; i++) {
				struct link_def *ld = menu[i].data;
				struct map_area *a;
				struct link *link;
				int shape =
		!ld->shape || !*ld->shape ? SHAPE_RECT :
		!casestrcmp(ld->shape, cast_uchar "default") ? SHAPE_DEFAULT :
		!casestrcmp(ld->shape, cast_uchar "rect") ? SHAPE_RECT :
		!casestrcmp(ld->shape, cast_uchar "circle") ? SHAPE_CIRCLE :
		!casestrcmp(ld->shape, cast_uchar "poly") ||
		!casestrcmp(ld->shape, cast_uchar "polygon") ? SHAPE_POLY : -1;
				if (shape == -1) continue;
				if ((unsigned)map->n_areas > (MAXINT - sizeof(struct image_map)) / sizeof(struct map_area) - 1) overalloc();
				map = mem_realloc(map, sizeof(struct image_map) + (map->n_areas + 1) * sizeof(struct map_area));
				a = &map->area[map->n_areas++];
				a->shape = shape;
				a->coords = DUMMY;
				a->ncoords = 0;
				if (ld->coords) {
					unsigned char *p = ld->coords;
					int num;
					next_coord:
					num = 0;
					while (*p && (*p < '0' || *p > '9')) p++;
					if (!*p) goto noc;
					while (*p >= '0' && *p <= '9' && num < 10000000) num = num * 10 + *p - '0', p++;
					if (*p == '.') {
						p++;
						while (*p >= '0' && *p <= '9') p++;
					}
					if (*p == '%' && num < 1000) {
						int m = io->goti.go.xw < io->goti.go.yw ? io->goti.go.xw : io->goti.go.yw;
						num = num * m / 100;
						p++;
					} else num = num * d_opt->image_scale / 100;
					if ((unsigned)a->ncoords > MAXINT / sizeof(int) - 1) overalloc();
					a->coords = mem_realloc(a->coords, (a->ncoords + 1) * sizeof(int));
					a->coords[a->ncoords++] = num;
					goto next_coord;
				}
				noc:
				if (!(link = new_link(p->data))) a->link_num = -1;
				else {
					link->pos = DUMMY;
					link->type = L_LINK;
					link->where = stracpy(ld->link);
					link->target = stracpy(ld->target);
					link->img_alt = stracpy(ld->label);
					link->where_img = stracpy(im->url);
#ifdef JS
					if (ld->onclick || ld->ondblclick || ld->onmousedown || ld->onmouseup || ld->onmouseover || ld->onmouseout || ld->onmousemove) {
						create_js_event_spec(&link->js_event);
						link->js_event->click_code = stracpy(ld->onclick);
						link->js_event->dbl_code = stracpy(ld->ondblclick);
						link->js_event->down_code = stracpy(ld->onmousedown);
						link->js_event->up_code = stracpy(ld->onmouseup);
						link->js_event->over_code = stracpy(ld->onmouseover);
						link->js_event->out_code = stracpy(ld->onmouseout);
						link->js_event->move_code = stracpy(ld->onmousemove);
					}
#endif
					a->link_num = (int)(link - p->data->links);
				}
				if (last_link) mem_free(last_link), last_link = NULL;
			}
			io->goti.map = map;
			freeml(ml);
			ft:;
		}
		if (tag) mem_free(tag);
	}
	ab:;
}

static void g_hr(struct g_part *gp, struct hr_param *hr)
{
	unsigned char bgstr[8];
	struct g_object_line *o;
	o = mem_calloc(sizeof(struct g_object_line));
	o->go.mouse_event = g_line_mouse;
	o->go.draw = g_line_draw;
	o->go.destruct = g_line_bg_destruct;
	o->go.get_list = g_line_get_list;
	/*o->go.x = 0;
	o->go.y = 0;*/
	o->go.xw = hr->width;
	o->go.yw = hr->size;
	table_bg(&format_, bgstr);
	o->bg = g_get_background(NULL, bgstr);
	o->n_entries = 0;
	flush_pending_text_to_line(gp);
	add_object_to_line(gp, &gp->line, &o->go);
	line_breax = 0;
	gp->cx = -1;
	gp->cx_w = 0;
}


static void *g_html_special(void *p_, int c, ...)
{
	struct g_part *p = p_;
	va_list l;
	unsigned char *t;
	size_t sl;
	struct form_control *fc;
	struct frameset_param *fsp;
	struct frame_param *fp;
	struct image_description *im;
	struct g_object_tag *tag;
	struct refresh_param *rp;
	struct hr_param *hr;
	va_start(l, c);
	switch (c) {
		case SP_TAG:
			t = va_arg(l, unsigned char *);
			va_end(l);
			/* not needed to convert %AB here because html_tag will be called anyway */
			sl = strlen(cast_const_char t);
			if (sl > MAXINT - sizeof(struct g_object_tag)) overalloc();
			tag = mem_calloc(sizeof(struct g_object_tag) + sl);
			tag->go.mouse_event = g_dummy_mouse;
			tag->go.draw = g_dummy_draw;
			tag->go.destruct = g_tag_destruct;
			memcpy(tag->name, t, sl + 1);
			flush_pending_text_to_line(p);
			add_object_to_line(p, &p->line, &tag->go);
			break;
		case SP_CONTROL:
			fc = va_arg(l, struct form_control *);
			va_end(l);
			g_html_form_control(p, fc);
			break;
		case SP_TABLE:
			va_end(l);
			return convert_table;
		case SP_USED:
			va_end(l);
			return (void *)(my_intptr_t)!!p->data;
		case SP_FRAMESET:
			fsp = va_arg(l, struct frameset_param *);
			va_end(l);
			return create_frameset(p->data, fsp);
		case SP_FRAME:
			fp = va_arg(l, struct frame_param *);
			va_end(l);
			create_frame(fp);
			break;
		case SP_SCRIPT:
			t = va_arg(l, unsigned char *);
			va_end(l);
			if (p->data) process_script(p->data, t);
			break;
		case SP_IMAGE:
			im = va_arg(l, struct image_description *);
			va_end(l);
			do_image(p, im);
			break;
		case SP_NOWRAP:
			va_end(l);
			break;
		case SP_REFRESH:
			rp = va_arg(l, struct refresh_param *);
			va_end(l);
			html_process_refresh(p->data, rp->url, rp->time);
			break;
		case SP_SET_BASE:
			t = va_arg(l, unsigned char *);
			va_end(l);
			if (p->data) set_base(p->data, t);
			break;
		case SP_HR:
			hr = va_arg(l, struct hr_param *);
			va_end(l);
			g_hr(p, hr);
			break;
		case SP_FORCE_BREAK:
			if (p->cx > par_format.leftmargin * G_HTML_MARGIN) {
				g_nobreak = 0;
				g_line_break(p);
			}
			break;
		default:
			va_end(l);
			internal_error("html_special: unknown code %d", c);
	}
	return NULL;
}

static const unsigned char to_je_ale_prasarna_[] = "";
#define to_je_ale_prasarna	(cast_uchar to_je_ale_prasarna_)

static unsigned char *cached_font_face = to_je_ale_prasarna;
static struct text_attrib_beginning ta_cache = { -1, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0 };

static void g_put_chars(void *p_, unsigned char *s, int l)
{
	struct g_part *p = p_;
	struct g_object_text *t;
	struct link *link;
	int ptl;
	unsigned char *sh;
	int qw;

	if (l < 0) overalloc();

	while (l > 2 && (sh = memchr(s + 1, 0xad, l - 2)) && sh[-1] == 0xc2) {
		sh++;
		g_put_chars(p_, s, (int)(sh - s));
		l -= (int)(sh - s);
		s = sh;
	}
	while (par_format.align != AL_NO && l >= 2 && (sh = memchr(s, ' ', l - 1))) {
		sh++;
		g_put_chars(p_, s, (int)(sh - s));
		l -= (int)(sh - s);
		s = sh;
	}

	/*fprintf(stderr, "%d: '%.*s'\n", l, l, s);*/

	t = NULL;

	if (putchars_link_ptr) {
		link = NULL;
		goto check_link;
	}
	while (par_format.align != AL_NO && par_format.align != AL_NO_BREAKABLE && p->cx == -1 && l && *s == ' ') s++, l--;
	if (!l) return;
	g_nobreak = 0;
	if (p->cx < par_format.leftmargin * G_HTML_MARGIN) p->cx = par_format.leftmargin * G_HTML_MARGIN;
	if (html_format_changed) {
		if (memcmp(&ta_cache, &format_, sizeof(struct text_attrib_beginning)) || xstrcmp(cached_font_face, format_.fontface) || cached_font_face == to_je_ale_prasarna ||
	xstrcmp(format_.link, last_link) || xstrcmp(format_.target, last_target) ||
	    xstrcmp(format_.image, last_image) || format_.form != last_form
	    || ((format_.js_event || last_js_event) && compare_js_event_spec(format_.js_event, last_js_event))	) {
			/*if (!html_format_changed) internal_error("html_format_changed not set");*/
			flush_pending_text_to_line(p);
			if (xstrcmp(cached_font_face, format_.fontface) || cached_font_face == to_je_ale_prasarna) {
				if (cached_font_face && cached_font_face != to_je_ale_prasarna) mem_free(cached_font_face);
				cached_font_face = stracpy(format_.fontface);
			}
			memcpy(&ta_cache, &format_, sizeof(struct text_attrib_beginning));
			if (p->current_style) g_free_style(p->current_style);
			p->current_style = get_style_by_ta(&format_);
		}
		html_format_changed = 0;
	}
	/*if (p->cx <= par_format.leftmargin * G_HTML_MARGIN && *s == ' ' && par_format.align != AL_NO && par_format.align != AL_NO_BREAKABLE) s++, l--;*/
	if (!p->text) {
		link = NULL;
		t = mem_calloc(sizeof(struct g_object_text) + ALLOC_GR);
		t->goti.go.mouse_event = g_text_mouse;
		t->goti.go.draw = g_text_draw;
		t->goti.go.destruct = g_text_destruct;
		t->goti.go.get_list = NULL;
		t->style = g_clone_style(p->current_style);
		t->goti.go.yw = t->style->height;
		/*t->goti.go.xw = 0;
		t->goti.go.y = 0;*/
		if (format_.baseline) {
			if (format_.baseline < 0) t->goti.go.y = -(t->style->height / 3);
			if (format_.baseline > 0) t->goti.go.y = get_real_font_size(format_.baseline) - (t->style->height / 2);
		}
		check_link:
		if (last_link || last_image || last_form || format_.link || format_.image || format_.form
		|| format_.js_event || last_js_event
		) goto process_link;
		back_link:
		if (putchars_link_ptr) {
			*putchars_link_ptr = link;
			return;
		}

		if (!link) t->goti.link_num = -1;
		else {
			t->goti.link_num = (int)(link - p->data->links);
			t->goti.link_order = link->obj_order++;
		}

		t->text[0] = 0;
		p->pending_text_len = 0;
		p->text = t;
	}
	if (p->pending_text_len == -1) {
		p->pending_text_len = (int)strlen(cast_const_char p->text->text);
		ptl = p->pending_text_len;
		if (!ptl) ptl = 1;
		goto a1;
	}
	ptl = p->pending_text_len;
	if (!ptl) ptl = 1;
	safe_add(safe_add(ptl, l), ALLOC_GR);
	if (((ptl + ALLOC_GR - 1) & ~(ALLOC_GR - 1)) != ((ptl + l + ALLOC_GR - 1) & ~(ALLOC_GR - 1))) a1: {
		struct g_object_text *t;
		if ((unsigned)l > MAXINT) overalloc();
		if ((unsigned)ptl + (unsigned)l > MAXINT - ALLOC_GR) overalloc();
		t = mem_realloc(p->text, sizeof(struct g_object_text) + ((ptl + l + ALLOC_GR - 1) & ~(ALLOC_GR - 1)));
		if (p->w.last_wrap >= p->text->text && p->w.last_wrap < p->text->text + p->pending_text_len) p->w.last_wrap = p->w.last_wrap + ((unsigned char *)t - (unsigned char *)p->text);
		if (p->w.last_wrap_obj == &p->text->goti.go) p->w.last_wrap_obj = &t->goti.go;
		p->text = t;
	}
	memcpy(p->text->text + p->pending_text_len, s, l), p->text->text[p->pending_text_len = safe_add(p->pending_text_len, l)] = 0;
	qw = g_text_width(p->text->style, p->text->text + p->pending_text_len - l);
	p->text->goti.go.xw = safe_add(p->text->goti.go.xw, qw); /* !!! FIXME: move to g_wrap_text */
	if (par_format.align != AL_NO /*&& par_format.align != AL_NO_BREAKABLE*/) {
		p->w.text = p->text->text + p->pending_text_len - l;
		p->w.style = p->text->style;
		p->w.obj = &p->text->goti.go;
		p->w.width = rm(par_format) - par_format.leftmargin * G_HTML_MARGIN;
		p->w.force_break = 0;
		if (p->w.width < 0) p->w.width = 0;
		if (!g_wrap_text(&p->w)) {
			split_line_object(p, p->w.last_wrap_obj, p->w.last_wrap);
		}
	}
	return;

	/* !!! WARNING: THE FOLLOWING CODE IS SHADOWED IN HTML_R.C */

	process_link:
	if ((last_link /*|| last_target*/ || last_image || last_form) &&
	    !putchars_link_ptr &&
	    !xstrcmp(format_.link, last_link) && !xstrcmp(format_.target, last_target) &&
	    !xstrcmp(format_.image, last_image) && format_.form == last_form
	    && ((!format_.js_event && !last_js_event) || !compare_js_event_spec(format_.js_event, last_js_event))) {
		if (!p->data) goto back_link;
		if (!p->data->nlinks) {
			internal_error("no link");
			goto back_link;
		}
		link = &p->data->links[p->data->nlinks - 1];
		goto back_link;
	} else {
		if (last_link) mem_free(last_link);
		if (last_target) mem_free(last_target);
		if (last_image) mem_free(last_image);
		free_js_event_spec(last_js_event);
		last_link = last_target = last_image = NULL;
		last_form = NULL;
		last_js_event = NULL;
		if (!(format_.link || format_.image || format_.form || format_.js_event)) goto back_link;
		/*if (d_opt->num_links) {
			unsigned char s[64];
			unsigned char *fl = format_.link, *ft = format_.target, *fi = format_.image;
			struct form_control *ff = format_.form;
			struct js_event_spec *js = format_.js_event;
			format_.link = format_.target = format_.image = NULL;
			format_.form = NULL;
			format_.js_event = NULL;
			s[0] = '[';
			snzprint(s + 1, 62, p->link_num);
			strcat(cast_char s, "]");
			g_put_chars(p, s, strlen(cast_const_char s));
			if (ff && ff->type == FC_TEXTAREA) g_line_break(p);
			if (p->cx < par_format.leftmargin * G_HTML_MARGIN) p->cx = par_format.leftmargin * G_HTML_MARGIN;
			format_.link = fl, format_.target = ft, format_.image = fi;
			format_.form = ff;
			format_.js_event = js;
		}*/
		p->link_num++;
		last_link = stracpy(format_.link);
		last_target = stracpy(format_.target);
		last_image = stracpy(format_.image);
		last_form = format_.form;
		copy_js_event_spec(&last_js_event, format_.js_event);
		if (!p->data) goto back_link;
		if (!(link = new_link(p->data))) goto back_link;
		link->num = p->link_num - 1;
		link->pos = DUMMY;
		copy_js_event_spec(&link->js_event, format_.js_event);
		if (!last_form) {
			link->type = L_LINK;
			link->where = stracpy(last_link);
			link->target = stracpy(last_target);
		} else {
			link->type = last_form->type == FC_TEXT || last_form->type == FC_PASSWORD || last_form->type == FC_FILE_UPLOAD ? L_FIELD : last_form->type == FC_TEXTAREA ? L_AREA : last_form->type == FC_CHECKBOX || last_form->type == FC_RADIO ? L_CHECKBOX : last_form->type == FC_SELECT ? L_SELECT : L_BUTTON;
			link->form = last_form;
			link->target = stracpy(last_form->target);
		}
		link->where_img = stracpy(last_image);
		link->sel_color = 0;
		link->n = 0;
	}
	goto back_link;
}

static void g_do_format(unsigned char *start, unsigned char *end, struct g_part *part, unsigned char *head)
{
	pr(
	parse_html(start, end, g_put_chars, g_line_break, g_html_special, part, head);
	flush_pending_text_to_line(part);
	flush_pending_line_to_obj(part, 0);
	) {};
}

struct g_part *g_format_html_part(unsigned char *start, unsigned char *end, int align, int m, int width, unsigned char *head, int link_num, unsigned char *bg, unsigned char *bgcolor, struct f_data *f_d)
{
	int wa;
	struct g_part *p;
	struct html_element *e;
	struct form_control *fc;
	struct list_head *lfc;
	int lm = margin;

	if (par_format.implicit_pre_wrap) {
		int limit = d_opt->xw - G_SCROLL_BAR_WIDTH;
		if (table_level) limit -= 2 * G_HTML_MARGIN * d_opt->margin;
		if (limit < 0) limit = d_opt->xw;
		if (width > limit)
			width = limit;
	}

	if (!f_d) {
		p = find_table_cache_entry(start, end, align, m, width, 0, link_num);
		if (p) return p;
	}
	margin = m;

	/*d_opt->tables = 0;*/

	if (last_link) mem_free(last_link);
	if (last_image) mem_free(last_image);
	if (last_target) mem_free(last_target);
	free_js_event_spec(last_js_event);
	last_link = last_image = last_target = NULL;
	last_form = NULL;
	last_js_event = NULL;

	cached_font_face = to_je_ale_prasarna;
	p = mem_calloc(sizeof(struct g_part));
	{
		struct g_object_area *a;
		a = mem_calloc(sizeof(struct g_object_area));
		a->bg = g_get_background(bg, bgcolor);
		if (bgcolor) decode_color(bgcolor, &format_.bg);
		if (bgcolor) decode_color(bgcolor, &par_format.bgcolor);
		a->go.mouse_event = g_area_mouse;
		a->go.draw = g_area_draw;
		a->go.destruct = g_area_destruct;
		a->go.get_list = g_area_get_list;
		/*a->n_lines = 0;*/
		p->root = a;
		init_list(p->uf);
	}
	p->data = f_d;
	p->x = p->y = 0;
	p->xmax = 0;
	html_stack_dup();
	e = &html_top;
	html_top.dontkill = 2;
	html_top.namelen = 0;
	par_format.align = align;
	par_format.leftmargin = m;
	par_format.rightmargin = m;
	par_format.width = width;
	par_format.list_level = 0;
	par_format.list_number = 0;
	par_format.dd_margin = 0;
	if (align == AL_NO || align == AL_NO_BREAKABLE)
		format_.attr |= AT_FIXED;
	p->cx = -1;
	p->cx_w = 0;
	g_nobreak = align != AL_NO && par_format.align != AL_NO_BREAKABLE;
	g_do_format(start, end, p, head);
	g_nobreak = 0;
	line_breax = 1;
	while (&html_top != e) {
		kill_html_stack_item(&html_top);
		if (!&html_top || (void *)&html_top == (void *)&html_stack) {
			internal_error("html stack trashed");
			break;
		}
	}
	html_top.dontkill = 0;

	wa = g_get_area_width(p->root);
	if (wa > p->x) p->x = wa;
	g_x_extend_area(p->root, p->x, 0, align);
	if (p->x > p->xmax) p->xmax = p->x;
	p->y = p->root->go.yw;
	/*debug("WIDTH: obj (%d, %d), p (%d %d)", p->root->xw, p->root->yw, p->x, p->y);*/

	kill_html_stack_item(&html_top);
	if (!f_d) g_release_part(p), p->root = NULL;
	if (cached_font_face && cached_font_face != to_je_ale_prasarna) mem_free(cached_font_face);
	cached_font_face = to_je_ale_prasarna;

	foreach(struct form_control, fc, lfc, p->uf) destroy_fc(fc);
	free_list(struct form_control, p->uf);

	margin = lm;

	if (last_link) mem_free(last_link);
	if (last_image) mem_free(last_image);
	if (last_target) mem_free(last_target);
	free_js_event_spec(last_js_event);
	last_link = last_image = last_target = NULL;
	last_form = NULL;
	last_js_event = NULL;

	if (table_level > 1 && !f_d) {
		add_table_cache_entry(start, end, align, m, width, 0, link_num, p);
	}

	return p;
}

void g_release_part(struct g_part *p)
{
	if (p->text) p->text->goti.go.destruct(&p->text->goti.go);
	if (p->line) p->line->go.destruct(&p->line->go);
	if (p->root) p->root->go.destruct(&p->root->go);
	if (p->current_style) g_free_style(p->current_style);
}

static void g_scan_lines(struct g_object_line **o, int n, int *w)
{
	while (n--) {
		if ((*o)->n_entries) {
			struct g_object *oo = (*o)->entries[(*o)->n_entries - 1];
			if (safe_add(safe_add((*o)->go.x, oo->x), oo->xw) > *w)
				*w = (*o)->go.x + oo->x + oo->xw;
		}
		o++;
	}
}

int g_get_area_width(struct g_object_area *a)
{
	int w = 0;
	g_scan_lines(a->lines, a->n_lines, &w);
	return w;
}

void g_x_extend_area(struct g_object_area *a, int width, int height, int align)
{
	struct g_object_line *l;
	int i;
	a->go.xw = width;
	for (i = 0; i < a->n_lines; i++) {
		a->lines[i]->go.xw = width;
	}
	if (align != AL_NO && par_format.align != AL_NO_BREAKABLE) for (i = a->n_lines - 1; i >= 0; i--) {
		l = a->lines[i];
		if (!l->n_entries) {
			a->go.yw -= l->go.yw;
			l->go.destruct(&l->go);
			a->n_lines--;
			continue;
		}
		break;
	}
	if (a->go.yw >= height) return;
	l = mem_calloc(sizeof(struct g_object_line));
	l->go.mouse_event = g_line_mouse;
	l->go.draw = g_line_draw;
	l->go.destruct = g_line_destruct;
	l->go.get_list = g_line_get_list;
	l->go.x = 0;
	l->go.y = a->go.yw;
	l->go.xw = width;
	l->go.yw = height - a->go.yw;
	l->bg = a->bg;
	l->n_entries = 0;
	a->lines[a->n_lines] = l;
	a->n_lines++;
}

#endif
