# Nhật ký làm việc — Project cá nhân FreeRTOS ESP32-S3

## 2026-11-11 — Task 1, 2, 3 (Tuần 2)
- **Đã làm:** viết Task 1 (LED1 blink T=200 ms, `vTaskDelayUntil`), Task 2 (đọc LDR 500 ms/lần, trung bình 8 mẫu, ghi `g_state` trong `xStateMutex`), Task 3 (polling Button1 20 ms + chống dội 3 lần quét → đảo LED2). Thêm `logf()` với `xSerialMutex`. Logic thuần tách ra `lib/AppLogic/app_logic.h`; mô phỏng PC `sim/sim_task123.cpp`.
- **Đối chiếu CLAUDE.md:** bỏ số trần trong `main.cpp` (baud, chờ Serial, độ phân giải ADC, buffer log, timeout mutex, stack từng task → `config.h`); log tiếng Việt không dấu; `createTaskOrHalt` log qua `logf`; bỏ phần chuẩn bị Tuần 3 (`led3Remain`, khởi tạo LED3/BTN2); sim lấy hằng số trực tiếp từ `config.h`.
- **Lỗi gặp:** dòng log đầu tiên của Task 1 báo 900 ms thay vì 1000 ms (mốc log đặt trùng lần đảo thứ 1).
- **Cách sửa:** đặt `xLogMark = xLastWake - xHalfPeriod` (mốc "lần đảo thứ 0"); thêm kiểm tra khoảng log vào sim, xác nhận sim bắt được lỗi cũ.
- **Kiểm thử:** build `pio run -e esp32s3_n16r8` SUCCESS, 0 warning; sim 12/12 PASS. Chưa chạy trên board thật.

## 2026-11-12 — Đổi cảm biến ánh sáng: LDR (ADC) → GY-30 / BH1750 (I2C)
- **Đã làm:** thêm `claws/BH1750` vào `lib_deps` (bản 1.3.0). `config.h`: bỏ `PIN_LDR`, `LDR_SAMPLES`, `ADC_*`; thêm `BH1750_ADDR` 0x23, `BH1750_MEAS_TIME_MS`, `BH1750_LUX_MAX`, `BH1750_ERR_NOT_READY`; `T2_STACK` tăng lên 4096. `setup()`: tạo `xI2CMutex`, `Wire.begin(8, 9)`, khởi tạo BH1750 chế độ `CONTINUOUS_HIGH_RES_MODE`; init lỗi thì log và chạy tiếp. Task 2: giữ `xI2CMutex` khi đọc, nhả, rồi mới giữ `xStateMutex` để ghi (không giữ 2 mutex cùng lúc). `app_logic.h`: bỏ `averageSamples`/`rawToPercent`, thêm `luxIsValid` + `LightState` + `lightUpdate`.
- **Chú thích:** bỏ toàn bộ comment trong `main.cpp`, chuyển sang `chu_thich_main_cpp.md` (thư mục gốc project); cập nhật quy tắc tương ứng trong `firmware/CLAUDE.md`.
- **Lỗi gặp (ở chương trình mô phỏng, không phải firmware):** (1) bài thử 2 luồng không phát hiện được việc thiếu `xStateMutex` vì đoạn cập nhật quá ngắn; (2) ở vài lần chạy luồng thứ hai gần như chưa chạy; (3) sau khi sửa, một luồng chiếm mutex liên tục làm mô phỏng mất 3,5 phút.
- **Cách sửa:** (1) cho luồng ghi nhường CPU giữa lúc cập nhật để mô phỏng bị preempt; (2) cả hai luồng chạy đến khi cùng đủ 20000 vòng; (3) thêm giữ nhịp, không luồng nào vượt luồng kia quá 32 vòng → còn 0,5 s.
- **Kiểm thử:** build `pio run -e esp32s3_n16r8` SUCCESS, 0 warning (RAM 5,9%, Flash 4,6%). Sim 12/12 PASS với 4 kịch bản cảm biến (tốt / lỗi rải rác / rút dây từng đợt / init lỗi). 4 bài kiểm tra ngược (bỏ `xI2CMutex`, bỏ `xStateMutex`, giữ 2 mutex cùng lúc, `lightUpdate` ghi đè lux khi lỗi) đều bị sim bắt lỗi. **Chưa chạy trên board: việc đọc BH1750 thật qua I2C chưa được kiểm chứng.**

