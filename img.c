/* img.c
 * Generic image decoding and PNG and JPG decoders.
 * (c) 2002 Karel 'Clock' Kulhavy
 * This is a part of the Links program, released under GPL.

 * Used in graphics mode of Links only
 TODO: odstranit zbytecne ditherovani z strip_optimized header_dimensions_known,
       protoze pozadi obrazku musi byt stejne jako pozadi stranky, a to se nikdy
       neditheruje, protoze je to vzdy jednolita barva. Kdyz uz to nepujde
       odstranit tak tam aspon dat fixne zaokrouhlovani.
 TODO: dodelat stripy do jpegu a png a tiff.
 */

#include "links.h"

#ifdef G

#define RESTART_SIZE 8192
/* Size of biggest chunk of compressed data that is processed in one run */

static struct g_object_image *global_goi;
struct cached_image *global_cimg;
int end_callback_hit;

static int is_image_size_sane(ssize_t x, ssize_t y)
{
	size_t a;
	if (x < 0 || y < 0) return 0;
	if (x >= MAXINT || y >= MAXINT) return 0;
	a = (size_t)x * (size_t)y * (drv->depth & 7);
	if (y && (size_t)a / (size_t)y / (drv->depth & 7) != (size_t)x) return 0;
	return a <= MAX_SIZE_T / 2;
}

/* mem_free(cimg->decoder) */
static void destroy_decoder (struct cached_image *cimg)
{
	if (cimg->decoder){
		switch(cimg->image_type){
		case IM_PNG:
			png_destroy_decoder(cimg);
			break;
#ifdef HAVE_JPEG
		case IM_JPG:
			jpeg_destroy_decoder(cimg);
			break;
#endif /* #ifdef HAVE_JPEG */
		case IM_GIF:
			gif_destroy_decoder(cimg);
			break;
		case IM_XBM:
			/* do nothing */
			break;
#ifdef HAVE_TIFF
		case IM_TIFF:
			tiff_destroy_decoder(cimg);
			break;
#endif
#ifdef HAVE_SVG
		case IM_SVG:
			svg_destroy_decoder(cimg);
			break;
#endif
#ifdef HAVE_WEBP
		case IM_WEBP:
			webp_destroy_decoder(cimg);
			break;
#endif
		}
		mem_free(cimg->decoder);
	}
}

static void mem_free_buffer(struct cached_image *cimg)
{
	mem_free(cimg->buffer);
}

static void img_destruct_image(struct g_object *object)
{
	struct g_object_image *goi = get_struct(object, struct g_object_image, goti.go);

	if (goi->orig_src) mem_free(goi->orig_src);
	if (goi->alt) mem_free(goi->alt);
	if (goi->name) mem_free(goi->name);
	if (goi->src) mem_free(goi->src);
	release_image_map(goi->goti.map);
	if (goi->list_entry.next) del_from_list(goi);
	if (goi->goti.go.xw && goi->goti.go.yw) {
		 /* At least one dimension is zero */
		deref_cached_image(goi->cimg);
	}
	mem_free(goi);
}

/* Frees all data allocated by cached_image including cached image itself */
void img_destruct_cached_image(struct cached_image *cimg)
{
	switch (cimg->state){
		case 0:
		case 1:
		case 2:
		case 3:
		case 9:
		case 11:
		break;

		case 12:
		case 14:
		if (cimg->gamma_table) mem_free(cimg->gamma_table);
		if (cimg->bmp_used){
			drv->unregister_bitmap(&(cimg->bmp));
		}
		if (cimg->strip_optimized){
			if (cimg->dregs) mem_free(cimg->dregs);
		}else{
			mem_free_buffer(cimg);
		}
		/*-fallthrough*/
		case 8:
		case 10:
		destroy_decoder(cimg);
		break;

		case 13:
		case 15:
		drv->unregister_bitmap(&(cimg->bmp));
		break;

#ifdef DEBUG
		default:
		fprintf(stderr, "img_destruct_cached_image: state=%d\n", cimg->state);
		internal_error("Invalid state in struct cached_image");
#endif /* #ifdef DEBUG */
	}
	mem_free(cimg->url);
	mem_free(cimg);
}

/* You throw in a vertical dimension of image and it returns
 * new dimension according to the aspect ratio and user-set image
 * scaling factor. When scaling factor is 100% and screen pixels
 * are non-square, the picture will be always in one dimension
 * untouched and in the second _ENLARGED_. So that no information
 * in the picture will be lost.
 * Input may be <0. In this case output=input
 * Input may be 0. In this case output=0.
 * If input is >0 the output is also >0.
 */
static ssize_t img_scale_h(unsigned scale, ssize_t in)
{
	ssize_t out;
	/* We assume unsigned long holds at least 32 bits */
	unsigned long pre;

	if (in<=0) return in;
	pre=((unsigned long)(aspect<65536UL?65536UL:aspect)*scale+128)>>8;
	out=(size_t)(((unsigned long)in*pre+12800UL)/25600UL);
	if (out<1) out=1;
	return out;
}

static ssize_t img_scale_v(unsigned scale, ssize_t in)
{
	ssize_t out;
	unsigned long divisor;

	if (in<=0) return in;
	divisor=(100*(aspect>=65536UL?65536UL:aspect)+128)>>8;
	out=(ssize_t)(((unsigned long)in*(scale*256)+(divisor>>1))/divisor);
	if (out<1) out=1;
	return out;
}

/* Returns height (pixels) for prescribed width (pixels). Honours aspect. */
static ssize_t width2height(double width_px, double width_mm, double height_mm)
{
	ssize_t height_px;

	if (width_px<=0) return 0;
	height_px=(ssize_t)((height_mm*width_px*65536)/(aspect*width_mm));
	if (height_px<1) height_px=1;
	return height_px;

}

/* Returns width (pixels) for prescribed height (pixels). Honours aspect. */
static ssize_t height2width(double height_px, double width_mm, double height_mm)
{
	ssize_t width_px;

	if (height_px<=0) return 0;
	width_px=(ssize_t)((width_mm*height_px*aspect)/(65536*height_mm));
	if (width_px<1) width_px=1;
	return width_px;

}

/* Compute 8-bit background for filling buffer with cimg->*_gamma
 * (performs rounding) */
void compute_background_8(struct cached_image *cimg, unsigned char rgb[3])
{
	unsigned short red, green, blue;

	round_color_sRGB_to_48(&red, &green, &blue, cimg->background_color);
	rgb[0] = ags_16_to_8(red, (float)(cimg->red_gamma/(float)(user_gamma tcc_hack)));
	rgb[1] = ags_16_to_8(green, (float)(cimg->green_gamma/(float)(user_gamma tcc_hack)));
	rgb[2] = ags_16_to_8(blue, (float)(cimg->blue_gamma/(float)(user_gamma tcc_hack)));
}

