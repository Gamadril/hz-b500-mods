#include <mod_defs.h>
#include <libc.h>
#include <kapi.h>
#include <fcntl.h>
#include <unistd.h>
#include "canbus_pdc.h"
#include "canbus_media.h"
#include "canbus_render.h"
#include "canbus_slcan.h"

static __mp g_mp;
static struct canbus_ui_state g_ui;
static void *g_ui_lock;
static volatile int g_shutdown;
static volatile int g_ui_done;
static volatile int g_rx_done;
static uint8_t g_rx_buf[4096];
static __mp *g_wireless_mp;

#define UI_STACK_BYTES 0x4000
#define RX_STACK_BYTES 0x2000
#define UART_PATH "/dev/uart3"

/* wireless.mod MAGIC type 0x8c gives system module ID 0xec on this firmware.
 * init.axf uses ioctl 0x6f/aux 2 for wireless AA. Key indexes 28/30/31/32
 * become Android keycodes Play/Stop/Next/Previous; wireless.mod checks AA. */
#define WIRELESS_MODULE_ID 0xec
#define WIRELESS_AA_KEY_IOCTL 0x6f
#define WIRELESS_AA_AUX 2

static int wireless_open(void)
{
	uint32_t mid;

	if (g_wireless_mp)
		return 1;
	mid = esMODS_MInstall("d:\\mod\\wireless.mod", 0);
	if (mid != WIRELESS_MODULE_ID) {
		eLIBs_printf("canbus: wireless.mod install failed or unexpected ID %lu\n",
		             (unsigned long)mid);
		return 0;
	}
	g_wireless_mp = esMODS_MOpen((uint8_t)mid, 0);
	if (!g_wireless_mp) {
		eLIBs_printf("canbus: wireless.mod open failed\n");
		return 0;
	}
	return 1;
}

static void send_aa_media_key(enum canbus_media_key key)
{
	uint32_t index;

	if (!wireless_open())
		return;
	if (key == CANBUS_MEDIA_NEXT) {
		eLIBs_printf("canbus: 1D6 Up -> AA next request\n");
		index = 31;
	} else if (key == CANBUS_MEDIA_PREVIOUS) {
		eLIBs_printf("canbus: 1D6 Down -> AA previous request\n");
		index = 32;
	} else if (key == CANBUS_MEDIA_PLAY) {
		eLIBs_printf("canbus: 1D6 Up long -> AA play request\n");
		index = 28;
	} else if (key == CANBUS_MEDIA_STOP) {
		eLIBs_printf("canbus: 1D6 Down long -> AA stop request\n");
		index = 30;
	} else
		return;
	esMODS_MIoctrl(g_wireless_mp, WIRELESS_AA_KEY_IOCTL, WIRELESS_AA_AUX,
	               &index);
}

/* init.axf uses ioctl(fd, 0, config) with four short-enum fields. */
struct uart_line_config {
	uint8_t baudrate;
	uint8_t word_length;
	uint8_t stop_bit;
	uint8_t parity;
};

static void ui_lock(void)
{
	uint8_t err;
	esKRNL_SemPend(g_ui_lock, 0, &err);
}

static void ui_unlock(void)
{
	esKRNL_SemPost(g_ui_lock);
}

static struct canbus_ui_state ui_snapshot(void)
{
	struct canbus_ui_state snapshot;

	ui_lock();
	snapshot = g_ui;
	ui_unlock();
	return snapshot;
}

static void pdc_from_frame(const struct canbus_frame *frame)
{
	ui_lock();
	canbus_pdc_apply(&g_ui, frame);
	ui_unlock();
}

static void pdc_set_active(uint8_t active)
{
	ui_lock();
	if (g_ui.back_active != active) {
		g_ui.back_active = active;
		g_ui.generation++;
	}
	ui_unlock();
}

static void wait_before_retry(void)
{
	int i;
	for (i = 0; i < OS_TICKS_PER_SEC && !g_shutdown; ++i)
		esKRNL_TimeDly(1);
}

