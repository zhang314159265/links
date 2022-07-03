#include "links.h"

#ifdef write
#undef write
#endif

my_uintptr_t decompressed_cache_size = 0;

static int display_error(struct terminal *term, unsigned char *msg, int *errp)
{
	if (errp) *errp = 1;
	if (!term) return 0;
	if (!errp) if (find_msg_box(term, msg, NULL, NULL)) return 0;
	return 1;
}

#ifdef HAVE_ANY_COMPRESSION

static void decoder_memory_init(unsigned char **p, size_t *size, off_t init_length)
{
	if (init_length > 0 && init_length < MAXINT) *size = (int)init_length;
	else *size = 4096;
again:
	if (*size <= 4096) {
		*p = mem_alloc(*size);
	} else {
		*p = mem_alloc_mayfail(*size);
		if (!*p) {
			*size >>= 1;
			goto again;
		}
	}
}

static int decoder_memory_expand(unsigned char **p, size_t size, size_t *addsize)
{
	unsigned char *pp;
	size_t add = size / 4 + 1;
	if (add > MAXINT) add = MAXINT;
	while (size + add < size || !(pp = mem_realloc_mayfail(*p, size + add))) {
		if (add > 1) add >>= 1;
		else goto ovf;
	}
	*addsize = add;
	*p = pp;
	return 0;

ovf:
	*addsize = 0;
	return -1;
}

static void decompress_error(struct terminal *term, struct cache_entry *ce, unsigned char *lib, unsigned char *msg, int *errp)
{
	unsigned char *u, *server;
	if ((u = parse_http_header(ce->head, cast_uchar "Content-Encoding", NULL))) {
		mem_free(u);
		if ((server = get_host_name(ce->url))) {
			add_blacklist_entry(server, BL_NO_COMPRESSION);
			mem_free(server);
		}
	}
	if (!display_error(term, TEXT_(T_DECOMPRESSION_ERROR), errp)) return;
	u = display_url(term, ce->url, 1);
	msg_box(term, getml(u, NULL), TEXT_(T_DECOMPRESSION_ERROR), AL_CENTER, TEXT_(T_ERROR_DECOMPRESSING_), u, TEXT_(T__wITH_), lib, cast_uchar ": ", msg, MSG_BOX_END, NULL, 1, TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC);
}

#endif

