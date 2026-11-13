#pragma once
/* ============================================================
 *  app_logic.h — Logic thuần (không phụ thuộc Arduino/FreeRTOS)
 *  Tách khỏi main.cpp để mã của các task ngắn gọn, dễ đọc và dễ kiểm tra.
 * ============================================================ */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

namespace app {

/* ---------- Chống dội phím (Task 3) ----------
 * Task quét nút mỗi BTN_POLL_MS. Mức đọc được chỉ được chấp nhận khi
 * giữ nguyên trong `required` lần quét liên tiếp. Hàm trả về true
 * đúng MỘT lần tại cạnh nhấn (nhả -> nhấn) đã ổn định.
 * Giữ nút lâu không sinh thêm sự kiện; nhả nút không sinh sự kiện. */
struct Debouncer {
    bool    rawLast;      // mức đọc lần trước (true = đang nhấn)
    bool    stable;       // trạng thái đã chống dội
    uint8_t stableCount;  // số lần quét liên tiếp rawLast không đổi
    uint8_t required;     // số lần quét cần để chấp nhận
};

inline void debounceInit(Debouncer &d, uint8_t required, bool pressed = false) {
    d.rawLast     = pressed;
    d.stable      = pressed;
    d.required    = (required == 0) ? 1 : required;
    d.stableCount = d.required;
}

inline bool debounceUpdate(Debouncer &d, bool rawPressed) {
    if (rawPressed != d.rawLast) {
        d.rawLast     = rawPressed;
        d.stableCount = 1;
    } else if (d.stableCount < 255) {
        d.stableCount++;
    }
    if (d.stableCount >= d.required && d.rawLast != d.stable) {
        d.stable = d.rawLast;
        return d.stable;          // true chỉ khi vừa chuyển sang "nhấn"
    }
    return false;
}

/* ---------- Xử lý số đo ánh sáng BH1750 (Task 2) ----------
 * Thư viện trả về lux >= 0 khi đọc được, số âm khi lỗi (-1: đọc I2C
 * thất bại, -2: chưa khởi tạo). Số đo hợp lệ nằm trong [0, luxMax].
 * Phép so sánh với NaN luôn sai nên NaN cũng bị coi là không hợp lệ. */
inline bool luxIsValid(float lux, float luxMax) {
    return lux >= 0.0f && lux <= luxMax;
}

// Dữ liệu ánh sáng mà Task 2 ghi vào g_state (Task 6 sẽ đọc).
struct LightState {
    float    lux;       // số đo hợp lệ gần nhất
    bool     valid;     // lần đọc gần nhất có hợp lệ không
    uint32_t seq;       // tổng số lần đọc
    uint32_t errCount;  // số lần đọc lỗi
};

// Cập nhật sau mỗi lần đọc. Đọc lỗi: giữ nguyên lux cũ, chỉ hạ cờ valid
// và tăng errCount -> nơi hiển thị biết dữ liệu đang không đáng tin.
inline void lightUpdate(LightState &s, float reading, float luxMax) {
    s.seq++;
    if (luxIsValid(reading, luxMax)) {
        s.lux   = reading;
        s.valid = true;
    } else {
        s.valid = false;
        s.errCount++;
    }
}

/* ---------- Tạo nội dung 3 dòng hiển thị OLED (Task 6) ----------
 * Mỗi hàm ghi 1 chuỗi vào out (kích thước n, đã gồm ký tự kết thúc).
 * snprintf tự cắt nếu chuỗi dài hơn n-1 nên không bao giờ tràn bộ đệm.
 * Ở cỡ chữ 2 màn 128 px chứa 10 ký tự/dòng -> các mẫu dưới đây đều <= 10. */
inline void formatLed2Line(char *out, size_t n, bool led2On) {
    snprintf(out, n, "LED2: %s", led2On ? "ON" : "OFF");
}

// Số đo hợp lệ: "LX:123.4" (lớn nhất "LX:54612.5" = 10 ký tự). Lỗi: "LX: ---".
inline void formatLightLine(char *out, size_t n, float lux, bool valid) {
    if (valid) {
        snprintf(out, n, "LX:%.1f", (double)lux);
    } else {
        snprintf(out, n, "LX: ---");
    }
}

// Đang đếm ngược: "LED3: 3s". Đã tắt (remain = 0): "LED3: OFF".
inline void formatLed3Line(char *out, size_t n, uint8_t remainSeconds) {
    if (remainSeconds > 0) {
        snprintf(out, n, "LED3: %us", (unsigned)remainSeconds);
    } else {
        snprintf(out, n, "LED3: OFF");
    }
}

} // namespace app