/* updates cimg state when header dimensions are know. Only allowed to be called
 * in state 8 and 10.
 * Allocates right amount of memory into buffer, formats it (with background or
 * zeroes, depens on buffer_bytes_per_pixel). Updates dimensions (xww and yww)
 * according to newly known header dimensions. Fills in gamma_stamp, bmp_used
 * (zero because we not bother with generating bitmap here)
 * and rows_added.
 * Resets strip_optimized if image will be scaled or
 * Allocates dregs if on exit strip_optimized is nonzero.
 * Allocates and computes gamma_table, otherwise
 *	sets gamma_table to NULL. Also doesn't make gamma table if image contains less
 *	than 1024 pixels (it would be probably a waste of time).
 * Output state is always 12 (from input state 8) or 14 (from input state 10).
 *
 * The caller must have set the following elements of cimg:
 *	width
 *	height
 *	buffer_bytes_per_pixel
 *	red_gamma
 *	green_gamma
 *	blue_gamma
 *	strip_optimized
 */
int header_dimensions_known(struct cached_image *cimg)
{
	unsigned short red, green, blue;

#ifdef DEBUG
	if ((cimg->state^8)&13){
		fprintf(stderr,"cimg->state=%d\n",cimg->state);
		internal_error("Invalid state in header_dimensions_known");
	}
#endif /* #ifdef DEBUG */
	if (cimg->width<1||cimg->height<1){
		/*fprintf(stderr,"width=%d height=%d\n",cimg->width, cimg->height);*/
		return 1;
	}
	if (!is_image_size_sane(cimg->width, cimg->height)) {
		return 1;
	}
	if (cimg->wanted_xw<0){
		/* Unspecified width */
		if (cimg->wanted_yw<0){
			/* Unspecified neither width nor height */
			cimg->xww=img_scale_h(cimg->scale, cimg->width);
			cimg->yww=img_scale_v(cimg->scale, cimg->height);
		}else{
			/* Unspecified width specified height */
			cimg->xww=height2width(cimg->yww,
					cimg->width, cimg->height);
			if (cimg->xww<=0) cimg->xww=1;

		}
	}else{
		/* Specified width */
		if (cimg->wanted_yw<0){
			/* Unspecified height, specified width */
			cimg->yww=width2height(cimg->xww,
					cimg->width, cimg->height);
			if (cimg->yww<=0) cimg->yww=1;
		}else if (cimg->wanted_xyw_meaning==MEANING_AUTOSCALE){
			/* Specified height and width and autoscale meant */
			/* Try first to nail the height */
			cimg->yww=cimg->wanted_yw;
			cimg->xww=height2width(cimg->yww,
					cimg->width, cimg->height);
			if (cimg->xww>cimg->wanted_xw)
			{
				/* Width too much, we nail the width */
				cimg->xww=cimg->wanted_xw;
				cimg->yww=width2height(cimg->xww,
						cimg->width, cimg->height);
			}

			/* Some sanity checks */
			if (cimg->xww<=0) cimg->xww=1;
			if (cimg->yww<=0) cimg->yww=1;
		}
	}
	if (!is_image_size_sane(cimg->xww, cimg->yww)) {
		cimg->xww = cimg->width;
		cimg->yww = cimg->height;
	}

#ifdef HAVE_SVG
	if (cimg->image_type == IM_SVG) {
		/* SVG images are scaled using the cairo library, not the Links scaler */
		cimg->width = cimg->xww;
		cimg->height = cimg->yww;
	}
#endif

	if (cimg->width!=cimg->xww||cimg->height!=cimg->yww) cimg->strip_optimized=0;
	cimg->gamma_stamp=gamma_stamp;
	if (cimg->strip_optimized){
		struct bitmap tmpbmp;
		unsigned short *buf_16;
		ssize_t i;

		tmpbmp.x = (int)cimg->width;
		tmpbmp.y = 1;
		/* No buffer, bitmap is valid from the very beginning */
		cimg->bmp.x = (int)cimg->width;
		cimg->bmp.y = (int)cimg->height;
		if (drv->get_empty_bitmap(&(cimg->bmp))) {
			cimg->dregs = NULL;
			goto skip_img;
		}
		if ((size_t)cimg->width > MAX_SIZE_T / sizeof(*buf_16) / 3) overalloc();
		buf_16 = mem_alloc(sizeof(*buf_16) * 3 * (size_t)cimg->width);
		round_color_sRGB_to_48(&red, &green, &blue
			, cimg->background_color);
		mix_one_color_48(buf_16,cimg->width, red, green, blue);
#ifdef DEBUG
		if (cimg->height<=0){
			fprintf(stderr,"cimg->height=%ld\n", (long)cimg->height);
			internal_error("Invalid cimg->height in strip_optimized section of header_dimensions_known");
		}
#endif /* #ifdef DEBUG */
		/* The skip is uninitialized here and is read by dither_start
		 * but is not used in any malicious way so it doesn't matter
		 */
		tmpbmp.data = cimg->bmp.data;
		tmpbmp.skip = cimg->bmp.skip;
		cimg->dregs=dither_images?dither_start(buf_16,&tmpbmp):NULL;
		tmpbmp.data=(unsigned char *)tmpbmp.data+cimg->bmp.skip;
		if (cimg->dregs)
			for (i=cimg->height-1;i;i--){
				dither_restart(buf_16,&tmpbmp,cimg->dregs);
				tmpbmp.data=(unsigned char *)tmpbmp.data+cimg->bmp.skip;
			}
		else
			for (i=cimg->height-1;i;i--){
				(*round_fn)(buf_16,&tmpbmp);
				tmpbmp.data=(unsigned char *)tmpbmp.data+cimg->bmp.skip;
			}
		mem_free(buf_16);
		skip_img:
		drv->register_bitmap(&(cimg->bmp));
		if(cimg->dregs) memset(cimg->dregs,0,cimg->width*sizeof(*cimg->dregs)*3);
		cimg->bmp_used=1; /* Nonzero value */
		/* This ensures the dregs are none and because strip
		 * optimization is unusable in interlaced pictures,
		 * this saves the zeroing out at the beginning of the
		 * decoder itself.
		 */
	}else {
		cimg->rows_added=1;
		cimg->bmp_used=0;
		if (cimg->width && (size_t)cimg->width * (size_t)cimg->height / (size_t)cimg->width != (size_t)cimg->height) overalloc();
		cimg->buffer = mem_alloc_mayfail((size_t)cimg->width * (size_t)cimg->height * (size_t)cimg->buffer_bytes_per_pixel);
		if (!cimg->buffer)
			return 1;
		if (cimg->buffer_bytes_per_pixel==4
				||cimg->buffer_bytes_per_pixel==4
				*sizeof(unsigned short))
			{
			/* Make the buffer contain full transparency */
			memset(cimg->buffer, 0, (size_t)cimg->width * (size_t)cimg->height * (size_t)cimg->buffer_bytes_per_pixel);
		}else{
			/* Fill the buffer with background color */
			if (cimg->buffer_bytes_per_pixel > 4){
				/* 16-bit */
				unsigned short red, green, blue;

				round_color_sRGB_to_48(&red, &green, &blue, cimg->background_color);

				red=ags_16_to_16(red, (float)(cimg->red_gamma / (float)(user_gamma tcc_hack)));
				green=ags_16_to_16(green, (float)(cimg->green_gamma / (float)(user_gamma tcc_hack)));
				blue=ags_16_to_16(blue, (float)(cimg->blue_gamma / (float)(user_gamma tcc_hack)));
				mix_one_color_48((unsigned short *)cimg->buffer, cimg->width*cimg->height, red, green, blue);
			} else {
				unsigned char rgb[3];

				/* 8-bit */
				compute_background_8(cimg, rgb);
				mix_one_color_24(cimg->buffer, cimg->width * cimg->height, rgb[0], rgb[1], rgb[2]);
			}
		}
	}
	if (cimg->buffer_bytes_per_pixel<=4&&cimg->width*cimg->height>=1024){
		make_gamma_table(cimg);
	}else if (cimg->buffer_bytes_per_pixel>=6&&cimg->width*cimg->height>=262144){
		make_gamma_table(cimg);
	}else cimg->gamma_table=NULL;
	cimg->state|=4; /* Update state */
	return 0;
}

