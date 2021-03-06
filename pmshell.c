/* pmshell.c
 * PMShell graphics driver
 * (c) 2002 Mikulas Patocka
 * This file is a part of the Links program, released under GPL.
 */

#include "cfg.h"

/*#define PM_DEBUG*/

#ifdef PM_DEBUG
#define debug_call(x) do { printf("%016llx, %d: ", (unsigned long long)get_time(), _gettid()); printf x; putchar('\n'); fflush(stdout); } while (0)
#else
#define debug_call(x)
#endif

#ifdef GRDRV_PMSHELL

#include "links.h"

static long pm_get_color(int rgb);
extern struct graphics_driver pmshell_driver;

#ifdef OS2

#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_GPI
#define INCL_WIN
#include <os2.h>

#include <sys/builtin.h>
#include <sys/fmutex.h>

#define PM_SPAWN_SUBPROC

/* Links on OS/2 crashes with 24-bit bpp and bitmaps wider than 10921 pixels,
   apparently there is something that expects 15-bit line stride in bitmap
   handling code in pmshell.
   We set this limit lower, to 128, so that we don't exhaust shared memory with
   big images */
#define OS2_MAX_BITMAP_SIZE	128

/* GpiDrawBits can't draw images with more than 32767 pixes in X direction.
   We'd better draw them line-by-line */
#define OS2_MAX_LINE_SIZE	8191

extern PPIB os2_pib;

static ULONG pm_hidden_frame = 0;
static ULONG pm_frame = (FCF_TITLEBAR | FCF_SYSMENU | FCF_SIZEBORDER | FCF_MINMAX | FCF_SHELLPOSITION | FCF_TASKLIST | FCF_NOBYTEALIGN);

static HAB hab_disp;
static HAB hab;
static HMQ hmq;
static HDC hdc_mem;
static HPS hps_mem;
static HWND hwnd_hidden;
static HPS hps_hidden;
static HPOINTER icon;
static HPAL hpal;

#define pm_use_palette	(hpal != NULLHANDLE && pmshell_driver.param->palette_mode)

static _fmutex pm_mutex;
static HEV pm_sem;
static ULONG pm_event_dummy;

static int pm_lock_init(void)
{
	if (_fmutex_create(&pm_mutex, 0))
		goto r0;
	if (DosCreateEventSem(NULL, &pm_sem, 0, 0))
		goto r1;
	return 0;

r1:
	_fmutex_checked_close(&pm_mutex);
r0:
	return -1;
}

static void pm_lock_close(void)
{
	APIRET r;
	r = DosCloseEventSem(pm_sem);
	if (r && r != ERROR_SEM_BUSY)
		fatal_exit("DoscloseEventSem failed: %ld", (long)r);
	_fmutex_checked_close(&pm_mutex);
}

static inline void pm_lock(void)
{
	_fmutex_request(&pm_mutex, _FMR_IGNINT);
}

static inline void pm_unlock(void)
{
	_fmutex_release(&pm_mutex);
}

static inline void pm_event_wait(void)
{
	APIRET r;
	wait_again:
	r = DosWaitEventSem(pm_sem, SEM_INDEFINITE_WAIT);
	if (r == ERROR_INTERRUPT)
		goto wait_again;
	if (r)
		fatal_exit("DosWaitEventSem failed: %ld", (long)r);
	if ((r = DosResetEventSem(pm_sem, &pm_event_dummy)))
		fatal_exit("DosResetEventSem failed: %ld", (long)r);
}

static inline void pm_event_signal(void)
{
	APIRET r;
	if ((r = DosPostEventSem(pm_sem)))
		fatal_exit("DosPostEventSem failed: %ld", (long)r);
}

static inline void SetCapture(HWND hwnd)
{
	WinSetCapture(HWND_DESKTOP, hwnd);
}

static inline void ReleaseCapture(void)
{
	WinSetCapture(HWND_DESKTOP, NULLHANDLE);
}

#define PM_START_DRAW()	do { } while (0)
#define PM_UNFLUSH()	do { } while (0)

#endif

#ifdef WIN

/*#define UNICODE*/
/*#define NO_STRICT*/

#include <windows.h>
#ifdef HAVE_WINDOWSX_H
#include <windowsx.h>
#endif
#include <pthread.h>

static HMODULE module_handle;
static pthread_t pthread_handle;
static int unicode_title_supported;
static int unicode_supported;
static ATOM window_class_atom;
static HDC screen_dc;
static HWND hwnd_hidden;
static HICON icon_big;
static HICON icon_small;

#define RECTL		RECT
#define xLeft		left
#define xRight		right
#define yTop		top
#define yBottom		bottom

#define WM_BUTTON1DOWN		WM_LBUTTONDOWN
#define WM_BUTTON1DBLCLK	WM_LBUTTONDBLCLK
#define WM_BUTTON2DOWN		WM_RBUTTONDOWN
#define WM_BUTTON2DBLCLK	WM_RBUTTONDBLCLK
#define WM_BUTTON3DOWN		WM_MBUTTONDOWN
#define WM_BUTTON3DBLCLK	WM_MBUTTONDBLCLK

#define WM_BUTTON1UP		WM_LBUTTONUP
#define WM_BUTTON2UP		WM_RBUTTONUP
#define WM_BUTTON3UP		WM_MBUTTONUP

