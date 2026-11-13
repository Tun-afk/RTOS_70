# PROJECT CÁ NHÂN (70%) — FreeRTOS trên ESP32

> Môn: Hệ điều hành thời gian thực (RTOS) — HK2, 25-26 — GVHD: Huỳnh Hoàng Hà
> Nguồn đề: `10.0. Newest_RTOS.pdf` (slide "Task GK + CK")
> File này là **hướng dẫn gốc** của project: đọc lại mỗi khi bắt đầu một buổi làm việc. Mọi code, ảnh, log, slide và report của project cá nhân đều nằm trong folder này.

---

## 0. Tóm tắt nhanh

| Mục | Nội dung |
|---|---|
| Đề tài | Ứng dụng đa nhiệm FreeRTOS trên ESP32 (6 task bắt buộc + task/kỹ thuật mở rộng) |
| Công cụ | VS Code + PlatformIO, FreeRTOS (có sẵn trong Arduino-ESP32), board ESP32-S3 N16R8, testboard |
| Ngoại vi | 3 LED, 2 nút nhấn 4 chân, 1 cảm biến cường độ ánh sáng GY-30 (BH1750, I2C) (cb a/s), 1 màn OLED 0.96" I2C |
| Chấm theo tiến độ | Tuần 2: Task 1–3 · Tuần 3: Task 4–6 + đề xuất mở rộng · Tuần 4: kỹ thuật mở rộng + hoàn thành |
| Sản phẩm cuối | Code chạy được trên phần cứng + **cuốn Report** (nộp tuần sau Tuần 4) |
| Yêu cầu xuyên suốt | Mỗi đặc điểm/kiến thức trong report **phải có minh chứng** (đoạn code + mô tả + kết quả) |

---

## 1. Cấu trúc folder

```
Project_CaNhan_FreeRTOS_ESP32/
├── instruction.md          ← file này (yêu cầu + kế hoạch + quy ước)
├── firmware/               ← project PlatformIO
│   ├── CLAUDE.md           ←   quy tắc cho Claude Code khi code
│   ├── platformio.ini      ←   cấu hình board ESP32-S3 N16R8
│   ├── include/config.h    ←   chân GPIO, chu kỳ, priority, stack (nguồn duy nhất)
│   ├── src/main.cpp        ←   các task FreeRTOS
│   ├── lib/AppLogic/       ←   logic thuần (dùng chung với mô phỏng)
│   └── sim/                ←   mô phỏng logic trên PC
├── docs/                   ← ghi chú kỹ thuật: sơ đồ chân, sơ đồ task, nhật ký (nhat_ky.md)
├── evidence/               ← minh chứng chạy thật
│   ├── photos/             ←   ảnh mạch, ảnh OLED
│   ├── videos/             ←   video demo từng task
│   └── logs/               ←   log Serial Monitor (.txt) có timestamp
├── slides/                 ← slide báo cáo tiến độ Tuần 2, 3, 4
└── report/                 ← cuốn Report cuối kỳ
    └── figures/            ←   hình vẽ dùng trong report (sơ đồ khối, sơ đồ task, ảnh chụp)
```

**Quy ước đặt tên minh chứng:** `T<số task>_<mô tả>_<ngày>.<ext>`, ví dụ `T5_led3_3s_countdown_2026-11-24.mp4`, `T2_sensor_log_2026-11-17.txt`. Nhờ vậy khi viết report chỉ cần tra theo số task.

---

## 2. Phần cứng & môi trường

### 2.1 Danh sách linh kiện (mua tại Thế Giới IC, giá tham khảo ngày 12/11/2026)

