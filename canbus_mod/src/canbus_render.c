#include <kapi.h>
#include <libc.h>
#include <mod_display.h>
#include "canbus_render.h"
#include "full_body_argb.h"

#define W 1024
#define H 600
#define FB_BYTES (W * H * sizeof(uint32_t))
#define FB_PAGES ((FB_BYTES + 1023) / 1024)
#define BODY_X ((W - FULL_BODY_W) / 2)
#define BODY_Y ((H - FULL_BODY_H) / 2)
#define VIDEO_LAYER_FIRST 100
#define VIDEO_LAYER_COUNT 4

/* Native screen-pixel radii. The inner edge clears the wider car body. */
#define ARC_R0 280
#define ARC_R1 315
#define ARC_R2 350
#define ARC_R3 385
#define ARC_R4 420
#define ARC_X0 (W / 2 - ARC_R4 - 2)
#define ARC_X1 (W / 2 - ARC_R0 * 866 / 1000 + 2)
#define ARC_X2 (W / 2 + ARC_R0 * 866 / 1000 - 2)
#define ARC_X3 (W / 2 + ARC_R4 + 2)
#define ARC_Y0 (H / 2 - ARC_R4 / 2 - 2)
#define ARC_Y1 (H / 2 + ARC_R4 / 2 + 2)

static void *display;
static unsigned long layer;
static uint32_t *buffers[2];
static unsigned front_buffer;
static unsigned hidden_video_layers;

static void hide_video_layers(void)
{
	unsigned long arg[3] = {0};
	__disp_layer_info_t info;
	int i;

	for (i = 0; i < VIDEO_LAYER_COUNT; ++i) {
		eLIBs_memset(&info, 0, sizeof(info));
		arg[0] = VIDEO_LAYER_FIRST + i;
		arg[1] = (unsigned long)&info;
		if (esMODS_MIoctrl(display, MOD_DISP_CMD_LAYER_GET_PARA, 0, arg) != EPDK_OK ||
		    !info.fb.addr[0] || !info.scn_win.width || !info.scn_win.height)
			continue;
		arg[1] = 0;
		if (esMODS_MIoctrl(display, MOD_DISP_CMD_LAYER_CLOSE, 0, arg) == EPDK_OK) {
			hidden_video_layers |= 1U << i;
			eLIBs_printf("canbus: PDC hid video layer %lu (format %lu)\n",
			             arg[0], (unsigned long)info.fb.format);
		} else
			eLIBs_printf("canbus: PDC could not hide video layer %lu\n", arg[0]);
	}
	if (!hidden_video_layers)
		eLIBs_printf("canbus: PDC found no active video layer to hide\n");
}

static void restore_video_layers(void)
{
	unsigned long arg[3] = {0};
	int i;

	if (!display) { hidden_video_layers = 0; return; }
	for (i = 0; i < VIDEO_LAYER_COUNT; ++i) {
		if (!(hidden_video_layers & (1U << i))) continue;
		arg[0] = VIDEO_LAYER_FIRST + i;
		if (esMODS_MIoctrl(display, MOD_DISP_CMD_LAYER_OPEN, 0, arg) != EPDK_OK)
			eLIBs_printf("canbus: PDC could not restore video layer %lu\n", arg[0]);
	}
	hidden_video_layers = 0;
}

static uint32_t arc_color(uint8_t cm, int band)
{
	if (band == 0 && cm <= 10) return 0xffff0000;
	if (band == 1 && cm <= 50) return 0xffffff00;
	if (band == 2 && cm < 100) return 0xff008000;
	if (band == 3 && cm < 200) return 0xff008000;
	return 0xff000000;
}

/* Coordinates have 1/8-pixel units. Return 0 for background, 1..32 for arcs. */
static unsigned arc_index(int x8, int y8)
{
	int dx = x8 - W * 4, dy = y8 - H * 4;
	int ax, ay, r2, band, sensor;

	ax = dx < 0 ? -dx : dx;
	ay = dy < 0 ? -dy : dy;
	if (ay * 1000 > ax * 577) return 0;
	r2 = dx * dx + dy * dy;
	if (r2 < (ARC_R0 * 8) * (ARC_R0 * 8) ||
	    r2 >= (ARC_R4 * 8) * (ARC_R4 * 8)) return 0;
	band = r2 < (ARC_R1 * 8) * (ARC_R1 * 8) ? 0 :
	       r2 < (ARC_R2 * 8) * (ARC_R2 * 8) ? 1 :
	       r2 < (ARC_R3 * 8) * (ARC_R3 * 8) ? 2 : 3;
	if (dy < 0) sensor = ay * 1000 > ax * 268 ? 0 : 1;
	else sensor = ay * 1000 < ax * 268 ? 2 : 3;
	if (dx < 0) sensor = 7 - sensor;
	return 1 + sensor * 4 + band;
}

