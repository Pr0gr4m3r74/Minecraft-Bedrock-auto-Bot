import sys
if sys.version_info < (3, 10):
    raise SystemExit("Python 3.10 oder höher erforderlich. Bitte mit aktueller Version starten.")

import re
import subprocess
import threading
import time
import tkinter as tk
from collections import deque
from pathlib import Path
from typing import Iterator
from tkinter import messagebox, ttk

import pyautogui
from pynput import keyboard


pyautogui.FAILSAFE = True

# Spieler steht links außerhalb des Feldes auf (0,1) und blickt auf (1,1).
# Player starts just left of the field at (0,1) facing into (1,1).
START_POS = (0, 1)
GRID_SIZE = 9
WATER_BLOCK = (5, 5)
# Koordinaten: x nimmt nach Osten zu, y nach Norden (seitliches Laufen nach links). / Coordinates:
# x increases east, y increases north (strafe left).


class BotApp:
    DIRECTION_TO_KEY = {(1, 0): "w", (-1, 0): "s", (0, 1): "a", (0, -1): "d"}
    LOOK_DOWN_RATIO = 0.25
    STATUS_DONE_PREFIX = "Fertig"

    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Minecraft Bedrock Auto Bot")
        self.stop_event = threading.Event()
        self.listener = None
        self.worker = None
        self.countdown_job = None
        self.status_var = tk.StringVar(value="Bereit")
        self._ignore_stop_keys = threading.Event()
        self._ignore_stop_lock = threading.Lock()

        self.countdown_var = tk.StringVar(value="3")
        self.break_time_var = tk.StringVar(value="0.6")
        self.step_time_var = tk.StringVar(value="0.25")
        self.replant_pause_var = tk.StringVar(value="0.1")

        self._build_ui()
        self.root.after(200, self._show_welcome_instructions)

    def _build_ui(self) -> None:
        frame = ttk.Frame(self.root, padding=12)
        frame.grid(row=0, column=0, sticky="nsew")
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)

        ttk.Label(frame, text="Countdown (Sekunden)").grid(row=0, column=0, sticky="w")
        self.countdown_entry = ttk.Entry(frame, textvariable=self.countdown_var, width=10)
        self.countdown_entry.grid(row=0, column=1, sticky="ew", padx=(6, 0))

        ttk.Label(frame, text="Abbau-Haltedauer (Sekunden)").grid(row=1, column=0, sticky="w")
        self.break_time_entry = ttk.Entry(frame, textvariable=self.break_time_var, width=10)
        self.break_time_entry.grid(row=1, column=1, sticky="ew", padx=(6, 0))

        ttk.Label(frame, text="Schritt-Dauer pro Block (Sekunden)").grid(row=2, column=0, sticky="w")
        self.step_time_entry = ttk.Entry(frame, textvariable=self.step_time_var, width=10)
        self.step_time_entry.grid(row=2, column=1, sticky="ew", padx=(6, 0))

        ttk.Label(frame, text="Pause nach Pflanzen (Sekunden)").grid(row=3, column=0, sticky="w")
        self.replant_pause_entry = ttk.Entry(frame, textvariable=self.replant_pause_var, width=10)
        self.replant_pause_entry.grid(row=3, column=1, sticky="ew", padx=(6, 0))

        btn_frame = ttk.Frame(frame)
        btn_frame.grid(row=4, column=0, columnspan=2, pady=(10, 0))

        self.start_button = ttk.Button(btn_frame, text="Start", command=self.handle_start)
        self.start_button.grid(row=0, column=0, padx=(0, 8))

        self.stop_button = ttk.Button(
            btn_frame, text="Stopp (beliebige Taste)", command=self.handle_stop, state=tk.DISABLED
        )
        self.stop_button.grid(row=0, column=1)

        status_frame = ttk.Frame(frame)
        status_frame.grid(row=5, column=0, columnspan=2, pady=(12, 0), sticky="ew")
        ttk.Label(status_frame, text="Status:").grid(row=0, column=0, sticky="w")
        ttk.Label(status_frame, textvariable=self.status_var).grid(row=0, column=1, sticky="w", padx=(4, 0))

        instructions = (
            "Windows 11 Komplett-Kit:\n"
            "1) „Installieren“ lädt alle benötigten Pakete.\n"
            "2) Minecraft ins Vordergrundfenster bringen und „Start“ drücken.\n"
            "3) Jede beliebige Taste (oder der Stopp-Button) beendet den Algorithmus sofort.\n"
            "4) Zum Entfernen „Deinstallieren“ drücken und danach diesen Ordner löschen."
        )
        ttk.Label(frame, text="Anleitung", font=("", 10, "bold")).grid(
            row=6, column=0, columnspan=2, sticky="w", pady=(12, 2)
        )
        ttk.Label(frame, text=instructions, wraplength=360, justify="left").grid(
            row=7, column=0, columnspan=2, sticky="w"
        )

        install_frame = ttk.Frame(frame)
        install_frame.grid(row=8, column=0, columnspan=2, pady=(10, 0))
        ttk.Button(install_frame, text="Installieren", command=self.handle_install).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(install_frame, text="Deinstallieren", command=self.handle_uninstall).grid(row=0, column=1)

    def handle_start(self) -> None:
        if self.worker and self.worker.is_alive():
            return
        try:
            countdown = max(0, float(self.countdown_var.get()))
            break_time = max(0.1, float(self.break_time_var.get()))
            step_time = max(0.05, float(self.step_time_var.get()))
            replant_pause = max(0.0, float(self.replant_pause_var.get()))
        except ValueError:
            messagebox.showerror("Fehler", "Bitte gültige Zahlen eingeben.")
            return

        if not messagebox.askokcancel(
            "Warnung",
            "Der Bot übernimmt Tastatur und Maus. Stelle sicher, dass Minecraft im Vordergrund ist.",
        ):
            return

        self._toggle_controls(disabled=True)
        self.stop_event.clear()
        self.status_var.set(f"Countdown {countdown:.1f}s ...")
        self._start_hotkey_listener()
        self._start_countdown(countdown, break_time, step_time, replant_pause)

    def handle_stop(self) -> None:
        self.status_var.set("Stop angefordert")
        self.stop_event.set()
        if self.countdown_job:
            self.root.after_cancel(self.countdown_job)
            self.countdown_job = None

    def _show_welcome_instructions(self) -> None:
        message = (
            "Windows 11 Komplett-Kit\n\n"
            "1) Klicke auf „Installieren“, damit das Programm alle Abhängigkeiten selbst lädt.\n"
            "2) Starte Minecraft und halte das Fenster im Vordergrund.\n"
            "3) Drücke „Start“ – der Bot läuft, bis du irgendeine Taste drückst oder „Stopp“ wählst.\n"
            "4) Für eine saubere Deinstallation auf „Deinstallieren“ klicken und anschließend diesen Ordner löschen."
        )
        messagebox.showinfo("Anleitung", message)

    def _requirements_path(self) -> Path:
        return Path(__file__).with_name("requirements.txt")

    def _requirements_valid(self, path: Path) -> bool:
        allowed_names = {"pyautogui", "pynput"}
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except OSError:
            return False
        for line in lines:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            if stripped.startswith("-"):
                return False
            normalized = stripped.replace(" ", "")
            if "://" in normalized or "@(" in normalized or "@git" in normalized:
                return False
            match = re.match(r"([A-Za-z0-9_.-]+)", normalized)
            if not match:
                return False
            name_part = match.group(1).lower()
            if name_part not in allowed_names:
                return False
        return True

    def handle_install(self) -> None:
        self.status_var.set("Installiere Abhängigkeiten ...")
        threading.Thread(target=self._run_install, daemon=True).start()

    def handle_uninstall(self) -> None:
        if not messagebox.askyesno(
            "Deinstallieren",
            "Dadurch werden die benötigten Pakete entfernt. Danach kannst du diesen Ordner einfach löschen.\nFortfahren?",
        ):
            return
        self.status_var.set("Deinstallation läuft ...")
        threading.Thread(target=self._run_uninstall, daemon=True).start()

    def _run_install(self) -> None:
        requirements_path = self._requirements_path()
        if not requirements_path.is_file():
            self.root.after(
                0,
                lambda: messagebox.showerror(
                    "Installation fehlgeschlagen", f"requirements.txt nicht gefunden unter {requirements_path}"
                ),
            )
            self.root.after(0, lambda: self.status_var.set("Installation fehlgeschlagen"))
            return
        if not self._requirements_valid(requirements_path):
            self.root.after(
                0,
                lambda: messagebox.showerror(
                    "Installation fehlgeschlagen", "requirements.txt enthält nicht erlaubte Einträge."
                ),
            )
            self.root.after(0, lambda: self.status_var.set("Installation fehlgeschlagen"))
            return
        try:
            subprocess.run(
                [sys.executable, "-m", "pip", "install", "-r", str(requirements_path)],
                check=True,
                shell=False,
                timeout=300,
                capture_output=True,
            )
        except subprocess.TimeoutExpired:
            self.root.after(
                0,
                lambda: messagebox.showerror(
                    "Installation fehlgeschlagen", "Paketinstallation hat zu lange gedauert (Timeout)."
                ),
            )
            self.root.after(0, lambda: self.status_var.set("Installation fehlgeschlagen"))
        except subprocess.CalledProcessError as exc:
            self.root.after(
                0,
                lambda: messagebox.showerror(
                    "Installation fehlgeschlagen", f"Pakete konnten nicht installiert werden:\n{exc}"
                ),
            )
            self.root.after(0, lambda: self.status_var.set("Installation fehlgeschlagen"))
        else:
            self.root.after(0, lambda: self.status_var.set("Abhängigkeiten installiert"))
            self.root.after(
                0,
                lambda: messagebox.showinfo(
                    "Installation abgeschlossen",
                    "Alles bereit. Minecraft in den Vordergrund holen und auf Start drücken.",
                ),
            )

    def _run_uninstall(self) -> None:
        requirements_path = self._requirements_path()
        if not requirements_path.is_file():
            self.root.after(
                0,
                lambda: messagebox.showerror(
                    "Deinstallation fehlgeschlagen", f"requirements.txt nicht gefunden unter {requirements_path}"
                ),
            )
            self.root.after(0, lambda: self.status_var.set("Deinstallation fehlgeschlagen"))
            return
        if not self._requirements_valid(requirements_path):
            self.root.after(
                0,
                lambda: messagebox.showerror(
                    "Deinstallation fehlgeschlagen", "requirements.txt enthält nicht erlaubte Einträge."
                ),
            )
            self.root.after(0, lambda: self.status_var.set("Deinstallation fehlgeschlagen"))
            return
        try:
            subprocess.run(
                [sys.executable, "-m", "pip", "uninstall", "-y", "-r", str(requirements_path)],
                check=True,
                shell=False,
                timeout=300,
                capture_output=True,
            )
        except subprocess.TimeoutExpired:
            self.root.after(
                0,
                lambda: messagebox.showerror(
                    "Deinstallation fehlgeschlagen", "Paket-Deinstallation hat zu lange gedauert (Timeout)."
                ),
            )
            self.root.after(0, lambda: self.status_var.set("Deinstallation fehlgeschlagen"))
        except subprocess.CalledProcessError as exc:
            self.root.after(
                0,
                lambda: messagebox.showerror(
                    "Deinstallation fehlgeschlagen", f"Pakete konnten nicht entfernt werden:\n{exc}"
                ),
            )
            self.root.after(0, lambda: self.status_var.set("Deinstallation fehlgeschlagen"))
        else:
            self.root.after(0, lambda: self.status_var.set("Deinstallation abgeschlossen"))
            self.root.after(
                0,
                lambda: messagebox.showinfo(
                    "Deinstallation abgeschlossen", "Pakete entfernt. Zum Aufräumen den Ordner löschen."
                ),
            )

    def _start_countdown(self, seconds: float, break_time: float, step_time: float, replant_pause: float) -> None:
        end_time = time.time() + seconds

        def tick() -> None:
            remaining = end_time - time.time()
            if remaining <= 0:
                if self.stop_event.is_set():
                    self._cleanup_after_run(True)
                    return
                self.status_var.set("Starte...")
                self.countdown_job = None
                self._launch_worker(break_time, step_time, replant_pause)
                return
            self.status_var.set(f"Countdown {max(0, remaining):.1f}s ...")
            self.countdown_job = self.root.after(100, tick)

        tick()

    def _launch_worker(self, break_time: float, step_time: float, replant_pause: float) -> None:
        self.worker = threading.Thread(
            target=self._run_bot,
            args=(break_time, step_time, replant_pause),
            daemon=True,
        )
        self.worker.start()

    def _run_bot(self, break_time: float, step_time: float, replant_pause: float) -> None:
        try:
            self._look_down()
            order = self._traversal_order()
            paths = []
            position = START_POS
            for target in order:
                paths.append(self._path_between(position, target))
                position = target

            position = START_POS
            for path, target in zip(paths, order):
                if self.stop_event.is_set():
                    break
                for step in path[1:]:
                    if self.stop_event.is_set():
                        break
                    self._step_towards(position, step, step_time)
                    position = step
                if self.stop_event.is_set():
                    break
                if position != START_POS:
                    self._harvest_and_plant(break_time, replant_pause)
            if not self.stop_event.is_set():
                self.status_var.set(f"{self.STATUS_DONE_PREFIX} - zurück an Ausgangsposition")
        finally:
            stopped = self.stop_event.is_set()
            self.root.after(0, lambda: self._cleanup_after_run(stopped))

    def _harvest_and_plant(self, break_time: float, replant_pause: float) -> None:
        pyautogui.mouseDown(button="left")
        if self.stop_event.wait(break_time):
            pyautogui.mouseUp(button="left")
            return
        pyautogui.mouseUp(button="left")
        pyautogui.click(button="right")
        if self.stop_event.wait(replant_pause):
            return

    def _step_towards(self, current: tuple[int, int], nxt: tuple[int, int], step_time: float) -> None:
        dx = nxt[0] - current[0]
        dy = nxt[1] - current[1]
        # Orientierung/Bearing: Blick nach Osten, y zeigt nach Norden.
        # Facing east with y increasing to the left/north; mapping lives in DIRECTION_TO_KEY.
        key = self.DIRECTION_TO_KEY.get((dx, dy))
        if key:
            with self._ignore_stop_lock:
                self._ignore_stop_keys.set()
                pyautogui.keyDown(key)
            try:
                self.stop_event.wait(step_time)
            finally:
                with self._ignore_stop_lock:
                    pyautogui.keyUp(key)
                    self._ignore_stop_keys.clear()

    def _traversal_order(self) -> list[tuple[int, int]]:
        order: list[tuple[int, int]] = []
        for y in range(1, GRID_SIZE + 1):
            xs = range(1, GRID_SIZE + 1) if y % 2 == 1 else range(GRID_SIZE, 0, -1)
            for x in xs:
                if (x, y) == WATER_BLOCK:
                    continue
                order.append((x, y))
        order.append(START_POS)
        return order

    def _path_between(self, start: tuple[int, int], goal: tuple[int, int]) -> list[tuple[int, int]]:
        queue: deque[tuple[int, int]] = deque([start])
        came_from: dict[tuple[int, int], tuple[int, int] | None] = {start: None}

        while queue:
            node = queue.popleft()
            if node == goal:
                break
            for nx, ny in self._neighbors(node):
                nxt = (nx, ny)
                if nxt not in came_from:
                    came_from[nxt] = node
                    queue.append(nxt)

        if goal not in came_from:
            return [start]

        path: list[tuple[int, int]] = []
        cur: tuple[int, int] | None = goal
        while cur is not None:
            path.append(cur)
            cur = came_from[cur]
        return list(reversed(path))

    def _neighbors(self, node: tuple[int, int]) -> Iterator[tuple[int, int]]:
        x, y = node
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if (nx, ny) == START_POS or (1 <= nx <= GRID_SIZE and 1 <= ny <= GRID_SIZE):
                if (nx, ny) != WATER_BLOCK:
                    yield nx, ny

    def _look_down(self) -> None:
        width, height = pyautogui.size()
        pyautogui.moveTo(width / 2, height / 2)
        pyautogui.moveRel(0, height * self.LOOK_DOWN_RATIO, duration=0.2)

    def _start_hotkey_listener(self) -> None:
        self.stop_button.config(state=tk.NORMAL)
        if self.listener:
            self.listener.stop()
            self.listener = None

        def on_press(key):
            with self._ignore_stop_lock:
                if self._ignore_stop_keys.is_set():
                    return True
            self.root.after(0, self.handle_stop)
            return False

        self.listener = keyboard.Listener(on_press=on_press, suppress=False)
        self.listener.start()

    def _cleanup_after_run(self, stopped: bool) -> None:
        self.stop_event.set()
        if self.countdown_job:
            self.root.after_cancel(self.countdown_job)
            self.countdown_job = None
        if self.listener:
            self.listener.stop()
            self.listener = None
        self._toggle_controls(disabled=False)
        if stopped and not self.status_var.get().startswith(self.STATUS_DONE_PREFIX):
            self.status_var.set("Bereit")

    def _toggle_controls(self, disabled: bool) -> None:
        state = tk.DISABLED if disabled else tk.NORMAL
        for entry in (
            self.countdown_entry,
            self.break_time_entry,
            self.step_time_entry,
            self.replant_pause_entry,
        ):
            entry.config(state=state)
        self.start_button.config(state=state)
        self.stop_button.config(state=tk.NORMAL if disabled else tk.DISABLED)


def main() -> None:
    root = tk.Tk()
    app = BotApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