/* Fills "tmp" buffer with the resulting data and does not free the input
 * buffer. May be called only in states 12 and 14 of cimg
 */
static unsigned short *buffer_to_16(unsigned short *tmp, struct cached_image *cimg, unsigned char *buffer, ssize_t height)
{
	unsigned short red, green, blue;

#ifdef DEBUG
	if (cimg->state != 12 && cimg->state != 14){
		fprintf(stderr, "cimg->state=%d\n", cimg->state);
		internal_error("invalid state in buffer_to_16");
	}
#endif /* #ifdef DEBUG */
	switch (cimg->buffer_bytes_per_pixel) {
		case 3:
			if (cimg->gamma_table) {
				agx_24_to_48_table(tmp, buffer, cimg->width*height, cimg->gamma_table);
			} else {
				agx_24_to_48(tmp, buffer, cimg->width * height,
					(float)((float)(user_gamma tcc_hack) / cimg->red_gamma),
					(float)((float)(user_gamma tcc_hack) / cimg->green_gamma),
					(float)((float)(user_gamma tcc_hack) / cimg->blue_gamma));
			}
		break;

		case 3 * sizeof(unsigned short):
			if (cimg->gamma_table) {
				agx_48_to_48_table(tmp, (unsigned short *)buffer, cimg->width * height, cimg->gamma_table);
			} else {
				agx_48_to_48(tmp, (unsigned short *)buffer, cimg->width * height,
					(float)((float)(user_gamma tcc_hack) / cimg->red_gamma),
					(float)((float)(user_gamma tcc_hack) / cimg->green_gamma),
					(float)((float)(user_gamma tcc_hack) / cimg->blue_gamma));
			}
		break;

		/* Alpha's: */
		case 4:
			round_color_sRGB_to_48(&red,&green,&blue,cimg->background_color);
			if (cimg->gamma_table) {
				agx_and_uc_32_to_48_table(tmp, buffer, cimg->width * height, cimg->gamma_table, red, green, blue);
			} else {
				agx_and_uc_32_to_48(tmp,buffer, cimg->width*height,
					(float)((float)(user_gamma tcc_hack) / cimg->red_gamma),
					(float)((float)(user_gamma tcc_hack) / cimg->green_gamma),
					(float)((float)(user_gamma tcc_hack) / cimg->blue_gamma),
					red, green, blue);
			}
		break;

		case 4 * sizeof(unsigned short):
			round_color_sRGB_to_48(&red, &green, &blue, cimg->background_color);
			if (cimg->gamma_table) {
				agx_and_uc_64_to_48_table(tmp, (unsigned short *)buffer, cimg->width * height, cimg->gamma_table, red, green, blue);
			} else {
				agx_and_uc_64_to_48(tmp, (unsigned short *)buffer, cimg->width * height,
					(float)((float)(user_gamma tcc_hack) / cimg->red_gamma),
					(float)((float)(user_gamma tcc_hack) / cimg->green_gamma),
					(float)((float)(user_gamma tcc_hack) / cimg->blue_gamma),
					red,green,blue);
			}
		break;

#ifdef DEBUG
		default:
			internal_error("buffer_to_16: unknown mem organization");
#endif /* #ifdef DEBUG */

	}
	return tmp;
}

/* Returns allocated buffer with the resulting data and does not free the input
 * buffer. May be called only in states 12 and 14 of cimg
 * use_strip: 1 if the image is already registered and prepare_strip and
 * commit_strip is to be used
 * 0: if the image is not yet registered and instead one big register_bitmap
 * will be used eventually
 * dregs must be externally allocated and contain required value or must be
 * NULL.
 * if !dregs then rounding is performed instead of dithering.
 * dregs are not freed.
 * bottom dregs are placed back into dregs.
 * Before return the bitmap will be in registered state and changes will be
 * commited.
 * height must be >=1 !!!
 */