static uint32_t arc_pixel(const uint32_t palette[33], int x, int y)
{
	unsigned a = arc_index(x * 8 + 1, y * 8 + 1);
	unsigned b = arc_index(x * 8 + 7, y * 8 + 1);
	unsigned c = arc_index(x * 8 + 1, y * 8 + 7);
	unsigned d = arc_index(x * 8 + 7, y * 8 + 7);
	unsigned r = 0, g = 0, bl = 0;
	int sx, sy;

	if (a == b && a == c && a == d) return palette[a];
	/* Only edge pixels take the 4x4 coverage path. */
	for (sy = 1; sy < 8; sy += 2) {
		for (sx = 1; sx < 8; sx += 2) {
			uint32_t color = palette[arc_index(x * 8 + sx, y * 8 + sy)];
			r += (color >> 16) & 255;
			g += (color >> 8) & 255;
			bl += color & 255;
		}
	}
	return 0xff000000 | (((r + 8) / 16) << 16) |
	       (((g + 8) / 16) << 8) | ((bl + 8) / 16);
}

static uint32_t body_over(uint32_t background, uint32_t src)
{
	unsigned alpha = src >> 24;
	unsigned inverse, red, green, blue;
	if (!alpha) return background;
	if (alpha == 255) return src;
	inverse = 255 - alpha;
	red = (((src >> 16) & 255) * alpha +
	       ((background >> 16) & 255) * inverse + 127) / 255;
	green = (((src >> 8) & 255) * alpha +
	         ((background >> 8) & 255) * inverse + 127) / 255;
	blue = ((src & 255) * alpha +
	        (background & 255) * inverse + 127) / 255;
	return 0xff000000 | (red << 16) | (green << 8) | blue;
}

static void draw_arc_half(uint32_t *pixels, const uint32_t palette[33],
			  int left, int right)
{
	int x, y;
	for (y = ARC_Y0; y < ARC_Y1; ++y) {
		for (x = left; x < right; ++x) {
			uint32_t color = arc_pixel(palette, x, y);
			/* The rectangular update also covers the car's front and back. */
			if ((unsigned)(x - BODY_X) < FULL_BODY_W &&
			    (unsigned)(y - BODY_Y) < FULL_BODY_H)
				color = body_over(color,
				                  full_body_argb[(y - BODY_Y) * FULL_BODY_W +
				                                x - BODY_X]);
			pixels[y * W + x] = color;
		}
	}
}

static void draw_body(uint32_t *pixels)
{
	int x, y;
	for (y = 0; y < FULL_BODY_H; ++y) {
		for (x = 0; x < FULL_BODY_W; ++x) {
			uint32_t src = full_body_argb[y * FULL_BODY_W + x];
			uint32_t *dst = &pixels[(BODY_Y + y) * W + BODY_X + x];
			*dst = body_over(*dst, src);
		}
	}
}

static void draw_static(uint32_t *pixels)
{
	int i;
	for (i = 0; i < W * H; ++i) pixels[i] = 0xff000000;
	draw_body(pixels);
	esMEMS_FlushDCacheRegion(pixels, FB_BYTES);
}

