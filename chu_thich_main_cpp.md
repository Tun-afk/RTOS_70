# Chú thích cho `firmware/src/main.cpp`

> `main.cpp` không chứa comment. Mọi giải thích nằm ở file này.
> Số dòng ứng với bản code ngày 12/11/2026 (đủ 6 task). Khi sửa code, cập nhật lại file này.
> Thông số (chân, chu kỳ, priority, stack, core) nằm ở `firmware/include/config.h`; logic thuần nằm ở `firmware/lib/AppLogic/app_logic.h`.

## Tổng quan

| Task | Hàm | Việc làm | Kỹ thuật FreeRTOS | Priority |
|---|---|---|---|---|
| 1 | `vTask1_Blink` | LED1 nhấp nháy, T = 200 ms | Task tuần hoàn bằng `vTaskDelayUntil` | 2 |
| 2 | `vTask2_Sensor` | Đọc GY-30 (BH1750) 500 ms/lần, lưu vào `g_state` | Task tuần hoàn + mutex bảo vệ bus I2C + mutex bảo vệ biến chung | 2 |
| 3 | `vTask3_Button1` | Button1 đảo LED2 | Polling + `vTaskDelay` + chống dội | 2 |
| 4 | `vTask4_Button2` | Nhấn Button2 thì cấp binary semaphore | Polling + `xSemaphoreGive` (task báo hiệu) | 3 |
| 5 | `vTask5_Led3` | Chờ semaphore, bật LED3 3 s rồi tắt, chờ tiếp | `xSemaphoreTake(portMAX_DELAY)`: task ở trạng thái Blocked khi chờ | 3 |
| 6 | `vTask6_Display` | Hiển thị LED2, ánh sáng, LED3 lên OLED 500 ms/lần | Task tiêu thụ dữ liệu: copy trong mutex rồi mới vẽ | 1 |

**Vì sao chọn priority như vậy:** scheduler của FreeRTOS là preemptive theo độ ưu tiên; task cùng priority chia thời gian (time slicing). Task 4 và 5 phản hồi thao tác của người dùng nên đặt cao nhất (3). Task 1–3 chạy định kỳ ngắn (2). Task 6 chậm nhất (gửi cả khung hình qua I2C) và không gấp, nên đặt thấp nhất (1) để không làm trễ các task khác.

---

## 1. Dữ liệu dùng chung và đối tượng đồng bộ (dòng 10–26)

**`struct SystemState` / `g_state` (dòng 10–16).** Biến toàn cục chứa dữ liệu mà nhiều task cùng dùng:

| Trường | Ý nghĩa | Ghi bởi | Đọc bởi |
|---|---|---|---|
| `light.lux` | Số đo ánh sáng hợp lệ gần nhất (lux) | Task 2 | Task 6 |
| `light.valid` | Lần đọc gần nhất có hợp lệ không | Task 2 | Task 6 |
| `light.seq` | Tổng số lần Task 2 đã đọc | Task 2 | — |
| `light.errCount` | Số lần đọc lỗi | Task 2 | — |
| `led2On` | Trạng thái LED2 | Task 3 | Task 6 |
| `led3Remain` | Số giây LED3 còn sáng, 0 = đã tắt | Task 5 | Task 6 |

**Ba mutex và một binary semaphore (dòng 17–20):**

| Đối tượng | Dùng để | Loại |
|---|---|---|
| `xStateMutex` | Bảo vệ biến `g_state` | Mutex bảo vệ dữ liệu chung |
| `xSerialMutex` | Bảo vệ cổng Serial | Mutex bảo vệ tài nguyên phần cứng |
| `xI2CMutex` | Bảo vệ bus I2C (GY-30 + OLED) | Mutex bảo vệ tài nguyên phần cứng |
| `semButton2` | Task 4 báo sự kiện "đã nhấn Button2" cho Task 5 | Binary semaphore, dùng để **đồng bộ sự kiện** |