## 2026-11-12 (tối) — Task 4, 5, 6 (Tuần 3): đủ 6 task
- **Đã làm:** Task 4 (`vTask4_Button2`: polling BTN2 + chống dội → `xSemaphoreGive(semButton2)`, log cả trường hợp give trả `pdFALSE`). Task 5 (`vTask5_Led3`: `xSemaphoreTake(portMAX_DELAY)` → LED3 sáng 3 bước × 1000 ms bằng `vTaskDelayUntil`, ghi `g_state.led3Remain` 3→2→1→0). Task 6 (`vTask6_Display`: mỗi 500 ms copy `g_state` trong `xStateMutex`, nhả, tạo 3 dòng chữ, vẽ vào bộ đệm, chỉ giữ `xI2CMutex` lúc `display()`). `setup()`: tạo `semButton2`, dò OLED ở 0x3C trước khi `begin()`; thiếu OLED vẫn chạy tiếp. Priority: T4 = T5 = 3, T1–T3 = 2, T6 = 1.
- **Logic thuần mới:** `formatLed2Line` / `formatLightLine` / `formatLed3Line` trong `app_logic.h` (mỗi dòng ≤ 10 ký tự cho cỡ chữ 2).
- **Mô phỏng mới:** `sim/sim_task456.cpp` — dòng thời gian từng ms cho T4/T5/T6 với 4 kịch bản nhấn (thưa / dồn dập / giữ lâu / hỗn hợp có nhiễu), đối chiếu chéo với lời giải mẫu tính theo sự kiện; kiểm tra định dạng dòng OLED; 4 luồng thật (T2, T4, T5, T6) kiểm tra bus I2C, dữ liệu chung, thứ tự khóa, token semaphore, deadlock.
- **Lỗi gặp:** không có lỗi ở firmware lẫn mô phỏng trong lần này.
- **Kiểm thử:** build SUCCESS, 0 warning (RAM 5,9%, Flash 4,6%). `sim_task456` 12/12 PASS; `sim_task123` chạy lại 12/12 PASS. 5 bài kiểm tra ngược (semaphore đếm thay vì binary, bỏ `xI2CMutex`, bỏ `xStateMutex`, dòng LED3 quá 10 ký tự, LED3 chỉ sáng 2 bước) đều bị sim bắt; riêng bỏ `xStateMutex` bị bắt ở 8/10 lần chạy (phụ thuộc thời điểm luồng). **Chưa chạy trên board: nút, LED3, OLED, BH1750 và timing thật chưa được kiểm chứng.**

## 2026-11-12 (tối) — Task 1–3 chạy trên board; gộp file mô phỏng
- **Chạy thật:** đã nạp và chạy Task 1, 2, 3 trên board ESP32-S3 N16R8; LED1, cảm biến GY-30 và Button1/LED2 hoạt động đúng yêu cầu trong `instruction.md` (theo quan sát của người làm; chưa lưu log vào `evidence/logs/`).
- **Đã làm:** gộp `sim_task123.cpp` + `sim_task456.cpp` thành một file `sim/sim_tasks.cpp` (hai `namespace` `t123`, `t456`, chung macro `CHECK` và một hàm `main`); xoá hai file cũ; sửa lệnh build trong `firmware/CLAUDE.md`. Firmware không đổi.
- **Kiểm thử:** `sim_tasks.exe 12` → Phần 1: 12/12 PASS, Phần 2: 12/12 PASS, số liệu trùng với trước khi gộp. Bản cố ý làm sai (bỏ `xI2CMutex` + semaphore đếm) báo FAIL ở cả hai phần. **Task 4–6 chưa chạy trên board.**

## 2026-11-12 (đêm) — Cả 6 task chạy trên board; sửa việc lưu log
- **Chạy thật:** đã nạp bản đủ 6 task. Theo quan sát của người làm: nhấn 2 nút nhiều lần thì LED2 và LED3 đổi trạng thái đúng, GY-30 đo được lux, OLED hiển thị đúng các thông số.
- **Lỗi gặp:** không có file nào trong `evidence/logs/`. Serial Monitor mở bằng nút của PlatformIO chỉ in ra terminal, không tự ghi file.
- **Cách sửa:** viết filter riêng `firmware/monitor/filter_tasklog.py` và khai báo `monitor_filters = esp32_exception_decoder, tasklog`. Mỗi lần mở Serial Monitor, log tự ghi vào `evidence/logs/tasks_logs.txt` và **ghi đè** lần trước (ban đầu thử `log2file` có sẵn của PlatformIO nhưng nó chỉ ghi được vào `firmware/logs/` với tên theo giờ, không đúng yêu cầu).
- **Kiểm thử filter (không cần board):** nạp filter bằng chính cơ chế của PlatformIO rồi đưa dữ liệu giả vào: PlatformIO nhận ra filter; file tạo đúng đường dẫn; nội dung đúng từng byte; lần chạy 2 ghi đè lần 1; mở monitor không có dữ liệu thì file rỗng → tất cả PASS. Chưa thử với board thật.
- **Còn thiếu:** chưa có log/ảnh/video của lần chạy này → cần chạy lại một lượt để lấy minh chứng cho report. Timing thật (chu kỳ LED1, LED3 sáng đúng 3 s) mới được quan sát bằng mắt, chưa đối chiếu bằng log.
- **Đổi tên + sửa điều kiện ghi đè:** file log đổi thành `evidence/logs/tasks_logs.txt`. Đọc mã nguồn PlatformIO thấy mỗi lần monitor tự kết nối lại (board bị rút/reset) filter bị gọi lại và sẽ xoá log giữa buổi → sửa: chỉ xoá khi mở monitor mới; kết nối lại trong cùng lần mở thì ghi nối tiếp sau một dòng đánh dấu. Thử bằng 2 tiến trình riêng (lần mở 1: ghi + kết nối lại 4 lần không mất log; lần mở 2: file bị xoá ngay khi mở) → tất cả PASS. Chưa thử với board thật.