int canbus_render_open(void)
{
	unsigned long arg[3] = {0};
	__disp_layer_info_t info;
	static int last_failure;
	int failure = 0;
	long ret;

	if (layer) return 0;
	/* The firmware's video module uses the system-owned display module handle. */
	display = esKSRV_Get_Display_Hld();
	if (!display) { failure = 1; goto fail; }
	buffers[0] = esMEMS_Palloc(FB_PAGES, 0);
	buffers[1] = esMEMS_Palloc(FB_PAGES, 0);
	if (!buffers[0] || !buffers[1]) { failure = 2; goto fail; }
	draw_static(buffers[0]);
	draw_static(buffers[1]);
	front_buffer = 0;

	arg[0] = MOD_DISP_LAYER_WORK_MODE_NORMAL;
	layer = esMODS_MIoctrl(display, MOD_DISP_LAYER_REQUEST, 0, arg);
	if ((long)layer <= 0) {
		ret = (long)layer;
		layer = 0;
		failure = 3;
		goto fail;
	}

	eLIBs_memset(&info, 0, sizeof(info));
	info.mode = MOD_DISP_LAYER_WORK_MODE_NORMAL;
	info.fb.addr[0] = (unsigned long)buffers[front_buffer];
	info.fb.size.width = W;
	info.fb.size.height = H;
	info.fb.mode = DISP_MOD_INTERLEAVED;
	info.fb.format = DISP_FORMAT_ARGB_8888;
	info.fb.seq = DISP_SEQ_ARGB;
	info.alpha_en = 1;
	info.alpha_val = 0xff;
	/* The device's video layer uses 0xff; 16 leaves PDC beneath it. */
	info.prio = 0xff;
	info.src_win.width =W ;
	info.src_win.height = H;
	info.scn_win.width = W;
	info.scn_win.height = H;
	arg[0] = layer;
	arg[1] = (unsigned long)&info;
	ret = esMODS_MIoctrl(display, MOD_DISP_CMD_LAYER_SET_PARA, 0, arg);
	if (ret != EPDK_OK) { failure = 4; goto fail; }
	arg[1] = 0;
	ret = esMODS_MIoctrl(display, MOD_DISP_CMD_LAYER_OPEN, 0, arg);
	if (ret != EPDK_OK) { failure = 5; goto fail; }
	last_failure = 0;
	hide_video_layers();
	eLIBs_printf("canbus: PDC layer open (handle %lu)\n", layer);
	return 0;
fail:
	if (failure != last_failure) {
		eLIBs_printf("canbus: PDC layer setup failed at step %d (return %ld)\n",
		             failure, failure >= 3 ? ret : 0L);
		last_failure = failure;
	}
	canbus_render_close();
	return -1;
}

void canbus_render_draw(const struct canbus_ui_state *state)
{
	uint32_t palette[33];
	uint32_t *pixels;
	__disp_video_fb_t fb;
	unsigned long arg[3] = {0};
	long ret;
	static int swap_failure;
	int i, band;
	if (!buffers[0] || !buffers[1] || !layer) return;
	pixels = buffers[front_buffer ^ 1];
	palette[0] = 0xff000000;
	for (i = 0; i < PDC_SENSOR_COUNT; ++i)
		for (band = 0; band < 4; ++band)
			palette[1 + i * 4 + band] = arc_color(state->pdc_cm[i], band);
	draw_arc_half(pixels, palette, ARC_X0, ARC_X1);
	draw_arc_half(pixels, palette, ARC_X2, ARC_X3);
	esMEMS_FlushDCacheRegion(pixels, FB_BYTES);
	/* display.mod's SET_FB reads the video FB address at offset 8. */
	eLIBs_memset(&fb, 0, sizeof(fb));
	fb.addr[0] = (unsigned long)pixels;
	fb.format = DISP_FORMAT_ARGB_8888;
	arg[0] = layer;
	arg[1] = (unsigned long)&fb;
	ret = esMODS_MIoctrl(display, MOD_DISP_CMD_LAYER_SET_FB, 0, arg);
	if (ret == EPDK_OK) {
		front_buffer ^= 1;
		swap_failure = 0;
	} else if (!swap_failure) {
		eLIBs_printf("canbus: PDC framebuffer swap failed (%ld)\n", ret);
		swap_failure = 1;
	}
}

void canbus_render_close(void)
{
	unsigned long arg[3] = {0};
	restore_video_layers();
	if (display && layer) {
		arg[0] = layer;
		esMODS_MIoctrl(display, MOD_DISP_CMD_LAYER_CLOSE, 0, arg);
		esMODS_MIoctrl(display, MOD_DISP_LAYER_RELEASE, 0, arg);
	}
	layer = 0;
	if (buffers[0]) esMEMS_Pfree(buffers[0], FB_PAGES);
	if (buffers[1]) esMEMS_Pfree(buffers[1], FB_PAGES);
	buffers[0] = 0;
	buffers[1] = 0;
	front_buffer = 0;
	/* esKSRV_Get_Display_Hld() returns a handle owned by the system. */
	display = 0;
}