void buffer_to_bitmap_incremental(struct cached_image *cimg,
	unsigned char *buffer, ssize_t height, ssize_t yoff, int *dregs, int use_strip)
{
#define max_height 16
/* max_height must be at least 1 */
	unsigned short *tmp;
	struct bitmap tmpbmp;
	ssize_t add1 = 0, add2;

#ifdef DEBUG
	if (cimg->state!=12&&cimg->state!=14){
		fprintf(stderr,"cimg->state=%d\n",cimg->state);
		internal_error("Invalid state in buffer_to_bitmap_incremental\n");
	}
	if (height<1){
		fprintf(stderr,"height=%ld\n", (long)height);
		internal_error("Invalid height in buffer_to_bitmap_incremental\n");
	}
	if (cimg->width<1||cimg->height<1){
		fprintf(stderr,"cimg->width=%ld, cimg->height=%ld\n", (long)cimg->width, (long)cimg->height);
		internal_error("Invalid cimg->width x cimg->height in\
buffer_to_bitmap_incremental");
	}
#endif /* #ifdef DEBUG */
	if ((size_t)cimg->width > MAX_SIZE_T / (size_t)max_height / 3 / sizeof(*tmp)) overalloc();
	tmp=mem_alloc((size_t)cimg->width * (size_t)(height < max_height ? height : max_height) * 3 * sizeof(*tmp));
	/* Prepare a fake bitmap for dithering */
	tmpbmp.x = (int)cimg->width;
	if (!use_strip){
	       tmpbmp.data=(unsigned char *)cimg->bmp.data+cimg->bmp.skip*yoff;
	       add1=cimg->bmp.skip*max_height;
	}
	add2=cimg->buffer_bytes_per_pixel*cimg->width*max_height;
not_enough:
	tmpbmp.y = (int)(height < max_height ? height : max_height);
	if (use_strip) {
		tmpbmp.data = drv->prepare_strip(&(cimg->bmp), (int)yoff, tmpbmp.y);
		if (!tmpbmp.data) goto prepare_failed;
	}
	tmpbmp.skip=cimg->bmp.skip;
	buffer_to_16(tmp, cimg, buffer, tmpbmp.y);
	if (dregs) {
		dither_restart(tmp, &tmpbmp, dregs);
	} else {
		(*round_fn)(tmp, &tmpbmp);
	}
	if (use_strip) {
		prepare_failed:
		drv->commit_strip(&(cimg->bmp), (int)yoff, tmpbmp.y);
	}
	height-=tmpbmp.y;
	if (!height) goto end;
	buffer+=add2;
	yoff+=tmpbmp.y;
	tmpbmp.data=(unsigned char *)tmpbmp.data+add1;
	/* This has no effect if use_strip but it's faster
	 * to add to bogus value than to play with
	 * conditional jumps.
	 */
	goto not_enough;
end:
	mem_free(tmp);
	if (!use_strip) drv->register_bitmap(&(cimg->bmp));
}

/* Takes the buffer and resamples the data into the bitmap. Automatically
 * destroys the previous bitmap. Must be called only when cimg->buffer is valid.
 * Sets bmp_used to non-zero.
 * If gamma_table is used, it must be still allocated here (take care if you
 * want to destroy gamma table and call buffer_to_bitmap, first call buffer_to_bitmap
 * and then destroy gamma_table).
 */
static void buffer_to_bitmap(struct cached_image *cimg)
{
	unsigned short *tmp, *tmp1;
	ssize_t ix, iy, ox, oy;
	int gonna_be_smart;
	int *dregs;

#ifdef DEBUG
	if(cimg->state!=12&&cimg->state!=14){
		fprintf(stderr,"cimg->state=%d\n",cimg->state);
		internal_error("buffer_to_bitmap called in invalid state");
	}
	if (cimg->strip_optimized) internal_error("strip_optimized in buffer_to_bitmap");
	if (cimg->width<1||cimg->height<1){
		fprintf(stderr,"cimg->width=%ld, cimg->height=%ld\n", (long)cimg->width, (long)cimg->height);
		internal_error("Invalid cimg->width x cimg->height in buffer_to_bitmap");
	}
#endif /* #ifdef DEBUG */


	if (!cimg->rows_added) return;

	/* Here of course width and height must be already filled */
	cimg->rows_added=0;
	ix=cimg->width;
	iy=cimg->height;
	ox=cimg->xww;
	oy=cimg->yww;
	if (ix==ox&&iy==oy) gonna_be_smart=1;
	else{
		gonna_be_smart=0;
		if (ix && (size_t)ix * (size_t)iy / (size_t)ix != (size_t)iy) overalloc();
		if ((size_t)ix * (size_t)iy > MAX_SIZE_T / sizeof(*tmp) / 3) overalloc();
		tmp = mem_alloc_mayfail((size_t)ix * (size_t)iy * 3 * sizeof(*tmp));
		if (tmp) buffer_to_16(tmp, cimg, cimg->buffer, iy);
		if (!cimg->decoder){
			mem_free_buffer(cimg);
			cimg->buffer=NULL;
		}

		/* Scale the image to said size */
#ifdef DEBUG
		if (ox<=0||oy<=0){
			internal_error("ox or oy <=0 before resampling image");
		}
#endif /* #ifdef DEBUG */
		if (tmp && (ix!=ox || iy!=oy)) {
			/* We must really scale */
			tmp1=tmp;
			scale_color(tmp1,ix,iy,&tmp,ox,oy);
		}
	}
	if (cimg->bmp_used) drv->unregister_bitmap(&cimg->bmp);
	cimg->bmp.x = (int)ox;
	cimg->bmp.y = (int)oy;
	if (drv->get_empty_bitmap(&(cimg->bmp))) {
		if (!gonna_be_smart) {
			if (tmp) {
				mem_free(tmp);
			}
		}
		goto bitmap_failed;
	}
	if (gonna_be_smart){
		if (dither_images) {
			if ((size_t)cimg->width > MAX_SIZE_T / 3 / sizeof(*dregs)) overalloc();
			dregs = mem_calloc(sizeof(*dregs) * 3 * (size_t)cimg->width);
		} else {
			dregs = NULL;
		}
		buffer_to_bitmap_incremental(cimg, cimg->buffer, cimg->height,
			0, dregs, 0);
		if (dregs) mem_free(dregs);
	}else{
		if (tmp) {
			if (dither_images)
				dither(tmp, &cimg->bmp);
			else
				(*round_fn)(tmp, &cimg->bmp);
			mem_free(tmp);
		} else {
			int i;
			unsigned char *ptr = cimg->bmp.data;
			for (i = 0; i < cimg->bmp.y; i++) {
				memset(ptr, 0, cimg->bmp.x * (drv->depth & 7));
				ptr += cimg->bmp.skip;
			}
		}
		bitmap_failed:
		drv->register_bitmap(&(cimg->bmp));
	}
	cimg->bmp_used=1;
	/* Indicate that the bitmap is valid. The value is just any
	   nonzero value */
	cimg->rows_added=0;
	/* Indicate the bitmap is up-to-date */
}

/* Performs state transition for end of stream or error in image or
 * end of image */
void img_end(struct cached_image *cimg)
{
	switch(cimg->state){
		case 12:
		case 14:
		if (cimg->strip_optimized){
		       if (cimg->dregs)	mem_free(cimg->dregs);
		}
		else{
			buffer_to_bitmap(cimg);
			mem_free_buffer(cimg);
		}
		if (cimg->gamma_table) mem_free(cimg->gamma_table);
		/*-fallthrough*/
		case 8:
		case 10:
		destroy_decoder(cimg);
		case 0:
		case 1:
		case 2:
		case 3:
		case 9:
		case 11:
		case 13:
		case 15:
		break;
#ifdef DEBUG
		default:
		fprintf(stderr,"state=%d\n",cimg->state);
		internal_error("Invalid state encountered in end");
#endif /* #ifdef DEBUG */
	}
	cimg->state|=1;
}