static void canbus_rx_thread(void *arg)
{
	struct canbus_slcan_parser parser = {{0}, 0, 0};
	struct canbus_media_state media = {0};
	struct canbus_frame frame;
	const struct uart_line_config config = {12, 3, 0, 0}; /* 921600 8N1 */
	int fd, logged_failure = 0;

	(void)arg;
	esKRNL_TaskNameSet((uint32_t)esKRNL_GetTIDCur(), "canbus_rx");
	while (!g_shutdown) {
		fd = open(UART_PATH, O_RDWR);
		if (fd < 0) {
			if (!logged_failure)
				eLIBs_printf("canbus: cannot open %s\n", UART_PATH);
			logged_failure = 1;
			wait_before_retry();
			continue;
		}
		if (ioctl(fd, 0, &config) != 0) {
			if (!logged_failure)
				eLIBs_printf("canbus: UART3 configuration failed\n");
			logged_failure = 1;
			close(fd);
			wait_before_retry();
			continue;
		}
		logged_failure = 0;
		parser.length = 0;
		parser.overflow = 0;
		eLIBs_printf("canbus: UART3 open at 921600 8N1 (750000 real)\n");
		while (!g_shutdown) {
			int i, result;
			esKRNL_TimeDly(1);
			if (g_shutdown) break;
			{
				enum canbus_media_key key = canbus_media_expire(&media,
						esKRNL_TimeGet(), OS_TICKS_PER_SEC / 2);
				if (key != CANBUS_MEDIA_NONE)
					send_aa_media_key(key);
			}
			result = read(fd, g_rx_buf, sizeof(g_rx_buf));
			if (result < 0) break;
			if (result == 0) continue;
			for (i = 0; i < result && !g_shutdown; ++i)
				if (canbus_slcan_feed(&parser, g_rx_buf[i], &frame)) {
					enum canbus_media_key key;
					pdc_from_frame(&frame);
					key = canbus_media_apply(&media, &frame,
								esKRNL_TimeGet(), OS_TICKS_PER_SEC / 2,
								OS_TICKS_PER_SEC);
					if (key != CANBUS_MEDIA_NONE)
						send_aa_media_key(key);
				}
		}
		close(fd);
		if (!g_shutdown) {
			eLIBs_printf("canbus: UART3 I/O failed; retrying\n");
			wait_before_retry();
		}
	}
	if (g_wireless_mp) {
		esMODS_MClose(g_wireless_mp);
		g_wireless_mp = 0;
	}
	g_rx_done = 1;
	esKRNL_TDel(EXEC_prioself);
}

static void canbus_ui_thread(void *arg)
{
	uint32_t last_generation = 0xffffffff;
	uint32_t sample_start = 0, draw_ticks = 0, max_draw_ticks = 0;
	unsigned int sample_frames = 0;
	int visible = 0;

	(void)arg;
	esKRNL_TaskNameSet((uint32_t)esKRNL_GetTIDCur(), "canbus_ui");
	while (!g_shutdown) {
		struct canbus_ui_state snapshot = ui_snapshot();
		if (snapshot.back_active && !visible) {
			if (canbus_render_open() == 0) {
				visible = 1;
				last_generation = 0xffffffff;
			}
		} else if (!snapshot.back_active && visible) {
			canbus_render_close();
			visible = 0;
		}
		snapshot = ui_snapshot();
		if (visible && snapshot.back_active &&
		    snapshot.generation != last_generation) {
			uint32_t started = esKRNL_TimeGet();
			uint32_t spent;
			uint32_t elapsed;
			if (sample_frames == 0) sample_start = started;
			canbus_render_draw(&snapshot);
			spent = esKRNL_TimeGet() - started;
			draw_ticks += spent;
			if (spent > max_draw_ticks) max_draw_ticks = spent;
			if (++sample_frames == 100) {
				elapsed = esKRNL_TimeGet() - sample_start;
				eLIBs_printf("canbus: PDC 100 frames: %lu fps, draw %lu ms total, %lu ms max\n",
				             elapsed ? (unsigned long)(100 * OS_TICKS_PER_SEC / elapsed) : 0UL,
				             (unsigned long)(draw_ticks * 1000 / OS_TICKS_PER_SEC),
				             (unsigned long)(max_draw_ticks * 1000 / OS_TICKS_PER_SEC));
				sample_frames = 0;
				draw_ticks = 0;
				max_draw_ticks = 0;
			}
			last_generation = snapshot.generation;
		}
		esKRNL_TimeDly(1);
	}
	if (visible) canbus_render_close();
	g_ui_done = 1;
	esKRNL_TDel(EXEC_prioself);
}

