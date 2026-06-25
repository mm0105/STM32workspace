"""树莓派 GPIO 直驱步进云台控制器（STEP/DIR）。"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from typing import Optional, Tuple

# Attempt to import RPi.GPIO; if unavailable (e.g., on Windows), use a no-op dummy.
try:
    import RPi.GPIO as _RPi_GPIO
except ImportError:  # pragma: no cover
    class _DummyGPIO:
        BOARD = BCM = None
        OUT = IN = None
        HIGH = LOW = 1
        def setwarnings(self, *args, **kwargs):
            pass
        def setmode(self, *args, **kwargs):
            pass
        def setup(self, *args, **kwargs):
            pass
        def output(self, *args, **kwargs):
            pass
        def cleanup(self, *args, **kwargs):
            pass
    _RPi_GPIO = _DummyGPIO()


@dataclass(frozen=True)
class StepMoveCommand:
    pan_steps: int
    tilt_steps: int


class RPIGPIOStepperGimbal:
    """树莓派GPIO直驱（STEP/DIR）单飞控制器。"""

    def __init__(
        self,
        pulse_us: int,
        settle_margin_s: float,
        max_pan_steps: int,
        max_tilt_steps: int,
        pan_step_pin: int,
        pan_dir_pin: int,
        tilt_step_pin: int,
        tilt_dir_pin: int,
        pan_dir_invert: bool = False,
        tilt_dir_invert: bool = False,
    ):
        self.mode = "gpio"
        self.pulse_us = max(1, int(pulse_us))
        self.settle_margin_s = max(0.0, float(settle_margin_s))
        self.max_pan_steps = max(1, int(max_pan_steps))
        self.max_tilt_steps = max(1, int(max_tilt_steps))

        self.pan_step_pin = int(pan_step_pin)
        self.pan_dir_pin = int(pan_dir_pin)
        self.tilt_step_pin = int(tilt_step_pin)
        self.tilt_dir_pin = int(tilt_dir_pin)
        self.pan_dir_invert = bool(pan_dir_invert)
        self.tilt_dir_invert = bool(tilt_dir_invert)

        self._GPIO = None
        self._stop_event = threading.Event()
        self._worker_thread: Optional[threading.Thread] = None
        self._command_lock = threading.Lock()
        self._pending_command: Optional[StepMoveCommand] = None
        self._in_flight = False

        self.total_pan_steps = 0
        self.total_tilt_steps = 0

    def start(self) -> None:
        # Use the module-level GPIO (real or dummy)
        self._GPIO = _RPi_GPIO
        self._GPIO.setwarnings(False)
        self._GPIO.setmode(self._GPIO.BCM)
        for pin in [
            self.pan_step_pin,
            self.pan_dir_pin,
            self.tilt_step_pin,
            self.tilt_dir_pin,
        ]:
            self._GPIO.setup(pin, self._GPIO.OUT, initial=self._GPIO.LOW)

        self._start_worker()

    def stop(self) -> None:
        self._stop_event.set()
        if self._worker_thread and self._worker_thread.is_alive():
            self._worker_thread.join(timeout=1.0)
        if self._GPIO is not None:
            try:
                self._GPIO.cleanup(
                    [
                        self.pan_step_pin,
                        self.pan_dir_pin,
                        self.tilt_step_pin,
                        self.tilt_dir_pin,
                    ]
                )
            except Exception:
                pass

    def can_accept_command(self) -> bool:
        with self._command_lock:
            return (not self._in_flight) and (self._pending_command is None)

    def enqueue_step_move(self, pan_steps: int, tilt_steps: int) -> bool:
        pan_steps = int(max(-self.max_pan_steps, min(self.max_pan_steps, pan_steps)))
        tilt_steps = int(
            max(-self.max_tilt_steps, min(self.max_tilt_steps, tilt_steps))
        )

        if pan_steps == 0 and tilt_steps == 0:
            return False

        cmd = StepMoveCommand(pan_steps=pan_steps, tilt_steps=tilt_steps)
        with self._command_lock:
            if self._in_flight or self._pending_command is not None:
                return False
            self._pending_command = cmd
            return True

    def get_position_steps(self) -> Tuple[int, int]:
        return self.total_pan_steps, self.total_tilt_steps

    def _start_worker(self) -> None:
        if self._worker_thread and self._worker_thread.is_alive():
            return
        self._stop_event.clear()
        self._worker_thread = threading.Thread(
            target=self._worker_loop,
            daemon=True,
            name="rpi-gpio-stepper-worker",
        )
        self._worker_thread.start()

    def _worker_loop(self) -> None:
        while not self._stop_event.is_set():
            cmd = None
            with self._command_lock:
                if self._pending_command is not None and not self._in_flight:
                    cmd = self._pending_command
                    self._pending_command = None
                    self._in_flight = True

            if cmd is None:
                self._stop_event.wait(0.002)
                continue

            try:
                self._execute_move(cmd)
            finally:
                with self._command_lock:
                    self._in_flight = False

    def _execute_move(self, cmd: StepMoveCommand) -> None:
        GPIO = self._GPIO
        if GPIO is None:
            raise RuntimeError("GPIO 未初始化")

        pan_abs = abs(cmd.pan_steps)
        tilt_abs = abs(cmd.tilt_steps)
        max_steps = max(pan_abs, tilt_abs)
        if max_steps <= 0:
            return

        pan_dir_high = int((cmd.pan_steps >= 0) ^ self.pan_dir_invert)
        tilt_dir_high = int((cmd.tilt_steps >= 0) ^ self.tilt_dir_invert)
        GPIO.output(self.pan_dir_pin, GPIO.HIGH if pan_dir_high else GPIO.LOW)
        GPIO.output(self.tilt_dir_pin, GPIO.HIGH if tilt_dir_high else GPIO.LOW)

        half_pulse_s = (self.pulse_us / 1_000_000.0) / 2.0
        for i in range(max_steps):
            if self._stop_event.is_set():
                break

            if i < pan_abs:
                GPIO.output(self.pan_step_pin, GPIO.HIGH)
            if i < tilt_abs:
                GPIO.output(self.tilt_step_pin, GPIO.HIGH)

            time.sleep(half_pulse_s)

            if i < pan_abs:
                GPIO.output(self.pan_step_pin, GPIO.LOW)
            if i < tilt_abs:
                GPIO.output(self.tilt_step_pin, GPIO.LOW)

            time.sleep(half_pulse_s)

        if self.settle_margin_s > 0:
            deadline = time.time() + self.settle_margin_s
            while not self._stop_event.is_set() and time.time() < deadline:
                self._stop_event.wait(0.001)

        self.total_pan_steps += cmd.pan_steps
        self.total_tilt_steps += cmd.tilt_steps