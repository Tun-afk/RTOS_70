# Filter cho Serial Monitor cua PlatformIO (khai bao trong platformio.ini:
#   monitor_filters = esp32_exception_decoder, tasklog)
#
# Toan bo noi dung Serial Monitor nhan duoc tu board duoc ghi vao
#   <thu muc project>/../evidence/logs/tasks_logs.txt
#
# Dieu kien ghi de:
#   - Moi lan MO Serial Monitor (mot lan chay lenh "pio device monitor" hoac
#     bam nut Monitor / Upload and Monitor) -> file bi XOA va ghi lai tu dau.
#   - Trong CUNG mot lan mo, neu board bi rut / reset va monitor tu ket noi
#     lai -> KHONG xoa, ghi NOI TIEP sau mot dong danh dau.
# Muon giu lai mot lan chay lam minh chung thi chep file ra ten khac truoc
# khi mo monitor lan sau.

import io
import os

from platformio.device.monitor.filters.base import DeviceMonitorFilterBase

LOG_DIR_FROM_PROJECT = os.path.join("..", "evidence", "logs")
LOG_FILE_NAME = "tasks_logs.txt"
RECONNECT_MARK = "\n--- [tasklog] monitor ket noi lai, ghi noi tiep ---\n"


class TaskLogToFile(DeviceMonitorFilterBase):
    NAME = "tasklog"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._log_fp = None

    def __call__(self):
        # PlatformIO goi ham nay moi khi (tai) tao ket noi. Doi tuong filter
        # ton tai suot mot lan mo monitor, nen _log_fp con mo = dang ket noi lai.
        if self._log_fp:
            self._log_fp.write(RECONNECT_MARK)
            self._log_fp.flush()
            return self

        project_dir = self.project_dir or os.getcwd()
        log_dir = os.path.abspath(os.path.join(project_dir, LOG_DIR_FROM_PROJECT))
        os.makedirs(log_dir, exist_ok=True)
        log_path = os.path.join(log_dir, LOG_FILE_NAME)
        print("--- Ghi log (ghi de) vao %s" % log_path)
        # "w": xoa noi dung cu. newline="": giu nguyen ky tu xuong dong board gui len.
        self._log_fp = io.open(log_path, "w", encoding="utf-8", newline="")
        return self

    def __del__(self):
        if self._log_fp:
            self._log_fp.close()

    def rx(self, text):
        if self._log_fp:
            self._log_fp.write(text)
            self._log_fp.flush()
        return text