int32_t CANBUS_MInit(void)
{
	int i;
	uint8_t err;

	g_shutdown = 0;
	g_ui_done = 0;
	g_rx_done = 0;
	g_wireless_mp = 0;
	g_ui_lock = esKRNL_SemCreate(1);
	if (!g_ui_lock) return EPDK_FAIL;
	for (i = 0; i < PDC_SENSOR_COUNT; ++i)
		g_ui.pdc_cm[i] = 255;
	g_ui.back_active = 0;
	g_ui.generation = 0;
	if (!esKRNL_TCreate(canbus_ui_thread, 0, UI_STACK_BYTES, KRNL_priolevel5)) {
		esKRNL_SemDel(g_ui_lock, OS_DEL_ALWAYS, &err);
		g_ui_lock = 0;
		return EPDK_FAIL;
	}
	if (!esKRNL_TCreate(canbus_rx_thread, 0, RX_STACK_BYTES, KRNL_priolevel5)) {
		g_shutdown = 1;
		for (i = 0; i < OS_TICKS_PER_SEC && !g_ui_done; ++i)
			esKRNL_TimeDly(1);
		if (g_ui_done) esKRNL_SemDel(g_ui_lock, OS_DEL_ALWAYS, &err);
		return EPDK_FAIL;
	}
	return EPDK_OK;
}

int32_t CANBUS_MExit(void)
{
	int i;
	g_shutdown = 1;
	for (i = 0; i < 30 && (!g_ui_done || !g_rx_done); ++i)
		esKRNL_TimeDly(OS_TICKS_PER_SEC / 10);
	if (!g_ui_done || !g_rx_done) return EPDK_FAIL;
	{
		uint8_t err;
		esKRNL_SemDel(g_ui_lock, OS_DEL_ALWAYS, &err);
		g_ui_lock = 0;
	}
	return EPDK_OK;
}

__mp *CANBUS_MOpen(uint32_t mid, uint32_t mode)
{
	g_mp.mid = (__u8)mid;
	return &g_mp;
}

int32_t CANBUS_MClose(__mp *mp)
{
	(void)mp;
	return EPDK_OK;
}

uint32_t CANBUS_MRead(void *pdata, uint32_t size, uint32_t n, __mp *mp)
{
	(void)pdata;
	(void)mp;
	return size * n;
}

uint32_t CANBUS_MWrite(const void *pdata, uint32_t size, uint32_t n, __mp *mp)
{
	(void)pdata;
	(void)mp;
	return size * n;
}

long CANBUS_MIoctrl(__mp *mp, uint32_t cmd, int32_t aux, void *pbuffer)
{
	(void)mp;
	switch (cmd) {
	case CANBUS_IOC_ENTER_BACK:
		pdc_set_active(1);
		return EPDK_OK;
	case CANBUS_IOC_LEAVE_BACK:
		pdc_set_active(0);
		return EPDK_OK;
	case CANBUS_IOC_SET_RADAR:
		if (!pbuffer)
			return EPDK_FAIL;
		ui_lock();
		eLIBs_memcpy(g_ui.pdc_cm, pbuffer, PDC_SENSOR_COUNT);
		g_ui.generation++;
		ui_unlock();
		return EPDK_OK;
	default:
		(void)aux;
		return EPDK_FAIL;
	}
}
