# =============================================================================
# TM4C123G Smart Home Controller — Desktop GUI v2.0
# =============================================================================
# Requires: pyserial  (pip install pyserial)
# Layout:
#   [Connection bar + watchdog]
#   [Temperature | Fan | Lighting]   <- sensor / control panels
#   [RGB Controls | Utilities]
#   [Alert log]
#   [Serial log  (collapsible)]
# =============================================================================

import json
import os
import threading
import queue
import time
import tkinter as tk
from tkinter import ttk, messagebox
from datetime import datetime
from collections import deque

import serial
import serial.tools.list_ports

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------
SETTINGS_FILE       = "tiva_bt_gui_settings.json"
STATE_POLL_MS       = 10_000    # send STATE every 10 seconds
WATCHDOG_CHECK_MS   = 1_000     # refresh "last seen" label every 1 second
WATCHDOG_WARN_S     = 90        # amber warning threshold
WATCHDOG_CRIT_S     = 180       # red critical threshold  (3 minutes)
SPARKLINE_MAX       = 15        # max temperature history points


# -----------------------------------------------------------------------------
# Persistence helpers
# -----------------------------------------------------------------------------
def load_settings() -> dict:
    if not os.path.exists(SETTINGS_FILE):
        return {}
    try:
        with open(SETTINGS_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def save_settings(data: dict):
    try:
        with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
    except Exception:
        pass


# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------
def _parse_kv(msg: str) -> dict:
    """Extract key=value tokens from a firmware response line."""
    fields = {}
    for token in msg.split():
        if "=" in token:
            k, v = token.split("=", 1)
            fields[k] = v
    return fields


def _ts() -> str:
    return datetime.now().strftime("%I:%M:%S %p")


# =============================================================================
# Main application
# =============================================================================
class TivaBtGui(tk.Tk):

    # -------------------------------------------------------------------------
    # Construction
    # -------------------------------------------------------------------------
    def __init__(self):
        super().__init__()
        self.title("TM4C123G Smart Home Controller")
        self.geometry("1020x760")
        self.resizable(True, True)

        # Serial / threading
        self.ser            = None
        self.reader_thread  = None
        self.stop_event     = threading.Event()
        self.rx_queue       = queue.Queue()

        # Persistent settings
        self.settings = load_settings()

        # --- UI state variables ---
        # Connection
        self.status_text    = tk.StringVar(value="Disconnected")
        self.status_color   = tk.StringVar(value="#cc3333")
        self.last_seen_var  = tk.StringVar(value="")

        # Temperature
        self.temp_var       = tk.StringVar(value="-- °F")
        self.humid_var      = tk.StringVar(value="--%")
        self.temp_history   = deque(maxlen=SPARKLINE_MAX)   # floats

        # Light
        self.light_var      = tk.StringVar(value="--%")

        # Fan
        self.fan_status_var = tk.StringVar(value="OFF")
        self.fan_mode_var   = tk.StringVar(value="AUTO")
        self.thresh_var     = tk.IntVar(value=80)

        # Motion
        self.motion_var     = tk.StringVar(value="ARMED")

        # LDR
        self.ldr_mode_var   = tk.StringVar(value="AUTO")
        self.ldr_bright_var = tk.IntVar(value=100)

        # LED strip
        self.led_strip_var  = tk.StringVar(value="OFF")

        # RGB
        self.r              = tk.BooleanVar(value=False)
        self.g              = tk.BooleanVar(value=False)
        self.b              = tk.BooleanVar(value=False)
        self.preset         = tk.StringVar(value="Custom")

        # Internal
        self.last_seen_time  = None
        self._state_poll_id  = None
        self._wdog_tick_id   = None
        self._log_collapsed  = False

        # Build UI
        self._build_ui()
        self._refresh_ports()

        # Restore last port selection
        last_port = self.settings.get("last_port")
        if last_port:
            self._select_port_by_device(last_port)

        # Start queue drain loop
        self.after(50, self._drain_rx_queue)

    # -------------------------------------------------------------------------
    # UI construction
    # -------------------------------------------------------------------------
    def _build_ui(self):
        outer = ttk.Frame(self, padding=8)
        outer.pack(fill="both", expand=True)

        self._build_connection_bar(outer)

        # Three sensor/control panels
        panels = ttk.Frame(outer)
        panels.pack(fill="x", pady=(8, 4))
        panels.columnconfigure(0, weight=1)
        panels.columnconfigure(1, weight=1)
        panels.columnconfigure(2, weight=1)
        self._build_temp_panel(panels)
        self._build_fan_panel(panels)
        self._build_lighting_panel(panels)

        # RGB + Utilities row
        ctrl_row = ttk.Frame(outer)
        ctrl_row.pack(fill="x", pady=(4, 4))
        ctrl_row.columnconfigure(0, weight=1)
        ctrl_row.columnconfigure(1, weight=1)
        self._build_rgb_panel(ctrl_row)
        self._build_utilities_panel(ctrl_row)

        self._build_alert_log(outer)
        self._build_serial_log(outer)

    # -- Connection bar -------------------------------------------------------
    def _build_connection_bar(self, parent):
        bar = ttk.Frame(parent)
        bar.pack(fill="x")

        ttk.Label(bar, text="COM Port:").pack(side="left")
        self.port_combo = ttk.Combobox(bar, width=28, state="readonly")
        self.port_combo.pack(side="left", padx=(4, 10))

        ttk.Label(bar, text="Baud:").pack(side="left")
        self.baud_combo = ttk.Combobox(bar, width=8, state="readonly",
                                       values=["9600", "38400", "115200"])
        self.baud_combo.set(str(self.settings.get("baud", 9600)))
        self.baud_combo.pack(side="left", padx=(4, 10))

        ttk.Button(bar, text="Refresh", command=self._refresh_ports).pack(side="left", padx=(0, 6))
        self.connect_btn    = ttk.Button(bar, text="Connect",    command=self._connect)
        self.disconnect_btn = ttk.Button(bar, text="Disconnect", command=self._disconnect,
                                         state="disabled")
        self.connect_btn.pack(side="left")
        self.disconnect_btn.pack(side="left", padx=(6, 0))

        # Right side: watchdog + status dot
        right = ttk.Frame(bar)
        right.pack(side="right")

        self.last_seen_label = ttk.Label(right, textvariable=self.last_seen_var,
                                         foreground="#888888")
        self.last_seen_label.pack(side="left", padx=(0, 12))

        self.status_dot = tk.Canvas(right, width=14, height=14, highlightthickness=0)
        self.status_dot.pack(side="left", padx=(0, 4))
        self._draw_status_dot()
        ttk.Label(right, textvariable=self.status_text).pack(side="left")

    # -- Temperature panel ----------------------------------------------------
    def _build_temp_panel(self, parent):
        box = ttk.LabelFrame(parent, text="Temperature", padding=8)
        box.grid(row=0, column=0, sticky="nsew", padx=(0, 4))

        ttk.Label(box, textvariable=self.temp_var,
                  font=("TkDefaultFont", 20, "bold")).pack()

        # Humidity sits directly under the temperature reading
        humid_row = ttk.Frame(box)
        humid_row.pack(pady=(2, 0))
        ttk.Label(humid_row, text="Humidity:",
                  foreground="#888888").pack(side="left", padx=(0, 4))
        ttk.Label(humid_row, textvariable=self.humid_var,
                  font=("TkDefaultFont", 11, "bold")).pack(side="left")

        # Sparkline canvas
        self.sparkline = tk.Canvas(box, height=50, bg="#1e1e1e",
                                   highlightthickness=1, highlightbackground="#444")
        self.sparkline.pack(fill="x", pady=(6, 6))

        ttk.Label(box, textvariable=self.light_var,
                  foreground="#888888").pack()

        ttk.Button(box, text="Read Now",
                   command=self._poll_state_once).pack(pady=(6, 0))
        ttk.Label(box, text="auto-updates every 10 s",
                  foreground="#888888", font=("TkDefaultFont", 8)).pack()

    # -- Fan panel ------------------------------------------------------------
    def _build_fan_panel(self, parent):
        box = ttk.LabelFrame(parent, text="Fan", padding=8)
        box.grid(row=0, column=1, sticky="nsew", padx=4)
        box.columnconfigure(0, weight=1)
        box.columnconfigure(1, weight=1)

        # Mode toggle
        mode_row = ttk.Frame(box)
        mode_row.grid(row=0, column=0, columnspan=2, pady=(0, 6))
        self.fan_mode_btn = ttk.Button(mode_row, text="Mode: AUTO",
                                       command=self._fan_mode_toggle, width=14)
        self.fan_mode_btn.pack(side="left")
        self.fan_status_lbl = ttk.Label(mode_row, textvariable=self.fan_status_var,
                                         foreground="#888888", font=("TkDefaultFont", 10, "bold"))
        self.fan_status_lbl.pack(side="left", padx=(10, 0))

        # Threshold slider
        ttk.Label(box, text="Threshold (°F):").grid(row=1, column=0, columnspan=2, sticky="w")
        thresh_row = ttk.Frame(box)
        thresh_row.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(2, 6))
        self.thresh_slider = ttk.Scale(thresh_row, from_=60, to=100,
                                       variable=self.thresh_var, orient="horizontal")
        self.thresh_slider.pack(side="left", fill="x", expand=True)
        self.thresh_slider.bind("<ButtonRelease-1>", self._on_thresh_release)
        ttk.Label(thresh_row, textvariable=self.thresh_var, width=4).pack(side="left")

        # Manual override buttons
        ttk.Label(box, text="Manual:").grid(row=3, column=0, columnspan=2, sticky="w")
        self.fan_on_btn  = ttk.Button(box, text="FAN ON",  command=self._fan_manual_on,
                                      state="disabled")
        self.fan_off_btn = ttk.Button(box, text="FAN OFF", command=self._fan_manual_off,
                                      state="disabled")
        self.fan_on_btn.grid (row=4, column=0, sticky="ew", padx=(0, 2), pady=(2, 0))
        self.fan_off_btn.grid(row=4, column=1, sticky="ew", padx=(2, 0), pady=(2, 0))

    # -- Lighting panel -------------------------------------------------------
    def _build_lighting_panel(self, parent):
        box = ttk.LabelFrame(parent, text="Lighting", padding=8)
        box.grid(row=0, column=2, sticky="nsew", padx=(4, 0))
        box.columnconfigure(0, weight=1)
        box.columnconfigure(1, weight=1)

        # Motion arm/disarm
        ttk.Label(box, text="Motion Sensor:").grid(row=0, column=0, sticky="w")
        self.motion_btn = ttk.Button(box, text="ARMED", command=self._motion_toggle,
                                     width=10)
        self.motion_btn.grid(row=0, column=1, sticky="e", pady=(0, 6))

        ttk.Separator(box, orient="horizontal").grid(row=1, column=0, columnspan=2,
                                                      sticky="ew", pady=4)

        # LDR mode
        ttk.Label(box, text="Ambient Light:").grid(row=2, column=0, sticky="w")
        self.ldr_mode_btn = ttk.Button(box, text="LDR: AUTO",
                                        command=self._ldr_mode_toggle, width=10)
        self.ldr_mode_btn.grid(row=2, column=1, sticky="e")

        # Brightness slider
        ttk.Label(box, text="Brightness (manual):").grid(row=3, column=0,
                                                           columnspan=2, sticky="w",
                                                           pady=(6, 0))
        bright_row = ttk.Frame(box)
        bright_row.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(2, 4))
        self.ldr_slider = ttk.Scale(bright_row, from_=0, to=100,
                                    variable=self.ldr_bright_var, orient="horizontal",
                                    state="disabled")
        self.ldr_slider.pack(side="left", fill="x", expand=True)
        self.ldr_slider.bind("<ButtonRelease-1>", self._on_ldr_release)
        ttk.Label(bright_row, textvariable=self.ldr_bright_var, width=4).pack(side="left")

        ttk.Button(box, text="Resume Auto",
                   command=self._ldr_resume_auto).grid(row=5, column=0,
                                                        columnspan=2, sticky="ew",
                                                        pady=(2, 0))

        ttk.Separator(box, orient="horizontal").grid(row=6, column=0, columnspan=2,
                                                      sticky="ew", pady=(8, 4))

        # LED strip manual on/off
        ttk.Label(box, text="LED Strip:").grid(row=7, column=0, sticky="w")
        self.led_status_lbl = ttk.Label(box, textvariable=self.led_strip_var,
                                         font=("TkDefaultFont", 9, "bold"),
                                         foreground="#888888")
        self.led_status_lbl.grid(row=7, column=1, sticky="e")

        strip_btn_row = ttk.Frame(box)
        strip_btn_row.grid(row=8, column=0, columnspan=2, sticky="ew", pady=(4, 0))
        self.led_on_btn  = ttk.Button(strip_btn_row, text="STRIP ON",
                                       command=self._strip_on)
        self.led_off_btn = ttk.Button(strip_btn_row, text="STRIP OFF",
                                       command=self._strip_off)
        self.led_on_btn.pack(side="left", expand=True, fill="x", padx=(0, 2))
        self.led_off_btn.pack(side="left", expand=True, fill="x", padx=(2, 0))

    # -- RGB panel ------------------------------------------------------------
    def _build_rgb_panel(self, parent):
        box = ttk.LabelFrame(parent, text="RGB LED Controls", padding=8)
        box.grid(row=0, column=0, sticky="nsew", padx=(0, 4))

        toggle_row = ttk.Frame(box)
        toggle_row.pack(fill="x")
        self.r_btn = ttk.Checkbutton(toggle_row, text="Red",   variable=self.r,
                                      command=self._apply_rgb_from_toggles)
        self.g_btn = ttk.Checkbutton(toggle_row, text="Green", variable=self.g,
                                      command=self._apply_rgb_from_toggles)
        self.b_btn = ttk.Checkbutton(toggle_row, text="Blue",  variable=self.b,
                                      command=self._apply_rgb_from_toggles)
        self.r_btn.pack(side="left", padx=4)
        self.g_btn.pack(side="left", padx=4)
        self.b_btn.pack(side="left", padx=4)
        ttk.Button(toggle_row, text="All Off", command=self._all_off).pack(side="left", padx=8)

        # Presets
        presets_box = ttk.LabelFrame(box, text="Presets", padding=6)
        presets_box.pack(fill="x", pady=(6, 0))
        presets = [("Off","Off"),("Red","Red"),("Green","Green"),
                   ("Blue","Blue"),("White","White"),("Cyan","Cyan"),
                   ("Magenta","Magenta"),("Yellow","Yellow"),("Custom","Custom")]
        for idx, (label, value) in enumerate(presets):
            ttk.Radiobutton(presets_box, text=label, value=value,
                            variable=self.preset,
                            command=self._apply_preset).grid(
                row=idx // 3, column=idx % 3, padx=6, pady=2, sticky="w")

    # -- Utilities panel ------------------------------------------------------
    def _build_utilities_panel(self, parent):
        box = ttk.LabelFrame(parent, text="Utilities", padding=8)
        box.grid(row=0, column=1, sticky="nsew", padx=(4, 0))
        box.columnconfigure(1, weight=1)

        ttk.Button(box, text="PING",  command=lambda: self._send_cmd("PING")).grid(
            row=0, column=0, padx=4, pady=3, sticky="ew")
        ttk.Button(box, text="STATE", command=self._poll_state_once).grid(
            row=0, column=1, padx=4, pady=3, sticky="ew")
        ttk.Button(box, text="HELP",  command=lambda: self._send_cmd("HELP")).grid(
            row=1, column=0, padx=4, pady=3, sticky="ew")
        ttk.Button(box, text="BUZZ:3", command=lambda: self._send_cmd("BUZZ:3")).grid(
            row=1, column=1, padx=4, pady=3, sticky="ew")

        ttk.Label(box, text="Manual cmd:").grid(row=2, column=0, padx=4,
                                                  pady=(10, 3), sticky="w")
        self.manual_entry = ttk.Entry(box, width=20)
        self.manual_entry.grid(row=2, column=1, padx=4, pady=(10, 3), sticky="ew")
        self.manual_entry.bind("<Return>", lambda e: self._send_manual())
        ttk.Button(box, text="Send", command=self._send_manual).grid(
            row=3, column=1, padx=4, pady=2, sticky="e")

        ttk.Separator(box, orient="horizontal").grid(row=4, column=0, columnspan=2,
                                                      sticky="ew", pady=(8, 4))
        ttk.Button(box, text="Exit App", command=self._exit_app).grid(
            row=5, column=0, columnspan=2, padx=4, pady=2, sticky="ew")

    # -- Alert log ------------------------------------------------------------
    def _build_alert_log(self, parent):
        alert_frame = ttk.LabelFrame(parent, text="Alerts", padding=6)
        alert_frame.pack(fill="x", pady=(4, 4))

        self.alert_log = tk.Text(alert_frame, height=4, wrap="word",
                                  state="disabled", font=("TkDefaultFont", 9))
        sb = ttk.Scrollbar(alert_frame, command=self.alert_log.yview)
        self.alert_log.configure(yscrollcommand=sb.set)
        self.alert_log.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")

    # -- Serial log -----------------------------------------------------------
    def _build_serial_log(self, parent):
        # Header row with toggle
        hdr = ttk.Frame(parent)
        hdr.pack(fill="x")
        ttk.Label(hdr, text="Serial Log (raw)").pack(side="left")
        self.log_toggle_btn = ttk.Button(hdr, text="▼ Hide",
                                          command=self._toggle_serial_log, width=8)
        self.log_toggle_btn.pack(side="right")

        self.log_frame = ttk.Frame(parent)
        self.log_frame.pack(fill="both", expand=True, pady=(2, 0))

        self.log = tk.Text(self.log_frame, height=8, wrap="word",
                            font=("Consolas", 9))
        sb2 = ttk.Scrollbar(self.log_frame, command=self.log.yview)
        self.log.configure(yscrollcommand=sb2.set)
        self.log.pack(side="left", fill="both", expand=True)
        sb2.pack(side="right", fill="y")

        self._log_line("Ready. Select a COM port and click Connect.")

    # -------------------------------------------------------------------------
    # Status dot
    # -------------------------------------------------------------------------
    def _draw_status_dot(self):
        self.status_dot.delete("all")
        c = self.status_color.get()
        self.status_dot.create_oval(2, 2, 12, 12, fill=c, outline=c)

    def _set_status(self, connected: bool):
        if connected:
            self.status_text.set("Connected")
            self.status_color.set("#2fbf4a")
        else:
            self.status_text.set("Disconnected")
            self.status_color.set("#cc3333")
        self._draw_status_dot()

    def _set_status_connecting(self):
        self.status_text.set("Connecting...")
        self.status_color.set("#e8a020")
        self._draw_status_dot()

    # -------------------------------------------------------------------------
    # Port management
    # -------------------------------------------------------------------------
    def _refresh_ports(self):
        ports = [f"{p.device} - {p.description}"
                 for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        current = self.port_combo.get()
        if current not in ports:
            self.port_combo.set(ports[0] if ports else "")

    def _select_port_by_device(self, device: str) -> bool:
        for entry in self.port_combo["values"]:
            if entry.split("-", 1)[0].strip().upper() == device.upper():
                self.port_combo.set(entry)
                return True
        return False

    # -------------------------------------------------------------------------
    # Connection
    # -------------------------------------------------------------------------
    def _connect(self, auto=False):
        if self.ser and self.ser.is_open:
            return

        selected = self.port_combo.get().strip()
        if not selected:
            if not auto:
                messagebox.showerror("No COM port", "Select a COM port first.")
            return

        port = selected.split("-", 1)[0].strip()
        baud = int(self.baud_combo.get())

        self.connect_btn.config(state="disabled")
        self.disconnect_btn.config(state="disabled")
        self._set_status_connecting()
        self._log_line(f"Opening {port} @ {baud}...")

        result_q = queue.Queue()

        def _try_open():
            try:
                s = serial.Serial(port, baudrate=baud, timeout=0.1)
                result_q.put(("ok", s))
            except Exception as e:
                result_q.put(("err", str(e)))

        threading.Thread(target=_try_open, daemon=True).start()
        deadline = time.monotonic() + 3.0
        self._poll_connect_result(result_q, port, baud, auto, deadline)

    def _poll_connect_result(self, result_q, port, baud, auto, deadline):
        try:
            status, payload = result_q.get_nowait()
        except queue.Empty:
            if time.monotonic() > deadline:
                self._log_line(f"Timed out connecting to {port}. Select port manually.")
                self.connect_btn.config(state="normal")
                self.disconnect_btn.config(state="disabled")
                self._set_status(False)
                return
            self.after(100, lambda: self._poll_connect_result(
                result_q, port, baud, auto, deadline))
            return

        if status == "err":
            self._log_line(f"Connect failed: {payload}")
            if not auto:
                messagebox.showerror("Connect failed", payload)
            self.connect_btn.config(state="normal")
            self.disconnect_btn.config(state="disabled")
            self._set_status(False)
            return

        self.ser = payload
        self.settings.update({"last_port": port, "baud": baud, "auto_connect": True})
        save_settings(self.settings)

        self.stop_event.clear()
        self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader_thread.start()

        self.connect_btn.config(state="disabled")
        self.disconnect_btn.config(state="normal")
        self._set_status(True)
        self._log_line(f"Connected to {port} @ {baud}.")
        self._alert(f"Connected to {port}")

        self.last_seen_time = time.time()
        self._start_state_polling()
        self._start_watchdog_tick()

    def _disconnect(self):
        self._cancel_polls()
        self.stop_event.set()

        try:
            if self.ser and self.ser.is_open:
                self.ser.close()
        except Exception:
            pass

        self.ser = None
        self.connect_btn.config(state="normal")
        self.disconnect_btn.config(state="disabled")
        self._set_status(False)
        self.last_seen_var.set("")
        self.last_seen_time = None

        # Reset display
        self.temp_var.set("-- °F")
        self.humid_var.set("--%")
        self.light_var.set("--%")
        self.fan_status_var.set("OFF")
        self.led_strip_var.set("OFF")

        self._log_line("Disconnected.")
        self._alert("Disconnected")

    def _cancel_polls(self):
        if self._state_poll_id:
            self.after_cancel(self._state_poll_id)
            self._state_poll_id = None
        if self._wdog_tick_id:
            self.after_cancel(self._wdog_tick_id)
            self._wdog_tick_id = None

    # -------------------------------------------------------------------------
    # State polling (replaces old temp-only poll)
    # -------------------------------------------------------------------------
    def _start_state_polling(self):
        self._poll_state_once()

    def _poll_state_once(self):
        self._send_cmd("STATE")
        if self._state_poll_id:
            self.after_cancel(self._state_poll_id)
        self._state_poll_id = self.after(STATE_POLL_MS, self._poll_state_once)

    # -------------------------------------------------------------------------
    # Watchdog tick — updates "last seen" label every second
    # -------------------------------------------------------------------------
    def _start_watchdog_tick(self):
        self._watchdog_tick()

    def _watchdog_tick(self):
        if self.last_seen_time is not None:
            elapsed = time.time() - self.last_seen_time
            if elapsed < WATCHDOG_WARN_S:
                self.last_seen_var.set(f"Last seen: {int(elapsed)}s ago")
                self.last_seen_label.config(foreground="#888888")
            elif elapsed < WATCHDOG_CRIT_S:
                self.last_seen_var.set(f"⚠ No response {int(elapsed)}s")
                self.last_seen_label.config(foreground="#e8a020")
            else:
                mins = int(elapsed // 60)
                self.last_seen_var.set(f"✗ Not responding ({mins}m)")
                self.last_seen_label.config(foreground="#cc3333")

        self._wdog_tick_id = self.after(WATCHDOG_CHECK_MS, self._watchdog_tick)

    # -------------------------------------------------------------------------
    # Serial reader (background thread)
    # -------------------------------------------------------------------------
    def _reader_loop(self):
        buf = b""
        while not self.stop_event.is_set():
            try:
                chunk = self.ser.read(64) if self.ser else b""
                if chunk:
                    buf += chunk
                    while b"\n" in buf or b"\r" in buf:
                        i_n = buf.find(b"\n")
                        i_r = buf.find(b"\r")
                        idxs = [i for i in (i_n, i_r) if i != -1]
                        cut  = min(idxs)
                        text = buf[:cut].decode(errors="replace").strip()
                        buf  = buf[cut + 1:]
                        if text:
                            self.rx_queue.put(text)
                else:
                    time.sleep(0.01)
            except Exception as e:
                self.rx_queue.put(f"[Serial error] {e}")
                break

    # -------------------------------------------------------------------------
    # Queue drain (Tkinter main thread, every 50 ms)
    # -------------------------------------------------------------------------
    def _drain_rx_queue(self):
        while True:
            try:
                msg = self.rx_queue.get_nowait()
            except queue.Empty:
                break

            self._log_line(f"<< {msg}")
            self._parse_message(msg)

        self.after(50, self._drain_rx_queue)

    # -------------------------------------------------------------------------
    # Message parser — dispatches to UI updaters
    # -------------------------------------------------------------------------
    def _parse_message(self, msg: str):
        # Any valid message from the device resets the watchdog clock
        if msg.startswith("OK") or msg.startswith("EVT") or msg.startswith("STARTUP"):
            self.last_seen_time = time.time()

        fields = _parse_kv(msg)

        # --- RGB ---
        if "RGB" in fields:
            bits = fields["RGB"]
            if len(bits) == 3 and all(c in "01" for c in bits):
                self.r.set(bits[0] == "1")
                self.g.set(bits[1] == "1")
                self.b.set(bits[2] == "1")
                self.preset.set("Custom")

        # --- Temperature ---
        if "TEMP" in fields:
            raw = fields["TEMP"].rstrip("F")
            try:
                val = float(raw)
                self.temp_var.set(f"{val:.1f} °F")
                self.temp_history.append(val)
                self._update_sparkline()
            except ValueError:
                pass

        # --- Humidity ---
        if "HUMID" in fields:
            raw = fields["HUMID"].rstrip("%")
            try:
                pct = int(raw)
                self.humid_var.set(f"{pct}%")
            except ValueError:
                self.humid_var.set("ERR")

        # --- Light ---
        if "LIGHT" in fields:
            try:
                pct = int(fields["LIGHT"])
                self.light_var.set(f"Light: {pct}%")
            except ValueError:
                pass

        # --- Fan ---
        if "FAN" in fields:
            on = fields["FAN"] == "1"
            self.fan_status_var.set("ON" if on else "OFF")
            self.fan_status_lbl.config(
                foreground="#2fbf4a" if on else "#888888")

        if "MODE" in fields:
            mode = fields["MODE"]
            self.fan_mode_var.set(mode)
            self.fan_mode_btn.config(text=f"Mode: {mode}")
            manual = (mode == "MANUAL")
            self.fan_on_btn.config( state="normal" if manual else "disabled")
            self.fan_off_btn.config(state="normal" if manual else "disabled")

        if "THRESH" in fields:
            try:
                self.thresh_var.set(int(fields["THRESH"]))
            except ValueError:
                pass

        # --- Motion ---
        if "MOTION" in fields:
            armed = fields["MOTION"] == "ARMED"
            self.motion_var.set("ARMED" if armed else "DISARMED")
            self.motion_btn.config(text="ARMED" if armed else "DISARMED")

        # --- LDR ---
        if "LDR" in fields:
            mode = fields["LDR"]
            self.ldr_mode_var.set(mode)
            self.ldr_mode_btn.config(text=f"LDR: {mode}")
            manual = (mode == "MANUAL")
            self.ldr_slider.config(state="normal" if manual else "disabled")

        # --- LED strip ---
        if "LED" in fields:
            on = fields["LED"] == "1"
            self.led_strip_var.set("ON" if on else "OFF")
            self.led_status_lbl.config(foreground="#2fbf4a" if on else "#888888")

        # --- Brightness ---
        if "BRIGHT" in fields:
            try:
                self.ldr_bright_var.set(int(fields["BRIGHT"]))
            except ValueError:
                pass

        # --- Unsolicited events (EVT) ---
        if msg.startswith("EVT"):
            self._handle_event(msg)

    def _handle_event(self, msg: str):
        if "MOTION" in msg:
            self._alert("Motion detected")
        elif "FAN_ON" in msg:
            fields = _parse_kv(msg)
            temp   = fields.get("TEMP", "?")
            self._alert(f"Fan turned ON  (temp: {temp})")
        elif "FAN_OFF" in msg:
            self._alert("Fan turned OFF")
        elif "LED_OFF" in msg:
            self.led_strip_var.set("OFF")
            self.led_status_lbl.config(foreground="#888888")
            self._alert("Strip auto-off (motion timeout)")
        elif "WDT_RESET" in msg:
            self._alert("⚠ Device restarted (watchdog reset)")
        else:
            self._alert(msg)

    # -------------------------------------------------------------------------
    # Sparkline
    # -------------------------------------------------------------------------
    def _update_sparkline(self):
        c = self.sparkline
        c.update_idletasks()
        w = c.winfo_width()
        h = c.winfo_height()
        c.delete("all")

        vals = list(self.temp_history)
        if len(vals) < 2 or w < 10:
            return

        mn, mx = min(vals), max(vals)
        if mx == mn:
            mn -= 1.0
            mx += 1.0

        pad = 6
        pts = []
        for i, v in enumerate(vals):
            x = pad + (w - 2 * pad) * i / (len(vals) - 1)
            y = (h - pad) - (h - 2 * pad) * (v - mn) / (mx - mn)
            pts.append((x, y))

        # Line segments
        for i in range(len(pts) - 1):
            c.create_line(pts[i][0], pts[i][1],
                          pts[i+1][0], pts[i+1][1],
                          fill="#2fbf4a", width=2)
        # Dots
        for x, y in pts:
            c.create_oval(x-2, y-2, x+2, y+2, fill="#2fbf4a", outline="")

        # Min / max labels
        c.create_text(pad, pad,     anchor="nw", text=f"{mx:.1f}°",
                      font=("TkDefaultFont", 7), fill="#aaaaaa")
        c.create_text(pad, h - pad, anchor="sw", text=f"{mn:.1f}°",
                      font=("TkDefaultFont", 7), fill="#aaaaaa")

    # -------------------------------------------------------------------------
    # Command sending
    # -------------------------------------------------------------------------
    def _send_cmd(self, cmd: str):
        if not (self.ser and self.ser.is_open):
            messagebox.showwarning("Not connected", "Connect to the device first.")
            return
        try:
            self.ser.write((cmd.strip() + "\n").encode("ascii", errors="ignore"))
            self._log_line(f">> {cmd.strip()}")
        except Exception as e:
            self._log_line(f"[Send error] {e}")

    def _send_manual(self):
        cmd = self.manual_entry.get().strip()
        if cmd:
            self._send_cmd(cmd)
            self.manual_entry.delete(0, tk.END)

    # -------------------------------------------------------------------------
    # Fan controls
    # -------------------------------------------------------------------------
    def _fan_mode_toggle(self):
        if self.fan_mode_var.get() == "AUTO":
            self._send_cmd("FAN1")   # switch to manual-on as a sensible default
        else:
            self._send_cmd("FAN_AUTO")

    def _fan_manual_on(self):
        self._send_cmd("FAN1")

    def _fan_manual_off(self):
        self._send_cmd("FAN0")

    def _on_thresh_release(self, _event):
        v = int(self.thresh_var.get())
        self.thresh_var.set(v)
        self._send_cmd(f"FANTHRESH:{v}")

    # -------------------------------------------------------------------------
    # Motion controls
    # -------------------------------------------------------------------------
    def _motion_toggle(self):
        if self.motion_var.get() == "ARMED":
            self._send_cmd("MOTION_DISARM")
        else:
            self._send_cmd("MOTION_ARM")

    # -------------------------------------------------------------------------
    # LDR controls
    # -------------------------------------------------------------------------
    def _ldr_mode_toggle(self):
        if self.ldr_mode_var.get() == "AUTO":
            v = int(self.ldr_bright_var.get())
            self._send_cmd(f"LDR_MAN:{v}")
        else:
            self._send_cmd("LDR_AUTO")

    def _ldr_resume_auto(self):
        self._send_cmd("LDR_AUTO")

    def _on_ldr_release(self, _event):
        v = int(self.ldr_bright_var.get())
        self.ldr_bright_var.set(v)
        self._send_cmd(f"LDR_MAN:{v}")

    def _strip_on(self):
        self._send_cmd("LED_ON")

    def _strip_off(self):
        self._send_cmd("LED_OFF")

    # -------------------------------------------------------------------------
    # RGB controls
    # -------------------------------------------------------------------------
    def _apply_rgb_from_toggles(self):
        self.preset.set("Custom")
        self._send_cmd(f"RGB:{int(self.r.get())}{int(self.g.get())}{int(self.b.get())}")

    def _apply_preset(self):
        p = self.preset.get()
        if p == "Custom":
            return
        mapping = {"Off":(0,0,0),"Red":(1,0,0),"Green":(0,1,0),"Blue":(0,0,1),
                   "White":(1,1,1),"Cyan":(0,1,1),"Magenta":(1,0,1),"Yellow":(1,1,0)}
        r, g, b = mapping.get(p, (0,0,0))
        self.r.set(bool(r)); self.g.set(bool(g)); self.b.set(bool(b))
        self._send_cmd(f"RGB:{r}{g}{b}")

    def _all_off(self):
        self.r.set(False); self.g.set(False); self.b.set(False)
        self.preset.set("Off")
        self._send_cmd("X")

    # -------------------------------------------------------------------------
    # Logging
    # -------------------------------------------------------------------------
    def _log_line(self, s: str):
        self.log.insert(tk.END, s + "\n")
        self.log.see(tk.END)
        # Keep log from growing unbounded
        lines = int(self.log.index("end-1c").split(".")[0])
        if lines > 500:
            self.log.delete("1.0", "100.0")

    def _alert(self, s: str):
        self.alert_log.config(state="normal")
        self.alert_log.insert(tk.END, f"{_ts()}  {s}\n")
        self.alert_log.see(tk.END)
        self.alert_log.config(state="disabled")

    def _toggle_serial_log(self):
        self._log_collapsed = not self._log_collapsed
        if self._log_collapsed:
            self.log_frame.pack_forget()
            self.log_toggle_btn.config(text="▶ Show")
        else:
            self.log_frame.pack(fill="both", expand=True, pady=(2, 0))
            self.log_toggle_btn.config(text="▼ Hide")

    # -------------------------------------------------------------------------
    # Exit
    # -------------------------------------------------------------------------
    def _exit_app(self):
        try:
            if self.ser and self.ser.is_open:
                self._send_cmd("EXIT")
                time.sleep(0.1)
        finally:
            self._cancel_polls()
            self._disconnect()
            self.destroy()

    def on_close(self):
        self._cancel_polls()
        self._disconnect()
        self.destroy()


# =============================================================================
if __name__ == "__main__":
    app = TivaBtGui()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()