#ifdef HAVE_ZLIB
#include <zlib.h>
static int decode_gzip(struct terminal *term, struct cache_entry *ce, int defl, int *errp)
{
	unsigned char err;
	unsigned char memory_error;
	unsigned char skip_gzip_header;
	unsigned char old_zlib;
	z_stream z;
	off_t offset;
	int r;
	unsigned char *p;
	struct fragment *f;
	struct list_head *lf;
	size_t size;

	retry_after_memory_error:
	memory_error = 0;
	decoder_memory_init(&p, &size, ce->length);
	init_again:
	err = 0;
	skip_gzip_header = 0;
	old_zlib = 0;
	memset(&z, 0, sizeof z);
	z.next_in = NULL;
	z.avail_in = 0;
	z.next_out = p;
	z.avail_out = (unsigned)size;
	z.zalloc = NULL;
	z.zfree = NULL;
	z.opaque = NULL;
	r = inflateInit2(&z, defl == 1 ? 15 : defl == 2 ? -15 : 15 + 16);
	init_failed:
	switch (r) {
		case Z_OK:		break;
		case Z_MEM_ERROR:	memory_error = 1;
					err = 1;
					goto after_inflateend;
		case Z_STREAM_ERROR:
					if (!defl && !old_zlib) {
						if (defrag_entry(ce)) {
							memory_error = 1;
							err = 1;
							goto after_inflateend;
						}
						r = inflateInit2(&z, -15);
						skip_gzip_header = 1;
						old_zlib = 1;
						goto init_failed;
					}
					decompress_error(term, ce, cast_uchar "zlib", z.msg ? (unsigned char *)z.msg : (unsigned char *)"Invalid parameter", errp);
					err = 1;
					goto after_inflateend;
		case Z_VERSION_ERROR:	decompress_error(term, ce, cast_uchar "zlib", z.msg ? (unsigned char *)z.msg : (unsigned char *)"Bad zlib version", errp);
					err = 1;
					goto after_inflateend;
		default:		decompress_error(term, ce, cast_uchar "zlib", z.msg ? (unsigned char *)z.msg : (unsigned char *)"Unknown return value on inflateInit2", errp);
					err = 1;
					goto after_inflateend;
	}
	offset = 0;
	foreach(struct fragment, f, lf, ce->frag) {
		if (f->offset != offset) break;
		z.next_in = f->data;
		z.avail_in = (unsigned)f->length;
		if ((off_t)z.avail_in != f->length) overalloc();
		repeat_frag:
		if (skip_gzip_header == 2) {
			if (z.avail_in < 8) goto finish;
			z.next_in = (unsigned char *)z.next_in + 8;
			z.avail_in -= 8;
			skip_gzip_header = 1;
		}
		if (skip_gzip_header) {
			/* if zlib is old, we have to skip gzip header manually
			   otherwise zlib 1.2.x can do it automatically */
			unsigned char *head = z.next_in;
			unsigned headlen = 10;
			if (z.avail_in <= 11) goto finish;
			if (head[0] != 0x1f || head[1] != 0x8b) {
				decompress_error(term, ce, cast_uchar "zlib", TEXT_(T_COMPRESSED_ERROR), errp);
				err = 1;
				goto finish;
			}
			if (head[2] != 8 || head[3] & 0xe0) {
				decompress_error(term, ce, cast_uchar "zlib", TEXT_(T_UNKNOWN_COMPRESSION_METHOD), errp);
				err = 1;
				goto finish;
			}
			if (head[3] & 0x04) {
				headlen += 2 + head[10] + (head[11] << 8);
				if (headlen >= z.avail_in) goto finish;
			}
			if (head[3] & 0x08) {
				do {
					headlen++;
					if (headlen >= z.avail_in) goto finish;
				} while (head[headlen - 1]);
			}
			if (head[3] & 0x10) {
				do {
					headlen++;
					if (headlen >= z.avail_in) goto finish;
				} while (head[headlen - 1]);
			}
			if (head[3] & 0x01) {
				headlen += 2;
				if (headlen >= z.avail_in) goto finish;
			}
			z.next_in = (unsigned char *)z.next_in + headlen;
			z.avail_in -= headlen;
			skip_gzip_header = 0;
		}
		r = inflate(&z, f->list_entry.next == &ce->frag ? Z_SYNC_FLUSH : Z_NO_FLUSH);
		switch (r) {
			case Z_OK:		break;
			case Z_BUF_ERROR:	break;
			case Z_STREAM_END:	r = inflateEnd(&z);
						if (r != Z_OK) goto end_failed;
						r = inflateInit2(&z, old_zlib ? -15 : defl ? 15 : 15 + 16);
						if (r != Z_OK) {
							old_zlib = 0;
							goto init_failed;
						}
						if (old_zlib) {
							skip_gzip_header = 2;
						}
						break;
			case Z_NEED_DICT:
			case Z_DATA_ERROR:	if (defl == 1) {
							defl = 2;
							r = inflateEnd(&z);
							if (r != Z_OK) goto end_failed;
							goto init_again;
						}
						decompress_error(term, ce, cast_uchar "zlib", z.msg ? (unsigned char *)z.msg : TEXT_(T_COMPRESSED_ERROR), errp);
						err = 1;
						goto finish;
			case Z_STREAM_ERROR:	decompress_error(term, ce, cast_uchar "zlib", z.msg ? (unsigned char *)z.msg : (unsigned char *)"Internal error on inflate", errp);
						err = 1;
						goto finish;
			case Z_MEM_ERROR:
			mem_error:		memory_error = 1;
						err = 1;
						goto finish;
			default:		decompress_error(term, ce, cast_uchar "zlib", z.msg ? (unsigned char *)z.msg : (unsigned char *)"Unknown return value on inflate", errp);
						err = 1;
						break;
		}
		if (!z.avail_out) {
			size_t addsize;
			if (decoder_memory_expand(&p, size, &addsize) < 0)
				goto mem_error;
			z.next_out = p + size;
			z.avail_out = (unsigned)addsize;
			size += addsize;
		}
		if (z.avail_in) goto repeat_frag;
		/* In zlib 1.1.3, inflate(Z_SYNC_FLUSH) doesn't work.
		   The following line fixes it --- for last fragment, loop until
		   we get an eof. */
		if (r == Z_OK && f->list_entry.next == &ce->frag) goto repeat_frag;
		offset += f->length;
	}
	finish:
	r = inflateEnd(&z);
	end_failed:
	switch (r) {
		case Z_OK:		break;
		case Z_STREAM_ERROR:	decompress_error(term, ce, cast_uchar "zlib", z.msg ? (unsigned char *)z.msg : (unsigned char *)"Internal error on inflateEnd", errp);
					err = 1;
					break;
		case Z_MEM_ERROR:	memory_error = 1;
					err = 1;
					break;
		default:		decompress_error(term, ce, cast_uchar "zlib", z.msg ? (unsigned char *)z.msg : (unsigned char *)"Unknown return value on inflateEnd", errp);
					err = 1;
					break;
	}
	after_inflateend:
	if (memory_error) {
		mem_free(p);
		if (out_of_memory(0, NULL, 0))
			goto retry_after_memory_error;
		decompress_error(term, ce, cast_uchar "zlib", z.msg ? (unsigned char *)z.msg : TEXT_(T_OUT_OF_MEMORY), errp);
		return 1;
	}
	if (err && (unsigned char *)z.next_out == p) {
		mem_free(p);
		return 1;
	}
	ce->decompressed = p;
	ce->decompressed_len = (unsigned char *)z.next_out - (unsigned char *)p;
	decompressed_cache_size += ce->decompressed_len;
	ce->decompressed = mem_realloc(ce->decompressed, ce->decompressed_len);
	return 0;
}
#endif

