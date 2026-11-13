# CLAUDE.md

Firmware FreeRTOS cho ESP32-S3 N16R8 (PlatformIO, framework Arduino, env `esp32s3_n16r8`) — Project cá nhân môn RTOS (70% điểm CK).

## Bối cảnh project
- Đề bài, yêu cầu chi tiết Task 1–6 + kỹ thuật mở rộng, kế hoạch tuần, quy tắc report: **`../instruction.md`**.
  - Đọc **mục 3** trước khi code một task mới; đọc **mục 5** khi viết report/slide.
- Code dùng để **báo cáo và làm minh chứng**: tác giả phải giải thích được từng dòng → ưu tiên code rõ ràng, dùng API FreeRTOS chuẩn, không viết "khéo" quá mức.
- Tiến độ: Tuần 2 = Task 1–3 (đã có, **đã chạy đúng trên board** 12/11/2026) · Tuần 3 = Task 4–6 (đã có, đã chạy trên board 12/11/2026, log minh chứng: `../evidence/logs/logs_r.txt`) · Tuần 4 = kỹ thuật mở rộng. Chỉ làm phần được yêu cầu, không tự làm trước task của tuần sau.
- Folder ngoài `firmware/`: `../docs/` (sơ đồ, nhật ký), `../evidence/{logs,photos,videos}/`, `../slides/`, `../report/`.

## Cấu trúc code
- `include/config.h`: **nguồn duy nhất** cho chân GPIO, chu kỳ, priority, stack, core. Không viết số "trần" trong `main.cpp`.
- `lib/AppLogic/app_logic.h`: logic thuần (không Arduino/FreeRTOS), dùng chung với `sim/`.
- `src/main.cpp`: mỗi task một hàm `vTaskN_<Ten>`. **Không viết comment trong `main.cpp`**; mọi giải thích (Task N làm gì, kỹ thuật FreeRTOS nào, vì sao) ghi vào `../chu_thich_main_cpp.md`, theo từng hàm kèm số dòng.
- `sim/sim_tasks.cpp`: mô phỏng trên PC, **một file duy nhất** cho cả 6 task (Phần 1 = `namespace t123`, Phần 2 = `namespace t456`). Thêm task/kỹ thuật mới thì thêm một phần vào file này, không tạo file sim mới.

## Phần cứng (không tự ý đổi)
- Không sửa cấu hình bộ nhớ trong `platformio.ini` (`qio_opi`, 16MB, `-DBOARD_HAS_PSRAM`).
- Cấm dùng GPIO26–37 (Flash + Octal PSRAM), 0/3/45/46 (strapping), 19/20 (USB), 43/44 (UART0), 38/48 (LED RGB onboard). Cảm biến analog chỉ dùng ADC1 (GPIO1–10).
- Đổi chân → sửa `config.h` **và** bảng chân mục 2.2 trong `../instruction.md`.

## Quy tắc code FreeRTOS
- Thân task là `for(;;)`, không bao giờ `return`; muốn kết thúc thì `vTaskDelete(NULL)`.
- Thời gian luôn qua `pdMS_TO_TICKS()`. Trong task dùng `vTaskDelay`/`vTaskDelayUntil`, **không** dùng `delay()` hay vòng chờ bận (chỉ chấp nhận trong `setup()`).
- Task tuần hoàn → `vTaskDelayUntil`. Task polling phải có `vTaskDelay` trong vòng lặp (tránh starvation).
- Stack trên ESP32 tính bằng **byte**. Tạo task qua `createTaskOrHalt()`; kiểm tra mọi handle (mutex/semaphore/queue/timer) khác `NULL`.
- Tạo mọi đối tượng đồng bộ **trước** khi tạo task dùng nó.
- Dữ liệu chung nằm trong `g_state`, chỉ đọc/ghi khi giữ `xStateMutex`. Giữ mutex thật ngắn: copy ra biến cục bộ rồi nhả; **không** gọi `logf`, I2C/OLED hay delay khi đang giữ mutex.
- Serial trong task chỉ in qua `logf(tag, ...)` (có `xSerialMutex` + timestamp). Tag: `T1`…`T6`, `SYS`, `EXT`. Chuỗi log viết tiếng Việt **không dấu**.
- ISR: hàm có `IRAM_ATTR`, chỉ dùng API `...FromISR`, khởi tạo `xHigherPriorityTaskWoken = pdFALSE`, kết thúc bằng `portYIELD_FROM_ISR(...)`; không Serial/`logf`/mutex trong ISR.
- Critical section dùng spinlock: `portMUX_TYPE` + `taskENTER_CRITICAL(&mux)` (ESP32-S3 có 2 lõi).
- Bus I2C (GY-30 + OLED) chỉ dùng khi giữ `xI2CMutex`. **Không** giữ `xStateMutex` và `xI2CMutex` cùng lúc: nhả cái này rồi mới lấy cái kia.
- Chú thích bằng tiếng Việt, ngắn, nêu rõ **kỹ thuật FreeRTOS** đang dùng (để trích vào report). Với `main.cpp`, chú thích nằm ở `../chu_thich_main_cpp.md` — sửa `main.cpp` thì cập nhật file đó (nội dung + số dòng). `config.h`, `app_logic.h`, `sim/` vẫn comment trong file.