static void r3l0ad(struct cached_image *cimg, struct g_object_image *goi)
{
	cimg->eof_hit=0;
	cimg->last_count=goi->af->rq->ce->count;
	cimg->last_count2=goi->af->rq->ce->count2;
	cimg->gamma_stamp=gamma_stamp;
	switch(cimg->state){
		case 8:
		case 10:
		destroy_decoder(cimg);
		case 1:
		case 3:
		case 9:
		case 11:
		case 0:
		case 2:
		break;

		case 12:
		if (cimg->gamma_table) mem_free(cimg->gamma_table);
		destroy_decoder(cimg);
		if (cimg->strip_optimized){
			if (cimg->dregs) mem_free(cimg->dregs);
		}else{
			mem_free_buffer(cimg);
		}
		if (cimg->bmp_used){
			case 13:
			drv->unregister_bitmap(&cimg->bmp);
		}
		cimg->xww=img_scale_h(cimg->scale, cimg->wanted_xw<0?32:cimg->wanted_xw);
		cimg->yww=img_scale_v(cimg->scale, cimg->wanted_yw<0?32:cimg->wanted_yw);
		break;

		case 14:
		if (cimg->gamma_table) mem_free(cimg->gamma_table);
		destroy_decoder(cimg);
		if (cimg->strip_optimized){
			if (cimg->dregs) mem_free(cimg->dregs);
		}else{
			mem_free_buffer(cimg);
		}
		if (cimg->bmp_used){
			case 15:
			drv->unregister_bitmap(&cimg->bmp);
		}
		break;

#ifdef DEBUG
		default:
		fprintf(stderr,"cimg->state=%d\n",cimg->state);
		internal_error("Invalid state in r3l0ad()");
#endif /* #ifdef DEBUG */
	}
	cimg->state&=2;
}

/* Returns 1 if match.
 * If doesn't return 1 then returns 0
 */
static inline int dtest(unsigned char *templat, unsigned char *test)
{
	return !casestrcmp(templat, test);
}

/* This may be called only in state 0 or 2 */
static void type(struct cached_image *cimg, unsigned char *content_type, unsigned char *data /* at least 4 bytes */)
{
#ifdef DEBUG
	if (cimg->state!=0&&cimg->state!=2){
		fprintf(stderr,"cimg->state=%d\n",cimg->state);
		internal_error("Invalid state encountered in type()");
	}
#endif /* #ifdef DEBUG */
#ifdef HAVE_JPEG
	if (data[0] == 0xff && data[1] == 0xd8)
		goto have_jpeg;
#endif
#ifdef HAVE_TIFF
	if (data[0] == 'I' && data[1] == 'I')
		goto have_tiff;
	if (data[0] == 'M' && data[1] == 'M')
		goto have_tiff;
#endif
#ifdef HAVE_SVG
	if (data[0] == '<' && data[1] == '?')
		goto have_svg;
#endif
#ifdef HAVE_WEBP
	if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F')
		goto have_webp;
#endif
	if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
		goto have_png;
	if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F')
		goto have_gif;
#ifdef HAVE_JPEG
	if (dtest(cast_uchar "image/jpeg",content_type) ||
	    dtest(cast_uchar "image/jpg",content_type) ||
	    dtest(cast_uchar "image/jpe",content_type) ||
	    dtest(cast_uchar "image/pjpe",content_type) ||
	    dtest(cast_uchar "image/pjpeg",content_type) ||
	    dtest(cast_uchar "image/pjpg",content_type)) {
		have_jpeg:
		cimg->image_type=IM_JPG;
		jpeg_start(cimg);
	} else
#endif /* #ifdef HAVE_JPEG */
	if (dtest(cast_uchar "image/png",content_type) ||
	    dtest(cast_uchar "image/x-png",content_type)) {
		have_png:
		cimg->image_type=IM_PNG;
		png_start(cimg);
	} else if (dtest(cast_uchar "image/gif",content_type)){
		have_gif:
		cimg->image_type=IM_GIF;
		gif_start(cimg);
	} else if (dtest(cast_uchar "image/x-xbitmap",content_type)){
		cimg->image_type=IM_XBM;
		xbm_start(cimg);
	} else
#ifdef HAVE_TIFF
	if (dtest(cast_uchar "image/tiff",content_type) ||
	    dtest(cast_uchar "image/tif",content_type)) {
		have_tiff:
		cimg->image_type=IM_TIFF;
		tiff_start(cimg);
	} else
#endif /* #ifdef HAVE_TIFF */
#ifdef HAVE_SVG
	if (dtest(cast_uchar "image/svg+xml",content_type) ||
	    dtest(cast_uchar "image/svg",content_type)) {
		have_svg:
		cimg->image_type=IM_SVG;
		svg_start(cimg);
	} else
#endif /* #ifdef HAVE_SVG */
#ifdef HAVE_WEBP
	if (dtest(cast_uchar "image/webp",content_type)) {
		have_webp:
		cimg->image_type=IM_WEBP;
		webp_start(cimg);
	} else
#endif /* #ifdef HAVE_WEBP */
	{
		/* Error */
		img_end(cimg);
		return;
	}
	cimg->state|=8; /* Advance the state according to the table in
			   links-doc.html */
	cimg->last_length=0;
}

/* Doesn't print anything. Downloads more data if available.
 * Sets up cimg->reparse and cimg->xww and cimg->yww accordingly to
 * the state of the decoder. When changing xww and yww also changes xw and yw
 * in g_object_image.
 *      return value 1 means the data were chopped and the caller shall not redraw
 *		(because it would be too slow and because we are probably choked
 *		up with the data)
 */