#ifdef HAVE_BROTLI
#if defined(__TINYC__) && defined(__STDC_VERSION__)
#undef __STDC_VERSION__
#endif
#include <brotli/decode.h>
static void *brotli_alloc(void *opaque, size_t size)
{
	return mem_alloc_mayfail(size);
}
static void brotli_free(void *opaque, void *ptr)
{
	if (ptr) mem_free(ptr);
}
static int decode_brotli(struct terminal *term, struct cache_entry *ce, int *errp)
{
	unsigned char err;
	BrotliDecoderState *br;
	const unsigned char *next_in;
	size_t avail_in;
	unsigned char *next_out;
	size_t avail_out;
	off_t offset;
	BrotliDecoderResult res;
	unsigned char *p;
	struct fragment *f;
	struct list_head *lf;
	size_t size;

	err = 0;
	decoder_memory_init(&p, &size, ce->length);
	next_out = p;
	avail_out = size;
	br = BrotliDecoderCreateInstance(brotli_alloc, brotli_free, NULL);
	if (!br) {
		decompress_error(term, ce, cast_uchar "brotli", TEXT_(T_OUT_OF_MEMORY), errp);
		err = 1;
		goto after_inflateend;
	}

	offset = 0;
	foreach(struct fragment, f, lf, ce->frag) {
		if (f->offset != offset) break;
		next_in = f->data;
		avail_in = (size_t)f->length;
		if ((off_t)avail_in != f->length) overalloc();
		repeat_frag:
		res = BrotliDecoderDecompressStream(br, &avail_in, &next_in, &avail_out, &next_out, NULL);
		if (res == BROTLI_DECODER_RESULT_ERROR) {
			decompress_error(term, ce, cast_uchar "brotli", cast_uchar BrotliDecoderErrorString(BrotliDecoderGetErrorCode(br)), errp);
			err = 1;
			goto finish;
		}
		if (!avail_out) {
			size_t addsize;
			if (decoder_memory_expand(&p, size, &addsize) < 0) {
				decompress_error(term, ce, cast_uchar "brotli", TEXT_(T_OUT_OF_MEMORY), errp);
				err = 1;
				goto finish;
			}
			next_out = p + size;
			avail_out = addsize;
			size += addsize;
			goto repeat_frag;
		}
		if (avail_in) goto repeat_frag;
		/*
		BrotliDecoderHasMoreOutput(br) returns BROTLI_BOOL which is defined differently for different compilers, so we must not use it
		if (BrotliDecoderHasMoreOutput(br)) goto repeat_frag;
		*/
		offset += f->length;
	}

	finish:
	BrotliDecoderDestroyInstance(br);
	after_inflateend:
	if (err && next_out == p) {
		mem_free(p);
		return 1;
	}
	ce->decompressed = p;
	ce->decompressed_len = next_out - p;
	decompressed_cache_size += ce->decompressed_len;
	ce->decompressed = mem_realloc(ce->decompressed, ce->decompressed_len);
	return 0;
}
#endif