- **Mutex khác binary semaphore ở đâu:** mutex dùng để *loại trừ lẫn nhau*: task nào lấy thì chính task đó phải nhả. Binary semaphore dùng để *báo hiệu*: Task 4 cấp (give), Task 5 nhận (take), hai task khác nhau.
- ESP32-S3 có 2 lõi và scheduler là preemptive, nên một task có thể bị ngắt giữa lúc đang ghi. Không có mutex thì task khác đọc phải dữ liệu "rách" (race condition).
- Quy tắc: chỉ đọc/ghi `g_state` khi đang giữ `xStateMutex`, và giữ càng ngắn càng tốt.
- **Không bao giờ giữ `xStateMutex` và `xI2CMutex` cùng lúc.** Nhả cái này rồi mới lấy cái kia, nên không thể xảy ra deadlock.

**Các đối tượng thiết bị (dòng 22–26).** `g_lightMeter` (BH1750) và `g_display` (OLED SSD1306), kèm hai cờ `g_bh1750Ready`, `g_oledReady`. Hai cờ chỉ được ghi một lần trong `setup()` trước khi tạo task, sau đó các task chỉ đọc, nên không cần mutex.

## 2. `logf()` — log có timestamp, an toàn đa task (dòng 28–39)

- In một dòng dạng `[  12345 ms][T2] noi dung`.
- Kỹ thuật: **mutex bảo vệ tài nguyên dùng chung** (cổng Serial), để dòng log của các task không bị trộn vào nhau.
- Chuỗi được định dạng bằng `vsnprintf` **trước** khi lấy mutex, nên mutex chỉ bị giữ trong lúc in.
- Chờ mutex tối đa `LOG_MUTEX_WAIT_MS` (50 ms). Quá hạn thì bỏ dòng log đó, task không bị treo vì log.
- Trong task, mọi lệnh in đều phải đi qua `logf`.

## 3. Task 1 — `vTask1_Blink` (dòng 41–62)

- **Yêu cầu:** LED1 nhấp nháy với chu kỳ T = 200 ms (100 ms sáng + 100 ms tắt).
- **Kỹ thuật:** task tuần hoàn bằng `vTaskDelayUntil` (dòng 60). Thời điểm thức dậy được tính từ mốc tuyệt đối `xLastWake`, nên chu kỳ **không bị trôi**.
  - Khác với `vTaskDelay`: hàm đó tính từ lúc gọi, nên thời gian xử lý của mỗi vòng bị cộng dồn vào chu kỳ. Mô phỏng cho thấy sau 300 s lịch đảo đèn chậm khoảng 16 s nếu dùng `vTaskDelay`.
- `xHalfPeriod` (dòng 43): nửa chu kỳ = 100 ms, đổi sang tick bằng `pdMS_TO_TICKS`.
- **Minh chứng chu kỳ** (dòng 53–59): cứ `T1_LOG_EVERY_N` = 10 lần đảo thì in thời gian đã trôi, kỳ vọng 10 × 100 = 1000 ms.
- `xLogMark = xLastWake - xHalfPeriod` (dòng 45): mốc log đặt tại "lần đảo thứ 0", tức trước lần đảo đầu tiên nửa chu kỳ. Nhờ vậy dòng log đầu tiên cũng báo 1000 ms; nếu đặt mốc trùng lần đảo đầu thì dòng đầu báo 900 ms.

## 4. Task 2 — `vTask2_Sensor` (dòng 64–94)

