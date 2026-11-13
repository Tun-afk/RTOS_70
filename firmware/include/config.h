#pragma once
/* ============================================================
 *  config.h — Sơ đồ chân & thông số chung (ESP32-S3 N16R8)
 *  Khớp với bảng chân mục 2.2 trong ../instruction.md
 *
 *  Ngoại vi              GPIO    Ghi chú
 *  LED1 (Task 1)         4       OUTPUT, HIGH = sáng (qua điện trở 330 Ω)
 *  LED2 (Task 3)         5       OUTPUT
 *  LED3 (Task 5)         6       OUTPUT
 *  Button1 (Task 3)      15      INPUT_PULLUP, nhấn = LOW
 *  Button2 (Task 4)      16      INPUT_PULLUP, nhấn = LOW
 *  Bus I2C SDA / SCL     8 / 9   DÙNG CHUNG cho OLED và GY-30 (bảo vệ bằng xI2CMutex)
 *  OLED SSD1306          (I2C)   địa chỉ 0x3C
 *  GY-30 BH1750 (Task 2) (I2C)   địa chỉ 0x23 (ADDR để trống/nối GND; nối 3V3 -> 0x5C)
 *
 *  KHÔNG dùng các chân sau trên ESP32-S3 N16R8:
 *   - GPIO26..32 : nối SPI Flash
 *   - GPIO33..37 : nối Octal PSRAM (chỉ bản R8 mới bị chiếm)
 *   - GPIO0, 3, 45, 46 : strapping (ảnh hưởng chế độ boot)
 *   - GPIO19, 20 : USB native (D-/D+)
 *   - GPIO43, 44 : UART0 TX/RX (Serial qua cổng COM)
 *   - GPIO48 (hoặc 38) : LED RGB onboard trên nhiều board
 * ============================================================ */

// ---- LED ----
#define PIN_LED1        4    // Task 1: blink T = 200 ms
#define PIN_LED2        5    // Task 3: đảo bởi Button1
#define PIN_LED3        6    // Task 5: sáng 3 s khi nhận semaphore

// ---- Nút nhấn (INPUT_PULLUP, nhấn = LOW) ----
#define PIN_BTN1        15   // Task 3
#define PIN_BTN2        16   // Task 4 (give binary semaphore)

// ---- Bus I2C (dùng chung: OLED + GY-30) ----
#define PIN_I2C_SDA     8
#define PIN_I2C_SCL     9

// ---- OLED SSD1306 I2C ----
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_RESET_PIN  (-1)              // module 4 chân không có chân reset
// Bố cục màn hình (Task 6): 1 dòng tiêu đề cỡ 1 + 3 dòng nội dung cỡ 2
#define OLED_TITLE       "FreeRTOS - 6 Task"
#define OLED_TITLE_SIZE  1                // ký tự 6x8 px
#define OLED_TEXT_SIZE   2                // ký tự 12x16 px -> 10 ký tự / dòng
#define OLED_LINE_CHARS  10               // số ký tự tối đa 1 dòng ở cỡ 2 (128 / 12)
#define OLED_LINE_BUF    (OLED_LINE_CHARS + 1)
#define OLED_TITLE_Y     0
#define OLED_LINE1_Y     16               // LED2
#define OLED_LINE2_Y     32               // ánh sáng
#define OLED_LINE3_Y     48               // LED3

// ---- Cảm biến ánh sáng GY-30 (BH1750, I2C) ----
#define BH1750_ADDR           0x23      // ADDR để trống hoặc nối GND
#define BH1750_MEAS_TIME_MS   180       // thời gian 1 lần đo ở chế độ High-Res (typ 120, max 180 ms)
#define BH1750_LUX_MAX        54612.5f  // lux lớn nhất đo được: 65535 / 1.2
#define BH1750_ERR_NOT_READY  (-2.0f)   // mã lỗi "chưa khởi tạo được" (cùng quy ước với thư viện)

// ---- Thông số thời gian (ms) ----
#define LED1_PERIOD_MS      200   // chu kỳ đầy đủ: 100 ms ON + 100 ms OFF
#define SENSOR_PERIOD_MS    500   // phải lớn hơn BH1750_MEAS_TIME_MS để mỗi lần đọc là số đo mới
#define OLED_PERIOD_MS      500
#define LED3_ON_TIME_S      3     // Task 5: LED3 sáng 3 s mỗi lần nhận semaphore
#define LED3_STEP_MS        1000  // mỗi bước đếm ngược 1 s (3 -> 2 -> 1 -> Off)
#define BTN_POLL_MS         20    // chu kỳ quét nút + chống dội
#define BTN_DEBOUNCE_COUNT  3     // số lần quét ổn định liên tiếp (3 x 20 = 60 ms)

// ---- Serial / log ----
#define SERIAL_BAUD         115200
#define SERIAL_WAIT_MS      3000  // chờ Serial monitor kết nối kịp để không mất dòng khởi động (chỉ dùng trong setup)
#define LOG_BUF_SIZE        128   // byte, bộ đệm 1 dòng log (nằm trên stack task)
#define LOG_MUTEX_WAIT_MS   50    // chờ xSerialMutex tối đa, quá hạn thì bỏ dòng log
#define T1_LOG_EVERY_N      10    // Task 1 in log sau mỗi 10 lần đảo (~1 s)
#define T6_LOG_EVERY_N      4     // Task 6 in log sau mỗi 4 lần vẽ (~2 s)
#define HALT_DELAY_MS       1000  // vòng dừng khi khởi tạo lỗi (chỉ trong setup)

// ---- Cấu hình task: priority / stack (byte) / core ----
#define T1_PRIO   2
#define T2_PRIO   2
#define T3_PRIO   2
#define T4_PRIO   3               // bắt sự kiện người dùng -> ưu tiên cao
#define T5_PRIO   3               // phản hồi sự kiện -> ưu tiên cao
#define T6_PRIO   1               // hiển thị: chậm nhất, không gấp -> ưu tiên thấp nhất
#define T1_STACK  3072            // byte; dư cho vsnprintf trong logf
#define T2_STACK  4096            // lớn hơn vì log số thực (%.1f) + gọi thư viện I2C
#define T3_STACK  3072
#define T4_STACK  3072
#define T5_STACK  3072
#define T6_STACK  4096            // thư viện đồ họa + định dạng số thực
#define APP_CORE  1               // APP_CPU, cùng lõi với loopTask