#ifdef HAVE_ZSTD
#include <zstd.h>
static int decode_zstd(struct terminal *term, struct cache_entry *ce, int *errp)
{
	ZSTD_DStream *zstd_stream = NULL;
	struct fragment *f;
	struct list_head *lf;
	off_t offset;
#ifdef HAVE_LONG_LONG
	unsigned long long dec_size;
#else
	size_t dec_size;
#endif
	size_t ret;
	unsigned char *p = NULL;
	size_t size;
	ZSTD_inBuffer in;
	ZSTD_outBuffer out;

	zstd_stream = ZSTD_createDStream();
	if (!zstd_stream) {
mem_error:
		decompress_error(term, ce, cast_uchar "zstd", TEXT_(T_OUT_OF_MEMORY), errp);
		goto err_ret;
	}
	ret = ZSTD_initDStream(zstd_stream);
	if (ZSTD_isError(ret)) {
		decompress_error(term, ce, cast_uchar "zstd", cast_uchar ZSTD_getErrorName(ret), errp);
		goto err_ret;
	}

	dec_size = 0;
	if (!list_empty(ce->frag)) {
		f = list_struct(ce->frag.next, struct fragment);
		dec_size = ZSTD_getDecompressedSize(f->data, f->length);
		/*debug("dec size %llx\n", dec_size);*/
		if (ZSTD_isError(dec_size))
			dec_size = 0;
	}

	decoder_memory_init(&p, &size, dec_size && dec_size < MAXINT ? (off_t)dec_size : ce->length);

	out.dst = p;
	out.size = size;
	out.pos = 0;

	offset = 0;
	foreach(struct fragment, f, lf, ce->frag) {
		if (f->offset != offset) break;
		in.src = f->data;
		in.size = f->length;
		in.pos = 0;

repeat_frag:
		ret = ZSTD_decompressStream(zstd_stream, &out, &in);
		if (ZSTD_isError(ret)) {
			decompress_error(term, ce, cast_uchar "zstd", cast_uchar ZSTD_getErrorName(ret), errp);
			goto err_ret;
		}
		if (out.pos == out.size) {
			size_t addsize;
			if (decoder_memory_expand(&p, size, &addsize) < 0)
				goto mem_error;
			out.dst = p + size;
			out.size = addsize;
			out.pos = 0;
			size += addsize;
			goto repeat_frag;
		}
		if (in.pos < in.size)
			goto repeat_frag;

		offset += f->length;
	}

	ZSTD_freeDStream(zstd_stream);
	zstd_stream = NULL;

	ce->decompressed = p;
	ce->decompressed_len = (unsigned char *)out.dst + out.pos - p;
	decompressed_cache_size += ce->decompressed_len;

	ce->decompressed = mem_realloc(ce->decompressed, ce->decompressed_len);

	return 0;

err_ret:
	if (p)
		mem_free(p);
	if (zstd_stream)
		ZSTD_freeDStream(zstd_stream);
	return 1;
}
#endif