- **Yêu cầu:** cứ 500 ms đọc cảm biến ánh sáng một lần, lưu vào biến toàn cục.
- **Phần cứng:** module GY-30 (BH1750) nối bus I2C, SDA = GPIO8, SCL = GPIO9, địa chỉ 0x23, **dùng chung bus với OLED**. Kết quả là lux, càng sáng càng lớn.
- **Kỹ thuật:** task tuần hoàn (`vTaskDelayUntil`, dòng 92) + **hai loại mutex**:
  1. `xI2CMutex` (dòng 75–77) là **mutex bảo vệ tài nguyên phần cứng**. Một giao dịch I2C gồm nhiều bước (gửi địa chỉ, nhận 2 byte). Nếu Task 6 chen vào giữa để vẽ OLED, hai giao dịch trộn vào nhau và cả hai cùng hỏng. Mutex đảm bảo mỗi lúc chỉ một task dùng bus.
  2. `xStateMutex` (dòng 80–83) là **mutex bảo vệ biến chung**. Nó giữ cho `lux`, `valid`, `seq`, `errCount` luôn khớp nhau khi Task 6 đọc.
  - Điểm khác nhau: cái thứ nhất tuần tự hoá việc dùng một **thiết bị**, cái thứ hai giữ tính nhất quán của **dữ liệu trong bộ nhớ**.
- **Thứ tự khoá:** lấy `xI2CMutex` → đọc → nhả, rồi mới lấy `xStateMutex` → ghi → nhả. Hai mutex không bao giờ bị giữ cùng lúc.
- **Chờ lần đo đầu** (dòng 68): ở chế độ High-Res, BH1750 cần tối đa `BH1750_MEAS_TIME_MS` = 180 ms cho một lần đo. Task chờ hết khoảng này rồi mới đọc lần đầu. Chu kỳ 500 ms lớn hơn 180 ms nên mỗi lần đọc sau đó đều là số đo mới.
- **Xử lý lỗi (không treo hệ thống):**
  - `readLightLevel()` trả số âm khi lỗi: `-1` = đọc I2C thất bại.
  - Nếu BH1750 không khởi tạo được (`g_bh1750Ready == false`), task **không gọi thư viện** và dùng mã `BH1750_ERR_NOT_READY` (-2). Lý do: khi chưa khởi tạo, thư viện tự in thẳng ra Serial, không qua `xSerialMutex`, làm trộn log.
  - `app::lightUpdate()` (dòng 81): số đo hợp lệ (0 … 54612,5 lx) thì cập nhật `lux` và bật `valid`. Lỗi thì **giữ nguyên `lux` cũ**, hạ `valid`, tăng `errCount`.
- **Copy rồi nhả** (dòng 82): chép `g_state.light` ra biến cục bộ `light` trong vùng mutex, nhả mutex, rồi mới gọi `logf`. Không gọi `logf` khi đang giữ mutex.
- **Log:** `Lan doc N: anh sang = X lx` khi đọc được; `Lan doc N: LOI doc BH1750 (ma ..., tong ... lan loi)` khi lỗi.

## 5. Task 3 — `vTask3_Button1` (dòng 96–116)

- **Yêu cầu:** mỗi lần nhấn Button1 thì LED2 đổi trạng thái.
- **Kỹ thuật:** polling trong vòng lặp + `vTaskDelay(BTN_POLL_MS)` (dòng 114). Giữa hai lần quét, task ở trạng thái Blocked và nhường CPU, nên không làm task khác bị đói (starvation).
- Nút dùng `INPUT_PULLUP`: nhấn = mức LOW (dòng 103).
- **Chống dội + bắt cạnh nhấn** (`app::debounceUpdate`, dòng 104): mức đọc phải giữ nguyên `BTN_DEBOUNCE_COUNT` = 3 lần quét liên tiếp (60 ms) mới được chấp nhận. Hàm trả `true` đúng một lần khi nút vừa chuyển từ nhả sang nhấn. Giữ nút lâu không đảo thêm.
- `debounceInit` (dòng 99) lấy trạng thái nút lúc khởi động, nên nếu đang giữ nút khi bật nguồn thì không sinh sự kiện giả.
- `g_state.led2On` được ghi trong vùng `xStateMutex` (dòng 108–110); `logf` gọi sau khi đã nhả mutex.

## 6. Task 4 — `vTask4_Button2` (dòng 118–134)