static int img_process_download(struct g_object_image *goi, struct f_data_c *fdatac)
{
	unsigned char *data, *ctype;
	size_t total_len;
	struct cached_image *cimg = goi->cimg;
	int chopped=0;

#ifdef DEBUG
	if (!goi->af) internal_error("NULL goi->af in process_download\n");
	if (cimg->state>=16){ /* Negative don't occur because it's unsigned char */
		fprintf(stderr,"cimg->state=%d\n",cimg->state);
		internal_error("Invalid cimg->state in img_process_download\n");
	}
#endif /* #ifdef DEBUG */
	if (!goi->af->rq) return 0;
	if (get_file(goi->af->rq, &data, &total_len)) goto end;
	if (total_len < 4) goto end;
	if (total_len > MAXINT) total_len = MAXINT;
	/*fprintf(stderr, "processing: %s\n", goi->af->rq->ce->url);*/
	if (goi->af->rq->ce->count2!=cimg->last_count2||
		(goi->af->rq->ce->count!=cimg->last_count && cimg->eof_hit) ||
		(cimg->state>=12&&gamma_stamp!=cimg->gamma_stamp)){
		/* Reload */
		r3l0ad(cimg,goi);
	}
	/*if (!goi->af->rq->ce->head) goto end;*/ /* Mikulas: head muze byt NULL*/ /* Mikulas: tak se to zpracuje a nebude se skakat na konec, kdyz je to NULL */

	if (cimg->state==0||cimg->state==2){
		/* Type still unknown */
		ctype=get_content_type(goi->af->rq->ce->head,
			goi->af->rq->url);
		if (!ctype) ctype = stracpy(cast_uchar "application/octet-stream");
		type(cimg,ctype,data);
		mem_free(ctype);
	}

	/* Now, if we are in state where decoder is running (8, 10, 12, 14), we may feed
	 * some data into it.
	 */

	if (!((cimg->state^8)&9)){
		int length = (int)total_len;
		if (length<=cimg->last_length) goto end; /* No new data */

		data+=cimg->last_length;
		length-=cimg->last_length;
		if (length>RESTART_SIZE){
			length=RESTART_SIZE;
			chopped=1;
			if (fdatac) {
				refresh_image(fdatac, &goi->goti.go, 0);
			}
		}
		/* Decoder has been already started */
		switch(cimg->image_type){
		case IM_PNG:
			png_restart(cimg,data,length);
			break;
#ifdef HAVE_JPEG
		case IM_JPG:
			jpeg_restart(cimg,data,length);
			break;
#endif /* #ifdef HAVE_JPEG */
		case IM_XBM:
			xbm_restart(cimg,data,length);
			break;
		case IM_GIF:
			gif_restart(data,length);
			break;
#ifdef HAVE_TIFF
		case IM_TIFF:
			tiff_restart(cimg,data,length);
			break;
#endif /* #ifdef HAVE_TIFF */
#ifdef HAVE_SVG
		case IM_SVG:
			svg_restart(cimg,data,length);
			break;
#endif /* #ifdef HAVE_SVG */
#ifdef HAVE_WEBP
		case IM_WEBP:
			webp_restart(cimg,data,length);
			break;
#endif /* #ifdef HAVE_WEBP */
#ifdef DEBUG
		default:
			fprintf(stderr,"cimg->image_type=%d\n",cimg->state);
			internal_error("Invalid image_type encountered when processing data in\
img_process_download.\n");
#endif /* #ifdef DEBUG */
		}
		cimg->last_length+=length;
	}
	end:

	/* Test end */
	if (!is_entry_used(goi->af->rq->ce) && (goi->af->rq->state < 0
		||(goi->af->rq->ce&&goi->af->rq->stat.state<0))){
		/* We must not perform end with chopped because some
		 * unprocessed data still wait for us :)
		 */
		if (!chopped){
			if (!((cimg->state^8)&9)) {
#ifdef HAVE_TIFF
				if (cimg->image_type==IM_TIFF)
					tiff_finish(cimg);
#endif
#ifdef HAVE_SVG
				if (cimg->image_type==IM_SVG)
					svg_finish(cimg);
#endif
#ifdef HAVE_WEBP
				if (cimg->image_type==IM_WEBP)
					webp_finish(cimg);
#endif
			}
			cimg->eof_hit=1;
			if (goi->af->rq->ce)
				cimg->last_count=goi->af->rq->ce->count;
			img_end(cimg);
		}
	} else if (!chopped) {
		if (fdatac) {
			if (f_is_finished(fdatac->f_data)) {
				refresh_image(fdatac, &goi->goti.go, 2000);
			} else {
	/*
	 * Fix a bug - if we have a text file with html and built-in image using
	 * the data:// url. If we press '\' to toggle the view from text to
	 * html, the image is not displayed correctly. We need to reset
	 * fdatac->done to force re-parse.
	 */
				fdatac->done = 0;
			}
		}
	}
	return chopped;
}

/* Input: rgb (sRGB) triplet (0...255)
 * Returns a color that is very contrasty on that background sRGB color
 */
int get_foreground(int rgb)
{
	int r,g,b;

	r=(rgb>>16)&255;
	g=(rgb>>8)&255;
	b=rgb&255;

	r=r<128?255:0;
	g=g<128?255:0;
	b=b<128?255:0;

	return (r<<16)|(g<<8)|b;
}

static void draw_frame_mark(struct graphics_device *dev, int x, int y, int xw, int yw, long bg, long fg, int broken)
{
#ifdef DEBUG
	if (xw<1||yw<1) internal_error("zero dimension in draw_frame_mark");
#endif /* #ifdef DEBUG */
	if (broken == 1){
		/* Draw between ( 0 and 1/4 ) and ( 3/4 and 1 ) of each
		 * side (0-1)
		 */
		 int xl, xh, yl, yh;

		 xh=xw-(xl=xw>>2);
		 yh=yw-(yl=yw>>2);
		/* Draw full sides and the box inside */
		drv->draw_hline(dev,x,y,x+xl,fg);
		drv->draw_hline(dev,x+xl,y,x+xh,bg);
		drv->draw_hline(dev,x+xh,y,x+xw,fg);
		if (yw>=1){
			if (yw>=2){
				drv->draw_vline(dev,x,y+1,y+yl,fg);
				drv->draw_vline(dev,x,y+yl,y+yh,bg);
				drv->draw_vline(dev,x,y+yh,y+yw-1,fg);
				if (xw>=1){
					if (xw>=2){
						drv->fill_area(dev,
							x+1,y+1,x+xw-1,y+yw-1,
							bg);
					}
					drv->draw_vline(dev,x+xw-1,y+1,y+yl,fg);
					drv->draw_vline(dev,x+xw-1,y+yl,y+yh,bg);
					drv->draw_vline(dev,x+xw-1,y+yh,y+yw-1,fg);
				}
			}
			drv->draw_hline(dev,x,y+yw-1,x+xl,fg);
			drv->draw_hline(dev,x+xl,y+yw-1,x+xh,bg);
			drv->draw_hline(dev,x+xh,y+yw-1,x+xw,fg);
		}
	}else {
		/* Draw full sides and the box inside */
		drv->draw_hline(dev,x,y,x+xw,fg);
		if (yw>=1){
			if (yw>=2){
				drv->draw_vline(dev,x,y+1,y+yw-1,fg);
				if (xw>=1){
					if (xw>=2){
						if (broken < 2) drv->fill_area(dev,
							x+1,y+1,x+xw-1,y+yw-1,
							bg);
					}
					drv->draw_vline(dev,x+xw-1,y+1,
						y+yw-1,fg);
				}
			}
			drv->draw_hline(dev,x,y+yw-1,x+xw,fg);
		}
		if (broken == 2 && xw > 2 && yw > 2) {
			draw_frame_mark(dev, x + 1, y + 1, xw - 2, yw - 2, bg, fg, 3);
		}
	}
}