#ifdef HAVE_BZIP2
#include <bzlib.h>
static int decode_bzip2(struct terminal *term, struct cache_entry *ce, int *errp)
{
	unsigned char err;
	unsigned char memory_error;
	bz_stream z;
	off_t offset;
	int r;
	unsigned char *p;
	struct fragment *f;
	struct list_head *lf;
	size_t size;

	retry_after_memory_error:
	err = 0;
	memory_error = 0;
	decoder_memory_init(&p, &size, ce->length);
	memset(&z, 0, sizeof z);
	z.next_in = NULL;
	z.avail_in = 0;
	z.next_out = cast_char p;
	z.avail_out = (unsigned)size;
	z.bzalloc = NULL;
	z.bzfree = NULL;
	z.opaque = NULL;
	r = BZ2_bzDecompressInit(&z, 0, 0);
	init_failed:
	switch (r) {
		case BZ_OK:		break;
		case BZ_MEM_ERROR:	memory_error = 1;
					err = 1;
					goto after_inflateend;
		case BZ_PARAM_ERROR:
					decompress_error(term, ce, cast_uchar "bzip2", cast_uchar "Invalid parameter", errp);
					err = 1;
					goto after_inflateend;
		case BZ_CONFIG_ERROR:	decompress_error(term, ce, cast_uchar "bzip2", cast_uchar "Bzlib is miscompiled", errp);
					err = 1;
					goto after_inflateend;
		default:		decompress_error(term, ce, cast_uchar "bzip2", cast_uchar "Unknown return value on BZ2_bzDecompressInit", errp);
					err = 1;
					goto after_inflateend;
	}
	offset = 0;
	foreach(struct fragment, f, lf, ce->frag) {
		if (f->offset != offset) break;
		z.next_in = cast_char f->data;
		z.avail_in = (unsigned)f->length;
		if ((off_t)z.avail_in != f->length) overalloc();
		repeat_frag:
		r = BZ2_bzDecompress(&z);
		switch (r) {
			case BZ_OK:		break;
			case BZ_STREAM_END:
						r = BZ2_bzDecompressEnd(&z);
						if (r != BZ_OK) goto end_failed;
						r = BZ2_bzDecompressInit(&z, 0, 0);
						if (r != BZ_OK) goto init_failed;
						break;
			case BZ_DATA_ERROR_MAGIC:
			case BZ_DATA_ERROR:	decompress_error(term, ce, cast_uchar "bzip2", TEXT_(T_COMPRESSED_ERROR), errp);
						err = 1;
						goto finish;
			case BZ_PARAM_ERROR:	decompress_error(term, ce, cast_uchar "bzip2", cast_uchar "Internal error on BZ2_bzDecompress", errp);
						err = 1;
						goto finish;
			case BZ_MEM_ERROR:
			mem_error:		memory_error = 1;
						err = 1;
						goto finish;
			default:		decompress_error(term, ce, cast_uchar "bzip2", cast_uchar "Unknown return value on BZ2_bzDecompress", errp);
						err = 1;
						break;
		}
		if (!z.avail_out) {
			size_t addsize;
			if (decoder_memory_expand(&p, size, &addsize) < 0)
				goto mem_error;
			z.next_out = cast_char(p + size);
			z.avail_out = (unsigned)addsize;
			size += addsize;
			goto repeat_frag;
		}
		if (z.avail_in) goto repeat_frag;
		offset += f->length;
	}
	finish:
	r = BZ2_bzDecompressEnd(&z);
	end_failed:
	switch (r) {
		case BZ_OK:		break;
		case BZ_PARAM_ERROR:	decompress_error(term, ce, cast_uchar "bzip2", cast_uchar "Internal error on BZ2_bzDecompressEnd", errp);
					err = 1;
					break;
		case BZ_MEM_ERROR:	memory_error = 1;
					err = 1;
					break;
		default:		decompress_error(term, ce, cast_uchar "bzip2", cast_uchar "Unknown return value on BZ2_bzDecompressEnd", errp);
					err = 1;
					break;
	}
	after_inflateend:
	if (memory_error) {
		mem_free(p);
		if (out_of_memory(0, NULL, 0))
			goto retry_after_memory_error;
		decompress_error(term, ce, cast_uchar "bzip2", TEXT_(T_OUT_OF_MEMORY), errp);
		return 1;
	}
	if (err && (unsigned char *)z.next_out == p) {
		mem_free(p);
		return 1;
	}
	ce->decompressed = p;
	ce->decompressed_len = (unsigned char *)z.next_out - (unsigned char *)p;
	decompressed_cache_size += ce->decompressed_len;
	ce->decompressed = mem_realloc(ce->decompressed, ce->decompressed_len);
	return 0;
}
#endif