- **Yêu cầu:** nhấn Button2 để "cấp" binary semaphore.
- **Kỹ thuật:** **đồng bộ sự kiện giữa hai task bằng binary semaphore.** Task 4 là task "báo hiệu": mỗi lần nhấn (đã chống dội, dòng 125) gọi `xSemaphoreGive(semButton2)` (dòng 126).
- Cách phát hiện nhấn giống Task 3: polling mỗi `BTN_POLL_MS` + chống dội + bắt cạnh nhấn. Đây là bản cơ bản theo đúng đề (Task 4 là một task). Bản dùng ngắt GPIO + `xSemaphoreGiveFromISR` để dành cho phần mở rộng Tuần 4.
- **Giá trị trả về của `xSemaphoreGive`:**
  - `pdTRUE`: semaphore đang rỗng, token được đặt vào. Log `Nhan BTN2 -> cap semaphore`.
  - `pdFALSE`: semaphore đã có sẵn một token chưa được Task 5 lấy. Binary semaphore chỉ giữ tối đa **1 token**, nên lần nhấn này không được tính thêm. Log ghi rõ điều đó.
- `xSemaphoreGive` không bao giờ chặn task gọi nó.

## 7. Task 5 — `vTask5_Led3` (dòng 136–166)

- **Yêu cầu:** đợi nhận binary semaphore; khi có thì bật LED3 3 s rồi tắt, đợi tiếp.
- **Kỹ thuật:** `xSemaphoreTake(semButton2, portMAX_DELAY)` (dòng 141). Khi chưa có token, task ở trạng thái **Blocked** và **không tốn CPU**; scheduler chỉ đánh thức nó khi Task 4 cấp semaphore. Đây là điểm khác căn bản so với polling ở Task 3/4, nơi task phải thức dậy mỗi 20 ms để kiểm tra.
- `xSemaphoreCreateBinary()` tạo semaphore ở trạng thái **rỗng**, nên Task 5 chờ cho tới lần nhấn đầu tiên.
- **Đếm ngược** (dòng 148–154): chia 3 s thành `LED3_ON_TIME_S` = 3 bước, mỗi bước `LED3_STEP_MS` = 1000 ms. Đầu mỗi bước ghi `g_state.led3Remain` = 3, 2, 1 (trong `xStateMutex`) để Task 6 hiển thị. Hết 3 bước thì tắt LED3 và ghi `led3Remain = 0` (dòng 156–160).
- Dùng `vTaskDelayUntil` với mốc `xLastWake` lấy ngay lúc nhận semaphore (dòng 143, 153), nên tổng thời gian sáng đúng 3000 ms, không cộng dồn thời gian xử lý. Log lúc tắt in thời gian đo được để làm minh chứng.
- **Nhấn Button2 khi LED3 đang sáng thì sao?** Lần nhấn đầu tiên trong lúc đó đặt 1 token vào semaphore (Task 5 đang bận, chưa lấy). Các lần nhấn sau đó bị bỏ qua vì semaphore đã đầy. Khi LED3 tắt, Task 5 quay lại `xSemaphoreTake`, thấy token và sáng thêm **đúng 1 lượt** nữa. Muốn đếm đủ số lần nhấn thì phải dùng counting semaphore (phần mở rộng).

## 8. Task 6 — `vTask6_Display` (dòng 168–211)