static long image_backgronud(struct cached_image *cimg)
{
	if (!cimg) return dip_get_color_sRGB(0x00c0c0c0);
	return dip_get_color_sRGB(cimg->background_color);
}

static long image_foregronud(struct cached_image *cimg)
{
	if (!cimg) return dip_get_color_sRGB(0x00000000);
	return dip_get_color_sRGB(get_foreground(cimg->background_color));
}

/* Entry is allowed only in states 12, 13, 14, 15
 * Draws the picture from bitmap.
 * Before doing so, ensures that bitmap is present and if not, converts it from
 * the buffer.
 */
static void draw_picture(struct f_data_c *fdatac, struct g_object_image *goi, int x, int y)
{
	struct graphics_device *dev = fdatac->ses->term->dev;
	struct cached_image *cimg = goi->cimg;
	struct rect saved;

#ifdef DEBUG
	if (goi->cimg->state < 12 || goi->cimg->state >= 16) {
		fprintf(stderr, "cimg->state=%d\n", cimg->state);
		internal_error("Invalid cimg->state in draw_picture");
	}
#endif /* #ifdef DEBUG */
	if (!(cimg->state & 1)) {
		if (!cimg->bmp_used)
			buffer_to_bitmap(cimg);
	}
#ifdef DEBUG
	else if (!cimg->bmp_used) {
		fprintf(stderr, "cimg->state=%d\n", cimg->state);
		internal_error("Nonexistent bitmap in said cimg->state in draw_picture");
	}
#endif /* #ifdef DEBUG */
	restrict_clip_area(dev, &saved, x, y, x + goi->goti.go.xw, y + goi->goti.go.yw);
	drv->draw_bitmap(dev, &cimg->bmp, x, y);
	if (cimg->bmp.x < goi->goti.go.xw ||
	    cimg->bmp.y < goi->goti.go.yw) {
		long bg = image_backgronud(cimg);
		drv->fill_area(dev, x + cimg->bmp.x, y, x + goi->goti.go.xw, y + cimg->bmp.y, bg);
		drv->fill_area(dev, x, y + cimg->bmp.y, x + goi->goti.go.xw, y + goi->goti.go.yw, bg);
	}
	set_clip_area(dev, &saved);
}

/* Ensures in buffer there is not newer picture than in bitmap. Allowed to be
 * called only in state 12, 13, 14, 15.
 */
static void update_bitmap(struct cached_image *cimg)
{
#ifdef DEBUG
	if (cimg->state < 12 || cimg->state >= 16) {
		fprintf(stderr, "cimg->state=%d\n", cimg->state);
		internal_error("Invalid state in update_bitmap");
	}
#endif /* #ifdef DEBUG */
	if (!(cimg->state & 1) && !cimg->strip_optimized && cimg->rows_added)
		buffer_to_bitmap(cimg);
}

/* Draws the image at x,y. Is called from other C sources. */
static void img_draw_image(struct f_data_c *fdatac, struct g_object *goi_, int x, int y)
{
	struct g_object_image *goi = get_struct(goi_, struct g_object_image, goti.go);
	struct cached_image *cimg = goi->cimg;
	struct rect r;
	/* refresh_image(fdatac, goi, 1000); To sem asi napsal mikulas jako
	 * navod, jak se vola to refresh_image.  Nicmene ja jsem milostive
	 * usoudil, ze zadnejch 1000, ale 0.
	 */

	if (!(goi->goti.go.xw && goi->goti.go.yw))
		return; /* At least one dimension is zero */

	memcpy(&r, &fdatac->ses->term->dev->clip, sizeof(struct rect));
	if (fdatac->vs->g_display_link && fdatac->active && fdatac->vs->current_link != -1 && fdatac->vs->current_link == goi->goti.link_num) {
		long fg = image_foregronud(cimg);
		draw_frame_mark(fdatac->ses->term->dev, x, y, goi->goti.go.xw,
			goi->goti.go.yw, fg, fg, 2);
		restrict_clip_area(fdatac->ses->term->dev, &r, x + 2, y + 2, x + goi->goti.go.xw - 2, y + goi->goti.go.yw - 2);
	}

	global_goi=goi;
	global_cimg=goi->cimg;
	if (img_process_download(goi, fdatac)) goto ret; /* Choked with data, will not
							* draw. */
	/* Now we will only draw... */
	if (!cimg || cimg->state < 12) {
		long fg = image_foregronud(cimg);
		long bg = image_backgronud(cimg);
		draw_frame_mark(fdatac->ses->term->dev, x, y, goi->goti.go.xw,
			goi->goti.go.yw, bg, fg, !cimg || cimg->state & 1);
	} else {
#ifdef DEBUG
		if (cimg->state >= 16)
			internal_error("Invalid state in img_draw_image: %d", cimg->state);
#endif
		update_bitmap(cimg);
		draw_picture(fdatac, goi, x, y);
	}
ret:
	set_clip_area(fdatac->ses->term->dev, &r);
#ifdef LINKS_TESTMODE_IMAGE_AUTO_EXIT
	if (cimg->state & 1)
		terminate_loop = 1;
#endif
}

/* Prior to calling this function you have to fill out
 * image -> xw (<0 means not known)
 * image -> yw (<0 means not known)
 * image -> xyw meaning (MEANING_AUTOSCALE or MEANING_DIMS)
 * image -> background
 *
 * The URL will not be freed.
 */
static void find_or_make_cached_image(struct g_object_image *image, unsigned char *url,
	int scale)
{
	struct cached_image *cimg;

	if (!(cimg = find_cached_image(image->background, url, image->goti.go.xw,
		image->goti.go.yw, image->xyw_meaning, scale, aspect))){
		/* We have to make a new image cache entry */
		cimg = mem_alloc(sizeof(*cimg));
		cimg->background_color = image->background;
#ifdef DEBUG
		if (!url)
			internal_error("NULL url as argument of\
find_or_make_cached_image");
#endif /* #ifdef DEBUG */
		cimg->scale = scale;
		cimg->aspect = aspect;
		cimg->url = stracpy(url);
		cimg->wanted_xw = image->goti.go.xw;
		cimg->wanted_yw = image->goti.go.yw;
		cimg->wanted_xyw_meaning = image->xyw_meaning;
		cimg->xww = image->goti.go.xw < 0 ? img_scale_h(cimg->scale, 32) : cimg->wanted_xw;
		cimg->yww = image->goti.go.yw < 0 ? img_scale_v(cimg->scale, 32) : cimg->wanted_yw;
		cimg->state=0;
		/* width, height, image_type, buffer, buffer_bytes_per_pixel, red_gamma,
		 * green_gamma, blue_gamma, gamma_stamp, bitmap, last_length, rows_added,
		 * and decoder is invalid in both state 0 and state 2. Thus is need no to
		 * be filled in.
		 */

		/* last_count2 is unitialized */
		cimg->eof_hit = 0;
		cimg->last_count = -1;
		cimg->last_count2 = -1;
		if (cimg->wanted_xw >= 0 && cimg->wanted_yw >=0) cimg->state |= 2;
		add_image_to_cache(cimg);
	}
	global_cimg = image->cimg = cimg;
}