## 2026-11-12 (20:15) — Log chạy thật đủ 6 task
- **Minh chứng:** `evidence/logs/T1-6_chay_thuc_te_2026-11-12.txt` (bản sao của `tasks_logs.txt`), 281 dòng, 74 s (từ 9288 ms đến 83288 ms sau khi boot). Filter `tasklog` hoạt động đúng với board thật.
- **Kết quả đo từ log:** T1: 74/74 dòng báo đúng 1000 ms cho 10 lần đảo (730 lần đảo trong 73000 ms). T2: 148 lần đọc, 0 lỗi, chu kỳ đúng 500 ms cả 147 khoảng; lux nền ~90, che cảm biến xuống 0,0 lx, chiếu đèn lên 5465 lx. T3: 12 lần nhấn, LED2 luân phiên ON/OFF đúng. T4/T5: 3 lần nhấn → 3 lượt LED3, mỗi lượt đúng 3000 ms, LED3 bật cùng mili-giây với lúc cấp semaphore. T6: 38/38 dòng OLED khớp với T2/T3/T5, chu kỳ đúng 2000 ms cho 4 lần vẽ, có đủ `3s`, `2s`, `1s`, `OFF`.
- **Không có:** lỗi đọc cảm biến, crash, reset, dòng log hỏng hay trộn lẫn.
- **Còn thiếu:** (1) phần khởi động (thông tin chip/Flash/PSRAM, dòng `SYS`) vì monitor mở sau khi board đã chạy 9 s; (2) chưa thử nhấn BTN2 khi LED3 đang sáng → chưa có minh chứng cho hành vi "binary semaphore chỉ giữ 1 token"; (3) chưa có ảnh/video.

## 2026-11-12 (20:25) — Nhấn RST thì Serial Monitor im lặng
- **Hiện tượng:** đang mở monitor qua cổng USB native (`ARDUINO_USB_CDC_ON_BOOT=1`), nhấn RST → monitor không in gì nữa. `tasks_logs.txt` dừng ở 22943 ms (20:23:41), không có dòng đánh dấu kết nối lại.
- **Đã kiểm tra:** (1) thử đường kết nối lại của PlatformIO với đúng 2 filter của project trên cổng giả → 4/4 lần kết nối OK, log ghi nối tiếp đúng, nên filter `tasklog` không phải nguyên nhân; (2) lúc 20:24:40 Windows còn thấy board ở COM11, từ 20:25:18 trở đi không thấy cổng nào của board.
- **Nhận định (chưa xác nhận trên board):** reset làm cổng USB native biến mất rồi xuất hiện lại; phiên monitor đang mở không tự nối lại được. Đây là đặc điểm của cổng USB native, không phải lỗi firmware. Lời khuyên "mở monitor rồi nhấn RST để lấy log khởi động" là sai với cổng này.
- **Cách làm đúng:** sau khi reset phải đóng monitor và mở lại; muốn lấy dòng khởi động thì dùng "Upload and Monitor" hoặc chuyển sang cổng COM/UART của board.