- **Yêu cầu:** mỗi 500 ms hiển thị lên OLED: trạng thái LED2, giá trị cảm biến ánh sáng, LED3 (số giây còn lại hoặc OFF).
- **Kỹ thuật:** task **tiêu thụ dữ liệu** từ Task 2, 3, 5. Mỗi chu kỳ gồm 4 bước:
  1. **Chụp dữ liệu** (dòng 178–180): lấy `xStateMutex`, copy cả `g_state` ra biến cục bộ `snapshot`, nhả ngay. Mutex chỉ bị giữ trong lúc copy vài byte.
  2. **Tạo 3 dòng chữ** (dòng 182–184) bằng các hàm thuần trong `app_logic.h`: `LED2: ON/OFF`, `LX:123.4` (hoặc `LX: ---` khi số đo không hợp lệ), `LED3: 3s` / `LED3: OFF`. Mỗi dòng tối đa 10 ký tự, vừa chiều ngang màn hình ở cỡ chữ 2.
  3. **Vẽ vào bộ đệm trong RAM** (dòng 187–197): `clearDisplay`, `setCursor`, `print` chỉ ghi vào bộ nhớ, chưa đụng tới bus I2C nên chưa cần mutex.
  4. **Gửi ra màn hình** (dòng 199–201): chỉ lúc gọi `display()` mới truyền dữ liệu qua I2C, nên chỉ đoạn này nằm trong `xI2CMutex`.
- **Thứ tự khoá:** nhả `xStateMutex` xong mới lấy `xI2CMutex`. Không vẽ OLED khi đang giữ `xStateMutex`: việc gửi một khung hình qua I2C mất hàng chục ms, giữ mutex lâu như vậy sẽ chặn Task 2, 3, 5.
- Task tuần hoàn bằng `vTaskDelayUntil` (dòng 209), priority thấp nhất.
- Nếu không tìm thấy OLED (`g_oledReady == false`), task bỏ qua phần vẽ nhưng vẫn chạy và ghi log, các task khác không bị ảnh hưởng.
- **Log** (dòng 204–208): cứ `T6_LOG_EVERY_N` = 4 lần vẽ (2 s) in một dòng chứa đúng nội dung đang hiển thị, để đối chiếu với ảnh chụp màn hình.

## 9. `createTaskOrHalt()` (dòng 213–219)

- Tạo task bằng `xTaskCreatePinnedToCore`, ghim vào `APP_CORE` (core 1).
- Stack trên ESP32 tính bằng **byte**.
- Nếu tạo thất bại (thiếu heap): báo lỗi qua `logf` rồi dừng trong vòng lặp.
- Hàm chỉ được gọi từ `setup()`, nên được phép dùng `delay()`.

## 10. `setup()` (dòng 221–282)

Thứ tự khởi tạo và lý do:

1. **Serial + thông tin board** (dòng 222–232): `delay(SERIAL_WAIT_MS)` (3000 ms) để Serial monitor kịp kết nối trước khi in. Với cổng USB native, monitor cần gần 2 s sau khi board reset mới nối lại được; chờ 1,5 s thì mất các dòng khởi động. In chip, Flash, PSRAM để xác nhận cấu hình N16R8 (kỳ vọng 16 MB / ~8 MB).
2. **GPIO** (dòng 234–241): 3 LED là OUTPUT và tắt sẵn; 2 nút là `INPUT_PULLUP`.
3. **Tạo 3 mutex + 1 binary semaphore và kiểm tra khác `NULL`** (dòng 243–250). Đối tượng đồng bộ phải được tạo **trước** khi tạo task dùng nó.
4. **Bus I2C** (dòng 252): `Wire.begin(SDA, SCL)`.
5. **BH1750** (dòng 254–260): chế độ `CONTINUOUS_HIGH_RES_MODE` (đo liên tục, độ phân giải 1 lx). Khởi tạo lỗi thì log và **vẫn chạy tiếp**.
6. **OLED** (dòng 262–272): trước hết dò xem có thiết bị trả lời ở địa chỉ 0x3C không (dòng 262–263), vì hàm `begin()` của thư viện không tự phát hiện được màn hình vắng mặt. Có thì khởi tạo và xoá màn hình. Không có thì log và **vẫn chạy tiếp**.
7. **Tạo Task 1–6** (dòng 274–279).

Ở bước 5 và 6 chưa có task nào chạy, nên chưa cần giữ `xI2CMutex`.

## 11. `loop()` (dòng 284–286)

`loop()` chạy trong task `loopTask` của Arduino. Project không dùng nên gọi `vTaskDelete(NULL)` để task tự xoá và nhường CPU.