/* The original (unscaled, in pixels pace) size is requested in im->xsize and im->ysize.
 * <0 means unknown. Autoscale is requested in autoscale. When autoscale is on,
 * the requested dimensions are not scaled and they mean maximum allowed
 * dimensions. */
struct g_object_image *insert_image(struct g_part *p, struct image_description *im)
{
	struct g_object_image *image;
	struct cached_image *cimg;
	int retval;
	ssize_t xw, yw;

	image=mem_calloc(sizeof(struct g_object_image));
	global_goi = image;
	image->goti.go.mouse_event = g_text_mouse;
	image->goti.go.draw = img_draw_image;
	image->goti.go.destruct = img_destruct_image;
	image->goti.go.get_list = NULL;
	image->goti.link_num = im->link_num;
	image->goti.link_order = im->link_order;
	image->goti.map = NULL;
	/*
	image->goti.go.x is already filled
	image->goti.go.y is already filled
	*/
	if (im->align == AL_MIDDLE) image->goti.go.y = G_OBJ_ALIGN_MIDDLE;
	if (im->align == AL_TOP) image->goti.go.y = G_OBJ_ALIGN_TOP;

	if (im->autoscale_x && im->autoscale_y) {
		/* Autoscale requested */
		xw = im->autoscale_x;
		yw = im->autoscale_y;
		image->goti.go.xw = (int)xw;
		image->goti.go.yw = (int)yw;
		image->xyw_meaning = MEANING_AUTOSCALE;
	}else{
		/* Autoscale not requested */
		xw = img_scale_h(d_opt->image_scale, im->xsize);
		yw = img_scale_v(d_opt->image_scale, im->ysize);
		image->goti.go.xw = (int)xw;
		image->goti.go.yw = (int)yw;
		image->xyw_meaning = MEANING_DIMS;
	}
	if (xw >= 0 && yw >= 0) {
		if (!is_image_size_sane(xw, yw)) {
			mem_free(image);
			return NULL;
		}
	}

	/* Put the data for javascript inside */
	image->id = current_f_data->n_images++;
	image->name = stracpy(im->name);
	image->alt = stracpy(im->alt);
	image->orig_src = stracpy(im->src);
	image->border = im->border;
	image->vspace = im->vspace;
	image->hspace = im->hspace;
	image->src = stracpy(im->url);

	if (!(image->goti.go.xw && image->goti.go.yw)) {
		/* At least one is zero */
		if (image->goti.go.xw < 0) image->goti.go.xw = 0;
		if (image->goti.go.yw < 0) image->goti.go.yw = 0;
		if (im->insert_flag) add_to_list(current_f_data->images, image);
		else image->list_entry.prev = image->list_entry.next = NULL;
		return image;
	}
	/*
	image->parent is already filled
	*/
	image->af = request_additional_file(current_f_data, im->url);
	if (image->goti.go.xw < 0 || image->goti.go.yw < 0) image->af->unknown_image_size = 1;
	image->background = hack_rgb(p->root->bg->sRGB);

	/* This supplies the result into image->cimg and global_cimg */
	find_or_make_cached_image(image, im->url, d_opt->image_scale);
	cimg = global_cimg;

next_chunk:
	retval = img_process_download(image, NULL);
	if (retval && !(cimg->state & 4)) goto next_chunk;
	image->goti.go.xw = (int)image->cimg->xww;
	image->goti.go.yw = (int)image->cimg->yww;
	if (cimg->state == 0 || cimg->state == 8 || image->af->unknown_image_size) if (image->af->need_reparse != -1) image->af->need_reparse = 1;
	if (im->insert_flag) add_to_list(current_f_data->images, image);
	else image->list_entry.prev = image->list_entry.next = NULL;
	return image;
}

#ifdef JS

void change_image (struct g_object_image *goi, unsigned char *url, unsigned char *src, struct f_data *fdata)
{
	/*struct cached_image *cimg;*/

	global_goi = goi;
	mem_free(goi->src);
	goi->src = stracpy(url);
	if (goi->orig_src) mem_free(goi->orig_src);
	goi->orig_src = stracpy(src);
	if (!(goi->goti.go.xw && goi->goti.go.yw)) return;
	deref_cached_image(goi->cimg);
	goi->af = request_additional_file(fdata, url);
	goi->af->need_reparse = -1;

	find_or_make_cached_image(goi, url, fdata->opt.image_scale);
	/* Automatically sets up global_cimg */

	refresh_image(fdata->fd, &goi->goti.go, 0);
}

#endif

#endif

int known_image_type(unsigned char *type)
{
#ifdef G
	if (!casestrcmp(type, cast_uchar "image/png")) return 1;
	if (!casestrcmp(type, cast_uchar "image/x-png")) return 1;
	if (!casestrcmp(type, cast_uchar "image/gif")) return 1;
	if (!casestrcmp(type, cast_uchar "image/x-xbitmap")) return 1;
#ifdef HAVE_JPEG
	if (!casestrcmp(type, cast_uchar "image/jpeg")) return 1;
	if (!casestrcmp(type, cast_uchar "image/jpg")) return 1;
	if (!casestrcmp(type, cast_uchar "image/jpe")) return 1;
	if (!casestrcmp(type, cast_uchar "image/pjpe")) return 1;
	if (!casestrcmp(type, cast_uchar "image/pjpeg")) return 1;
	if (!casestrcmp(type, cast_uchar "image/pjpg")) return 1;
#endif
#ifdef HAVE_TIFF
	if (!casestrcmp(type, cast_uchar "image/tiff")) return 1;
	if (!casestrcmp(type, cast_uchar "image/tif")) return 1;
#endif
#ifdef HAVE_SVG
	if (!casestrcmp(type, cast_uchar "image/svg+xml")) return 1;
	if (!casestrcmp(type, cast_uchar "image/svg")) return 1;
#endif
#ifdef HAVE_WEBP
	if (!casestrcmp(type, cast_uchar "image/webp")) return 1;
#endif
#endif
	return 0;
}