#ifdef HAVE_LZMA
#include <lzma.h>
#define internal internal_
static int decode_lzma(struct terminal *term, struct cache_entry *ce, int *errp)
{
	unsigned char err;
	unsigned char memory_error;
	lzma_stream z = LZMA_STREAM_INIT;
	off_t offset;
	int r;
	unsigned char *p;
	struct fragment *f;
	struct list_head *lf;
	size_t size;

	retry_after_memory_error:
	err = 0;
	memory_error = 0;
	decoder_memory_init(&p, &size, ce->length);
	z.next_in = NULL;
	z.avail_in = 0;
	z.next_out = p;
	z.avail_out = size;
	r = lzma_auto_decoder(&z, UINT64_MAX, 0);
	init_failed:
	switch (r) {
		case LZMA_OK:		break;
		case LZMA_MEM_ERROR:	memory_error = 1;
					err = 1;
					goto after_inflateend;
		case LZMA_OPTIONS_ERROR:
					decompress_error(term, ce, cast_uchar "lzma", cast_uchar "Invalid parameter", errp);
					err = 1;
					goto after_inflateend;
		case LZMA_PROG_ERROR:	decompress_error(term, ce, cast_uchar "lzma", cast_uchar "Lzma is miscompiled", errp);
					err = 1;
					goto after_inflateend;
		default:		decompress_error(term, ce, cast_uchar "lzma", cast_uchar "Unknown return value on lzma_auto_decoder", errp);
					err = 1;
					goto after_inflateend;
	}
	offset = 0;
	foreach(struct fragment, f, lf, ce->frag) {
		if (f->offset != offset) break;
		z.next_in = f->data;
		z.avail_in = (size_t)f->length;
		if ((off_t)z.avail_in != f->length) overalloc();
		repeat_frag:
		r = lzma_code(&z, LZMA_RUN);
		switch (r) {
			case LZMA_OK:
			case LZMA_NO_CHECK:
			case LZMA_UNSUPPORTED_CHECK:
			case LZMA_GET_CHECK:
						break;
			case LZMA_STREAM_END:
						lzma_end(&z);
						r = lzma_auto_decoder(&z, UINT64_MAX, 0);
						if (r != LZMA_OK) goto init_failed;
						break;
			case LZMA_MEM_ERROR:
			mem_error:		memory_error = 1;
						err = 1;
						goto finish;
			case LZMA_MEMLIMIT_ERROR:
						decompress_error(term, ce, cast_uchar "lzma", cast_uchar "Memory limit was exceeded", errp);
						err = 1;
						goto finish;
			case LZMA_FORMAT_ERROR:
			case LZMA_DATA_ERROR:
			case LZMA_BUF_ERROR:
						decompress_error(term, ce, cast_uchar "lzma", TEXT_(T_COMPRESSED_ERROR), errp);
						err = 1;
						goto finish;
			case LZMA_OPTIONS_ERROR:decompress_error(term, ce, cast_uchar "lzma", cast_uchar "File contains unsupported options", errp);
						err = 1;
						goto finish;
			case LZMA_PROG_ERROR:	decompress_error(term, ce, cast_uchar "lzma", cast_uchar "Lzma is miscompiled", errp);
						err = 1;
						goto finish;
			default:		decompress_error(term, ce, cast_uchar "lzma", cast_uchar "Unknown return value on lzma_code", errp);
						err = 1;
						break;
		}
		if (!z.avail_out) {
			size_t addsize;
			if (decoder_memory_expand(&p, size, &addsize) < 0)
				goto mem_error;
			z.next_out = p + size;
			z.avail_out = addsize;
			size += addsize;
			goto repeat_frag;
		}
		if (z.avail_in) goto repeat_frag;
		offset += f->length;
	}
	finish:
	lzma_end(&z);
	after_inflateend:
	if (memory_error) {
		mem_free(p);
		if (out_of_memory(0, NULL, 0))
			goto retry_after_memory_error;
		decompress_error(term, ce, cast_uchar "lzma", TEXT_(T_OUT_OF_MEMORY), errp);
		return 1;
	}
	if (err && (unsigned char *)z.next_out == p) {
		mem_free(p);
		return 1;
	}
	ce->decompressed = p;
	ce->decompressed_len = (unsigned char *)z.next_out - (unsigned char *)p;
	decompressed_cache_size += ce->decompressed_len;
	ce->decompressed = mem_realloc(ce->decompressed, ce->decompressed_len);
	return 0;
}
#endif

