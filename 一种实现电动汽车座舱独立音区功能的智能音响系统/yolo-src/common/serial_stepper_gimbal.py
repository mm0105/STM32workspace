"""串口步进云台控制器（上位机 -> STM32 -> STEP/DIR）。"""

from __future__ import annotations

import glob
import platform
import sys
import threading
import time
from dataclasses import dataclass
from typing import Iterable, Optional, Sequence, Tuple

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover
    serial = None
    list_ports = None
    _SERIAL_IMPORT_ERROR = exc
else:
    _SERIAL_IMPORT_ERROR = None


@dataclass(frozen=True)
class StepMoveCommand:
    pan_steps: int
    tilt_steps: int


class SerialStepperGimbal:
    """串口 ASCII 协议单飞控制器。

    协议：
      PC/RPi -> STM32: MOVE <pan_steps> <tilt_steps>\n
      STM32  -> PC/RPi: OK\n | BUSY\n | ERR\n
    """

    def __init__(
        self,
        serial_port: Optional[str],
        baudrate: int,
        max_pan_steps: int,
        max_tilt_steps: int,
        ack_timeout_s: float,
        read_timeout_s: float,
        write_timeout_s: float,
        windows_ports: Sequence[str] = (),
        rpi_ports: Sequence[str] = (),
        extra_ports: Sequence[str] = (),
    ):
        self.mode = "serial"
        self.serial_port = serial_port
        self.baudrate = int(baudrate)
        self.max_pan_steps = max(1, int(max_pan_steps))
        self.max_tilt_steps = max(1, int(max_tilt_steps))
        self.ack_timeout_s = max(0.1, float(ack_timeout_s))
        self.read_timeout_s = max(0.01, float(read_timeout_s))
        self.write_timeout_s = max(0.1, float(write_timeout_s))
        self.windows_ports = tuple(windows_ports)
        self.rpi_ports = tuple(rpi_ports)
        self.extra_ports = tuple(extra_ports)

        self._serial = None
        self._active_port: Optional[str] = None
        self._stop_event = threading.Event()
        self._worker_thread: Optional[threading.Thread] = None
        self._command_lock = threading.Lock()
        self._pending_command: Optional[StepMoveCommand] = None
        self._in_flight = False
        self._last_error: Optional[str] = None

        self.total_pan_steps = 0
        self.total_tilt_steps = 0

    @property
    def active_port(self) -> Optional[str]:
        return self._active_port

    @property
    def last_error(self) -> Optional[str]:
        return self._last_error

    def start(self) -> None:
        if serial is None:
            raise RuntimeError(
                "缺少 pyserial 依赖，或当前 Python 环境无法导入 serial。\n"
                f"当前 Python: {sys.executable}\n"
                "请用同一个解释器安装依赖，例如：\n"
                f'  "{sys.executable}" -m pip install -r requirements.txt\n'
                f"原始导入错误: {_SERIAL_IMPORT_ERROR}"
            )

        port = self._resolve_serial_port()
        self._serial = serial.Serial(
            port=port,
            baudrate=self.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=self.read_timeout_s,
            write_timeout=self.write_timeout_s,
        )
        self._active_port = port
        self._last_error = None
        self._start_worker()

    def stop(self) -> None:
        self._stop_event.set()
        if self._worker_thread and self._worker_thread.is_alive():
            self._worker_thread.join(timeout=1.0)
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
            finally:
                self._serial = None

    def can_accept_command(self) -> bool:
        with self._command_lock:
            return (not self._in_flight) and (self._pending_command is None)

    def _send_line_and_wait(
        self,
        line: bytes,
        manage_in_flight: bool = True,
    ) -> Optional[str]:
        ser = self._serial
        if ser is None or not ser.is_open:
            raise RuntimeError("串口未初始化")

        if manage_in_flight:
            with self._command_lock:
                if self._in_flight or self._pending_command is not None:
                    self._last_error = "STM32 串口当前有未完成命令"
                    return None
                self._in_flight = True

        try:
            ser.reset_input_buffer()
            ser.write(line)
            ser.flush()

            deadline = time.monotonic() + self.ack_timeout_s
            while not self._stop_event.is_set() and time.monotonic() < deadline:
                raw = ser.readline()
                if not raw:
                    continue

                response = raw.decode("ascii", errors="replace").strip()
                if not response:
                    continue

                return response

            self._last_error = (
                f"等待 STM32 ACK 超时({self.ack_timeout_s:.2f}s): "
                f"{line.decode('ascii', errors='replace').strip()}"
            )
            return None
        except Exception as exc:
            self._last_error = f"串口命令失败: {exc}"
            return None
        finally:
            if manage_in_flight:
                with self._command_lock:
                    self._in_flight = False

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
        with self._command_lock:
            return self.total_pan_steps, self.total_tilt_steps

    def _resolve_serial_port(self) -> str:
        if self.serial_port:
            return self.serial_port

        candidates = list(self._iter_candidate_ports())
        if not candidates:
            raise RuntimeError(
                "未找到可用串口候选，请通过 --serial-port 指定 STM32 串口"
            )

        errors = []
        for port in candidates:
            try:
                probe = serial.Serial(
                    port=port,
                    baudrate=self.baudrate,
                    timeout=self.read_timeout_s,
                    write_timeout=self.write_timeout_s,
                )
                probe.close()
                return port
            except Exception as exc:
                errors.append(f"{port}: {exc}")

        joined = "; ".join(errors[:5])
        raise RuntimeError(f"串口自动探测失败，请通过 --serial-port 指定端口。{joined}")

    def _iter_candidate_ports(self) -> Iterable[str]:
        seen = set()

        def emit(port: str):
            if port and port not in seen:
                seen.add(port)
                return port
            return None

        system = platform.system().lower()

        configured = []
        if "windows" in system:
            configured.extend(self.windows_ports)
            if list_ports is not None:
                configured.extend(port.device for port in list_ports.comports())
            configured.extend(f"COM{i}" for i in range(1, 33))
        else:
            configured.extend(self.rpi_ports)
            if list_ports is not None:
                configured.extend(port.device for port in list_ports.comports())
            configured.extend(glob.glob("/dev/ttyUSB*"))
            configured.extend(glob.glob("/dev/ttyACM*"))
            configured.extend(glob.glob("/dev/ttyAMA*"))

        configured.extend(self.extra_ports)

        for port in configured:
            unique = emit(port)
            if unique is not None:
                yield unique

    def _start_worker(self) -> None:
        if self._worker_thread and self._worker_thread.is_alive():
            return
        self._stop_event.clear()
        self._worker_thread = threading.Thread(
            target=self._worker_loop,
            daemon=True,
            name="serial-stepper-worker",
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
        ser = self._serial
        if ser is None or not ser.is_open:
            raise RuntimeError("串口未初始化")

        line = f"MOVE {cmd.pan_steps} {cmd.tilt_steps}\n".encode("ascii")
        response = self._send_line_and_wait(line, manage_in_flight=False)
        if response is None:
            return

        if self._is_ok_response(response):
            self._apply_completed_move(cmd)
            self._last_error = None
            return
        if response == "BUSY":
            self._last_error = "STM32 返回 BUSY"
            return
        if response == "ERR":
            self._last_error = "STM32 返回 ERR"
            return

        self._last_error = f"未知串口响应: {response}"

    @staticmethod
    def _is_ok_response(response: str) -> bool:
        return response == "OK" or response.startswith("OK ")

    def _apply_completed_move(self, cmd: StepMoveCommand) -> None:
        with self._command_lock:
            self._apply_completed_move_unlocked(cmd)

    def _apply_completed_move_unlocked(self, cmd: StepMoveCommand) -> None:
        self.total_pan_steps += cmd.pan_steps
        self.total_tilt_steps += cmd.tilt_steps