| Linh kiện | SL cần | Gợi ý sản phẩm | Ghi chú |
|---|---|---|---|
| Board ESP32-S3 N16R8 | 1 | (đã có) | Module ESP32-S3-WROOM-1: 16 MB Flash QIO + 8 MB PSRAM OPI, 2 lõi Xtensa LX7 |
| LED 5mm | 3 | [LED Đỏ 5mm](https://www.thegioiic.com/led-do-5mm-sieu-sang-trong-suot-chan-dai) / [Xanh lá](https://www.thegioiic.com/led-xanh-la-5mm-sieu-sang-trong-suot-chan-dai) / [Vàng](https://www.thegioiic.com/led-vang-5mm-sieu-sang-trong-suot-chan-dai) | ~285 đ/con, mua tối thiểu 20. Nên dùng 3 màu khác nhau để dễ quay video minh chứng |
| Điện trở 330 Ω 1/4W | 3 | [Điện Trở 330 Ohm 1/4W 5%](https://www.thegioiic.com/dien-tro-330-ohm-1-4w-5-4-vong-mau) | ~55 đ/con, mua tối thiểu 50 |
| Nút nhấn 6x6mm 4 chân xuyên lỗ | 2 | [Danh mục Nút Nhấn Tactile](https://www.thegioiic.com/product/nut-nhan-tactile) (cao 6–12 mm) | ~450–550 đ/cái, mua tối thiểu 5–10 |
| Cảm biến cường độ ánh sáng **GY-30 (BH1750)** | 1 | [GY-30 BH1750VI Cảm Biến Cường Độ Ánh Sáng](https://www.thegioiic.com/gy-30-bh1750vi-cam-bien-cuong-do-anh-sang) | ~34.200 đ. Nguồn 3–5 VDC, giao tiếp I2C, ra giá trị **lux** (ADC 16 bit tích hợp) |
| OLED 0.96" SSD1306 **I2C 4 chân** | 1 | [LCD OLED SSD1306 0.96inch Chữ Trắng 4 Chân IIC](https://www.thegioiic.com/lcd-oled-ssd1306-0-96inch-128x64-chu-trang-4-chan-giao-tiep-iic) | ~52–57k. Tránh bản 7 chân SPI / 30 chân, và bản dùng driver SH1106 |
| Testboard + dây cắm đực-đực | 1 bộ | | |

Tổng chi phí khoảng 95–100k (chưa tính board và phí ship).

### 2.1b Nguồn & cáp
- **Không cần nguồn 5V riêng.** Cáp USB nối laptop cấp 5V, ổn áp trên board hạ xuống 3,3V. Toàn hệ thống chỉ tốn khoảng 100–150 mA, trong khi cổng USB cấp được ≥ 500 mA. Mọi ngoại vi lấy nguồn từ chân **3V3**, GND nối chung.
- Không cấp thêm 5V ngoài vào chân 5V/VIN khi đang cắm USB.
- Cắm cổng **COM** của board. Cáp Type-C phải là loại **có truyền dữ liệu**.
- Nếu dùng cáp C-to-C mà board không lên nguồn (một số board thiếu điện trở CC 5,1 kΩ), đổi sang cáp **A-to-C**.
- Board tự reset kèm thông báo `Brownout detector was triggered`: đổi cáp hoặc đổi cổng USB khác.

### 2.2 Sơ đồ chân (ESP32-S3 N16R8). Nguồn chuẩn nằm ở `firmware/include/config.h`

**Chân KHÔNG được dùng trên S3 N16R8:** GPIO26–32 (SPI Flash), **GPIO33–37 (Octal PSRAM, chỉ bản R8 bị chiếm)**, GPIO0/3/45/46 (strapping), GPIO19/20 (USB native), GPIO43/44 (UART0), GPIO48 hoặc 38 (LED RGB onboard trên nhiều board).

| Ngoại vi | GPIO | Ghi chú |
|---|---|---|
| LED1 (Task 1) | 4 | OUTPUT |
| LED2 (Task 3) | 5 | OUTPUT |
| LED3 (Task 5) | 6 | OUTPUT |
| Button1 (Task 3) | 15 | INPUT_PULLUP, nhấn = LOW |
| Button2 (Task 4) | 16 | INPUT_PULLUP, nhấn = LOW (có thể gắn ngắt) |
| Bus I2C – SDA / SCL | 8 / 9 | I2C mặc định của Arduino-ESP32S3, **dùng chung** cho OLED và GY-30 |
| OLED SSD1306 | (I2C) | Địa chỉ 0x3C |
| GY-30 BH1750 (Task 2) | (I2C) | Địa chỉ **0x23** (chân ADDR để trống hoặc nối GND; nối 3V3 thì thành 0x5C) |

**Cách nối:**
- **LED:** GPIO → điện trở 330 Ω → chân dài (anode) của LED; chân ngắn (cathode) nối GND. Mức HIGH = sáng. Dòng mỗi LED khoảng 4 mA.
- **Nút nhấn 4 chân:** bên trong 4 chân chia thành 2 cặp luôn thông nhau. Cắm nút **vắt ngang rãnh giữa** testboard, nối **2 chân chéo nhau**: một chân vào GPIO, chân đối diện vào GND. Không cần điện trở ngoài vì code đã bật `INPUT_PULLUP`.
  - Nếu LED2 không bao giờ đổi trạng thái, hoặc log báo nút đang nhấn liên tục, thường là do chọn nhầm 2 chân cùng một cặp.
- **OLED và GY-30 cắm song song trên cùng bus I2C:** VCC → **3V3**, GND → GND, SDA → GPIO8, SCL → GPIO9. Chân ADDR của GY-30 để trống hoặc nối GND (địa chỉ 0x23). Hai module thường có sẵn điện trở kéo lên cho SDA/SCL nên không cần thêm.
- Sơ đồ mạch đầy đủ: `docs/so_do_mach.svg` (bản PNG cho report: `report/figures/so_do_mach.png`).

→ Khi lắp xong, chụp ảnh mạch, đặt vào `evidence/photos/` và vẽ lại sơ đồ chân vào `docs/`.

### 2.3 Môi trường phần mềm
- VS Code → cài extension **PlatformIO IDE**.
- Project PlatformIO **đã tạo sẵn** trong `firmware/`: mở bằng *PIO Home → Open Project → chọn folder `firmware`*.
  - Board: `esp32-s3-devkitc-1` (trong wizard là *Espressif ESP32-S3-DevKitC-1-N8 (8 MB QD, No PSRAM)*), sau đó **ghi đè bộ nhớ** trong `platformio.ini`: `board_build.arduino.memory_type = qio_opi`, `flash_size = 16MB`, `partitions = default_16MB.csv`, `-DBOARD_HAS_PSRAM`. Sai `memory_type` thì board sẽ boot loop hoặc không thấy PSRAM.
  - Framework **Arduino**: FreeRTOS đã tích hợp sẵn, không cần cài riêng.
  - Board có 2 cổng USB-C: cắm cổng **COM/UART** để nạp và xem Serial. Nếu cắm cổng **USB** (native) thì bật `-DARDUINO_USB_CDC_ON_BOOT=1` trong `platformio.ini`.
  - Nếu nạp không vào: giữ nút **BOOT**, nhấn **RST**, rồi thả BOOT để vào chế độ download.
- Thư viện OLED (đã khai báo trong `lib_deps`): `adafruit/Adafruit SSD1306` + `Adafruit GFX`.
- Khi khởi động, `main.cpp` in thông tin chip, Flash, PSRAM ra Serial (kỳ vọng 16 MB / ~8 MB) để xác nhận cấu hình board đúng, rồi mới tạo các task.
- Tiến độ code: Task 1–3 đã viết, build thành công và mô phỏng PASS (11/11/2026), **chưa chạy trên board thật**.

### 2.4 Những điểm FreeRTOS trên ESP32 khác bản gốc (cần nêu trong report)
- ESP32-S3 có **2 lõi Xtensa LX7** (PRO_CPU = core 0, APP_CPU = core 1), chạy FreeRTOS bản SMP do Espressif sửa đổi. Có thể ghim task vào lõi bằng `xTaskCreatePinnedToCore()`.
- **Stack size trong `xTaskCreate` trên ESP32 tính bằng byte** (FreeRTOS gốc tính bằng word).
- Với Arduino-ESP32, `setup()`/`loop()` chạy trong một task tên `loopTask` (core 1, priority 1). Scheduler đã chạy sẵn, **không** gọi `vTaskStartScheduler()`.
- Tick mặc định là 1 ms (1000 Hz). Luôn viết `pdMS_TO_TICKS(ms)` thay vì truyền số tick trần.
- Critical section trên 2 lõi phải dùng spinlock: `portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED; taskENTER_CRITICAL(&mux); ... taskEXIT_CRITICAL(&mux);`

---

## 3. Giải thích yêu cầu từng Task

> Đề ghi "Task hoạt động như Thread": mỗi chức năng là **một task FreeRTOS riêng**, tạo bằng `xTaskCreate`/`xTaskCreatePinnedToCore`, thân task là vòng `for(;;)` và **không bao giờ return**.

### Dữ liệu dùng chung giữa các task (thiết kế trước khi code)

Các biến chung được gộp trong `struct SystemState g_state`, bảo vệ bằng **một mutex** `xStateMutex` (đã có trong code). Report có thể giải thích cách này chống **race condition** (Bài 4–5).

| Trường trong `g_state` / đối tượng | Ghi bởi | Đọc bởi | Bảo vệ |
|---|---|---|---|
| giá trị cảm biến a/s (lux) + `sensorSeq` | Task 2 | Task 6 | `xStateMutex` |
| `led2On` (On/Off) | Task 3 | Task 6 | `xStateMutex` |
| `led3Remain` (số giây đếm ngược của LED3, 0 = Off) — thêm ở Tuần 3 | Task 5 | Task 6 | `xStateMutex` |
| Serial (tài nguyên dùng chung) | mọi task qua `logf()` | — | `xSerialMutex` |
| **Bus I2C** (tài nguyên phần cứng dùng chung) | Task 2 (đọc GY-30), Task 6 (vẽ OLED) | — | `xI2CMutex` |
| `semButton2` (Binary Semaphore) — thêm ở Tuần 3 | Task 4 (give) | Task 5 (take) | Là chính cơ chế đồng bộ |

### Task 1: LED1 nhấp nháy với T = 200 ms
- **Yêu cầu:** LED1 nhấp nháy đều với chu kỳ T = 200 ms.
- **Cách hiểu:** chu kỳ đầy đủ 200 ms tức là 100 ms sáng + 100 ms tắt (đảo trạng thái mỗi 100 ms). *Nếu thầy hiểu là "đảo mỗi 200 ms" thì chỉ cần sửa một hằng số. Nên hỏi lại để chắc.*
- **Kỹ thuật thể hiện:** task tuần hoàn. Nên dùng `vTaskDelayUntil()` (hoặc `xTaskDelayUntil()`) để chu kỳ **không bị trôi**, rồi so sánh với `vTaskDelay()` (tính delay tương đối từ lúc gọi, nên chu kỳ thực tế = thời gian xử lý + delay).
- **Minh chứng:** video LED1, log timestamp (`xTaskGetTickCount()` hoặc `millis()`) mỗi lần đảo để chứng minh chu kỳ ≈ 100/200 ms.

### Task 2: Đọc cảm biến, lưu vào biến toàn cục (500 ms/lần)
- **Yêu cầu:** cứ 500 ms đọc cảm biến ánh sáng một lần và lưu vào biến toàn cục.
- **Kỹ thuật thể hiện:** task tuần hoàn 500 ms, **chia sẻ dữ liệu qua biến toàn cục** và phải bảo vệ khi ghi (mutex) vì Task 6 đọc cùng lúc. Có thể lọc nhiễu (trung bình vài mẫu).
- **Cảm biến GY-30 (BH1750):** đọc qua I2C bằng thư viện `claws/BH1750`, kết quả là **lux** (càng sáng càng lớn). Ở chế độ đo liên tục độ phân giải cao, mỗi lần đo mất khoảng 120 ms, vẫn nằm gọn trong chu kỳ 500 ms.
- **Kỹ thuật thêm:** bus I2C dùng chung với OLED (Task 6), nên mỗi lần đọc phải giữ `xI2CMutex`. Đây là ví dụ **mutex bảo vệ tài nguyên phần cứng** (Bài 5) để trình bày trong report, khác với mutex bảo vệ biến chung `g_state`.
- **Minh chứng:** log Serial in giá trị theo thời gian; che/chiếu sáng cảm biến thì giá trị thay đổi.

### Task 3: Button1 đảo trạng thái LED2 (loop)
- **Yêu cầu:** mỗi lần nhấn Button1 thì LED2 đổi trạng thái (On↔Off). "(loop)" nghĩa là task **liên tục kiểm tra (polling)** nút trong vòng lặp.
- **Kỹ thuật thể hiện:**
  - Polling có `vTaskDelay(pdMS_TO_TICKS(10–20))` trong vòng lặp. Nếu không delay, task chiếm CPU và làm các task ưu tiên thấp hơn bị **đói (starvation)**.
  - **Chống dội phím (debounce)** và **bắt cạnh nhấn** (chỉ đảo khi chuyển từ nhả sang nhấn), để giữ nút không làm LED nhấp nháy liên tục.
  - Cập nhật `g_state.led2On` cho Task 6.
- **Minh chứng:** video nhấn nút; log "BTN1 pressed → LED2 = ON/OFF".

### Task 4: Nhấn Button2 để "cấp" Binary Semaphore
- **Yêu cầu:** khi nhấn Button2 thì gọi `xSemaphoreGive(semButton2)`.
- **Kỹ thuật thể hiện:** **đồng bộ sự kiện** giữa 2 task bằng Binary Semaphore (task "báo hiệu" → task "chờ").
  - Mức cơ bản: task polling Button2 (giống Task 3) rồi `xSemaphoreGive()`.
  - Mức nâng cao (điểm cộng, nên làm ở Tuần 3–4): gắn **ngắt GPIO** cho Button2, trong ISR gọi `xSemaphoreGiveFromISR()` + `portYIELD_FROM_ISR()`. Đây là mô hình *deferred interrupt processing*. Khi đó report so sánh được polling (Task 3) với interrupt-driven (Task 4).
  - **Giữ đúng đề:** đề liệt kê Task 4 là **một task**. Vì vậy hãy nộp bản cơ bản (Task 4 là task polling) ở Tuần 3. Bản dùng ngắt đưa vào phần mở rộng (mục C), hoặc thiết kế để ISR chỉ đánh thức Task 4 (bằng notification), còn Task 4 vẫn là task gọi `xSemaphoreGive()`. Trong report phải ghi rõ đã chọn cách nào.
- **Lưu ý:** `xSemaphoreCreateBinary()` tạo semaphore ở trạng thái **rỗng**, nên Task 5 sẽ chờ cho đến lần nhấn đầu tiên. Đây là đúng ý đồ.
- **Minh chứng:** log "BTN2 → give semaphore" xuất hiện ngay trước log của Task 5.

### Task 5: Đợi nhận Binary Semaphore, khi có thì bật LED3 3 s rồi tắt, đợi tiếp
- **Yêu cầu:** task chặn ở `xSemaphoreTake(semButton2, portMAX_DELAY)`. Khi nhận được: LED3 sáng 3 giây, sau đó tắt rồi quay lại chờ.
- **Kỹ thuật thể hiện:**
  - Trạng thái **Blocked** của task: trong lúc chờ, task không tốn CPU. Đây là điểm mấu chốt cần giải thích khi so với polling.
  - Đếm ngược: chia 3 s thành 3 bước 1 s (hoặc 6 bước 500 ms), mỗi bước cập nhật `g_state.led3Remain` để Task 6 hiển thị "3 → 2 → 1 → Off".
- **Hành vi cần quan sát và giải thích trong report:** nếu nhấn Button2 **trong lúc** LED3 đang sáng thì sao? Binary semaphore chỉ giữ tối đa 1 "token", nên nhấn nhiều lần chỉ được ghi nhận 1 lần, và LED3 sẽ sáng thêm đúng 1 lượt nữa. (So sánh với Counting Semaphore nếu làm phần mở rộng.)
- **Minh chứng:** video nhấn Button2 → LED3 sáng 3 s; log có timestamp lúc bật/tắt.

### Task 6: Hiển thị lên OLED (500 ms/lần)
- **Yêu cầu:** mỗi 500 ms cập nhật OLED với 3 thông tin:
  1. Trạng thái LED2: `LED2: ON` / `LED2: OFF`
  2. Giá trị cảm biến ánh sáng
  3. LED3: số giây còn lại khi đang đếm ngược, `OFF` khi đã tắt
- **Kỹ thuật thể hiện:** task **tiêu thụ dữ liệu** từ nhiều task khác. Đọc biến chung trong vùng mutex (copy ra biến cục bộ rồi nhả mutex ngay, **không** vẽ OLED trong lúc giữ mutex). Đây là task chậm nhất nên đặt **priority thấp nhất**.
- Bus I2C dùng chung với GY-30 (Task 2), nên phải giữ `xI2CMutex` trong lúc gửi dữ liệu ra OLED (`display.display()`). Thứ tự khoá cố định: không bao giờ giữ `xStateMutex` và `xI2CMutex` cùng lúc. Copy dữ liệu xong, nhả `xStateMutex` rồi mới lấy `xI2CMutex`, để tránh deadlock.
- **Minh chứng:** ảnh/video OLED khi thay đổi từng thông số.

### Task 7+: Mở rộng ("More… thêm nhiều kỹ thuật mà FreeRTOS cung cấp, thêm ngoại vi nếu muốn")
Tuần 3 phải **đề xuất**, Tuần 4 phải **báo cáo đã áp dụng được**. Nên chọn **3–5 mục**, mỗi mục là một kỹ thuật FreeRTOS khác nhau (ưu tiên các mục gắn với lý thuyết Bài 3–6):

| # | Kỹ thuật | Ý tưởng áp dụng | Gắn với lý thuyết |
|---|---|---|---|
| A | **Queue** | Task 2 gửi giá trị cảm biến qua queue (hoặc `xQueueOverwrite` kiểu mailbox) thay cho biến toàn cục | IPC – message passing (Bài 4) |
| B | **Mutex + Priority Inheritance** | Bảo vệ Serial/I2C dùng chung; dựng thí nghiệm đảo ưu tiên với 3 task L/M/H, so sánh khi dùng Binary Semaphore và khi dùng Mutex | Đồng bộ, priority inversion (Bài 5) |
| C | **ISR + FromISR API** | Button2 dùng ngắt (xem Task 4) | Ngắt, deferred processing |
| D | **Task Notification** | Thay binary semaphore Task 4→5 bằng `xTaskNotifyGive`/`ulTaskNotifyTake`, so sánh RAM/tốc độ | Đồng bộ nhẹ |
| E | **Software Timer** | Làm LED1 bằng `xTimerCreate` thay vì task, hoặc dùng one-shot timer để tắt LED3 sau 3 s | Timer service task |
| F | **Event Group** | Chờ nhiều sự kiện cùng lúc (ví dụ trời tối **và** nhấn nút thì bật LED) | Đồng bộ nhiều điều kiện |
| G | **Counting Semaphore** | Đếm số lần nhấn Button2 để LED3 sáng đủ số lượt | So sánh với binary |
| H | **Dual-core** | Ghim task hiển thị vào core 0, các task điều khiển vào core 1 | Lập lịch đa xử lý (Bài 3) |
| I | **Giám sát hệ thống** | `uxTaskGetStackHighWaterMark()`, `xPortGetFreeHeapSize()`; `vTaskList()`/run-time stats nếu bật được cấu hình | Quản lý bộ nhớ (Bài 6), trạng thái task |
| J | **Suspend/Resume, thay đổi priority** | Nút giữ lâu để tạm dừng/tiếp tục Task 1; `vTaskPrioritySet` | Trạng thái task (Bài 2–3) |
| K | **Watchdog / Stack overflow hook** | Phát hiện task treo/tràn stack | Độ tin cậy RTOS |

### Gợi ý priority, stack và core ban đầu (chỉnh lại khi chạy thực tế)

| Task | Priority | Stack (byte) | Lý do |
|---|---|---|---|
| Task 5 (LED3 / chờ semaphore) | 3 | 2048 | Phản hồi sự kiện, cần phản ứng ngay |
| Task 4 (Button2 → give) | 3 | 2048 | Bắt sự kiện người dùng |
| Task 3 (Button1 → LED2) | 2 | 2048 | Polling ngắn |
| Task 1 (LED1 blink) | 2 | 2048 | Tuần hoàn, cần đúng nhịp |
| Task 2 (Sensor) | 2 | 2048 | Tuần hoàn 500 ms |
| Task 6 (OLED) | 1 | 4096 | Chậm, tốn stack do thư viện đồ họa nên đặt thấp nhất |

→ Report cần **giải thích lý do** chọn priority (ví dụ: lập lịch preemptive theo độ ưu tiên; task cùng priority chia thời gian theo time-slicing).

---

## 4. Kế hoạch làm việc theo tuần

> Giả định: buổi học hàng tuần vào **thứ Ba**, Tuần 1 = 10/11/2026. Nếu lịch khác thì dời ngày cho đúng.

| Tuần | Ngày báo cáo | Cần đạt (theo đề) | Việc cần làm | Slide báo cáo |
|---|---|---|---|---|
| **1** | 10/11 | Tự nghiên cứu | Mua linh kiện (mục 2.1); cài VS Code + PlatformIO; chạy thử blink; đọc tài liệu FreeRTOS (task, scheduler, delay); xem video chương 3 | — |
| **2** | 17/11 | **Mua đồ, cài đặt, chạy được Task 1, 2, 3** (có thể mượn phần cứng của bạn) | Lắp mạch; code Task 1–3; thiết kế `SystemState` + mutex; quay video, lưu log; bắt đầu ghi `docs/` | `slides/Tuan2_*.pptx`: kết quả đạt được + kế hoạch tuần 3 |
| **3** | 24/11 | **Chạy được Task 4, 5, 6 + đề xuất task/kỹ thuật khác** | Code Task 4–6 (OLED); test các tình huống (nhấn liên tục, che cảm biến); chọn 3–5 kỹ thuật ở mục Task 7+; bắt đầu viết chương "Tìm hiểu FreeRTOS" | `slides/Tuan3_*.pptx`: kết quả + danh sách kỹ thuật đề xuất + kế hoạch tuần 4 |
| **4** | 01/12 | **Báo cáo các task/kỹ thuật mới đã áp dụng được** | Hoàn thành các kỹ thuật mở rộng; đóng băng code; thu đủ minh chứng; viết gần xong report | `slides/Tuan4_*.pptx`: kết quả đạt được |
| **5** | 08/12 | **Nộp cuốn Report** | Hoàn thiện, rà minh chứng, xuất PDF/Word | — |

Song song mỗi tuần còn có kiểm tra lý thuyết cuối buổi (Tuần 2: chương 3 · Tuần 3: chương 4, 5 · Tuần 4: chương 6). Lý thuyết các chương này cũng là nguồn cho chương "Tìm hiểu FreeRTOS" của report.

**Mỗi slide tiến độ nên có:** (1) mục tiêu tuần, (2) kết quả đạt được + ảnh/video minh chứng, (3) đoạn code chính và giải thích ngắn, (4) khó khăn/lỗi gặp phải và cách xử lý, (5) kế hoạch tuần sau. Đề yêu cầu **"Cần hiểu"**, nên phải giải thích được từng dòng code mình trình bày.

---

## 5. Yêu cầu Report và cách viết

### 5.1 Yêu cầu của đề
1. **Tìm hiểu FreeRTOS:** kiến trúc, đặc điểm, nguyên lý, thành phần, chiến lược…
2. **Phân tích chương trình:** các kỹ thuật đã dùng (nhiều task, giao tiếp, đồng bộ…), ý nghĩa các lệnh/hàm (`xTaskCreate`, `vTaskDelay`, `vTaskDelayUntil`, `xSemaphoreTake`…).
3. **Mỗi đặc điểm/kiến thức phải kèm minh chứng** (chỉ ra đoạn code, mô tả).
4. Viết theo **dạng tài liệu kỹ thuật**: bố cục hợp lý, **ngắn gọn, xúc tích, không lan man**.

### 5.2 Bố cục đề xuất

```
1. Giới thiệu
   1.1 Mục tiêu đề tài   1.2 Phạm vi   1.3 Công cụ & phần cứng
2. Tìm hiểu FreeRTOS
   2.1 Kiến trúc (kernel, port layer, cấu hình FreeRTOSConfig / sdkconfig)
   2.2 Đặc điểm (preemptive, deterministic, nhỏ gọn, mã nguồn mở…)
   2.3 Nguyên lý: tick, scheduler (preemptive priority + time slicing), trạng thái task
       (Running/Ready/Blocked/Suspended), context switch, idle task
   2.4 Thành phần: Task, Queue, Semaphore (binary/counting), Mutex, Task Notification,
       Software Timer, Event Group, quản lý bộ nhớ (heap_1..5)
   2.5 Chiến lược: lập lịch, cấp phát bộ nhớ, xử lý ngắt (FromISR, deferred interrupt)
   2.6 FreeRTOS trên ESP32 (SMP 2 lõi, stack tính theo byte, pinned-to-core, loopTask)
3. Thiết kế hệ thống
   3.1 Sơ đồ khối phần cứng + bảng chân GPIO
   3.2 Kiến trúc phần mềm: sơ đồ task, priority, stack, core
   3.3 Sơ đồ giao tiếp/đồng bộ giữa các task (biến chung + mutex, semaphore, queue…)
4. Phân tích chương trình (mỗi task một mục, theo mẫu ở 5.3)
   4.1 Task 1 … 4.6 Task 6
5. Các kỹ thuật mở rộng (mỗi kỹ thuật một mục, theo mẫu ở 5.3)
6. Kiểm thử & đánh giá
   Bảng test case (đầu vào → kỳ vọng → thực tế → đạt/không), đo chu kỳ thực tế,
   stack high water mark, các lỗi đã gặp và cách sửa
7. Kết luận & hướng phát triển
Tài liệu tham khảo
Phụ lục: mã nguồn đầy đủ (hoặc link repo)
```

### 5.3 Mẫu viết một mục "có minh chứng" (dùng cho chương 4, 5)

> **Kỹ thuật:** Đồng bộ sự kiện bằng Binary Semaphore
> **Lý thuyết (2–3 câu):** Binary semaphore có 2 trạng thái (rỗng/có token)… task gọi Take khi rỗng sẽ chuyển sang Blocked…
> **Minh chứng code:** `firmware/src/main.cpp`, dòng xx–yy
> ```c
> if (xSemaphoreTake(semButton2, portMAX_DELAY) == pdTRUE) { ... }
> ```
> **Giải thích:** `portMAX_DELAY` → chờ vô hạn, task không chiếm CPU khi chờ…
> **Minh chứng chạy thực tế:** Hình x (OLED đếm ngược), log `evidence/logs/T5_...txt`:
> `[12034 ms] BTN2 -> give` / `[12035 ms] T5 take -> LED3 ON` / `[15036 ms] LED3 OFF`
> **Nhận xét:** độ trễ từ lúc give đến lúc take ≈ 1 ms vì Task 5 có priority cao hơn…

Nguyên tắc: **lý thuyết ngắn → code → giải thích → kết quả chạy thật**. Không chép lý thuyết dài; với mỗi khái niệm đã viết ở chương 2, phải chỉ ra được nó xuất hiện ở đâu trong code.

### 5.4 Checklist trước khi nộp
- [ ] Mỗi task (1–6) và mỗi kỹ thuật mở rộng đều có: code trích dẫn + giải thích API + ảnh/log/video.
- [ ] Có sơ đồ task/IPC và bảng priority kèm lý do.
- [ ] Giải thích được `vTaskDelay` và `vTaskDelayUntil`, polling và blocking, mutex và binary semaphore.
- [ ] Không có đoạn lý thuyết nào "treo" (không gắn với code).
- [ ] Hình có số thứ tự + chú thích; code có số dòng/tên file.
- [ ] Bố cục gọn, không lan man; có tài liệu tham khảo (FreeRTOS.org docs, ESP-IDF Programming Guide, *Mastering the FreeRTOS Real Time Kernel*).

---

## 6. Quy ước làm việc

1. **Mọi file của project cá nhân nằm trong folder này** (code, log, ảnh, slide, report). Không để rải rác ở folder khác.
2. **Code:** một task một hàm (`vTask1_Blink`, `vTask2_Sensor`, …); hằng số chân/chu kỳ để ở đầu file hoặc trong `include/config.h`; comment ngắn ghi rõ "kỹ thuật nào", để sau này trích vào report.
3. **Log Serial** luôn kèm timestamp và tên task, ví dụ `[%lu ms][T2] light=%d`, vì đây là minh chứng chính.
4. **Sau mỗi buổi làm:** lưu minh chứng vào `evidence/`, ghi 3–5 dòng vào `docs/nhat_ky.md` (đã làm gì, lỗi gì, sửa thế nào). Đây là nguyên liệu cho slide và chương "Kiểm thử".
5. Nên dùng **git** cho `firmware/` để quay lại được bản chạy ổn định của từng tuần (tag `tuan2`, `tuan3`, `tuan4`).
