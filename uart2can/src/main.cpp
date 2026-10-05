/*
 * UART <-> CAN bridge using SLCAN (Lawicel) protocol, LISTEN-ONLY.
 *
 * Board : ESP32-C3 SuperMini
 * CAN   : SN65HVD230 on GPIO2 (TX) / GPIO3 (RX), RS pin tied to GND (high-speed mode)
 * UART  : GPIO20 (RX) / GPIO21 (TX) (UART0)
 * Mode  : TWAI listen-only (never drives the bus, no ACK), default 100 kbit/s
 *
 * Supported SLCAN commands (terminated with '\r'):
 *   O            open channel (start receiving)
 *   C            close channel
 *   S2..S8       set bitrate (channel must be closed)
 *                  S2=50k S3=100k S4=125k S5=250k S6=500k S7=800k S8=1M
 *   Z0 / Z1      timestamps off / on (4 hex digits, ms, wraps at 60000)
 *   V / v / N    version / firmware string / serial number
 *   F            status flags
 *   M, m, L, l   accepted and ignored
 *   t T r R      transmit -> rejected (BEL), since we are listen-only
 *
 * Reply: '\r' = OK, '\a' (0x07) = error.
 * Received frames: tiiildd.. / Tiiiiiiiildd.. / riiil / Riiiiiiiil (+ optional timestamp)
 */

#include <Arduino.h>
#include "driver/twai.h"

#ifndef CAN_TX_PIN
#define CAN_TX_PIN 2
#endif
#ifndef CAN_RX_PIN
#define CAN_RX_PIN 3
#endif
#ifndef UART_RX_PIN
#define UART_RX_PIN 0
#endif
#ifndef UART_TX_PIN
#define UART_TX_PIN 1
#endif
#ifndef UART_BAUD
#define UART_BAUD 115200
#endif
#ifndef DEFAULT_CAN_SPEED_IDX
#define DEFAULT_CAN_SPEED_IDX 3
#endif
#ifndef AUTO_OPEN
#define AUTO_OPEN 1
#endif

#define FW_VERSION "V1013\r"
#define CR  '\r'
#define BEL '\a'

static HardwareSerial bridge(1);

static bool     channelOpen = false;
static bool     timestamps  = false;
static uint8_t  speedIdx    = DEFAULT_CAN_SPEED_IDX;

static char     cmdBuf[48];
static size_t   cmdLen = 0;
static bool     cmdOverflow = false;

// ---------------------------------------------------------------------------
static bool getTiming(uint8_t idx, twai_timing_config_t &t) {
  switch (idx) {
    case 2: t = TWAI_TIMING_CONFIG_50KBITS();  return true;
    case 3: t = TWAI_TIMING_CONFIG_100KBITS(); return true;
    case 4: t = TWAI_TIMING_CONFIG_125KBITS(); return true;
    case 5: t = TWAI_TIMING_CONFIG_250KBITS(); return true;
    case 6: t = TWAI_TIMING_CONFIG_500KBITS(); return true;
    case 7: t = TWAI_TIMING_CONFIG_800KBITS(); return true;
    case 8: t = TWAI_TIMING_CONFIG_1MBITS();   return true;
    default: return false;   // S0/S1 (10k/20k) not supported
  }
}

static bool canOpen() {
  if (channelOpen) return false;

  twai_timing_config_t t;
  if (!getTiming(speedIdx, t)) return false;

  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
  g.rx_queue_len = 256;
  g.tx_queue_len = 1;
  g.alerts_enabled = TWAI_ALERT_NONE;

  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
  if (twai_start() != ESP_OK) {
    twai_driver_uninstall();
    return false;
  }
  channelOpen = true;
  return true;
}

static bool canClose() {
  if (!channelOpen) return false;
  twai_stop();
  twai_driver_uninstall();
  channelOpen = false;
  return true;
}

// ---------------------------------------------------------------------------
static inline void reply(bool ok) { bridge.write(ok ? (uint8_t)CR : (uint8_t)BEL); }

static const char HEXCH[] = "0123456789ABCDEF";

