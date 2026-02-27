import json
import os
import threading
import queue
import time
import tkinter as tk
from tkinter import ttk, messagebox

import serial
import serial.tools.list_ports


SETTINGS_FILE = "tiva_bt_gui_settings.json"


def load_settings():
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


class TivaBtGui(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("TM4C123 + HC-05 Controller")
        self.geometry("820x520")

        self.ser = None
        self.reader_thread = None
        self.stop_event = threading.Event()
        self.rx_queue = queue.Queue()

        self.settings = load_settings()

        # Local state mirrors device state
        self.r = tk.BooleanVar(value=False)
        self.g = tk.BooleanVar(value=False)
        self.b = tk.BooleanVar(value=False)

        # Presets UI
        self.preset = tk.StringVar(value="Custom")

        # Connection status UI
        self.status_text = tk.StringVar(value="Disconnected")
        self.status_color = tk.StringVar(value="#cc3333")  # red

        self._build_ui()
        self._refresh_ports()

        # Auto-select last port if present
        last_port = self.settings.get("last_port")
        if last_port:
            self._select_port_by_device(last_port)

        # Auto-connect if desired
        if self.settings.get("auto_connect", True):
            # Give UI time to render first
            self.after(250, self._auto_connect_if_possible)

        self.after(50, self._drain_rx_queue)

    def _build_ui(self):
        outer = ttk.Frame(self, padding=10)
        outer.pack(fill="both", expand=True)

        # --- Top: Connection row ---
        top = ttk.Frame(outer)
        top.pack(fill="x")

        ttk.Label(top, text="COM Port:").pack(side="left")
        self.port_combo = ttk.Combobox(top, width=26, state="readonly")
        self.port_combo.pack(side="left", padx=(6, 12))

        ttk.Label(top, text="Baud:").pack(side="left")
        self.baud_combo = ttk.Combobox(top, width=10, state="readonly", values=["9600", "38400", "115200"])
        self.baud_combo.set(str(self.settings.get("baud", 9600)))
        self.baud_combo.pack(side="left", padx=(6, 12))

        ttk.Button(top, text="Refresh Ports", command=self._refresh_ports).pack(side="left", padx=(0, 12))

        self.connect_btn = ttk.Button(top, text="Connect", command=self._connect)
        self.connect_btn.pack(side="left")

        self.disconnect_btn = ttk.Button(top, text="Disconnect", command=self._disconnect, state="disabled")
        self.disconnect_btn.pack(side="left", padx=(8, 0))

        # Status indicator (dot + text)
        status_box = ttk.Frame(top)
        status_box.pack(side="right")

        self.status_dot = tk.Canvas(status_box, width=14, height=14, highlightthickness=0)
        self.status_dot.pack(side="left", padx=(0, 6))
        self._draw_status_dot()

        ttk.Label(status_box, textvariable=self.status_text).pack(side="left")

        # --- Middle: Controls ---
        mid = ttk.Frame(outer)
        mid.pack(fill="x", pady=(12, 8))

        # RGB Controls
        led_box = ttk.LabelFrame(mid, text="RGB Controls", padding=10)
        led_box.pack(side="left", fill="x", expand=True)

        # Toggle row
        self.r_btn = ttk.Checkbutton(led_box, text="Red", variable=self.r, command=self._apply_rgb_from_toggles)
        self.g_btn = ttk.Checkbutton(led_box, text="Green", variable=self.g, command=self._apply_rgb_from_toggles)
        self.b_btn = ttk.Checkbutton(led_box, text="Blue", variable=self.b, command=self._apply_rgb_from_toggles)
        self.r_btn.grid(row=0, column=0, sticky="w", padx=6, pady=4)
        self.g_btn.grid(row=0, column=1, sticky="w", padx=6, pady=4)
        self.b_btn.grid(row=0, column=2, sticky="w", padx=6, pady=4)

        ttk.Button(led_box, text="All Off", command=self._all_off).grid(row=1, column=0, padx=6, pady=8, sticky="w")

        # Presets
        presets_box = ttk.LabelFrame(led_box, text="Presets", padding=8)
        presets_box.grid(row=2, column=0, columnspan=3, sticky="ew", padx=6, pady=(6, 0))

        # Radio presets (nicer than lots of buttons)
        presets = [
            ("Off", "Off"),
            ("Red", "Red"),
            ("Green", "Green"),
            ("Blue", "Blue"),
            ("White", "White"),
            ("Cyan", "Cyan"),
            ("Magenta", "Magenta"),
            ("Yellow", "Yellow"),
            ("Custom", "Custom"),
        ]

        # Lay them out in 3 columns
        for idx, (label, value) in enumerate(presets):
            r = idx // 3
            c = idx % 3
            ttk.Radiobutton(
                presets_box,
                text=label,
                value=value,
                variable=self.preset,
                command=self._apply_preset
            ).grid(row=r, column=c, padx=6, pady=3, sticky="w")

        # Utilities
        util_box = ttk.LabelFrame(mid, text="Utilities", padding=10)
        util_box.pack(side="left", fill="x", expand=True, padx=(12, 0))

        ttk.Button(util_box, text="HELP", command=lambda: self._send_cmd("HELP")).grid(row=0, column=0, padx=6, pady=4, sticky="ew")
        ttk.Button(util_box, text="STATE", command=self._request_state).grid(row=0, column=1, padx=6, pady=4, sticky="ew")

        ttk.Label(util_box, text="Manual command:").grid(row=1, column=0, padx=6, pady=(12, 4), sticky="w")
        self.manual_entry = ttk.Entry(util_box, width=26)
        self.manual_entry.grid(row=1, column=1, padx=6, pady=(12, 4), sticky="ew")
        self.manual_entry.bind("<Return>", lambda e: self._send_manual())

        ttk.Button(util_box, text="Send", command=self._send_manual).grid(row=2, column=1, padx=6, pady=4, sticky="e")

        # Exit button (sends EXIT then closes)
        ttk.Separator(util_box).grid(row=3, column=0, columnspan=2, sticky="ew", padx=6, pady=(12, 8))
        ttk.Button(util_box, text="Exit App", command=self._exit_app).grid(row=4, column=0, columnspan=2, padx=6, pady=4, sticky="ew")

        util_box.columnconfigure(1, weight=1)

        # --- Bottom: Log ---
        bottom = ttk.Frame(outer)
        bottom.pack(fill="both", expand=True)

        ttk.Label(bottom, text="Log:").pack(anchor="w")

        self.log = tk.Text(bottom, height=14, wrap="word")
        self.log.pack(fill="both", expand=True)

        self._log_line("Ready. Select COM port and click Connect (or auto-connect will try).")

    def _draw_status_dot(self):
        self.status_dot.delete("all")
        color = self.status_color.get()
        self.status_dot.create_oval(2, 2, 12, 12, fill=color, outline=color)

    def _set_status(self, connected: bool):
        if connected:
            self.status_text.set("Connected")
            self.status_color.set("#2fbf4a")  # green
        else:
            self.status_text.set("Disconnected")
            self.status_color.set("#cc3333")  # red
        self._draw_status_dot()

    def _refresh_ports(self):
        ports = []
        for p in serial.tools.list_ports.comports():
            ports.append(f"{p.device} - {p.description}")

        self.port_combo["values"] = ports

        # Keep current selection if it still exists
        current = self.port_combo.get()
        if current in ports:
            return

        # Otherwise select first
        if ports:
            self.port_combo.set(ports[0])
        else:
            self.port_combo.set("")

    def _select_port_by_device(self, device_name: str):
        # device_name like "COM8"
        for entry in self.port_combo["values"]:
            dev = entry.split("-", 1)[0].strip()
            if dev.upper() == device_name.upper():
                self.port_combo.set(entry)
                return True
        return False

    def _auto_connect_if_possible(self):
        if self.ser and self.ser.is_open:
            return
        selected = self.port_combo.get().strip()
        if not selected:
            self._log_line("Auto-connect skipped: no COM ports found.")
            return
        self._log_line("Auto-connect attempt...")
        self._connect()

    def _connect(self):
        if self.ser and self.ser.is_open:
            return

        selected = self.port_combo.get().strip()
        if not selected:
            messagebox.showerror("No COM port", "Select a COM port first.")
            return

        port = selected.split("-", 1)[0].strip()
        baud = int(self.baud_combo.get())

        try:
            self.ser = serial.Serial(port, baudrate=baud, timeout=0.1)
        except Exception as e:
            messagebox.showerror("Connect failed", str(e))
            self._set_status(False)
            return

        # Save settings
        self.settings["last_port"] = port
        self.settings["baud"] = baud
        self.settings["auto_connect"] = True
        save_settings(self.settings)

        self.stop_event.clear()
        self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader_thread.start()

        self.connect_btn.config(state="disabled")
        self.disconnect_btn.config(state="normal")

        self._set_status(True)
        self._log_line(f"Connected to {port} @ {baud}.")
        self._send_cmd("STATE")

    def _disconnect(self):
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
        self._log_line("Disconnected.")

    def _reader_loop(self):
        buf = b""
        while not self.stop_event.is_set():
            try:
                chunk = self.ser.read(64) if self.ser else b""
                if chunk:
                    buf += chunk
                    while b"\n" in buf or b"\r" in buf:
                        idx_n = buf.find(b"\n")
                        idx_r = buf.find(b"\r")
                        idxs = [i for i in [idx_n, idx_r] if i != -1]
                        cut = min(idxs) if idxs else -1
                        line = buf[:cut]
                        buf = buf[cut + 1:]

                        text = line.decode(errors="replace").strip()
                        if text:
                            self.rx_queue.put(text)
                else:
                    time.sleep(0.01)
            except Exception as e:
                self.rx_queue.put(f"[Serial read error] {e}")
                break

    def _drain_rx_queue(self):
        while True:
            try:
                msg = self.rx_queue.get_nowait()
            except queue.Empty:
                break

            self._log_line(f"<< {msg}")
            self._maybe_update_state_from_msg(msg)

        self.after(50, self._drain_rx_queue)

    def _maybe_update_state_from_msg(self, msg: str):
        # Example: "OK RGB=101"
        if "RGB=" in msg:
            try:
                idx = msg.index("RGB=") + 4
                bits = msg[idx:idx + 3]
                if len(bits) == 3 and all(c in "01" for c in bits):
                    self.r.set(bits[0] == "1")
                    self.g.set(bits[1] == "1")
                    self.b.set(bits[2] == "1")
                    self.preset.set("Custom")
            except Exception:
                pass

    def _send_cmd(self, cmd: str):
        if not (self.ser and self.ser.is_open):
            messagebox.showwarning("Not connected", "Connect to the device first.")
            return
        try:
            payload = (cmd.strip() + "\n").encode("ascii", errors="ignore")
            self.ser.write(payload)
            self._log_line(f">> {cmd.strip()}")
        except Exception as e:
            self._log_line(f"[Send error] {e}")

    def _send_manual(self):
        cmd = self.manual_entry.get().strip()
        if not cmd:
            return
        self._send_cmd(cmd)
        self.manual_entry.delete(0, tk.END)

    def _apply_rgb_from_toggles(self):
        self.preset.set("Custom")
        cmd = f"RGB:{int(self.r.get())}{int(self.g.get())}{int(self.b.get())}"
        self._send_cmd(cmd)

    def _apply_preset(self):
        p = self.preset.get()
        if p == "Custom":
            return

        mapping = {
            "Off": (0, 0, 0),
            "Red": (1, 0, 0),
            "Green": (0, 1, 0),
            "Blue": (0, 0, 1),
            "White": (1, 1, 1),
            "Cyan": (0, 1, 1),
            "Magenta": (1, 0, 1),
            "Yellow": (1, 1, 0),
        }

        r, g, b = mapping.get(p, (0, 0, 0))
        self.r.set(bool(r))
        self.g.set(bool(g))
        self.b.set(bool(b))
        self._send_cmd(f"RGB:{r}{g}{b}")

    def _all_off(self):
        self.r.set(False)
        self.g.set(False)
        self.b.set(False)
        self.preset.set("Off")
        self._send_cmd("X")

    def _request_state(self):
        self._send_cmd("STATE")

    def _exit_app(self):
        # Sends EXIT for your firmware (even though it won't close PuTTY,
        # it can still print a friendly message / stop processing on device if you add that later)
        try:
            if self.ser and self.ser.is_open:
                self._send_cmd("EXIT")
                time.sleep(0.1)
        finally:
            self._disconnect()
            self.destroy()

    def _log_line(self, s: str):
        self.log.insert(tk.END, s + "\n")
        self.log.see(tk.END)

    def on_close(self):
        self._disconnect()
        self.destroy()


if __name__ == "__main__":
    app = TivaBtGui()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()
    
    
    