#ifndef WM_XBUTTONDOWN
#define WM_XBUTTONDOWN		0x20b
#endif
#ifndef WM_XBUTTONDBLCLK
#define WM_XBUTTONDBLCLK	0x20d
#endif
#ifndef WM_XBUTTONUP
#define WM_XBUTTONUP		0x20c
#endif
#ifndef XBUTTON1
#define XBUTTON1		0x0001
#endif
#ifndef XBUTTON2
#define XBUTTON2		0x0002
#endif
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp)	((short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp)	((short)HIWORD(lp))
#endif

static HANDLE winapi_semaphore;
static HANDLE winapi_event;
static CRITICAL_SECTION winapi_draw_lock;
static tcount winapi_draw_progress;

static int pm_lock_init(void)
{
	winapi_semaphore = CreateSemaphoreA(NULL, 1, 1, NULL);
	if (!winapi_semaphore)
		goto r0;
	winapi_event = CreateEventA(NULL, FALSE, FALSE, NULL);
	if (!winapi_event)
		goto r1;
	InitializeCriticalSection(&winapi_draw_lock);
	winapi_draw_progress = 0;
	return 0;

r1:
	if (!CloseHandle(winapi_semaphore))
		fatal_exit("CloseHandle failed");
r0:
	return -1;
}

static void pm_lock_close(void)
{
	DeleteCriticalSection(&winapi_draw_lock);
	if (!CloseHandle(winapi_event))
		fatal_exit("CloseHandle failed");
	if (!CloseHandle(winapi_semaphore))
		fatal_exit("CloseHandle failed");
}

static inline void pm_lock(void)
{
	if (WaitForSingleObject(winapi_semaphore, INFINITE) == WAIT_FAILED)
		fatal_exit("WaitForSingleObject failed");
}

static inline void pm_unlock(void)
{
	if (!ReleaseSemaphore(winapi_semaphore, 1, NULL))
		fatal_exit("ReleaseSemaphore failed");
}

static inline void pm_event_wait(void)
{
	if (WaitForSingleObject(winapi_event, INFINITE) == WAIT_FAILED)
		fatal_exit("WaitForSingleObject failed");
}

static inline void pm_event_signal(void)
{
	if (!SetEvent(winapi_event))
		fatal_exit("SetEvent failed");
}

static void pm_do_flush(void *ignore)
{
	if (winapi_draw_progress & 1) {
		if (!GdiFlush())
			{/*error("GdiFlush failed: %u", (unsigned)GetLastError());*/}
		EnterCriticalSection(&winapi_draw_lock);
		winapi_draw_progress++;
		LeaveCriticalSection(&winapi_draw_lock);
	}
}

ATTR_NOINLINE static void pm_start_draw_slow(void)
{
	register_bottom_half(pm_do_flush, NULL);
	EnterCriticalSection(&winapi_draw_lock);
	winapi_draw_progress++;
	LeaveCriticalSection(&winapi_draw_lock);
}

static inline void PM_START_DRAW(void)
{
	if (!(winapi_draw_progress & 1))
		pm_start_draw_slow();
}

#define PM_UNFLUSH()	unregister_bottom_half(pm_do_flush, NULL)

#endif


#define PM_ALLOC_ZERO		1
#define PM_ALLOC_MAYFAIL	2

#if defined(WIN) && !defined(USE_WIN32_HEAP)

/* space allocated with malloc may span multiple mapped areas
   and SetDIBitsToDevice doesn't like it. So use HeapAlloc/HeapFree */

static void *pm_alloc(size_t size, int flags)
{
	void *data;
again:
	data = HeapAlloc(GetProcessHeap(), flags & PM_ALLOC_ZERO ? HEAP_ZERO_MEMORY : 0, size);
	if (!data) {
		if (out_of_memory(0, flags & PM_ALLOC_MAYFAIL ? NULL : cast_uchar "pm_alloc", size))
			goto again;
	}
	return data;
}

static void pm_free(void *ptr)
{
	HeapFree(GetProcessHeap(), 0, ptr);
}

#else

static void *pm_alloc(size_t size, int flags)
{
	if (!(flags & PM_ALLOC_MAYFAIL)) {
		if (!(flags & PM_ALLOC_ZERO))
			return mem_alloc(size);
		else
			return mem_calloc(size);
	} else {
		if (!(flags & PM_ALLOC_ZERO))
			return mem_alloc_mayfail(size);
		else
			return mem_calloc_mayfail(size);
	}
}

#define pm_free	mem_free

#endif


static BITMAPINFO *pm_bitmapinfo;
static int icon_set;

#define pm_class_name	"links"
#define pm_class_name_l	L"links"

#define E_KEY		1
#define E_MOUSE		2
#define E_REDRAW	3
#define E_RESIZE	4

struct pm_event {
	list_entry_1st
	int type;
	int x1, y1, x2, y2;
	list_entry_last
};

struct pm_window {
	list_entry_1st
	int x, y;
	unsigned char in;
	unsigned char minimized;
	struct pm_window *nxt;
	struct pm_window **prv;
#ifdef OS2
	HPS ps;
	HWND h;
#endif
#ifdef WIN
	HDC dc;
	HDC new_dc;
	HDC old_dc;
	HPEN pen_orig;
	HPEN pen_cache;
	HBRUSH brush_cache;
	int pen_cache_color;
	int brush_cache_color;
	tcount drawing_cookie;
	int timer_active;
#endif
	HWND hc;
	HRGN rgn;
	struct graphics_device *dev;
	int button;
	unsigned long lastpos;
	struct list_head queue;
	list_entry_last
};

#define WIN_HASH	64

static int HASH_VALUE(HWND hw)
{
	return (unsigned long)hw & (WIN_HASH - 1);
}

static struct pm_window *pm_windows[WIN_HASH];

static void pm_hash_window(struct pm_window *win)
{
	int pos = HASH_VALUE(win->hc);
	win->prv = &pm_windows[pos];
	if ((win->nxt = pm_windows[pos])) pm_windows[pos]->prv = &win->nxt;
	pm_windows[pos] = win;
}

static void pm_unhash_window(struct pm_window *win)
{
	if (win->nxt) win->nxt->prv = win->prv;
	*win->prv = win->nxt;
}

static inline struct pm_window *pm_lookup_window(HWND h)
{
	struct pm_window *win;
	for (win = pm_windows[HASH_VALUE(h)]; win && win->hc != h; win = win->nxt) ;
	return win;
}

#define pm_win(dev) ((struct pm_window *)dev->driver_data)

static int pm_pipe[2];

static unsigned char *pm_status;

static int pm_cp;

static struct list_head pm_event_windows = { &pm_event_windows, &pm_event_windows };

static unsigned char pm_flush_font_cache = 0;

static void pm_send_event(struct pm_window *win, int t, int x1, int y1, int x2, int y2)
{
	/* must be called with pm_lock */
	struct pm_event *ev;
	if ((ev = malloc(sizeof(struct pm_event)))) {
		ev->type = t;
		ev->x1 = x1, ev->y1 = y1;
		ev->x2 = x2, ev->y2 = y2;
		if (!win->in) {
			if (list_empty(pm_event_windows)) {
				int wr;
				EINTRLOOP(wr, write(pm_pipe[1], "x", 1));
			}
			add_to_list(pm_event_windows, win);
			win->in = 1;
		}
		add_to_list(win->queue, ev);
	}
}

static void pm_send_mouse_event(struct pm_window *win, int x1, int y1, int b)
{
	if (!list_empty(win->queue)) {
		struct pm_event *last = list_struct(win->queue.next, struct pm_event);
		if (last->type == E_MOUSE && last->x2 == b) {
			last->x1 = x1;
			last->y1 = y1;
			return;
		}
	}
	pm_send_event(win, E_MOUSE, x1, y1, b, 0);
}

static void pm_cancel_event(struct pm_window *win, int t, struct pm_event **pev)
{
	struct pm_event *ev;
	struct list_head *lev;
	if (pev) *pev = NULL;
	foreachback(struct pm_event, ev, lev, win->queue) if (ev->type == t) {
		if (pev) *pev = ev;
		else {
			del_from_list(ev);
			free(ev);
		}
		return;
	}
}

static void pm_resize(struct pm_window *win, RECTL *r)
{
	struct pm_event *ev;
	win->x = r->xRight;
	win->y = r->yTop;
	pm_cancel_event(win, E_REDRAW, NULL);
	pm_cancel_event(win, E_RESIZE, &ev);
	if (ev) {
		ev->x2 = r->xRight;
		ev->y2 = r->yTop;
	} else pm_send_event(win, E_RESIZE, 0, 0, r->xRight, r->yTop);
}

static void pm_redraw(struct pm_window *win, RECTL *r)
{
	struct pm_event *ev;
	pm_cancel_event(win, E_RESIZE, &ev);
	if (ev) return;
	pm_cancel_event(win, E_REDRAW, &ev);
	if (ev) {
		if (r->xLeft < ev->x1) ev->x1 = r->xLeft;
		if (r->xRight > ev->x2) ev->x2 = r->xRight;
		if (win->y - r->yTop < ev->y1) ev->y1 = win->y - r->yTop;
		if (win->y - r->yBottom > ev->y2) ev->y2 = win->y - r->yBottom;
		return;
	}
	pm_send_event(win, E_REDRAW, r->xLeft, win->y - r->yTop, r->xRight, win->y - r->yBottom);
}

#ifdef OS2

#define N_VK	0x42

static struct os2_key pm_vk_table[N_VK] = {
	{0, 0}, {0, 0}, {0, 0}, {0, 0}, {KBD_CTRL_C, 0}, {KBD_BS, 0}, {KBD_TAB, 0}, {KBD_TAB, KBD_SHIFT},
	{KBD_ENTER, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {KBD_ESC, 0},
	{' ', 0}, {KBD_PAGE_UP, 0}, {KBD_PAGE_DOWN, 0}, {KBD_END, 0}, {KBD_HOME, 0}, {KBD_LEFT, 0}, {KBD_UP, 0}, {KBD_RIGHT, 0},
	{KBD_DOWN, 0}, {0, 0}, {KBD_INS, 0}, {KBD_DEL, 0}, {0, 0}, {0, 0}, {KBD_ENTER, 0}, {0, 0},
	{KBD_F1, 0}, {KBD_F2, 0}, {KBD_F3, 0}, {KBD_F4, 0}, {KBD_F5, 0}, {KBD_F6, 0}, {KBD_F7, 0}, {KBD_F8, 0},
	{KBD_F9, 0}, {KBD_F10, 0}, {KBD_F11, 0}, {KBD_F12, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
	{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
	{0, 0}, {KBD_DEL, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
	{0, 0}, {0, 0}
};

#endif

static int win_x(struct pm_window *win)
{
#ifdef OS2
	int x = (short)(win->lastpos & 0xffff);
#endif
#ifdef WIN
	int x = GET_X_LPARAM(win->lastpos);
#endif
	if (x >= win->x) x = win->x - 1;
	if (x < 0) x = 0;
	return x;
}

static int win_y(struct pm_window *win)
{
#ifdef OS2
	int y = (short)(win->y - (win->lastpos >> 16));
#endif
#ifdef WIN
	int y = GET_Y_LPARAM(win->lastpos);
#endif
	if (y >= win->y) y = win->y - 1;
	if (y < 0) y = 0;
	return y;
}

static void pm_user_msg(unsigned long msg, void *mp1, void *mp2);

#ifdef OS2
static MRESULT EXPENTRY pm_window_proc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
#define mouse_pos	((unsigned)mp1)
#endif
#ifdef WIN
static LRESULT CALLBACK pm_window_proc(HWND hwnd, UINT msg, WPARAM mp1, LPARAM mp2)
#define mouse_pos	((unsigned long)mp2)
#endif
{
#ifdef OS2
	MRESULT ret;
	int k_usch;
	int scancode;
#endif
	int flags;
	long k_usvk;
	long k_fsflags;
	long key;
	struct pm_window *win;
	RECTL wr, ur;
	debug_call(("T: pm_window_proc: %lx %lx %p %p", hwnd, msg, mp1, mp2));
	if (hwnd == hwnd_hidden) {
		debug_call(("T: pm_user_msg"));
		pm_user_msg(msg, (void *)mp1, (void *)mp2);
	} else switch (msg) {
#ifdef OS2
		case WM_REALIZEPALETTE:
			pm_lock();
			if (pm_use_palette) {
				if ((win = pm_lookup_window(hwnd))) {
					ULONG pcclr;
					HWND hc = win->hc;
					HPS ps = win->ps;
					LONG nc;
					pm_unlock();
					nc = WinRealizePalette(hc, ps, &pcclr);
					if (nc == PAL_ERROR)
						error("WinRealizePalette failed: %lx", WinGetLastError(hab_disp));
					/*fprintf(stderr, "WinRealizePalette: %lu, %ld\n", pcclr, nc);*/
					pm_lock();
				}
			}
			if ((win = pm_lookup_window(hwnd))) {
				pm_flush_font_cache = 1;
				if (!WinQueryWindowRect(win->h, &wr)) {
					error("WinQueryWindowRect failed: %lx", WinGetLastError(hab_disp));
					memset(&wr, 0, sizeof wr);
				}
				pm_redraw(win, &wr);
			}
			pm_unlock();
			return 0;
#endif
		case WM_PAINT:
			debug_call(("T: WM_PAINT: pm_lock"));
			pm_lock();
#ifdef OS2
			debug_call(("T: WM_PAINT: WinQueryUpdateRect"));
			if (!WinQueryUpdateRect(hwnd, &ur)) {
				error("WinQueryUpdateRect failed: %lx", WinGetLastError(hab_disp));
				memset(&ur, 0, sizeof ur);
			}
			debug_call(("T: WM_PAINT: WinQueryWindowRect"));
			if (!WinQueryWindowRect(hwnd, &wr)) {
				error("WinQueryWindowRect failed: %lx", WinGetLastError(hab_disp));
				memset(&wr, 0, sizeof wr);
			}
			debug_call(("T: WM_PAINT: WinValidateRect"));
			if (!WinValidateRect(hwnd, &ur, FALSE))
				error("WinValidateRect failed: %lx", WinGetLastError(hab_disp));
#endif
#ifdef WIN
			GetUpdateRect(hwnd, &ur, FALSE);
			if (!GetClientRect(hwnd, &wr)) {
				memset(&wr, 0, sizeof wr);
			}
			wr.yTop = wr.yBottom;
			wr.yBottom = 0;
			if (!ValidateRect(hwnd, &ur))
				fatal_exit("ValidateRect failed");
#endif
			debug_call(("T: WM_PAINT: pm_lookup_window"));
			if (!(win = pm_lookup_window(hwnd))) {
				debug_call(("T: WM_PAINT: pm_lookup_window failed"));
				pm_unlock();
				debug_call(("T: WM_PAINT: return 0"));
				return 0;
			}
			if (win->minimized) {
				debug_call(("T: WM_PAINT: win->minimized"));
				pm_unlock();
				debug_call(("T: WM_PAINT: break"));
				break;
			}
#ifdef WIN
			ur.yTop = win->y - ur.yTop;
			ur.yBottom = win->y - ur.yBottom;
#endif
			if (wr.xRight != win->x || wr.yTop != win->y) {
#if 0
				if (WinQueryWindowRect(win->h, &wr)) {
					pm_default_window_width = wr.xRight;
					pm_default_window_height = wr.yTop;
				}
#endif
				debug_call(("T: WM_PAINT: pm_resize"));
#ifdef WIN
				KillTimer(win->hc, 0);
				KillTimer(win->hc, 1);
#endif
				pm_resize(win, &wr);
			} else {
				debug_call(("T: WM_PAINT: pm_redraw"));
				pm_redraw(win, &ur);
			}
			debug_call(("T: WM_PAINT: pm_unlock"));
			pm_unlock();
			debug_call(("T: WM_PAINT: return 0"));
			return 0;
#ifdef OS2
		case WM_MINMAXFRAME:
			debug_call(("T: WM_MINMAXFRAME: pm_lock"));
			pm_lock();
			debug_call(("T: WM_MINMAXFRAME: pm_lookup_window"));
			if (!(win = pm_lookup_window(hwnd))) {
				debug_call(("T: WM_MINMAXFRAME: pm_lookup_window failed"));
				pm_unlock();
				debug_call(("T: WM_MINMAXFRAME: return 0"));
				return 0;
			}
			if (((SWP *)mp1)->fl & (SWP_HIDE | SWP_MINIMIZE))
				win->minimized = 1;
			else
				win->minimized = 0;
			debug_call(("T: WM_MINMAXFRAME: minimized: %d, win->minimized", win->minimized));
			pm_unlock();
			debug_call(("T: WM_MINMAXFRAME: break"));
			break;
#endif
		case WM_CLOSE:
			debug_call(("T: WM_CLOSE"));
		case WM_QUIT:
			debug_call(("T: WM_QUIT: pm_lock"));
			pm_lock();
			debug_call(("T: WM_QUIT: pm_lookup_window"));
			if (!(win = pm_lookup_window(hwnd))) {
				debug_call(("T: WM_QUIT: pm_lookup_window failed"));
				pm_unlock();
				debug_call(("T: WM_QUIT: return 0"));
				return 0;
			}
			debug_call(("T: WM_QUIT: pm_send_event"));
			pm_send_event(win, E_KEY, KBD_CTRL_C, 0, 0, 0);
			debug_call(("T: WM_QUIT: pm_unlock"));
			pm_unlock();
			debug_call(("T: WM_QUIT: return 0"));
			return 0;
		case WM_CHAR:
			debug_call(("T: WM_CHAR"));
#ifdef WIN
		case WM_SYSCHAR:
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
#endif
#ifdef OS2
			k_fsflags = (int)mp1;
			scancode = ((unsigned long)mp1 >> 24) & 0xff;
			k_usch = (int)mp2 & 0xffff;
			k_usvk = ((int)mp2 >> 16) & 0xffff;

			/*
			 * F1 keydown is lost for unknown reason --- so catch
			 * keyup instead.
			 */
			if ((k_fsflags & (KC_VIRTUALKEY | KC_CHAR)) == KC_VIRTUALKEY && k_usvk == 0x20)
				k_fsflags ^= KC_KEYUP;

			if (k_fsflags & (KC_KEYUP | KC_DEADKEY | KC_INVALIDCOMP)) {
				debug_call(("T: WM_CHAR: return 0 @ 1"));
				return 0;
			}

			flags = (k_fsflags & KC_SHIFT ? KBD_SHIFT : 0) | (k_fsflags & KC_CTRL ? KBD_CTRL : 0) | (k_fsflags & KC_ALT ? KBD_ALT : 0);
			if (k_fsflags & KC_ALT && ((scancode >= 0x47 && scancode <= 0x49) || (scancode >= 0x4b && scancode <= 0x4d) || (scancode >= 0x4f && scancode <= 0x52))) {
				debug_call(("T: WM_CHAR: return 0 @ 2"));
				return 0;
			}
			if ((k_fsflags & (KC_VIRTUALKEY | KC_CHAR)) == KC_VIRTUALKEY) {
				if (k_usvk < N_VK && (key = pm_vk_table[k_usvk].x)) {
					flags |= pm_vk_table[k_usvk].y;
					if (key == KBD_CTRL_C) flags &= ~KBD_CTRL;
					goto s;
				}
			}
			if (k_usch & 0xff) {
				key = k_usch & 0xff;
				if (!(flags & KBD_CTRL)) {
					if (key == 0x0d) key = KBD_ENTER;
					if (key == 0x08) key = KBD_BS;
					if (key == 0x09) key = KBD_TAB;
					if (key == 0x1b) key = KBD_ESC;
				}
				if (key >= 0 && key < ' ') key += '@', flags |= KBD_CTRL;
			} else key = os2xtd[k_usch >> 8].x, flags |= os2xtd[k_usch >> 8].y;
			if ((key & 0xdf) == 'C' && (flags & KBD_CTRL)) key = KBD_CTRL_C, flags &= ~KBD_CTRL;
			s:
			if (!key) {
				debug_call(("T: WM_CHAR: return 0 @ 3"));
				return 0;
			}
			/*if (key >= 0) flags &= ~KBD_SHIFT;*/
			if (key >= 0x80 && pm_cp) {
				if ((key = cp2u(key, pm_cp)) < 0) {
					debug_call(("T: WM_CHAR: return 0 @ 4"));
					return 0;
				}
			}
#endif
#ifdef WIN
			k_fsflags = (long)mp2;
			flags = 0;
			if (k_fsflags & (1 << 29))
				flags |= KBD_ALT;
			if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
				/*fprintf(stderr, "vk: %lx %lx\n", (long)mp1, (long)mp2);*/
				k_usvk = (long)mp1;
				switch (k_usvk) {
					case VK_CANCEL: key = KBD_CTRL_C; break;
					case VK_PRIOR: key = KBD_PAGE_UP; break;
					case VK_NEXT: key = KBD_PAGE_DOWN; break;
					case VK_END: key = KBD_END; break;
					case VK_HOME: key = KBD_HOME; break;
					case VK_LEFT: key = KBD_LEFT; break;
					case VK_RIGHT: key = KBD_RIGHT; break;
					case VK_DOWN: key = KBD_DOWN; break;
					case VK_UP: key = KBD_UP; break;
					case VK_INSERT: key = KBD_INS; break;
					case VK_DELETE: key = KBD_DEL; break;
					case VK_F1: key = KBD_F1; break;
					case VK_F2: key = KBD_F2; break;
					case VK_F3: key = KBD_F3; break;
					case VK_F4: key = KBD_F4; break;
					case VK_F5: key = KBD_F5; break;
					case VK_F6: key = KBD_F6; break;
					case VK_F7: key = KBD_F7; break;
					case VK_F8: key = KBD_F8; break;
					case VK_F9: key = KBD_F9; break;
					case VK_F10: key = KBD_F10; break;
					case VK_F11: key = KBD_F11; break;
					case VK_F12: key = KBD_F12; break;
#ifdef VK_HELP
					case VK_HELP: key = KBD_HELP; break;
#endif
#ifdef VK_APPS
					case VK_APPS: key = KBD_MENU; break;
#endif
#ifdef VK_BROWSER_BACK
					case VK_BROWSER_BACK: key = KBD_BACK; break;
#endif
#ifdef VK_BROWSER_FORWARD
					case VK_BROWSER_FORWARD: key = KBD_FORWARD; break;
#endif
#ifdef VK_BROWSER_REFRESH
					case VK_BROWSER_REFRESH: key = KBD_RELOAD; break;
#endif
#ifdef VK_BROWSER_STOP
					case VK_BROWSER_STOP: key = KBD_STOP; break;
#endif
#ifdef VK_BROWSER_SEARCH
					case VK_BROWSER_SEARCH: key = KBD_FIND; break;
#endif
#ifdef VK_BROWSER_FAVORITES
					case VK_BROWSER_FAVORITES: key = KBD_BOOKMARKS; break;
#endif
#ifdef VK_BROWSER_HOME
					case VK_BROWSER_HOME: key = KBD_OPEN; break;
#endif
					default: return 0;
				}
			} else {
				/*fprintf(stderr, "ch: %lx %lx\n", (long)mp1, (long)mp2);*/
				key = (long)mp1;
				if (key <= 0) return 0;
				if (key > 0 && key < 0x20) {
					if (key == 3) key = KBD_CTRL_C;
					else if (key == 8) key = KBD_BS;
					else if (key == 9) key = KBD_TAB;
					else if (key == 10) key = KBD_ENTER;
					else if (key == 13) key = KBD_ENTER;
					else if (key == 27) key = KBD_ESC;
					else if (key == 127) key = KBD_BS;
					else {
						key += 'A' - 1;
						flags |= KBD_CTRL;
					}
				} else if (!unicode_supported) {
					key = cp2u(key, pm_cp);
				}
			}
#endif
			debug_call(("T: WM_CHAR: pm_lock"));
			pm_lock();
			debug_call(("T: WM_CHAR: pm_lookup_window"));
			if (!(win = pm_lookup_window(hwnd))) {
				debug_call(("T: WM_CHAR: pm_lookup_window failed"));
				pm_unlock();
				debug_call(("T: WM_CHAR: return 0"));
				return 0;
			}
			debug_call(("T: WM_CHAR: pm_send_event"));
			pm_send_event(win, E_KEY, key, flags, 0, 0);
			debug_call(("T: WM_CHAR: pm_unlock"));
			pm_unlock();
			debug_call(("T: WM_CHAR: return 0"));
			return 0;
		case WM_BUTTON1DOWN:
		case WM_BUTTON1DBLCLK:
			flags = B_LEFT;
			goto button_down;
		case WM_BUTTON2DOWN:
		case WM_BUTTON2DBLCLK:
			flags = B_RIGHT;
			goto button_down;
		case WM_BUTTON3DOWN:
		case WM_BUTTON3DBLCLK:
			flags = B_MIDDLE;
			goto button_down;
		button_down:
			debug_call(("T: BUTTON_DOWN: pm_lock"));
			pm_lock();
			debug_call(("T: BUTTON_DOWN: pm_lookup_window"));
			if (!(win = pm_lookup_window(hwnd))) {
				debug_call(("T: BUTTON_DOWN: pm_lookup_window failed"));
				pm_unlock();
				debug_call(("T: BUTTON_DOWN: break"));
				break;
			}
			win->button |= 1 << flags;
			win->lastpos = mouse_pos;
			debug_call(("T: BUTTON_DOWN: pm_send_event (%d)", flags));
			pm_send_event(win, E_MOUSE, win_x(win), win_y(win), B_DOWN | flags, 0);
			debug_call(("T: BUTTON_DOWN: pm_unlock"));
			pm_unlock();
			debug_call(("T: BUTTON_DOWN: SetCapture"));
			SetCapture(hwnd);
			debug_call(("T: BUTTON_DOWN: break"));
			break;
		case WM_BUTTON1UP:
#ifdef OS2
		case WM_BUTTON1MOTIONEND:
#endif
			flags = B_LEFT;
			goto button_up;
		case WM_BUTTON2UP:
#ifdef OS2
		case WM_BUTTON2MOTIONEND:
#endif
			flags = B_RIGHT;
			goto button_up;
		case WM_BUTTON3UP:
#ifdef OS2
		case WM_BUTTON3MOTIONEND:
#endif
			flags = B_MIDDLE;
			goto button_up;
		button_up:
			debug_call(("T: BUTTON_UP: pm_lock"));
			pm_lock();
			debug_call(("T: BUTTON_UP: pm_lookup_window"));
			if (!(win = pm_lookup_window(hwnd))) {
				debug_call(("T: BUTTON_UP: pm_lookup_window failed"));
				pm_unlock();
				debug_call(("T: BUTTON_UP: break"));
				break;
			}
			if (msg == WM_BUTTON1UP) win->lastpos = mouse_pos;
			if (win->button & (1 << flags)) {
				debug_call(("T: BUTTON_UP: pm_send_event (%d)", flags));
				pm_send_event(win, E_MOUSE, win_x(win), win_y(win), B_UP | flags, 0);
			}
			win->button &= ~(1 << flags);
			debug_call(("T: BUTTON_UP: pm_unlock"));
			pm_unlock();
			if (!win->button) {
				debug_call(("T: BUTTON_UP: ReleaseCapture"));
				ReleaseCapture();
			}
			debug_call(("T: BUTTON_UP: break"));
			break;
#ifdef WIN
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
			if (HIWORD(mp1) == XBUTTON1)
				flags = B_FOURTH;
			else if (HIWORD(mp1) == XBUTTON2)
				flags = B_FIFTH;
			else break;
			goto button_down;
		case WM_XBUTTONUP:
			if (HIWORD(mp1) == XBUTTON1)
				flags = B_FOURTH;
			else if (HIWORD(mp1) == XBUTTON2)
				flags = B_FIFTH;
			else break;
			goto button_up;
#endif
		case WM_MOUSEMOVE:
			debug_call(("T: WM_MOUSEMOVE: pm_lock"));
			pm_lock();
			debug_call(("T: WM_MOUSEMOVE: pm_lookup_window"));
			if (!(win = pm_lookup_window(hwnd))) {
				debug_call(("T: WM_MOUSEMOVE: pm_lookup_window failed"));
				pm_unlock();
				debug_call(("T: WM_MOUSEMOVE: break"));
				break;
			}
#ifdef OS2
			debug_call(("T: WM_MOUSEMOVE: %x -> WinQueryCapture", win->button));
			if (win->button && WinQueryCapture(HWND_DESKTOP) != hwnd) {
				debug_call(("T: WM_MOUSEMOVE: clear buttons"));
				for (flags = B_LEFT; flags <= B_SIXTH; flags++)
					if (win->button & (1 << flags))
						pm_send_event(win, E_MOUSE, win_x(win), win_y(win), B_UP | flags, 0);
				win->button = 0;
			}
#endif
			if (win->lastpos == mouse_pos) {
				debug_call(("T: WM_MOUSEMOVE: pm_unlock"));
				pm_unlock();
				debug_call(("T: WM_MOUSEMOVE: break"));
				break;
			}
			win->lastpos = mouse_pos;
			debug_call(("T: WM_MOUSEMOVE: pm_send_mouse_event"));
			pm_send_mouse_event(win, win_x(win), win_y(win),
				(win->button ? B_DRAG : B_MOVE) |
				(win->button & (1 << B_LEFT) ? B_LEFT :
				win->button & (1 << B_MIDDLE) ? B_MIDDLE :
				win->button & (1 << B_RIGHT) ? B_RIGHT :
				win->button & (1 << B_FOURTH) ? B_FOURTH :
				win->button & (1 << B_FIFTH) ? B_FIFTH :
				win->button & (1 << B_SIXTH) ? B_SIXTH :
				0));
			debug_call(("T: WM_MOUSEMOVE: pm_unlock"));
			pm_unlock();
			debug_call(("T: WM_MOUSEMOVE: break"));
			break;
#ifdef WIN
		case WM_CAPTURECHANGED:
			pm_lock();
			if (!(win = pm_lookup_window(hwnd))) { pm_unlock(); break; }
			if ((HWND)mp2 == win->hc) { pm_unlock(); break; }
			for (flags = B_LEFT; flags <= B_SIXTH; flags++)
				if (win->button & (1 << flags))
					pm_send_event(win, E_MOUSE, win_x(win), win_y(win), B_UP | flags, 0);
			win->button = 0;
			pm_unlock();
			break;
#endif
#ifdef OS2
		case WM_VSCROLL:
			debug_call(("T: VM_VSCROLL: pm_lock"));
			pm_lock();
			debug_call(("T: VM_VSCROLL: pm_lookup_window"));
			if (!(win = pm_lookup_window(hwnd))) {
				debug_call(("T: VM_VSCROLL: pm_lookup_window failed"));
				pm_unlock();
				debug_call(("T: VM_VSCROLL: break"));
				break;
			}
			if ((unsigned long)mp2 == SB_LINEUP << 16 || (unsigned long)mp2 == SB_LINEDOWN << 16) {
				debug_call(("T: VM_VSCROLL: pm_send_event"));
				pm_send_event(win, E_MOUSE, win_x(win), win_y(win), ((unsigned long)mp2 == SB_LINEUP << 16 ? B_WHEELUP1 : B_WHEELDOWN1) | B_MOVE, 0);
			}
			debug_call(("T: VM_VSCROLL: pm_unlock"));
			pm_unlock();
			debug_call(("T: VM_VSCROLL: break"));
			break;
		case WM_HSCROLL:
			debug_call(("T: VM_HSCROLL: pm_lock"));
			pm_lock();
			debug_call(("T: VM_HSCROLL: pm_lookup_window"));
			if (!(win = pm_lookup_window(hwnd))) {
				debug_call(("T: VM_HSCROLL: pm_lookup_window failed"));
				pm_unlock();
				debug_call(("T: VM_HSCROLL: break"));
				break;
			}
			if ((unsigned long)mp2 == SB_LINELEFT << 16 || (unsigned long)mp2 == SB_LINERIGHT << 16) {
				debug_call(("T: VM_HSCROLL: pm_send_event"));
				pm_send_event(win, E_MOUSE, win_x(win), win_y(win), ((unsigned long)mp2 == SB_LINELEFT << 16 ? B_WHEELLEFT1 : B_WHEELRIGHT1) | B_MOVE, 0);
			}
			debug_call(("T: VM_HSCROLL: pm_unlock"));
			pm_unlock();
			debug_call(("T: VM_HSCROLL: break"));
			break;
#endif
#ifdef WIN
		case WM_MOUSEWHEEL:
		case 0x020e:
			flags = HIWORD(mp1);
			if (!flags) break;
			pm_lock();
			if (!(win = pm_lookup_window(hwnd))) { pm_unlock(); break; }
			pm_send_event(win, E_MOUSE, win_x(win), win_y(win), (flags > 0 && flags < 0x8000 ? (msg == WM_MOUSEWHEEL ? B_WHEELUP : B_WHEELRIGHT) : (msg == WM_MOUSEWHEEL ? B_WHEELDOWN: B_WHEELLEFT)) | B_MOVE, 0);
			pm_unlock();
			break;
#endif
#ifdef WIN
		case WM_TIMER:
			if (!GetClientRect(hwnd, &wr)) {
				break;
			}
			wr.yTop = wr.yBottom;
			wr.yBottom = 0;
			pm_lock();
			if ((win = pm_lookup_window(hwnd))) {
				KillTimer(win->hc, 0);
				KillTimer(win->hc, 1);
				win->timer_active = 0;
				pm_redraw(win, &wr);
			}
			pm_unlock();
			break;
		case WM_WINDOWPOSCHANGING:
			if (!is_winnt())
				break;
			pm_lock();
			if ((win = pm_lookup_window(hwnd)) && win->dc) {
				EnterCriticalSection(&winapi_draw_lock);
				win->drawing_cookie = winapi_draw_progress;
				LeaveCriticalSection(&winapi_draw_lock);
			}
			pm_unlock();
			break;
		case WM_WINDOWPOSCHANGED:
			if (!is_winnt())
				break;
			pm_lock();
			if ((win = pm_lookup_window(hwnd)) && win->dc) {
				int not_racing;
				EnterCriticalSection(&winapi_draw_lock);
				not_racing = win->drawing_cookie == winapi_draw_progress && !(winapi_draw_progress & 1);
				LeaveCriticalSection(&winapi_draw_lock);
				if (not_racing)
					goto skip_windowposchanged;
				if (!win->new_dc)
					win->new_dc = GetDC(win->hc);
				if (win->old_dc) {
					if (!ReleaseDC(win->hc, win->old_dc))
						error("ReleaseDC failed %u", (unsigned)GetLastError());
					win->old_dc = NULL;
				}
				SetTimer(win->hc, 0, WIN32_REPAINT_IDLE_TIME, NULL);
				if (!win->timer_active) {
					SetTimer(win->hc, 1, WIN32_REPAINT_BUSY_TIME, NULL);
					win->timer_active = 1;
				}
			}
			skip_windowposchanged:
			pm_unlock();
			break;
#endif
	}
#ifdef OS2
	debug_call(("T: WinDefWindowProc: %lx %lx %p %p", hwnd, msg, mp1, mp2));
	ret = WinDefWindowProc(hwnd, msg, mp1, mp2);
	debug_call(("T: WinDefWindowProc returned %p", ret));
	return ret;
#endif
#ifdef WIN
	if (!unicode_supported)
		return DefWindowProcA(hwnd, msg, mp1, mp2);
	else
		return DefWindowProcW(hwnd, msg, mp1, mp2);
#endif
}

static int pm_thread_shutdown;

#define MSG_CREATE_WINDOW	(WM_USER + 0)
#define MSG_DELETE_WINDOW	(WM_USER + 1)
#define MSG_SET_WINDOW_TITLE	(WM_USER + 2)
#define MSG_SHUTDOWN_THREAD	(WM_USER + 3)

static void pm_user_msg(unsigned long msg, void *mp1, void *mp2)
{
	struct pm_window *win;
	switch (msg) {
		case MSG_CREATE_WINDOW:
			win = mp1;
#ifdef OS2
			debug_call(("T: WinCreateStdWindow"));
			win->h = WinCreateStdWindow(HWND_DESKTOP, 0, &pm_frame, pm_class_name, "Links", 0, 0, 0, &win->hc);
			debug_call(("T: WinCreateStdWindow: %lx", win->h));
			if (win->h != NULLHANDLE) {
				if (icon != NULLHANDLE) {
					debug_call(("T: WinSendMsg"));
					WinSendMsg(win->h, WM_SETICON, (void *)icon, 0);
				}
				debug_call(("T: WinSetWindowPos"));
				if (!WinSetWindowPos(win->h, HWND_TOP, 0, 0, 0, 0, SWP_ACTIVATE | SWP_SHOW | SWP_ZORDER))
					error("WinSetWindowPos failed: %lx", WinGetLastError(hab_disp));
			}
#endif
#ifdef WIN
			if (unicode_supported) {
				win->hc = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_LEFT, (void *)(unsigned long)window_class_atom, L"Links", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, module_handle, NULL);
			} else {
				win->hc = CreateWindowExA(WS_EX_APPWINDOW | WS_EX_LEFT, (void *)(unsigned long)window_class_atom, "Links", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, module_handle, NULL);
			}
			if (!win->hc)
				goto fail_createwindowex;
			if ((win->dc = GetDC(win->hc)) == NULL)
				goto fail_getdc;
			if (unicode_supported) {
				if (icon_big != NULL) SendMessageW(win->hc, WM_SETICON, ICON_BIG, (LPARAM)icon_big);
				if (icon_small != NULL) SendMessageW(win->hc, WM_SETICON, ICON_SMALL, (LPARAM)icon_small);
			} else {
				if (icon_big != NULL) SendMessageA(win->hc, WM_SETICON, ICON_BIG, (LPARAM)icon_big);
				if (icon_small != NULL) SendMessageA(win->hc, WM_SETICON, ICON_SMALL, (LPARAM)icon_small);
			}
			ShowWindow(win->hc, SW_SHOW);
			/*SetWindowPos(win->hc, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);*/
			SetForegroundWindow(win->hc);
#endif
			debug_call(("T: pm_lock"));
			pm_lock();
			debug_call(("T: pm_event_signal"));
			pm_event_signal();
			debug_call(("T: break"));
			break;
		case MSG_DELETE_WINDOW:
			win = mp1;
#ifdef OS2
			debug_call(("T: WinDestroyWindow"));
			if (!WinDestroyWindow(win->h))
				fatal_exit("WinDestroyWindow failed");
#endif
#ifdef WIN
			if (win->dc) {
				if (!ReleaseDC(win->hc, win->dc))
					error("ReleaseDC failed %u", (unsigned)GetLastError());
				win->dc = NULL;
			}
			if (win->old_dc) {
				if (!ReleaseDC(win->hc, win->old_dc))
					error("ReleaseDC failed %u", (unsigned)GetLastError());
				win->old_dc = NULL;
			}
			if (win->new_dc) {
				if (!ReleaseDC(win->hc, win->new_dc))
					error("ReleaseDC failed %u", (unsigned)GetLastError());
				win->new_dc = NULL;
			}
fail_getdc:
			if (!DestroyWindow(win->hc))
				fatal_exit("DestroyWindow failed");
fail_createwindowex:
			win->hc = NULL;
#endif
			debug_call(("T: pm_lock"));
			pm_lock();
			debug_call(("T: pm_event_signal"));
			pm_event_signal();
			debug_call(("T: break"));
			break;
		case MSG_SET_WINDOW_TITLE:
			win = mp1;
#ifdef OS2
			debug_call(("T: WinSetWindowText"));
			WinSetWindowText(win->h, mp2);
#endif
#ifdef WIN
			if (unicode_supported && unicode_title_supported)
				SetWindowTextW(win->hc, mp2);
			else
				SetWindowTextA(win->hc, mp2);
#endif
			debug_call(("T: pm_lock"));
			pm_lock();
			debug_call(("T: pm_event_signal"));
			pm_event_signal();
			debug_call(("T: break"));
			break;
		case MSG_SHUTDOWN_THREAD:
			debug_call(("T: pm_thread_shutdown"));
			pm_thread_shutdown = 1;
			break;
	}
}

static void pm_send_msg(int msg, void *param, void *param2)
{
#ifdef OS2
	debug_call(("M: calling WinPostMsg(%d, %p, %p)", msg, param, param2));
	while (!WinPostMsg(hwnd_hidden, msg, (MPARAM)param, (MPARAM)param2)) {
		debug_call(("M: WinPostMsg failed: %lx", WinGetLastError(hab)));
		portable_sleep(1000);
	}
	debug_call(("M: WinPostMsg succeeded"));
#endif
#ifdef WIN
	BOOL r;
	if (!GdiFlush())
		{/*error("GdiFlush failed: %u", (unsigned)GetLastError());*/}
	retry:
	if (!unicode_supported)
		r = PostMessageA(hwnd_hidden, msg, (unsigned long)param, (unsigned long)param2);
	else
		r = PostMessageW(hwnd_hidden, msg, (unsigned long)param, (unsigned long)param2);
	if (!r) {
		DWORD err = GetLastError();
		if (err == ERROR_NOT_ENOUGH_QUOTA) {
			portable_sleep(1000);
			goto retry;
		}
		fatal_exit("PostMessage failed: %x", (unsigned)GetLastError());
	}
#endif
	pm_event_wait();
	debug_call(("M: pm_event_wait succeeded"));
#ifdef WIN
	if (msg == MSG_SHUTDOWN_THREAD) {
		if (pthread_join(pthread_handle, NULL))
			fatal_exit("pthread_join failed");
	}
#endif
}

static void pm_dispatcher(void *p)
{
#ifdef OS2
	QMSG msg;
	HWND hwnd_hidden_h;
#endif
#ifdef WIN
	MSG msg;
	HCURSOR hcursor;
	WNDCLASSEXW class_w;
	WNDCLASSEXA class_a;
#endif
	pm_status = NULL;
#ifdef OS2
	/*DosSetPriority(PRTYS_THREAD, PRTYC_FOREGROUNDSERVER, 1, 0);*/
	/*DosSetPriority(PRTYS_THREAD, PRTYC_TIMECRITICAL, 1, 0);*/
	DosSetPriority(PRTYS_THREAD, PRTYC_NOCHANGE, 1, 0);
	if ((hab_disp = WinInitialize(0)) == NULLHANDLE) {
		pm_status = cast_uchar "WinInitialize failed in pm thread.\n";
		goto os2_fail0;
	}
	if ((hmq = WinCreateMsgQueue(hab_disp, 0)) == NULLHANDLE) {
		ERRORID e = WinGetLastError(hab_disp);
		if ((e & 0xffff) == PMERR_NOT_IN_A_PM_SESSION) pm_status = cast_uchar "Not in a pmshell session.\n";
		else pm_status = cast_uchar "WinCreateMsgQueue failed in pm thread.\n";
		goto os2_fail1;
	}
	if ((pm_cp = WinQueryCp(hmq))) {
		unsigned char a[12];
		snprint(a, 12, pm_cp);
		if ((pm_cp = get_cp_index(a)) < 0 || pm_cp == utf8_table) pm_cp = 0;
	}
	/*{
		ULONG cp_list[100];
		int n, i;
		debug("WinQueryCp: %d", WinQueryCp(hmq));
		n = WinQueryCpList(hab_disp, 100, cp_list);
		debug("%d", n);
		for (i = 0; i < n; i++) fprintf(stderr, "%d, ", cp_list[i]);
	}*/
	if (WinRegisterClass(hab_disp, pm_class_name, pm_window_proc, CS_SIZEREDRAW, 0) == FALSE) {
		pm_status = cast_uchar "WinRegisterClass failed for.\n";
		goto os2_fail2;
	}
	if ((hwnd_hidden_h = WinCreateStdWindow(HWND_DESKTOP, 0, &pm_hidden_frame, pm_class_name, NULL, 0, 0, 0, &hwnd_hidden)) == NULLHANDLE) {
		pm_status = cast_uchar "Could not create hidden window.\n";
		goto os2_fail3;
	}
	if ((hps_hidden = WinGetPS(hwnd_hidden)) == NULLHANDLE) {
		pm_status = cast_uchar "Could not get hidden window ps.\n";
		goto os2_fail4;
	}
#if 0
	{
		int c, d;
		LONG colors[256];
		c = GpiQueryRealColors(hps_hidden, 0, 0, 256, colors);
		if (c == GPI_ALTERROR) {
			error("GpiQueryRealColors failed: %lx", WinGetLastError(hab_disp));
		} else {
			for (d = 0; d < c; d++)
				fprintf(stderr, "color[%03d] = %06lx\n", d, colors[d]);
		}
	}
#endif
#endif
#ifdef WIN
	{
		unsigned long (WINAPI *f_SetThreadDpiAwarenessContext)(unsigned long ctx);
		f_SetThreadDpiAwarenessContext = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "SetThreadDpiAwarenessContext");
		/*debug("result: %p", f_SetThreadDpiAwarenessContext);*/
		if (f_SetThreadDpiAwarenessContext) {
			f_SetThreadDpiAwarenessContext(-3);
		}
	}

	pm_cp = get_windows_cp(0);

	hcursor = LoadCursorA(0, (void *)IDC_ARROW);

	memset(&class_w, 0, sizeof class_w);
	class_w.cbSize = sizeof class_w;
	class_w.style = CS_HREDRAW | CS_VREDRAW;
	class_w.lpfnWndProc = pm_window_proc;
	class_w.hInstance = module_handle;
	class_w.hCursor = hcursor;
	class_w.lpszClassName = pm_class_name_l;
	window_class_atom = RegisterClassExW(&class_w);
	if (window_class_atom) {
		OSVERSIONINFO v;
		unicode_supported = 1;
		unicode_title_supported = 0;
		v.dwOSVersionInfoSize = sizeof v;
		if (is_winnt() && GetVersionEx(&v) && v.dwMajorVersion >= 5)
			unicode_title_supported = 1;
	} else {
		unicode_supported = 0;
		unicode_title_supported = 0;
		memset(&class_a, 0, sizeof class_a);
		class_a.cbSize = sizeof class_a;
		class_a.style = CS_HREDRAW | CS_VREDRAW;
		class_a.lpfnWndProc = pm_window_proc;
		class_a.hInstance = module_handle;
		class_a.hCursor = hcursor;
		class_a.lpszClassName = pm_class_name;
		window_class_atom = RegisterClassExA(&class_a);
	}
	if (!window_class_atom) {
		pm_status = cast_uchar "Unable to register window class.\n";
		goto win32_fail;
	}
	if (unicode_supported) {
		hwnd_hidden = CreateWindowExW(WS_EX_LEFT, (void *)(unsigned long)window_class_atom, L"Links", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, module_handle, NULL);
	} else {
		hwnd_hidden = CreateWindowExA(WS_EX_LEFT, (void *)(unsigned long)window_class_atom, "Links", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, module_handle, NULL);
	}
	if (!hwnd_hidden) {
		pm_status = cast_uchar "Could not create hidden window.\n";
		goto win32_fail;
	}
	if (!unicode_supported)
		PeekMessageA(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
	else
		PeekMessageW(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
#endif
	pm_event_signal();
	while (!pm_thread_shutdown) {
#ifdef OS2
		/*BOOL ret;
		debug_call(("T: calling WinPeekMsg"));
		ret = WinPeekMsg(hab_disp, &msg, 0L, 0, 0, PM_REMOVE);
		debug_call(("T: WinPeekMsg: %ld", ret));
		if (!ret) {
			debug_call(("T: calling WinWaitMsg"));
			ret = WinWaitMsg(hab_disp, 0, 0);
			debug_call(("T: WinWaitMsg: %ld", ret));
			continue;
		}*/
		debug_call(("T: calling WinGetMsg"));
		WinGetMsg(hab_disp, &msg, 0L, 0, 0);
		debug_call(("T: WinGetMsg: %lx, %lx, %p, %p, %lx, %lx.%lx, %lx", msg.hwnd, msg.msg, msg.mp1, msg.mp2, msg.time, msg.ptl.x, msg.ptl.y, msg.reserved));
		WinDispatchMsg(hab_disp, &msg);
#endif
#ifdef WIN
		if (!unicode_supported)
			GetMessageA(&msg, NULL, 0, 0);
		else
			GetMessageW(&msg, NULL, 0, 0);
		TranslateMessage(&msg);
		if (!unicode_supported)
			DispatchMessageA(&msg);
		else
			DispatchMessageW(&msg);
#endif
	}

#ifdef OS2
		if (!WinReleasePS(hps_hidden))
			error("WinReleasePS failed: %lx", WinGetLastError(hab_disp));
	os2_fail4:
		if (!WinDestroyWindow(hwnd_hidden_h))
			error("WinDestroyWindow failed: %lx", WinGetLastError(hab_disp));
	os2_fail3:
	os2_fail2:
		if (!WinDestroyMsgQueue(hmq))
			error("WinDestroyMsgQueue failed: %lx", WinGetLastError(hab_disp));
	os2_fail1:
		if (!WinTerminate(hab_disp))
			error("WinTerminate failed: %lx", WinGetLastError(hab_disp));
	os2_fail0:
#endif
#ifdef WIN
	if (!DestroyWindow(hwnd_hidden))
		error("DestroyWindow failed: %u", (unsigned)GetLastError());
	win32_fail:
#endif
	pm_event_signal();
	return;
}

#ifdef WIN
static void *pm_dispatcher_win32(void *p)
{
	pm_dispatcher(NULL);
	return NULL;
}
#endif

#ifdef WIN
static void pm_release_cached_objects(struct pm_window *win)
{
	if (win->pen_orig) {
		if (!SelectObject(win->dc, win->pen_orig))
			error("SelectObject failed for original pen %u", (unsigned)GetLastError());
		win->pen_orig = NULL;
	}
	if (win->pen_cache) {
		if (!DeleteObject(win->pen_cache))
			error("DeleteObject failed for pen cache %u", (unsigned)GetLastError());
		win->pen_cache = NULL;
		win->pen_cache_color = -1;
	}
	if (win->brush_cache) {
		if (!DeleteObject(win->brush_cache))
			error("DeleteObject failed for brush cache %u", (unsigned)GetLastError());
		win->brush_cache = NULL;
		win->brush_cache_color = -1;
	}
	if (!GdiFlush())
		{/*error("GdiFlush failed %u", (unsigned)GetLastError());*/}
}
#endif

static void pm_handler(void *p)
{
	unsigned char flush_font;
	unsigned char c;
	struct pm_window *win = NULL;
	struct pm_event *ev = NULL;
	debug_call(("M: pm_handler: pm_lock"));
	pm_lock();
	debug_call(("M: pm_handler: pm_locked"));
	if (!list_empty(pm_event_windows)) {
		win = list_struct(pm_event_windows.prev, struct pm_window);
		if (!list_empty(win->queue)) {
			ev = list_struct(win->queue.prev, struct pm_event);
			del_from_list(ev);
		}
		if (list_empty(win->queue)) {
			del_from_list(win);
			win->in = 0;
		}
	}
	if (list_empty(pm_event_windows)) {
		int rd;
		debug_call(("M: pm_handler: read"));
		EINTRLOOP(rd, read(pm_pipe[0], &c, 1));
		if (rd != 1) {
			if (rd < 0)
				fatal_exit("error on pm pipe: %s", strerror(errno));
			else
				fatal_exit("eof on pm pipe");
		}
	}
	debug_call(("M: pm_handler: pm_unlock"));
	flush_font = pm_flush_font_cache;
	pm_flush_font_cache = 0;
#ifdef WIN
	if (win) {
		if (win->new_dc && !win->old_dc) {
			pm_release_cached_objects(win);
			win->old_dc = win->dc;
			win->dc = win->new_dc;
			win->new_dc = NULL;
		}
	}
#endif
	pm_unlock();

	flush_bitmaps(flush_font, 0, 0);

	debug_call(("M: pm_handler: pm_unlocked: %p", ev));
	if (!ev) return;
	debug_call(("M: pm_handler: event: %d", ev->type));
	switch (ev->type) {
		struct rect r;
		case E_KEY:
			if (win->dev->keyboard_handler)
				win->dev->keyboard_handler(win->dev, ev->x1, ev->y1);
			break;
		case E_MOUSE:
			if (win->dev->mouse_handler)
				win->dev->mouse_handler(win->dev, ev->x1, ev->y1, ev->x2);
			break;
		case E_REDRAW:
			if (win->dev->redraw_handler) {
				r.x1 = ev->x1; r.y1 = ev->y1;
				r.x2 = ev->x2; r.y2 = ev->y2;
				win->dev->redraw_handler(win->dev, &r);
			}
			break;
		case E_RESIZE:
			win->dev->size.x2 = ev->x2;
			win->dev->size.y2 = ev->y2;
			if (win->dev->resize_handler) {
				win->dev->resize_handler(win->dev);
			}
	}
	debug_call(("M: pm_handler: free ev"));
	free(ev);
	debug_call(("M: pm_handler: done"));
}

static int pm_bitmap_count = 0;

static unsigned char *pm_get_af_unix_name(void)
{
	return cast_uchar "";
}

#ifdef PM_SPAWN_SUBPROC

static int pm_sin, pm_sout, pm_serr, pm_ip[2], pm_op[2], pm_ep[2];
static int pm_cons_ok = 0;

static void pm_setup_console(int undo)
{
	int rs;
	if (pm_cons_ok != undo) return;
	if (pm_cons_ok) goto fail9;
	setmode(1, O_BINARY);
	setmode(2, O_BINARY);
	pm_sin = c_dup(0);
	if (pm_sin < 0) goto fail;
	pm_sout = c_dup(1);
	if (pm_sout < 0) goto fail1;
	pm_serr = c_dup(2);
	if (pm_serr < 0) goto fail2;
	if (c_pipe(pm_ip)) goto fail3;
	if (c_pipe(pm_op)) goto fail4;
	if (c_pipe(pm_ep)) goto fail5;
	EINTRLOOP(rs, dup2(pm_ip[0], 0));
	if (rs != 0) goto fail6;
	EINTRLOOP(rs, dup2(pm_op[1], 1));
	if (rs != 1) goto fail7;
	EINTRLOOP(rs, dup2(pm_ep[1], 2));
	if (rs != 2) goto fail8;
	EINTRLOOP(rs, close(pm_ip[0]));
	EINTRLOOP(rs, close(pm_op[1]));
	EINTRLOOP(rs, close(pm_ep[1]));
	pm_cons_ok = 1;
	return;
fail9:
	EINTRLOOP(rs, dup2(pm_serr, 2));
fail8:
	EINTRLOOP(rs, dup2(pm_sout, 1));
fail7:
	EINTRLOOP(rs, dup2(pm_sin, 0));
fail6:
	EINTRLOOP(rs, close(pm_ep[0]));
	EINTRLOOP(rs, close(pm_ep[1]));
fail5:
	EINTRLOOP(rs, close(pm_op[0]));
	EINTRLOOP(rs, close(pm_op[1]));
fail4:
	EINTRLOOP(rs, close(pm_ip[0]));
	EINTRLOOP(rs, close(pm_ip[1]));
fail3:
	EINTRLOOP(rs, close(pm_serr));
fail2:
	EINTRLOOP(rs, close(pm_sout));
fail1:
	EINTRLOOP(rs, close(pm_sin));
fail:	pm_cons_ok = 0;
}

static int pm_do_console_step(int slp)
{
#define CONS_BUF 20
	int did_something = 0;
	unsigned char buffer[CONS_BUF];
	fd_set s;
	struct timeval tv = { 0, 0 };
	int rs;
	int m = pm_op[0] > pm_ep[0] ? pm_op[0] : pm_ep[0];
	int r, w;
	if (pm_sin > m) m = pm_sin;
	m++;
	if (!pm_cons_ok) return -1;
	FD_ZERO(&s);
	/*FD_SET(pm_sin, &s);*/
	if (m > (int)FD_SETSIZE) {
		fatal_exit("too big handle %d", m - 1);
	}
	FD_SET(pm_op[0], &s);
	FD_SET(pm_ep[0], &s);
	EINTRLOOP(rs, select(m, &s, NULL, NULL, slp ? NULL : &tv));
	if (rs <= 0) return -1;
#define SEL_CHK(ih, oh)							\
	if (FD_ISSET(ih, &s)) {						\
		EINTRLOOP(r, read(ih, buffer, CONS_BUF));		\
		if (r <= 0) return -1;					\
		do {							\
			if ((w = hard_write(oh, buffer, r)) <= 0) return -1;\
			did_something = 1;				\
			r -= w;						\
		} while (r > 0);					\
	}

	block_signals(0, 0);
	/*SEL_CHK(pm_sin, pm_ip[1]);*/
	SEL_CHK(pm_op[0], pm_sout);
	SEL_CHK(pm_ep[0], pm_serr);
	unblock_signals();
	return did_something;
#undef SEL_CHK
}

static void pm_do_console(void)
{
	while (pm_do_console_step(1) >= 0)
		;
}

static void pm_sigcld(void *p)
{
	int st;
	pid_t w;
	EINTRLOOP(w, wait(&st));
	while (pm_do_console_step(0) > 0)
		;
	if (w > 0 && WIFEXITED(st)) exit(WEXITSTATUS(st));
	else exit(RET_FATAL);
}

static unsigned char *pm_spawn_subproc(void)
{
	char **arg;
	int rs;
	pid_t pm_child_pid;

	if ((unsigned)g_argc > MAXINT / sizeof(char *) - 1) overalloc();
	arg = mem_alloc((g_argc + 1) * sizeof(char *));
	memcpy(arg, g_argv, g_argc * sizeof(char *));
	arg[g_argc] = NULL;
	pm_child_pid = -1;
	install_signal_handler(SIGCHLD, pm_sigcld, NULL, 1);
	pm_setup_console(0);
	pm_child_pid = spawnvp(P_PM, path_to_exe, arg);
	mem_free(arg);
	if (pm_child_pid < 0) {
		set_sigcld();
		pm_setup_console(1);
		return cast_uchar "Unable to spawn subprocess.\n";
	}
	pm_do_console();
	pm_setup_console(1);
	while (1)
		EINTRLOOP(rs, select(1, NULL, NULL, NULL, NULL));

	return cast_uchar "Not reached.\n";
}

#endif

static void pm_set_palette(void)
{
#ifdef OS2
	HPAL p;
	int i;
	struct pm_window *win;
	if (pm_use_palette) {
		p = hpal;
		pmshell_driver.get_color = get_color_fn(pmshell_driver.depth);
	} else {
		p = NULLHANDLE;
		pmshell_driver.get_color = pm_get_color;
	}

	if (GpiSelectPalette(hps_hidden, p) == (HPAL)PAL_ERROR)
		error("GpiSelectPalette failed: %lx", WinGetLastError(hab));
	if (!pm_use_palette) {
		if (!GpiCreateLogColorTable(hps_hidden, LCOL_RESET | LCOL_PURECOLOR, LCOLF_RGB, 0, 0, NULL))
			error("GpiCreateLogColorTable failed: %lx", WinGetLastError(hab));
	}

	for (i = 0; i < WIN_HASH; i++) for (win = pm_windows[i]; win; win = win->nxt) {
		if (GpiSelectPalette(win->ps, p) == (HPAL)PAL_ERROR)
			error("GpiSelectPalette failed: %lx", WinGetLastError(hab));
		if (!pm_use_palette) {
			if (!GpiCreateLogColorTable(win->ps, LCOL_RESET | LCOL_PURECOLOR, LCOLF_RGB, 0, 0, NULL))
				error("GpiCreateLogColorTable failed: %lx", WinGetLastError(hab));
		}
		if (pm_use_palette) {
			ULONG pcclr;
			LONG nc;
			nc = WinRealizePalette(win->hc, win->ps, &pcclr);
			if (nc == PAL_ERROR)
				error("WinRealizePalette failed: %lx", WinGetLastError(hab));
		}
	}
#endif
#ifdef WIN
	pmshell_driver.get_color = pm_get_color;
#endif
}

static unsigned char *pm_init_driver(unsigned char *param, unsigned char *display)
{
	unsigned char *s;
	int rs;

#ifdef OS2
	if (os2_pib) {
		if (os2_pib->pib_ultype != 3 &&
		    os2_pib->pib_ultype != 4) {
#ifdef PM_SPAWN_SUBPROC
			s = pm_spawn_subproc();
			goto r0;
#else
			os2_pib->pib_ultype = 3;
#endif
		}
	}

	if ((hab = WinInitialize(0)) == 0) {
		s = cast_uchar "WinInitialize failed.\n";
		goto r0;
	}
#endif
#ifdef WIN
	module_handle = GetModuleHandleA(NULL);
	if (!module_handle) {
		s = cast_uchar "Unable to get module handle.\n";
		goto r0;
	}
#endif
	if (c_pipe(pm_pipe)) {
		s = cast_uchar "Could not create pipe.\n";
		goto r1;
	}
	set_nonblock(pm_pipe[1]);
	memset(pm_windows, 0, sizeof(struct pm_window *) * WIN_HASH);
	if (pm_lock_init()) {
		s = cast_uchar "Could not create mutext.\n";
		goto r2;
	}
	pm_thread_shutdown = 0;
#ifdef OS2
	if (_beginthread(pm_dispatcher, NULL, 65536, NULL) == -1) {
		s = cast_uchar "Could not start thread.\n";
		goto r3;
	}
#endif
#ifdef WIN
	if (pthread_create(&pthread_handle, NULL, pm_dispatcher_win32, NULL)) {
		s = cast_uchar "Could not start thread.\n";
		goto r3;
	}
#endif
	pm_event_wait();
	if (pm_status) {
		s = pm_status;
		goto r3;
	}
#ifdef OS2
	pmshell_driver.flags &= ~GD_SWITCH_PALETTE;
	pmshell_driver.depth = 0xc3;

	{
		unsigned i;
		LONG formats[16];
		memset(formats, 0, sizeof formats);
		if (GpiQueryDeviceBitmapFormats(hps_hidden, array_elements(formats), formats) == FALSE) goto db;
		for (i = 0; array_elements(formats) - i > 1; i += 2) {
			/*if (formats[i] || formats[i + 1]) fprintf(stderr, "format[%d]: %ld, %ld\n", i, formats[i], formats[i + 1]);*/
			if (formats[i] == 1) switch (formats[i+1]) {
				case 4:
/* 216 colors with rounding makes text look better, but images worse */
#if 0
					pmshell_driver.depth = 0x21;
					pmshell_driver.depth |= 0x300;
					goto db;
#endif
				case 8:
					pmshell_driver.depth = 0x41;
					pmshell_driver.depth |= 0x300;
					goto db;
				case 15:
					pmshell_driver.depth |= 15 * 1024;
					goto db;
				case 16:
					pmshell_driver.depth |= 16 * 1024;
					goto db;
				case 24:
					goto db;
			}
		}

	}
db:
	{
		unsigned i, pm_colors;
		unsigned bit_count = (pmshell_driver.depth >> 3) & 0x1f;
		if (bit_count <= 4)
			pm_colors = !(pmshell_driver.depth & 0x300) ? 16 : 8;
		else
			pm_colors = !(pmshell_driver.depth & 0x300) ? 256 : 216;
		pm_bitmapinfo = pm_alloc(sizeof(BITMAPINFOHEADER) + (bit_count <= 8 ? 256 : 1) * sizeof(RGB), PM_ALLOC_ZERO);
		pm_bitmapinfo->cbFix = sizeof(BITMAPINFOHEADER);
		pm_bitmapinfo->cPlanes = 1;
		pm_bitmapinfo->cBitCount = bit_count <= 8 ? 8 : bit_count;
		hpal = NULLHANDLE;
		if (bit_count <= 8) {
			LONG dev_planes, dev_bitcount, dev_additional;
			ULONG pm_palette[256];
			HDC hdc;
			memset(pm_palette, 0, sizeof pm_palette);
			for (i = 0; i < 256; i++) {
				unsigned rgb[3];
				q_palette(pm_colors, i, 255, rgb);
				pm_bitmapinfo->argbColor[i].bBlue = rgb[2];
				pm_bitmapinfo->argbColor[i].bGreen = rgb[1];
				pm_bitmapinfo->argbColor[i].bRed = rgb[0];
				pm_palette[i] = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
			}
			hdc = GpiQueryDevice(hps_hidden);
			if (hdc == HDC_ERROR) {
				error("GpiQueryDevice failed: %lx", WinGetLastError(hab));
				goto skip_palette;
			}
			if (!DevQueryCaps(hdc, CAPS_COLOR_PLANES, 1, &dev_planes) ||
			    !DevQueryCaps(hdc, CAPS_COLOR_BITCOUNT, 1, &dev_bitcount) ||
			    !DevQueryCaps(hdc, CAPS_ADDITIONAL_GRAPHICS, 1, &dev_additional)) {
				error("GpiQueryCaps failed: %lx", WinGetLastError(hab));
				goto skip_palette;
			}
			/*fprintf(stderr, "caps: %lx %lx %lx\n", dev_planes, dev_bitcount, dev_additional);*/
			if (dev_planes != 1)
				goto skip_palette;
			if (dev_bitcount < 8)
				goto skip_palette;
			if (!(dev_additional & CAPS_PALETTE_MANAGER))
				goto skip_palette;
			hpal = GpiCreatePalette(hab, LCOL_PURECOLOR /*| LCOL_OVERRIDE_DEFAULT_COLORS*/, LCOLF_CONSECRGB, pm_colors, pm_palette);
			if (hpal == GPI_ERROR) {
				error("GpiCreatePalette failed: %lx", WinGetLastError(hab));
				hpal = NULLHANDLE;
			}
			if (hpal != NULLHANDLE) {
				if (GpiSelectPalette(hps_hidden, hpal) == (HPAL)PAL_ERROR) {
					error("GpiSelectPalette failed: %lx", WinGetLastError(hab));
					if (!GpiDeletePalette(hpal))
						error("GpiDeletePalette failed: %lx", WinGetLastError(hab));
					hpal = NULLHANDLE;
				}
				if (GpiSelectPalette(hps_hidden, NULLHANDLE) == (HPAL)PAL_ERROR)
					error("GpiSelectPalette failed: %lx", WinGetLastError(hab));
				pmshell_driver.flags |= GD_SWITCH_PALETTE;
			}
		}
skip_palette:
		pm_set_palette();
	}

	{
		SIZEL sizl = { 0, 0 };
		PSZ data[4] = { "DISPLAY", NULL, NULL, NULL };
		hdc_mem = DevOpenDC(hab, OD_MEMORY, "*", 4L, (PDEVOPENDATA)data, NULLHANDLE);
		if (!hdc_mem) {
			s = cast_uchar "Unable to create memory device context.\n";
			goto r4;
		}
		hps_mem = GpiCreatePS(hab, hdc_mem, &sizl, GPIA_ASSOC | PU_PELS | GPIT_MICRO);
		if (!hps_mem) {
			s = cast_uchar "Unable to create memory presentation space.\n";
			goto r5;
		}
	}

	icon = WinLoadPointer(HWND_DESKTOP, 0, 1);
#endif
#ifdef WIN
	{
		int bpp;

		screen_dc = GetDC(NULL);
		if (!screen_dc) {
			s = cast_uchar "Unable to get screen device context.\n";
			goto r4;
		}
		bpp = GetDeviceCaps(screen_dc, BITSPIXEL);

		if (bpp < 24)
			pmshell_driver.depth = 0x7a;
		else
			pmshell_driver.depth = 0xc3;

		pm_bitmapinfo = pm_alloc(sizeof(BITMAPINFO), PM_ALLOC_ZERO);
		pm_bitmapinfo->bmiHeader.biSize = sizeof(pm_bitmapinfo->bmiHeader);
		pm_bitmapinfo->bmiHeader.biPlanes = 1;
		pm_bitmapinfo->bmiHeader.biBitCount = bpp < 24 ? 16 : 24;
		pm_bitmapinfo->bmiHeader.biCompression = BI_RGB;
	}

	icon_big = LoadImage(module_handle, MAKEINTRESOURCE(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
	icon_small = LoadImage(module_handle, MAKEINTRESOURCE(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

	pm_set_palette();
#endif
	icon_set = 0;
	set_handlers(pm_pipe[0], pm_handler, NULL, NULL);

	return NULL;

#ifdef OS2
	r5:
	if (DevCloseDC(hdc_mem) == DEV_ERROR)
		error("DevCloseDC failed: %lx", WinGetLastError(hab));
#endif
	r4:
	pm_free(pm_bitmapinfo);
	pm_send_msg(MSG_SHUTDOWN_THREAD, NULL, NULL);
	r3:
	PM_UNFLUSH();
	pm_lock_close();
	r2:
	EINTRLOOP(rs, close(pm_pipe[0]));
	EINTRLOOP(rs, close(pm_pipe[1]));
	r1:
#ifdef OS2
	if (!WinTerminate(hab))
		error("WinTerminate failed: %lx", WinGetLastError(hab));
#endif
	r0:
	return stracpy(s);
}

static void pm_shutdown_driver(void)
{
	int rs;
	pm_send_msg(MSG_SHUTDOWN_THREAD, NULL, NULL);
#ifdef OS2
	if (icon != NULLHANDLE)
		if (!WinDestroyPointer(icon))
			error("WinDestroyPointer failed: %lx", WinGetLastError(hab));
	if (hpal != NULLHANDLE) {
		if (!GpiDeletePalette(hpal))
			error("GpiDeletePalette failed: %lx", WinGetLastError(hab));
	}
	if (!GpiDestroyPS(hps_mem))
		error("GpiDestroyPS failed: %lx", WinGetLastError(hab));
	if (DevCloseDC(hdc_mem) == DEV_ERROR)
		error("DevCloseDC failed: %lx", WinGetLastError(hab));
#endif
#ifdef WIN
	if (icon_small != NULL)
		if (!DestroyIcon(icon_small))
			error("DestroIcon failed: %u", (unsigned)GetLastError());
	if (icon_big != NULL)
		if (!DestroyIcon(icon_big))
			error("DestroIcon failed: %u", (unsigned)GetLastError());
	if (!ReleaseDC(NULL, screen_dc))
		error("ReleaseDC failed: %u", (unsigned)GetLastError());
#endif
	pm_free(pm_bitmapinfo);
	PM_UNFLUSH();
	pm_lock_close();
	close_socket(&pm_pipe[0]);
	EINTRLOOP(rs, close(pm_pipe[1]));
#ifdef OS2
	if (!WinTerminate(hab))
		error("WinTerminate failed: %lx", WinGetLastError(hab));
#endif
	if (pm_bitmap_count) internal_error("pm_shutdown_driver: %d bitmaps leaked", pm_bitmap_count);
}

#ifdef OS2

static HPOINTER pmshell_create_icon(void)
{
	unsigned char *icon_data, *icon_data_2;
	int icon_w, icon_h;
	ssize_t icon_skip;
	int size;
	int i, j;
	HBITMAP hbm;
	HPOINTER hicon;
	HPAL prev_pal;
	const int border = 4;

	get_links_icon(&icon_data, &icon_w, &icon_h, &icon_skip, 4);
	size = (int)(icon_h * icon_skip);
	icon_data_2 = pm_alloc(size * 2, 0);
	for (i = 0; i < icon_h; i++)
		memcpy(icon_data_2 + i * icon_skip, icon_data + (icon_h - 1 - i) * icon_skip, icon_skip);
	mem_free(icon_data);
	for (i = 0; i < size; i++) {
		icon_data_2[i + size] = 0;
	}
#define set_trans(x, y) memset(icon_data_2 + ((y) + icon_h) * icon_skip + (x) * (pmshell_driver.depth & 7), 0xff, pmshell_driver.depth & 7);
	for (j = 0; j < border; j++) for (i = 0; i < icon_w; i++) set_trans(i, j);
	for (j = icon_h - border; j < icon_h; j++) for (i = 0; i < icon_w; i++) set_trans(i, j);
	for (j = 0; j < icon_h; j++) for (i = 0; i < border; i++) set_trans(i, j);
	for (j = 0; j < icon_h; j++) for (i = icon_w - border; i < icon_w; i++) set_trans(i, j);
#undef set_trans
	pm_bitmapinfo->cx = icon_w;
	pm_bitmapinfo->cy = icon_h * 2;
	if ((prev_pal = GpiSelectPalette(hps_hidden, NULLHANDLE)) == (HPAL)PAL_ERROR)
		error("GpiSelectPalette failed: %lx", WinGetLastError(hab));
	hbm = GpiCreateBitmap(hps_hidden, (PBITMAPINFOHEADER2)pm_bitmapinfo, CBM_INIT, icon_data_2, (PBITMAPINFO2)pm_bitmapinfo);
	if (prev_pal != (HPAL)PAL_ERROR) {
		if (GpiSelectPalette(hps_hidden, prev_pal) == (HPAL)PAL_ERROR)
			error("GpiSelectPalette failed: %lx", WinGetLastError(hab));
	}
	pm_free(icon_data_2);
	if (hbm == GPI_ERROR)
		return NULLHANDLE;
	hicon = WinCreatePointer(HWND_DESKTOP, hbm, FALSE, 0, 0);
	if (!hicon)
		error("WinCreatePointer failed: %lx", WinGetLastError(hab));
	if (!GpiDeleteBitmap(hbm))
		error("GpiDeleteBitmap failed: %lx", WinGetLastError(hab));
	return hicon;
}

#endif

#ifdef WIN

static HICON win32_create_icon(int x, int y)
{
	unsigned char *icon_data, *zero_data;
	int icon_w, icon_h;
	ssize_t icon_skip;
	int left, right, top, bottom;
	int i, j;
	HBITMAP hbm_icon;
	HBITMAP hbm_mask;
	ICONINFO ic;
	HICON hicon = NULL;
	HDC memory_dc;
	HGDIOBJ prev_obj;

	get_links_icon(&icon_data, &icon_w, &icon_h, &icon_skip, 4);
	zero_data = pm_alloc(icon_h * icon_skip, 0);
	memcpy(zero_data, icon_data, icon_h * icon_skip);
	mem_free(icon_data);
	icon_data = zero_data;

	zero_data = pm_alloc(icon_h * icon_skip, PM_ALLOC_ZERO);

	memory_dc = CreateCompatibleDC(NULL);
	if (!memory_dc) {
		error("CreateCompatibleDC failed: %u", (unsigned)GetLastError());
		goto ret1;
	}

	hbm_icon = CreateCompatibleBitmap(screen_dc, x, y);
	if (!hbm_icon) {
		error("CreateBitmap failed: %u", (unsigned)GetLastError());
		goto ret2;
	}
	prev_obj = SelectObject(memory_dc, hbm_icon);
	if (!prev_obj) {
		error("SelectObject failed: %u", (unsigned)GetLastError());
		goto ret3;
	}
	pm_bitmapinfo->bmiHeader.biWidth = icon_w;
	pm_bitmapinfo->bmiHeader.biHeight = -icon_h;
	if (!StretchDIBits(memory_dc, 0, 0, x, y, 0, 0, icon_w, icon_h, icon_data, pm_bitmapinfo, DIB_RGB_COLORS, SRCCOPY)) {
		error("StretchDIBits failed: %u", (unsigned)GetLastError());
		prev_obj = SelectObject(memory_dc, prev_obj);
		if (!prev_obj) {
			error("SelectObject failed: %u", (unsigned)GetLastError());
			goto ret3;
		}
		goto ret3;
	}
	prev_obj = SelectObject(memory_dc, prev_obj);
	if (!prev_obj) {
		error("SelectObject failed: %u", (unsigned)GetLastError());
		goto ret3;
	}
	hbm_mask = CreateBitmap(x, y, 1, 1, zero_data);
	if (!hbm_mask) {
		error("CreateCompatibleBitmap failed: %u", (unsigned)GetLastError());
		goto ret3;
	}
	prev_obj = SelectObject(memory_dc, hbm_mask);
	if (!prev_obj) {
		error("SelectObject failed %u", (unsigned)GetLastError());
		goto ret4;
	}
	if (x == 16 && y == 16) {
		top = bottom = left = right = 1;
		if (is_winnt())
			top = 2;
	} else if (x == 32 && y == 32) {
		top = bottom = left = right = 3;
	} else {
		top = bottom = left = right = 0;
	}
	for (j = 0; j < top; j++) for (i = 0; i < x; i++) SetPixel(memory_dc, i, j, 0xffffff);
	for (j = y - bottom; j < y; j++) for (i = 0; i < x; i++) SetPixel(memory_dc, i, j, 0xffffff);
	for (j = 0; j < y; j++) for (i = 0; i < left; i++) SetPixel(memory_dc, i, j, 0xffffff);
	for (j = 0; j < y; j++) for (i = x - left; i < x; i++) SetPixel(memory_dc, i, j, 0xffffff);
	prev_obj = SelectObject(memory_dc, prev_obj);
	if (!prev_obj) {
		error("SelectObject failed %u", (unsigned)GetLastError());
		goto ret4;
	}
	memset(&ic, 0, sizeof ic);
	ic.fIcon = TRUE;
	ic.hbmMask = hbm_mask;
	ic.hbmColor = hbm_icon;
	hicon = CreateIconIndirect(&ic);
	if (!hicon)
		error("CreateIconIndirect failed %u", (unsigned)GetLastError());

ret4:
	if (!DeleteBitmap(hbm_mask))
		error("DeleteBitmap failed %u", (unsigned)GetLastError());
ret3:
	if (!DeleteBitmap(hbm_icon))
		error("DeleteBitmap failed %u", (unsigned)GetLastError());
ret2:
	if (!DeleteDC(memory_dc))
		error("DeleteDC failed %u", (unsigned)GetLastError());
ret1:
	pm_free(icon_data);
	pm_free(zero_data);
	return hicon;
}

#endif

static struct graphics_device *pm_init_device(void)
{
	RECTL wr;
	struct graphics_device *dev;
	struct pm_window *win;

	if (!icon_set) {
#ifdef OS2
		if (icon == NULLHANDLE) icon = pmshell_create_icon();
#endif
#ifdef WIN
		if (!icon_big) icon_big = win32_create_icon(32, 32);
		if (!icon_small) icon_small = win32_create_icon(16, 16);
#endif
	}
	icon_set = 1;

	win = mem_alloc(sizeof(struct pm_window));
	win->lastpos = -1L;
	win->button = 0;
	init_list(win->queue);
	win->in = 0;
	win->minimized = 0;
#ifdef WIN
	win->dc = NULL;
	win->old_dc = NULL;
	win->new_dc = NULL;
	win->pen_orig = NULL;
	win->pen_cache = NULL;
	win->pen_cache_color = -1;
	win->brush_cache = NULL;
	win->brush_cache_color = -1;
	win->drawing_cookie = (tcount)-1;
	win->timer_active = 0;
#endif
	debug_call(("M: pm_init_device: pm_send_msg"));
	pm_send_msg(MSG_CREATE_WINDOW, win, NULL);
	debug_call(("M: pm_init_device: pm_send_msg done"));
#ifdef OS2
	if (win->h == NULLHANDLE) {
		goto r1;
	}
	debug_call(("M: pm_init_device: WinGetPS"));
	if ((win->ps = WinGetPS(win->hc)) == NULLHANDLE) {
		goto r2;
	}
	if ((win->rgn = GpiCreateRegion(win->ps, 0, NULL)) == RGN_ERROR) {
		goto r3;
	}
#endif
#ifdef WIN
	if (win->hc == NULL) {
		goto r1;
	}
	if ((win->rgn = CreateRectRgn(0, 0, 0, 0)) == NULL) {
		goto r3;
	}
#endif
	dev = mem_calloc(sizeof(struct graphics_device));
	dev->driver_data = win;
	win->dev = dev;
#ifdef OS2
	debug_call(("M: pm_init_device: WinQueryWindowRect"));
	if (!WinQueryWindowRect(win->hc, &wr)) {
		memset(&wr, 0, sizeof wr);
	}
#endif
#ifdef WIN
	if (!GetClientRect(win->hc, &wr)) {
		memset(&wr, 0, sizeof wr);
	}
	wr.yTop = wr.yBottom;
#endif
	win->x = dev->size.x2 = wr.xRight;
	win->y = dev->size.y2 = wr.yTop;
	dev->clip.x1 = dev->clip.y1 = 0;
	dev->clip.x2 = dev->size.x2;
	dev->clip.y2 = dev->size.y2;
	debug_call(("M: pm_init_device: pm_hash_window"));
	pm_hash_window(win);
	debug_call(("M: pm_init_device: pm_unlock"));
	pm_unlock();

	pm_set_palette();

	debug_call(("M: pm_init_device: return"));
	return dev;

r3:
#ifdef OS2
	if (!WinReleasePS(win->ps))
		error("WinReleasePS failed: %lx", WinGetLastError(hab));
r2:
#endif
	pm_unlock();
	pm_send_msg(MSG_DELETE_WINDOW, win, NULL);
r1:
	if (win->in) del_from_list(win);
	pm_unlock();
	mem_free(win);
	return NULL;
}

static void pm_shutdown_device(struct graphics_device *dev)
{
	struct pm_window *win = pm_win(dev);
#ifdef OS2
	debug_call(("M: pm_shutdown_device: WinReleasePS"));
	if (!GpiDestroyRegion(win->ps, win->rgn))
		error("GpiDestroyRegion failed: %lx", WinGetLastError(hab));
	if (!WinReleasePS(win->ps))
		error("WinReleasePS failed: %lx", WinGetLastError(hab));
#endif
#ifdef WIN
	if (!DeleteObject(win->rgn))
		error("DeleteObject failed for region %u", (unsigned)GetLastError());
	pm_release_cached_objects(win);
#endif
	debug_call(("M: pm_shutdown_device: pm_send_msg"));
	pm_send_msg(MSG_DELETE_WINDOW, win, NULL);
	debug_call(("M: pm_shutdown_device: pm_unhash_window"));
	pm_unhash_window(win);
	if (win->in) del_from_list(win);
	debug_call(("M: pm_shutdown_device: pm_unlock"));
	pm_unlock();
	debug_call(("M: pm_shutdown_device: free"));
	while (!list_empty(win->queue)) {
		struct pm_event *ev = list_struct(win->queue.next, struct pm_event);
		del_from_list(ev);
		free(ev);
	}
	mem_free(win);
	mem_free(dev);
	debug_call(("M: pm_shutdown_device: return"));
}

#define MAX_TITLE_SIZE	512

static void pm_set_window_title(struct graphics_device *dev, unsigned char *title)
{
	unsigned char *text;
	debug_call(("M: pm_set_window_title: start"));
#ifdef WIN
	if (unicode_supported && unicode_title_supported) {
		int l = 0;
		text = init_str();
		while (*title && l < MAX_TITLE_SIZE * 2) {
			unsigned u;
			GET_UTF_8(title, u);
			if (u > 0 && u <= 0xffff) {
				add_chr_to_str(&text, &l, u & 0xff);
				add_chr_to_str(&text, &l, u >> 8);
			}
		}
		add_chr_to_str(&text, &l, 0);
	} else
#endif
	{
		text = convert(utf8_table, pm_cp, title, NULL);
		clr_white(text);
		if (strlen(cast_const_char text) > MAX_TITLE_SIZE) text[MAX_TITLE_SIZE] = 0;
	}

	debug_call(("M: pm_set_window_title: pm_send_msg"));
	pm_send_msg(MSG_SET_WINDOW_TITLE, pm_win(dev), text);
	debug_call(("M: pm_set_window_title: pm_unlock"));
	pm_unlock();
	/*SendMessage(pm_win(dev)->hc, WM_SETTEXT, NULL, text);*/
	debug_call(("M: pm_set_window_title: mem_free"));
	mem_free(text);
	debug_call(("M: pm_set_window_title: return"));
}

static int pm_get_empty_bitmap(struct bitmap *bmp)
{
	unsigned size;
	debug_call(("M: get_empty_bitmap (%dx%d)", bmp->x, bmp->y));
	if ((unsigned)bmp->x > (unsigned)MAXINT / (pmshell_driver.depth & 7) - 4) {
over:
		bmp->data = NULL;
		bmp->flags = NULL;
		return -1;
	}
	bmp->skip = (bmp->x * (pmshell_driver.depth & 7) + 3) & ~3;
	size = (unsigned)bmp->skip * (unsigned)bmp->y;
	if (bmp->skip && size / (unsigned)bmp->skip != (unsigned)bmp->y) goto over;
	if (size > MAXINT) goto over;
	bmp->data = pm_alloc(size, PM_ALLOC_MAYFAIL);
	if (!bmp->data) goto over;
	bmp->data = (unsigned char *)bmp->data + size - bmp->skip;
	bmp->skip = -bmp->skip;
	debug_call(("M: get_empty_bitmap done"));
	return 0;
}

static inline unsigned char *bmp_base_pointer(struct bitmap *bmp)
{
	return (unsigned char *)bmp->data + bmp->skip * (bmp->y - 1);
}

static void pm_register_bitmap(struct bitmap *bmp)
{
#ifdef OS2
	HBITMAP hbm;
	unsigned char *pointer;
#endif
	debug_call(("M: register_bitmap (%dx%d)", bmp->x, bmp->y));
	pm_bitmap_count++;
#ifdef OS2
	if (!bmp->data) {
		bmp->flags = (void *)GPI_ERROR;
		return;
	}
	pointer = bmp_base_pointer(bmp);
again:
	if (bmp->x > OS2_MAX_BITMAP_SIZE || bmp->y > OS2_MAX_BITMAP_SIZE) {
		hbm = GPI_ERROR;
	} else {
		pm_bitmapinfo->cx = bmp->x;
		pm_bitmapinfo->cy = bmp->y;
		hbm = GpiCreateBitmap(hps_hidden, (PBITMAPINFOHEADER2)pm_bitmapinfo, CBM_INIT, pointer, (PBITMAPINFO2)pm_bitmapinfo);
		if (hbm == GPI_ERROR) {
			if (out_of_memory(MF_GPI, NULL, 0))
				goto again;
		}
	}
	bmp->flags = (void *)hbm;
	if (hbm != GPI_ERROR) {
		mem_free(pointer);
		bmp->data = NULL;
	}
	debug_call(("M: register_bitmap done"));
#endif
}

static void *pm_prepare_strip(struct bitmap *bmp, int top, int lines)
{
#ifdef OS2
	debug_call(("M: prepare_strip (%dx%d)", bmp->x, bmp->y));
	if (bmp->flags == (void *)GPI_ERROR) {
		if (bmp->data)
			goto return_data;
		over:
		bmp->data = NULL;
		return NULL;
	}
	if (bmp->data)
		internal_error("pm_prepare_strip: bmp->data should not be set here");
	if (-bmp->skip && (unsigned)-bmp->skip * (unsigned)lines / (unsigned)-bmp->skip != (unsigned)lines) goto over;
	if ((unsigned)-bmp->skip * (unsigned)lines > MAXINT) goto over;
	bmp->data = pm_alloc(-bmp->skip * lines, PM_ALLOC_MAYFAIL);
	if (!bmp->data) goto over;
	debug_call(("M: prepare_strip done"));
	return (unsigned char *)bmp->data - bmp->skip * (lines - 1);
return_data:
#endif
	return ((unsigned char *)bmp->data) + bmp->skip * top;
}

static void pm_commit_strip(struct bitmap *bmp, int top, int lines)
{
#ifdef OS2
	LONG r;
	HBITMAP old;
	HBITMAP new = (HBITMAP)bmp->flags;
	debug_call(("M: commit_strip (%dx%d)", bmp->x, bmp->y));
	if (new == GPI_ERROR || !bmp->data)
		return;
	debug_call(("M: commit_strip done"));
	old = GpiSetBitmap(hps_mem, new);
	if (old == HBM_ERROR)
		goto ret;
	pm_bitmapinfo->cx = bmp->x;
	pm_bitmapinfo->cy = bmp->y;
again:
	r = GpiSetBitmapBits(hps_mem, bmp->y - top - lines, lines, bmp->data, (PBITMAPINFO2)pm_bitmapinfo);
	if (r == GPI_ALTERROR) {
		if (out_of_memory(MF_GPI, NULL, 0))
			goto again;
	}
	GpiSetBitmap(hps_mem, old);
	ret:
	pm_free(bmp->data);
	bmp->data = NULL;
	debug_call(("M: commit_strip done"));
#endif
}

static void pm_unregister_bitmap(struct bitmap *bmp)
{
	debug_call(("M: unregister_bitmap (%dx%d)", bmp->x, bmp->y));
	pm_bitmap_count--;
#ifdef OS2
	if ((HBITMAP)bmp->flags != GPI_ERROR) {
		if (bmp->data)
			internal_error("pm_unregister_bitmap: bmp->data should not be set here");
		if (!GpiDeleteBitmap((HBITMAP)bmp->flags))
			error("GpiDeleteBitmap failed: %lx", WinGetLastError(hab));
	} else
#endif
	if (bmp->data) {
		pm_free(bmp_base_pointer(bmp));
	}
	debug_call(("M: unregister_bitmap done"));
}

static void pm_draw_bitmap(struct graphics_device *dev, struct bitmap *bmp, int x, int y)
{
	POINTL p;
	RECTL r;
	debug_call(("M: draw_bitmap (%dx%d -> %x,%x)", bmp->x, bmp->y, x, y));

	CLIP_DRAW_BITMAP

	PM_START_DRAW();

	r.xLeft = x < dev->clip.x1 ? dev->clip.x1 - x : 0;
	r.xRight = x + bmp->x > dev->clip.x2 ? dev->clip.x2 - x : bmp->x;
	r.yTop = bmp->y - (y < dev->clip.y1 ? dev->clip.y1 - y : 0);
	r.yBottom = y + bmp->y > dev->clip.y2 ? bmp->y - dev->clip.y2 + y : 0;
	p.x = x + r.xLeft;
	p.y = pm_win(dev)->y - y - bmp->y + r.yBottom;
#ifdef OS2
	if ((HBITMAP)bmp->flags != GPI_ERROR) {
		WinDrawBitmap(pm_win(dev)->ps, (HBITMAP)bmp->flags, &r, &p, 0, 1, DBM_NORMAL);
	} else if (!bmp->data) {
		return;
	} else {
		POINTL pp[4];
		if (bmp->x > OS2_MAX_LINE_SIZE) {
			int i;
			unsigned char *data_ptr = (unsigned char *)bmp->data + bmp->skip * (bmp->y - r.yTop) + r.xLeft * (pmshell_driver.depth & 7);
			for (i = r.yTop - r.yBottom - 1; i >= 0; i--) {
				pm_bitmapinfo->cx = r.xRight - r.xLeft;
				pm_bitmapinfo->cy = 1;
				pp[0].x = p.x;
				pp[0].y = p.y + i;
				pp[1].x = p.x + (r.xRight - r.xLeft) - 1;
				pp[1].y = p.y + i;
				pp[2].x = 0;
				pp[2].y = 0;
				pp[3].x = r.xRight - r.xLeft;
				pp[3].y = 1;
				GpiDrawBits(pm_win(dev)->ps, data_ptr, (PBITMAPINFO2)pm_bitmapinfo, 4, pp, ROP_SRCCOPY, 0);
				data_ptr += bmp->skip;
			}
		} else {
			pm_bitmapinfo->cx = bmp->x;
			pm_bitmapinfo->cy = r.yTop - r.yBottom;
			pp[0].x = p.x;
			pp[0].y = p.y;
			pp[1].x = p.x + (r.xRight - r.xLeft) - 1;
			pp[1].y = p.y + (r.yTop - r.yBottom) - 1;
			pp[2].x = r.xLeft;
			pp[2].y = 0;
			pp[3].x = r.xRight;
			pp[3].y = r.yTop - r.yBottom;
			GpiDrawBits(pm_win(dev)->ps, (unsigned char *)bmp->data + bmp->skip * (bmp->y - 1 - r.yBottom), (PBITMAPINFO2)pm_bitmapinfo, 4, pp, ROP_SRCCOPY, 0);
		}
	}
#endif
#ifdef WIN
	if (!bmp->data)
		return;
	pm_bitmapinfo->bmiHeader.biWidth = bmp->x;
	pm_bitmapinfo->bmiHeader.biHeight = r.yTop - r.yBottom;
	SetDIBitsToDevice(pm_win(dev)->dc, p.x, pm_win(dev)->y - p.y - (r.yTop - r.yBottom), r.xRight - r.xLeft, r.yTop - r.yBottom, r.xLeft, 0, 0, r.yTop - r.yBottom, (unsigned char *)bmp->data + bmp->skip * (bmp->y - 1 - r.yBottom), pm_bitmapinfo, DIB_RGB_COLORS);
#endif
	debug_call(("M: draw_bitmap done"));
}

static long pm_get_color(int rgb)
{
#ifdef OS2
	int c;
	rgb &= 0xffffff;
	c = GpiQueryNearestColor(hps_hidden, 0, rgb);
	if (c == GPI_ALTERROR) return rgb;
	/*fprintf(stderr, "pm_get_color: %x -> %x\n", rgb, c);*/
	return c;
#endif
#ifdef WIN
	COLORREF c, d;
	c = ((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb & 0xff0000) >> 16);
	d = GetNearestColor(screen_dc, c);
	if (d == CLR_INVALID) return c;
	return d;
#endif
}

#ifdef WIN

static int win32_select_brush(struct pm_window *win, int color)
{
	HBRUSH hbrush;
	if (color == win->brush_cache_color)
		return 0;
	hbrush = CreateSolidBrush(color);
	if (!hbrush) {
		error("CreateSolidBrush failed %u", (unsigned)GetLastError());
		return -1;
	}
	if (win->brush_cache != NULL)
		if (!DeleteObject(win->brush_cache))
			error("DeleteObject failed for previous brush %u", (unsigned)GetLastError());
	win->brush_cache = hbrush;
	win->brush_cache_color = color;
	return 0;
}

static int win32_select_pen(struct pm_window *win, int color)
{
	HPEN hpen;
	HGDIOBJ orig;
	if (color == win->pen_cache_color)
		return 0;
	hpen = CreatePen(PS_SOLID, 0, color);
	if (!hpen) {
		error("CreatePen failed %u", (unsigned)GetLastError());
		return -1;
	}
	orig = SelectObject(win->dc, hpen);
	if (!orig) {
		error("SelectObject failed for pen %u", (unsigned)GetLastError());
		if (!DeleteObject(hpen))
			error("DeleteObject failed for pen %u", (unsigned)GetLastError());
		return -1;
	}
	if (!win->pen_orig)
		win->pen_orig = orig;
	else if (!DeleteObject(orig))
		error("DeleteObject failed for previous pen %u", (unsigned)GetLastError());
	win->pen_cache = hpen;
	win->pen_cache_color = color;
	return 0;
}

#endif

static void pm_fill_area(struct graphics_device *dev, int x1, int y1, int x2, int y2, long color)
{
	RECTL r;
	debug_call(("M: fill_area (%d,%d)->(%d,%d)", x1, y1, x2, y2));

	CLIP_FILL_AREA

	PM_START_DRAW();

	r.xLeft = x1;
	r.yBottom = y2;
	r.xRight = x2;
	r.yTop = y1;
#ifdef OS2
	r.yBottom = pm_win(dev)->y - r.yBottom;
	r.yTop = pm_win(dev)->y - r.yTop;
	WinFillRect(pm_win(dev)->ps, &r, color);
#endif
#ifdef WIN
	if (win32_select_brush(pm_win(dev), color))
		return;
	if (!FillRect(pm_win(dev)->dc, &r, pm_win(dev)->brush_cache))
		error("FillRect failed %u", (unsigned)GetLastError());
#endif
	debug_call(("M: fill_area done"));
}

static void pm_draw_hline(struct graphics_device *dev, int x1, int y, int x2, long color)
{
#ifdef OS2
	HPS ps = pm_win(dev)->ps;
	POINTL p;
#endif
	debug_call(("M: draw_hline (%d,%d)->(%d)", x1, y, x2));

	CLIP_DRAW_HLINE

	PM_START_DRAW();

#ifdef OS2
	GpiSetColor(ps, color);
	p.x = x1;
	p.y = pm_win(dev)->y - y - 1;
	GpiMove(ps, &p);
	p.x = x2 - 1;
	GpiLine(ps, &p);
#endif
#ifdef WIN
	if (win32_select_pen(pm_win(dev), color))
		return;
	if (!MoveToEx(pm_win(dev)->dc, x1, y, NULL)) {
		error("MoveToEx failed %u", (unsigned)GetLastError());
	} else if (!LineTo(pm_win(dev)->dc, x2, y)) {
		error("LineTo failed %u", (unsigned)GetLastError());
	}
#endif
	debug_call(("M: draw_hline done"));
}

static void pm_draw_vline(struct graphics_device *dev, int x, int y1, int y2, long color)
{
#ifdef OS2
	HPS ps = pm_win(dev)->ps;
	POINTL p;
#endif
	debug_call(("M: draw_vline (%d,%d)->(%d)", x, y1, y2));

	CLIP_DRAW_VLINE

	PM_START_DRAW();

#ifdef OS2
	GpiSetColor(ps, color);
	p.x = x;
	p.y = pm_win(dev)->y - y1 - 1;
	GpiMove(ps, &p);
	p.y = pm_win(dev)->y - y2;
	GpiLine(ps, &p);
#endif
#ifdef WIN
	if (win32_select_pen(pm_win(dev), color))
		return;
	if (!MoveToEx(pm_win(dev)->dc, x, y1, NULL)) {
		error("MoveToEx failed %u", (unsigned)GetLastError());
	} else if (!LineTo(pm_win(dev)->dc, x, y2)) {
		error("LineTo failed %u", (unsigned)GetLastError());
	}
#endif
	debug_call(("M: draw_vline done"));
}

static void pm_scroll_redraws(struct pm_window *win, struct rect *r, int scx, int scy)
{
	struct pm_event *e;
	pm_cancel_event(win, E_REDRAW, &e);
	if (!e) return;
	if (scx >= 0) {
		if (e->x2 > r->x1 && e->x2 < r->x2) {
			e->x2 += scx;
			if (e->x2 > r->x2) e->x2 = r->x2;
		}
	} else {
		if (e->x1 > r->x1 && e->x1 < r->x2) {
			e->x1 += scx;
			if (e->x1 < r->x1) e->x1 = r->x1;
		}
	}
	if (scy >= 0) {
		if (e->y2 > r->y1 && e->y2 < r->y2) {
			e->y2 += scy;
			if (e->y2 > r->y2) e->y2 = r->y2;
		}
	} else {
		if (e->y1 > r->y1 && e->y1 < r->y2) {
			e->y1 += scy;
			if (e->y1 < r->y1) e->y1 = r->y1;
		}
	}
}

static int pm_scroll(struct graphics_device *dev, struct rect_set **set, int scx, int scy)
{
	struct pm_window *win = pm_win(dev);
#ifdef OS2
	RGNRECT rgnrect;
#endif
#ifdef WIN32
	size_t sz;
	RGNDATA *rgndata;
#endif
	RECTL *rects;
	size_t i;
	RECTL r;

	PM_START_DRAW();

	debug_call(("M: scroll (%d,%d)", scx, scy));
	pm_lock();
	r.xLeft = dev->clip.x1;
	r.yBottom = dev->clip.y2;
	r.xRight = dev->clip.x2;
	r.yTop = dev->clip.y1;
#ifdef OS2
	r.yBottom = win->y - r.yBottom;
	r.yTop = win->y - r.yTop;
	WinScrollWindow(win->hc, scx, -scy, &r, &r, win->rgn, NULL, 0);
	memset(&rgnrect, 0, sizeof rgnrect);
	rgnrect.ulDirection = RECTDIR_LFRT_TOPBOT;
	if (!GpiQueryRegionRects(win->ps, win->rgn, NULL, &rgnrect, NULL)) {
		error("GpiQueryRegionRects failed: %lx", WinGetLastError(hab));
		goto end;
	}
	/*fprintf(stderr, "GpiQueryRegionRects: %lu, %lu, %lu, %lu\n", rgnrect.ircStart, rgnrect.crc, rgnrect.crcReturned, rgnrect.ulDirection);*/
	if (rgnrect.crcReturned <= 1) {
		if (!WinInvalidateRegion(win->hc, win->rgn, FALSE))
			error("WinInvalidateRegion failed: %lx", WinGetLastError(hab));
		goto end;
	}
	rgnrect.crc = rgnrect.crcReturned;
	if (rgnrect.crcReturned > MAXINT / sizeof(RECTL)) overalloc();
	rects = pm_alloc(rgnrect.crcReturned * sizeof(RECTL), 0);
	if (!GpiQueryRegionRects(win->ps, win->rgn, NULL, &rgnrect, rects)) {
		error("GpiQueryRegionRects failed: %lx", WinGetLastError(hab));
		goto end_rects;
	}
	for (i = 0; i < rgnrect.crcReturned; i++) {
		struct rect xr;
		xr.x1 = rects[i].xLeft;
		xr.x2 = rects[i].xRight;
		xr.y1 = win->y - rects[i].yTop;
		xr.y2 = win->y - rects[i].yBottom;
		/*fprintf(stderr, "returned: %d, %d, %d, %d\n", xr.x1, xr.x2, xr.y1, xr.y2);*/
		add_to_rect_set(set, &xr);
	}
end_rects:
	pm_free(rects);
#endif
#ifdef WIN
	if (ScrollWindowEx(win->hc, scx, scy, &r, &r, win->rgn, NULL, 0) == ERROR) {
		error("ScrollWindowEx failed %u", (unsigned)GetLastError());
		goto end;
	}
	sz = GetRegionData(win->rgn, 0, NULL);
	if (!sz) {
		error("GetRegionData failed %u", (unsigned)GetLastError());
		goto end;
	}
	rgndata = pm_alloc(sz, 0);
	sz = GetRegionData(win->rgn, sz, rgndata);
	if (!sz) {
		error("GetRegionData failed %u", (unsigned)GetLastError());
		goto end_rgndata;
	}
	if (rgndata->rdh.iType != RDH_RECTANGLES || rgndata->rdh.nCount <= 1) {
		if (!InvalidateRgn(win->hc, win->rgn, FALSE))
			error("InvalidateRgn failed %u", (unsigned)GetLastError());
	} else {
		rects = (RECTL *)rgndata->Buffer;
		for (i = 0; i < rgndata->rdh.nCount; i++) {
			struct rect xr;
			xr.x1 = rects[i].xLeft;
			xr.x2 = rects[i].xRight;
			xr.y1 = rects[i].yTop;
			xr.y2 = rects[i].yBottom;
			/*fprintf(stderr, "returned: %d, %d, %d, %d\n", xr.x1, xr.x2, xr.y1, xr.y2);*/
			add_to_rect_set(set, &xr);
		}
	}
end_rgndata:
	pm_free(rgndata);
#endif
end:
	pm_scroll_redraws(win, &dev->clip, scx, scy);
	pm_unlock();
	debug_call(("M: scroll done"));
	return 0;
}

#ifdef OS2
#define pm_flush	NULL
#endif
#ifdef WIN
static void pm_flush(struct graphics_device *dev)
{
	PM_UNFLUSH();
	pm_do_flush(NULL);
}
#endif

struct graphics_driver pmshell_driver = {
#ifdef OS2
	cast_uchar "pmshell",
#endif
#ifdef WIN
	cast_uchar "windows",
#endif
	pm_init_driver,
	pm_init_device,
	pm_shutdown_device,
	pm_shutdown_driver,
	NULL,
	NULL,
	NULL,
	pm_get_af_unix_name,
	NULL,
	NULL,
	pm_get_empty_bitmap,
	pm_register_bitmap,
	pm_prepare_strip,
	pm_commit_strip,
	pm_unregister_bitmap,
	pm_draw_bitmap,
	NULL,		/* pm_get_color */
	pm_fill_area,
	pm_draw_hline,
	pm_draw_vline,
	pm_scroll,
	NULL,
	pm_flush,
	NULL, /* block */
	NULL, /* unblock */
	pm_set_palette,
	NULL, /* get_real_colors */
	pm_set_window_title,
	NULL, /* exec */
	NULL, /* set_clipboard_text */
	NULL, /* get_clipboard_text */
	0,			/* depth */
	0, 0,			/* x, y */
	GD_UNICODE_KEYS,	/* flags */
	NULL,			/* param */
};

#ifdef _UWIN

int main(int argc, char **argv);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nShow)
{
	int i, neednew, quote, clen;
	int argc = 1;
	char **argv;
	if (!(argv = malloc(2 * sizeof(char *)))) alloc_err: fatal_exit("can't allocate commandline");
	if (!(argv[0] = cast_char strdup("links"))) goto alloc_err;
	argv[1] = NULL;
	neednew = 1;
	quote = 0;
	clen = 0;
	for (i = 0; lpCmdLine[i]; i++) {
		unsigned c = lpCmdLine[i];
		if (c == ' ' && !quote) {
			neednew = 1;
			continue;
		}
		if (c == '"') {
			quote ^= 1;
			continue;
		}
		if (c == '\\' && lpCmdLine[i + 1]) {
			c = lpCmdLine[++i];
		}
		if (neednew) {
			if (!(argv = realloc(argv, (argc + 2) * sizeof(char *)))) goto alloc_err;
			if (!(argv[argc] = malloc(1))) goto alloc_err;
			argv[argc + 1] = NULL;
			argc++;
			neednew = 0;
			clen = 0;
		}
		if (!(argv[argc - 1] = realloc(argv[argc - 1], clen + 1))) goto alloc_err;
		argv[argc - 1][clen] = c;
		argv[argc - 1][clen + 1] = 0;
		clen++;
	}
	/*debug("cmdline: -%s-", lpCmdLine);*/
	return main(argc, argv);
}

#endif

#endif