## 2026-11-12 (20:30) — Log minh chứng mới: `evidence/logs/logs_r.txt`
- **Đã làm:** chạy lại 128 s (1943 ms → 129943 ms), 524 dòng. Chép `tasks_logs.txt` đè lên bản cũ `T1-6_chay_thuc_te_2026-11-12.txt` rồi đổi tên thành `logs_r.txt` (từ nay chỉ giữ một bản minh chứng này). Bản 74 s cũ không còn; bản mới có đủ mọi nội dung của bản cũ và thêm phần thử semaphore.
- **Kết quả đo:** T1 128/128 dòng đúng 1000 ms/10 lần đảo. T2 257 lần đọc, 0 lỗi; 252/256 khoảng đúng 500 ms, 4 khoảng lệch ±1 ms theo cặp (không trôi); che cảm biến xuống 0,0 lx, chiếu đèn lên 4820 lx. T3 34 lần nhấn (nhanh nhất cách nhau 320 ms), LED2 luân phiên đúng. T4/T5: 17 lần nhấn → 12 token được nhận, 5 lần bị bỏ qua; 12 lượt LED3, mỗi lượt đúng 3000 ms. T6 64 dòng, chu kỳ đúng 2000 ms/4 lần vẽ.
- **Minh chứng binary semaphore:** 100942 ms nhấn → LED3 sáng; 102062 ms nhấn lần 2 → được giữ làm token chờ; 102942/103442/103642 ms nhấn tiếp → `semaphore dang co san`; 103942 ms LED3 tắt và sáng lại ngay đúng 1 lượt.
- **Một điểm cần hiểu đúng (không phải lỗi):** dòng T6 lúc 55288 ms ghi `LED2: ON` trong khi T3 đã đổi sang OFF lúc 55282 ms. Task 6 chụp dữ liệu lúc thức dậy (~55263 ms), vẽ và gửi OLED mất ~25 ms rồi mới log. Lần vẽ kế tiếp hiển thị đúng. Suy ra: một lần gửi khung hình OLED mất khoảng 25 ms.
- **Còn thiếu:** dòng khởi động (thông tin chip, `SYS`) vì monitor nối được sau khi board đã chạy ~1,9 s; ảnh/video.

## 2026-11-12 (20:40) — Tăng thời gian chờ lúc khởi động lên 3 s
- **Lý do:** log chạy thật bắt đầu ở 1943 ms, mất các dòng khởi động (thông tin chip, `SYS`), vì với cổng USB native monitor cần gần 2 s sau reset mới nối lại được, trong khi firmware chỉ chờ 1,5 s.
- **Đã làm:** `SERIAL_WAIT_MS` 1500 → 3000 trong `config.h` (chỉ ảnh hưởng `setup()`: các task bắt đầu chậm hơn 1,5 s, chu kỳ không đổi). Cập nhật `chu_thich_main_cpp.md`.
- **Kiểm thử:** build SUCCESS, 0 warning. `sim_tasks.exe 12`: Phần 1 12/12 PASS, Phần 2 12/12 PASS. **Chưa nạp lên board: chưa biết 3 s đã đủ để bắt được dòng khởi động hay chưa.**

## 2026-11-12 (20:42) — Log có đủ dòng khởi động; `logs_r.txt` giữ 2 lần chạy
- **Chạy thật bản `SERIAL_WAIT_MS` = 3000:** chờ 3 s là đủ, log bắt được từ đầu: `Chip: ESP32-S3 rev 0, 2 nhan @ 240 MHz`, `Flash: 16 MB`, `PSRAM: 8386295 byte` (~8 MB, đúng N16R8), `Tick: 1000 Hz`; BH1750 sẵn sàng lúc 3134 ms, OLED lúc 3162 ms, 6 task được tạo lúc 3162 ms.
- **Kết quả đo (104 s, 471 dòng):** T1 104/104 dòng đúng 1000 ms/10 lần đảo. T2 209 lần đọc, 0 lỗi, 207/208 khoảng đúng 500 ms (1 khoảng 499 ms); lần đọc đầu lúc 3343 ms = 3162 + 180 ms chờ lần đo đầu; lux từ 0,0 đến 7142,5. T3 56 lần nhấn (nhanh nhất cách 240 ms), LED2 luân phiên đúng. T4/T5 14 lần nhấn → 14 lượt LED3 đúng 3000 ms, trong đó 6 lượt nối tiếp ngay lượt trước do nhấn khi LED3 đang sáng. T6 52 dòng, chu kỳ 2000 ms.
- **Thiếu so với lần trước:** không có lần nhấn BTN2 nào bị từ chối (0 dòng `semaphore dang co san`), vì không lần nào nhấn 2 lần trở lên trong cùng một lượt sáng.
- **Đã làm:** `evidence/logs/logs_r.txt` giờ chứa cả hai lần chạy, ngăn bằng khối tiêu đề `#`: LẦN CHẠY A (mới, có dòng khởi động) ở trên, LẦN CHẠY B (20:30, có 5 dòng từ chối semaphore) ở dưới. Không ghi đè hẳn để không mất minh chứng binary semaphore.
