import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
from collections import deque

import pyautogui
from pynput import keyboard


pyautogui.FAILSAFE = True

START_POS = (0, 1)
GRID_SIZE = 9
WATER_BLOCK = (5, 5)


class BotApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Minecraft Bedrock Auto Bot")
        self.stop_event = threading.Event()
        self.listener = None
        self.worker = None
        self.countdown_job = None
        self.status_var = tk.StringVar(value="Bereit")

        self.countdown_var = tk.StringVar(value="3")
        self.break_time_var = tk.StringVar(value="0.6")
        self.step_time_var = tk.StringVar(value="0.25")
        self.replant_pause_var = tk.StringVar(value="0.1")

        self._build_ui()

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

        self.stop_button = ttk.Button(btn_frame, text="Stopp (Taste M)", command=self.handle_stop, state=tk.DISABLED)
        self.stop_button.grid(row=0, column=1)

        status_frame = ttk.Frame(frame)
        status_frame.grid(row=5, column=0, columnspan=2, pady=(12, 0), sticky="ew")
        ttk.Label(status_frame, text="Status:").grid(row=0, column=0, sticky="w")
        ttk.Label(status_frame, textvariable=self.status_var).grid(row=0, column=1, sticky="w", padx=(4, 0))

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
            position = START_POS
            for target in order:
                if self.stop_event.is_set():
                    break
                path = self._path_between(position, target)
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
                self.status_var.set("Fertig - zurück an Ausgangsposition")
        finally:
            stopped = self.stop_event.is_set()
            self.root.after(0, lambda: self._cleanup_after_run(stopped))

    def _harvest_and_plant(self, break_time: float, replant_pause: float) -> None:
        pyautogui.mouseDown(button="left")
        time.sleep(break_time)
        pyautogui.mouseUp(button="left")
        pyautogui.click(button="right")
        if replant_pause:
            time.sleep(replant_pause)

    def _step_towards(self, current: tuple[int, int], nxt: tuple[int, int], step_time: float) -> None:
        dx = nxt[0] - current[0]
        dy = nxt[1] - current[1]
        # Orientierung: Blick nach Osten. w = vorwärts (x+), s = rückwärts (x-),
        # a = seitlich links/norden (y+), d = seitlich rechts/süden (y-).
        key = {(1, 0): "w", (-1, 0): "s", (0, 1): "a", (0, -1): "d"}.get((dx, dy))
        if key:
            pyautogui.keyDown(key)
            time.sleep(step_time)
            pyautogui.keyUp(key)

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

    def _neighbors(self, node: tuple[int, int]):
        x, y = node
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if (nx, ny) == START_POS or (1 <= nx <= GRID_SIZE and 1 <= ny <= GRID_SIZE):
                if (nx, ny) != WATER_BLOCK:
                    yield nx, ny

    def _look_down(self) -> None:
        width, height = pyautogui.size()
        pyautogui.moveTo(width / 2, height / 2)
        pyautogui.moveRel(0, height * 0.25, duration=0.2)

    def _start_hotkey_listener(self) -> None:
        self.stop_button.config(state=tk.NORMAL)

        def on_press(key):
            if isinstance(key, keyboard.KeyCode) and key.char and key.char.lower() == "m":
                self.handle_stop()
                return False
            return True

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
        if stopped and not self.status_var.get().startswith("Fertig"):
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