static size_t putHex(char *p, uint32_t v, uint8_t digits) {
  for (int i = digits - 1; i >= 0; i--) {
    p[i] = HEXCH[v & 0xF];
    v >>= 4;
  }
  return digits;
}

static void sendFrame(const twai_message_t &m) {
  char out[40];
  char *p = out;

  bool ext = m.extd;
  bool rtr = m.rtr;

  if (ext) {
    *p++ = rtr ? 'R' : 'T';
    p += putHex(p, m.identifier & 0x1FFFFFFF, 8);
  } else {
    *p++ = rtr ? 'r' : 't';
    p += putHex(p, m.identifier & 0x7FF, 3);
  }

  uint8_t dlc = m.data_length_code > 8 ? 8 : m.data_length_code;
  *p++ = '0' + dlc;

  if (!rtr) {
    for (uint8_t i = 0; i < dlc; i++) p += putHex(p, m.data[i], 2);
  }

  if (timestamps) p += putHex(p, millis() % 60000UL, 4);

  *p++ = CR;
  bridge.write((const uint8_t *)out, p - out);
}

// ---------------------------------------------------------------------------
static void sendStatus() {
  uint8_t flags = 0;
  if (channelOpen) {
    twai_status_info_t s;
    if (twai_get_status_info(&s) == ESP_OK) {
      if (s.msgs_to_rx >= 256)                         flags |= 0x01; // RX queue full
      if (s.tx_error_counter >= 96 || s.rx_error_counter >= 96) flags |= 0x04; // error warning
      if (s.rx_missed_count > 0)                       flags |= 0x08; // data overrun
      if (s.tx_error_counter >= 128 || s.rx_error_counter >= 128) flags |= 0x20; // error passive
      if (s.bus_error_count > 0)                       flags |= 0x80; // bus error
    }
  }
  char out[5] = {'F', HEXCH[flags >> 4], HEXCH[flags & 0xF], CR, 0};
  bridge.write((const uint8_t *)out, 4);
}

static void handleCommand(const char *c, size_t len) {
  if (len == 0) { return; }

  switch (c[0]) {
    case 'O': reply(canOpen());  break;
    case 'C': reply(canClose()); break;

    case 'S': {
      if (len != 2 || channelOpen) { reply(false); break; }
      uint8_t idx = c[1] - '0';
      twai_timing_config_t t;
      if (getTiming(idx, t)) { speedIdx = idx; reply(true); }
      else reply(false);
      break;
    }

    case 'Z':
      if (len == 2 && !channelOpen && (c[1] == '0' || c[1] == '1')) {
        timestamps = (c[1] == '1');
        reply(true);
      } else reply(false);
      break;

    case 'V': bridge.print(FW_VERSION); break;
    case 'v': bridge.print("vESP32C3-SLCAN-LO\r"); break;
    case 'N': bridge.print("N0001\r"); break;
    case 'F': sendStatus(); break;

    case 'M': case 'm':   // acceptance code/mask: ignored (accept all)
    case 'L': case 'l':   // listen-only: always on
      reply(true);
      break;

    case 't': case 'T': case 'r': case 'R':   // TX not possible in listen-only
    default:
      reply(false);
      break;
  }
}

static void pollUart() {
  while (bridge.available()) {
    char ch = (char)bridge.read();
    if (ch == CR) {
      if (cmdOverflow) reply(false);
      else             handleCommand(cmdBuf, cmdLen);
      cmdLen = 0;
      cmdOverflow = false;
    } else if (ch == '\n') {
      // ignore LF
    } else if (cmdLen < sizeof(cmdBuf)) {
      cmdBuf[cmdLen++] = ch;
    } else {
      cmdOverflow = true;
    }
  }
}

static void pollCan() {
  if (!channelOpen) return;
  twai_message_t m;
  // drain everything available, but stay responsive to UART input
  for (int i = 0; i < 32; i++) {
    if (twai_receive(&m, 0) != ESP_OK) break;
    sendFrame(m);
  }
}

// ---------------------------------------------------------------------------
void setup() {
  bridge.setRxBufferSize(1024);
  bridge.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
#if AUTO_OPEN
  canOpen();
#endif

}

void loop() {
  pollUart();
  pollCan();
}