"""
RP2350-CAN: pinball solenoid controller.

CAN protocol:
  RX - Message ID 0x100 (SOLENOID_CMD), 1 byte payload:
    bit 0: left flipper solenoid
    bit 1: right flipper solenoid
    bit 2: start button solenoid

  TX - Message ID 0x101 (PINBALL_STATE), 1 byte payload:
    bit 0: heartbeat (toggles each send)

Based on the Waveshare RP2350-CAN demo (XL2515, MCP2515-compatible).
Flash MicroPython to the RP2350 first, then copy this file as main.py.
"""
from machine import Pin, SPI
import time

# -- Hardware pins (from schematic) --
XL2515_SPI_PORT = 1
XL2515_SCLK_PIN = 10
XL2515_MOSI_PIN = 11
XL2515_MISO_PIN = 12
XL2515_CS_PIN = 9
XL2515_INT_PIN = 8
LED_PIN = 25
SOLENOID_L_PIN = 2
SOLENOID_R_PIN = 3
SOLENOID_START_PIN = 4

# -- CAN protocol --
SOLENOID_CMD_ID = 0x100
PINBALL_STATE_ID = 0x101
CAN_RATE = "500KBPS"
HEARTBEAT_MS = 1000

# -- XL2515 registers (MCP2515-compatible) --
CANSTAT  = 0x0E
CANCTRL  = 0x0F
CNF1     = 0x2A
CNF2     = 0x29
CNF3     = 0x28
CANINTE  = 0x2B
CANINTF  = 0x2C

TXB0SIDH = 0x31
TXB0SIDL = 0x32
TXB0DLC  = 0x35
TXB0D0   = 0x36

CAN_RTS_TXB0 = 0x81

RXB0CTRL = 0x60
RXB0SIDH = 0x61
RXB0SIDL = 0x62
RXB0DLC  = 0x65
RXB0D0   = 0x66

RXM0SIDH = 0x20
RXM0SIDL = 0x21

CAN_RESET = 0xC0
CAN_READ  = 0x03
CAN_WRITE = 0x02

# Bit rate configs for 16MHz crystal [CNF1, CNF2, CNF3]
RATE_CONFIGS = {
    "125KBPS": [0x03, 0x9E, 0x03],
    "250KBPS": [0x01, 0x1E, 0x03],
    "500KBPS": [0x00, 0x9E, 0x03],
    "1000KBPS": [0x00, 0x82, 0x02],
}


class XL2515:
    def __init__(self, rate="500KBPS"):
        self.spi = SPI(XL2515_SPI_PORT, 10_000_000, polarity=0, phase=0, bits=8,
                       sck=Pin(XL2515_SCLK_PIN), mosi=Pin(XL2515_MOSI_PIN), miso=Pin(XL2515_MISO_PIN))
        self.cs = Pin(XL2515_CS_PIN, Pin.OUT, value=1)
        self.int_pin = Pin(XL2515_INT_PIN, Pin.IN, Pin.PULL_UP)
        self.recv_flag = False
        self.int_pin.irq(handler=self._int_cb, trigger=Pin.IRQ_FALLING)

        self._reset()
        time.sleep_ms(100)
        self._config(rate)

    def _reset(self):
        self.cs(0)
        self.spi.write(bytes([CAN_RESET]))
        self.cs(1)

    def _read(self, reg):
        self.cs(0)
        self.spi.write(bytes([CAN_READ, reg]))
        data = self.spi.read(1)
        self.cs(1)
        return data[0]

    def _write(self, reg, val):
        self.cs(0)
        self.spi.write(bytes([CAN_WRITE, reg, val]))
        self.cs(1)

    def _int_cb(self, pin):
        self.recv_flag = True

    def _config(self, rate):
        cfg = RATE_CONFIGS[rate]
        self._write(CNF1, cfg[0])
        self._write(CNF2, cfg[1])
        self._write(CNF3, cfg[2])

        # TX defaults
        self._write(TXB0SIDH, 0xFF)
        self._write(TXB0SIDL, 0xE0)
        self._write(TXB0DLC, 0x40 | 0x08)

        # RX: accept all message IDs (RXM bits = 11 = no filter)
        self._write(RXB0CTRL, 0x60)
        self._write(RXB0DLC, 0x08)

        # Mask = 0x000 (accept everything)
        self._write(RXM0SIDH, 0x00)
        self._write(RXM0SIDL, 0x00)

        # Interrupts
        self._write(CANINTF, 0x00)
        self._write(CANINTE, 0x01)

        # Normal mode
        self._write(CANCTRL, 0x04)  # REQOP_NORMAL | CLKOUT_ENABLED
        if (self._read(CANSTAT) & 0xE0) != 0x00:
            self._write(CANCTRL, 0x04)

    def send(self, can_id, data):
        # Set TX buffer standard ID
        self._write(TXB0SIDH, (can_id >> 3) & 0xFF)
        self._write(TXB0SIDL, (can_id << 5) & 0xE0)
        # Set DLC and data bytes
        self._write(TXB0DLC, len(data))
        for i, b in enumerate(data):
            self._write(TXB0D0 + i, b)
        # Request to send
        self.cs(0)
        self.spi.write(bytes([CAN_RTS_TXB0]))
        self.cs(1)

    def recv(self):
        if not self.recv_flag:
            return None, None
        self.recv_flag = False

        sid_h = self._read(RXB0SIDH)
        sid_l = self._read(RXB0SIDL)
        can_id = (sid_h << 3) | (sid_l >> 5)

        # Wait for RX buffer ready
        timeout = 100
        while not (self._read(CANINTF) & 0x01):
            timeout -= 1
            if timeout <= 0:
                return None, None

        dlc = self._read(RXB0DLC) & 0x0F
        data = bytearray(dlc)
        for i in range(dlc):
            data[i] = self._read(RXB0D0 + i)

        # Clear interrupt and RX buffer
        self._write(CANINTF, 0x00)
        self._write(CANINTE, 0x01)

        return can_id, data


def main():
    led = Pin(LED_PIN, Pin.OUT, value=0)
    sol_l = Pin(SOLENOID_L_PIN, Pin.OUT, value=0)
    sol_r = Pin(SOLENOID_R_PIN, Pin.OUT, value=0)
    sol_start = Pin(SOLENOID_START_PIN, Pin.OUT, value=0)
    can = XL2515(CAN_RATE)
    print(f"Listening for CAN ID 0x{SOLENOID_CMD_ID:03X} at {CAN_RATE}...")

    heartbeat = 0
    last_heartbeat_ms = time.ticks_ms()

    while True:
        # Send heartbeat (PINBALL_STATE) periodically
        now = time.ticks_ms()
        if time.ticks_diff(now, last_heartbeat_ms) >= HEARTBEAT_MS:
            can.send(PINBALL_STATE_ID, bytes([heartbeat & 0x01]))
            heartbeat ^= 1
            last_heartbeat_ms = now

        # Receive solenoid commands
        can_id, data = can.recv()
        if can_id is not None and data is not None:
            if can_id == SOLENOID_CMD_ID and len(data) >= 1:
                sol_l.value(data[0] & 0x01)
                sol_r.value((data[0] >> 1) & 0x01)
                sol_start.value((data[0] >> 2) & 0x01)
                active = data[0] & 0x07
                led.value(1 if active else 0)
                print(f"ID=0x{can_id:03X} L={data[0]&1} R={(data[0]>>1)&1} S={(data[0]>>2)&1}")
            else:
                print(f"ID=0x{can_id:03X} data={[hex(b) for b in data]} (ignored)")
        time.sleep_ms(1)


main()