#ifdef HAVE_LZIP
#include <lzlib.h>
static int decode_lzip(struct terminal *term, struct cache_entry *ce, int *errp)
{
	unsigned char err;
	unsigned char memory_error;
	void *lz;
	off_t offset;
	int r;
	enum LZ_Errno le;
	unsigned char *p;
	struct fragment *f;
	struct list_head *lf;
	size_t size;
	size_t used_size;

	retry_after_memory_error:
	err = 0;
	memory_error = 0;
	decoder_memory_init(&p, &size, ce->length);
	used_size = 0;

	lz = LZ_decompress_open();
	if (!lz) {
		err = 1;
		memory_error = 1;
		goto after_inflateend;
	}
	if (LZ_decompress_errno(lz) != LZ_ok) {
lz_error:
		le = LZ_decompress_errno(lz);
		if (0)
mem_error:		le = LZ_mem_error;
		err = 1;
		if (le == LZ_mem_error) {
			memory_error = 1;
		} else if (!ce->incomplete) {
			decompress_error(term, ce, cast_uchar "lzip", cast_uchar LZ_strerror(le), errp);
		}
		goto finish;
	}

	offset = 0;
	foreach(struct fragment, f, lf, ce->frag) {
		unsigned char *current_ptr;
		int current_len;
		if (f->offset != offset) break;
		current_ptr = f->data;
		current_len = (int)f->length;
		while (current_len) {
			r = LZ_decompress_write(lz, current_ptr, current_len);
			if (r == -1)
				goto lz_error;
			current_ptr += r;
			current_len -= r;
			do {
				if (used_size == size) {
					size_t addsize;
					if (decoder_memory_expand(&p, size, &addsize) < 0)
						goto mem_error;
					size += addsize;
				}
				r = LZ_decompress_read(lz, p + used_size, (int)(size - used_size));
				if (r == -1)
					goto lz_error;
				used_size += r;
			} while (r);
		}
		offset += f->length;
	}
	r = LZ_decompress_finish(lz);
	if (r == -1)
		goto lz_error;
	while ((r = LZ_decompress_finished(lz)) == 0) {
		if (used_size == size) {
			size_t addsize;
			if (decoder_memory_expand(&p, size, &addsize) < 0)
				goto mem_error;
			size += addsize;
		}
		r = LZ_decompress_read(lz, p + used_size, (int)(size - used_size));
		if (r == -1)
			goto lz_error;
		used_size += r;
	}
	if (r == -1)
		goto lz_error;

finish:
	LZ_decompress_close(lz);
after_inflateend:
	if (memory_error) {
		mem_free(p);
		if (out_of_memory(0, NULL, 0))
			goto retry_after_memory_error;
		decompress_error(term, ce, cast_uchar "lzip", TEXT_(T_OUT_OF_MEMORY), errp);
		return 1;
	}
	if (err && !used_size) {
		mem_free(p);
		return 1;
	}
	ce->decompressed = p;
	ce->decompressed_len = used_size;
	decompressed_cache_size += ce->decompressed_len;
	ce->decompressed = mem_realloc(ce->decompressed, ce->decompressed_len);
	return 0;
}
#endif

int get_file_by_term(struct terminal *term, struct cache_entry *ce, unsigned char **start, size_t *len, int *errp)
{
	unsigned char *enc;
	struct fragment *fr;
	int e;
	if (errp) *errp = 0;
	*start = NULL;
	*len = 0;
	if (!ce) return 1;
	if (ce->decompressed) {
#if defined(HAVE_ANY_COMPRESSION)
		return_decompressed:
#endif
		*start = ce->decompressed;
		*len = ce->decompressed_len;
		return 0;
	}
	enc = get_content_encoding(ce->head, ce->url, 0);
	if (enc) {
#ifdef HAVE_ZLIB
		if (!casestrcmp(enc, cast_uchar "gzip") || !casestrcmp(enc, cast_uchar "x-gzip") || !casestrcmp(enc, cast_uchar "deflate")) {
			int defl = !casestrcmp(enc, cast_uchar "deflate");
			mem_free(enc);
			if (decode_gzip(term, ce, defl, errp)) goto uncompressed;
			goto return_decompressed;
		}
#endif
#ifdef HAVE_BROTLI
		if (!casestrcmp(enc, cast_uchar "br")) {
			mem_free(enc);
			if (decode_brotli(term, ce, errp)) goto uncompressed;
			goto return_decompressed;
		}
#endif
#ifdef HAVE_ZSTD
		if (!casestrcmp(enc, cast_uchar "zstd")) {
			mem_free(enc);
			if (decode_zstd(term, ce, errp)) goto uncompressed;
			goto return_decompressed;
		}
#endif
#ifdef HAVE_BZIP2
		if (!casestrcmp(enc, cast_uchar "bzip2")) {
			mem_free(enc);
			if (decode_bzip2(term, ce, errp)) goto uncompressed;
			goto return_decompressed;
		}
#endif
#ifdef HAVE_LZMA
		if (!casestrcmp(enc, cast_uchar "lzma") || !casestrcmp(enc, cast_uchar "lzma2")) {
			mem_free(enc);
			if (decode_lzma(term, ce, errp)) goto uncompressed;
			goto return_decompressed;
		}
#endif
#ifdef HAVE_LZIP
		if (!casestrcmp(enc, cast_uchar "lzip")) {
			mem_free(enc);
			if (decode_lzip(term, ce, errp)) goto uncompressed;
			goto return_decompressed;
		}
#endif
		mem_free(enc);
		goto uncompressed;
	}
	uncompressed:
	if ((e = defrag_entry(ce)) < 0) {
		unsigned char *msg = get_err_msg(e, term);
		if (display_error(term, TEXT_(T_ERROR), errp)) {
			unsigned char *u = display_url(term, ce->url, 1);
			msg_box(term, getml(u, NULL), TEXT_(T_ERROR), AL_CENTER, TEXT_(T_ERROR_LOADING), cast_uchar " ", u, cast_uchar ":\n\n", msg, MSG_BOX_END, NULL, 1, TEXT_(T_CANCEL), msg_box_null, B_ENTER | B_ESC);
		}
	}
	if (list_empty(ce->frag)) return 1;
	fr = list_struct(ce->frag.next, struct fragment);
	if (fr->offset || !fr->length) return 1;
	*start = fr->data;
	*len = fr->length;
	return 0;
}