## Quy tắc bắt buộc: kiểm thử sau mỗi lần code

Mỗi lần viết hoặc sửa code xong, **luôn** làm đủ các bước sau trước khi báo hoàn thành:

1. **Build & kiểm lỗi**
   - Chạy `pio run -e esp32s3_n16r8` và đảm bảo build thành công, không có lỗi (error).
   - Đọc kỹ các cảnh báo (warning) liên quan đến code vừa sửa và xử lý nếu có.
   - Nếu build lỗi: sửa rồi build lại cho đến khi sạch lỗi.

2. **Chạy thử / mô phỏng ít nhất 10 lần**
   - Mô phỏng phần logic vừa viết **tối thiểu 10 lần** (ví dụ: test host-side `pio test -e native`, mô phỏng Wokwi, hoặc chương trình C/C++/Python mô phỏng logic task, queue, semaphore, timing…).
   - Mỗi lần chạy nên dùng điều kiện/đầu vào khác nhau (giá trị biên, thứ tự task khác nhau, dữ liệu ngẫu nhiên, tình huống lỗi) để bắt được race condition, tràn bộ đệm, deadlock, sai timing.
   - Nếu có lần nào lỗi: tìm nguyên nhân, sửa, rồi chạy lại đủ 10 lần từ đầu.
   - Mô phỏng sẵn có: `sim/sim_tasks.cpp` (dùng chung `lib/AppLogic/app_logic.h` **và** `include/config.h` với firmware — sim không tự chép hằng số). Máy không có Windows SDK nên MSVC không dùng được → biên dịch bằng zig (`python -m pip install --target <scratch>/zig ziglang`):
     `zig c++ -std=c++17 -O2 -Wall -Wextra -I include -I lib/AppLogic sim/sim_tasks.cpp -o sim_tasks.exe && ./sim_tasks.exe 12`
   - Một lần chạy kiểm cả hai phần (Task 1–3 và Task 4–6), mỗi phần N lần; dòng cuối `TONG KET CHUNG` phải là `ALL PASS`.
   - Logic mới nên viết dạng hàm thuần trong `lib/AppLogic/` để mô phỏng được trên PC.

3. **Báo cáo kết quả**
   - Đưa ra kết quả cụ thể: lệnh build đã chạy + kết quả, và bảng kết quả 10 (hoặc hơn) lần mô phỏng (lần chạy, đầu vào/kịch bản, kết quả PASS/FAIL).
   - Chỉ khẳng định "code chạy không lỗi" khi build sạch và **tất cả** các lần mô phỏng đều PASS.
   - Nếu không thể chạy được bước nào (thiếu công cụ, cần phần cứng thật…), phải nói rõ bước đó chưa được kiểm chứng và lý do — không được báo là đã chạy.
   - Mô phỏng chỉ kiểm được **logic**. GPIO/ADC/I2C/OLED và timing thật chỉ được coi là đã kiểm chứng khi chạy trên board — phải ghi rõ trong báo cáo kết quả.

## Nạp & xem log
- Build: `pio run -e esp32s3_n16r8` · Nạp: `pio run -e esp32s3_n16r8 -t upload` · Log: `pio device monitor`.
- Chỉ upload/monitor khi người dùng yêu cầu (cần board cắm vào máy; `monitor` chạy không tự dừng).

## Sau mỗi thay đổi đáng kể
- Ghi 3–5 dòng vào `../docs/nhat_ky.md`: ngày, đã làm gì, lỗi gặp, cách sửa (nguyên liệu cho slide và chương Kiểm thử).
- Log chạy thật: Serial Monitor tự ghi vào `../evidence/logs/tasks_logs.txt` (filter `tasklog` ở `monitor/filter_tasklog.py`, khai báo trong `monitor_filters`). File bị **ghi đè mỗi lần mở Serial Monitor** (kể cả khi board chưa gửi gì); board bị rút/reset rồi monitor tự kết nối lại trong cùng lần mở thì ghi **nối tiếp**, không xoá. Chỉ giữ lần mở gần nhất.
- Bản log giữ làm minh chứng cho report là `../evidence/logs/logs_r.txt`: khi người dùng yêu cầu lưu một lần chạy, phân tích log mới rồi chép `tasks_logs.txt` vào `logs_r.txt` (chỉ một file, không tạo thêm file theo ngày). Nếu bản mới có đủ mọi nội dung của bản cũ thì ghi đè; nếu bản cũ có minh chứng mà bản mới thiếu thì giữ phần cũ bên dưới, mỗi lần chạy có một khối tiêu đề bắt đầu bằng `#` (hiện có: LẦN CHẠY A = có dòng khởi động, LẦN CHẠY B = có dòng `semaphore dang co san`), và báo cho người dùng.