int get_file(struct object_request *o, unsigned char **start, size_t *len)
{
	struct terminal *term;
	*start = NULL;
	*len = 0;
	if (!o) return 1;
	term = find_terminal(o->term);
	return get_file_by_term(term, o->ce, start, len, NULL);
}

void free_decompressed_data(struct cache_entry *e)
{
	if (e->decompressed) {
		if (decompressed_cache_size < e->decompressed_len)
			internal_error("free_decompressed_data: decompressed_cache_size underflow %lu, %lu", (unsigned long)decompressed_cache_size, (unsigned long)e->decompressed_len);
		decompressed_cache_size -= e->decompressed_len;
		e->decompressed_len = 0;
		mem_free(e->decompressed);
		e->decompressed = NULL;
	}
}

#ifdef HAVE_ANY_COMPRESSION

void add_compress_methods(unsigned char **s, int *l)
{
	int cl = 0;
#ifdef HAVE_ZLIB
	{
		if (!cl) cl = 1; else add_to_str(s, l, cast_uchar ", ");
		add_to_str(s, l, cast_uchar "ZLIB");
#ifdef zlib_version
		add_to_str(s, l, cast_uchar " (");
		add_to_str(s, l, (unsigned char *)zlib_version);
		add_chr_to_str(s, l, ')');
#endif
	}
#endif
#ifdef HAVE_BROTLI
	{
		unsigned bv = BrotliDecoderVersion();
		if (!cl) cl = 1; else add_to_str(s, l, cast_uchar ", ");
		add_to_str(s, l, cast_uchar "BROTLI");
		add_to_str(s, l, cast_uchar " (");
		add_num_to_str(s, l, bv >> 24);
		add_chr_to_str(s, l, '.');
		add_num_to_str(s, l, (bv >> 12) & 0xfff);
		add_chr_to_str(s, l, '.');
		add_num_to_str(s, l, bv & 0xfff);
		add_chr_to_str(s, l, ')');
	}
#endif
#ifdef HAVE_ZSTD
	{
		unsigned zv = ZSTD_versionNumber();
		if (!cl) cl = 1; else add_to_str(s, l, cast_uchar ", ");
		add_to_str(s, l, cast_uchar "ZSTD");
		add_to_str(s, l, cast_uchar " (");
		add_num_to_str(s, l, zv / 10000);
		add_chr_to_str(s, l, '.');
		add_num_to_str(s, l, zv / 100 % 100);
		add_chr_to_str(s, l, '.');
		add_num_to_str(s, l, zv % 100);
		add_chr_to_str(s, l, ')');
	}
#endif
#ifdef HAVE_BZIP2
	{
		unsigned char *b = (unsigned char *)BZ2_bzlibVersion();
		int bl = (int)strcspn(cast_const_char b, ",");
		if (!cl) cl = 1; else add_to_str(s, l, cast_uchar ", ");
		add_to_str(s, l, cast_uchar "BZIP2");
		add_to_str(s, l, cast_uchar " (");
		add_bytes_to_str(s, l, b, bl);
		add_chr_to_str(s, l, ')');
	}
#endif
#ifdef HAVE_LZMA
	{
		if (!cl) cl = 1; else add_to_str(s, l, cast_uchar ", ");
		add_to_str(s, l, cast_uchar "LZMA");
		add_to_str(s, l, cast_uchar " (");
		add_to_str(s, l, cast_uchar lzma_version_string());
		add_chr_to_str(s, l, ')');
	}
#endif
#ifdef HAVE_LZIP
	{
		if (!cl) cl = 1; else add_to_str(s, l, cast_uchar ", ");
		add_to_str(s, l, cast_uchar "LZIP");
		add_to_str(s, l, cast_uchar " (");
		add_to_str(s, l, cast_uchar LZ_version());
		add_chr_to_str(s, l, ')');
	}
#endif
}

#endif
