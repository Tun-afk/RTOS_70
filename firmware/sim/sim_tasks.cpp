/* ============================================================
 *  sim_tasks.cpp — Mô phỏng logic cả 6 task trên PC (1 file duy nhất)
 *  Dùng CHUNG lib/AppLogic/app_logic.h và include/config.h với firmware:
 *  sim không tự chép hằng số hay logic.
 *
 *  Phần 1 (namespace t123): Task 1, 2, 3
 *   T1: vTaskDelayUntil giữ chu kỳ đảo đúng 100 ms dù thời gian xử lý dao động
 *   T2: (GY-30 / BH1750 qua I2C)
 *       a) luồng số đo: hợp lệ / lỗi đọc (-1) / chưa khởi tạo (-2) / rác (NaN, quá dải)
 *          -> lightUpdate phải giữ lux cũ, hạ cờ valid, đếm lỗi đúng
 *       b) chu kỳ đúng 500 ms dù phải chờ bus I2C
 *       c) 2 luồng thật: xI2CMutex chống 2 giao dịch I2C chồng nhau, xStateMutex
 *          chống đọc dữ liệu rách, KHÔNG bao giờ giữ 2 mutex cùng lúc
 *   T3: chống dội: số lần đảo LED2 = số lần nhấn thật, bỏ qua xung nhiễu,
 *       giữ nút không đảo thêm, độ trễ phản hồi trong giới hạn
 *
 *  Phần 2 (namespace t456): Task 4, 5, 6
 *   A) Dòng thời gian theo từng ms (đúng thứ tự thân task trong main.cpp):
 *      T4: quét BTN2 + chống dội -> mỗi lần nhấn thật cấp semaphore đúng 1 lần
 *      T5: binary semaphore chỉ giữ 1 token; LED3 sáng đúng 3000 ms mỗi lượt,
 *          các lượt không chồng nhau; nhấn nhiều lần khi LED3 đang sáng chỉ
 *          được thêm đúng 1 lượt. Đối chiếu chéo với "lời giải mẫu" tính theo sự kiện.
 *      T6: mỗi 500 ms chụp dữ liệu -> dòng "LED3: 3s/2s/1s/OFF" khớp LED3 thật
 *   B) Định dạng 3 dòng OLED: không vượt 10 ký tự, đọc ngược lại đúng giá trị
 *   C) 4 luồng thật (T2, T4, T5, T6): không va chạm bus I2C, không đọc dữ liệu
 *      rách, không giữ 2 mutex cùng lúc, không mất/thừa token, không deadlock
 *
 *  Build: zig c++ -std=c++17 -O2 -Wall -Wextra -I include -I lib/AppLogic sim/sim_tasks.cpp -o sim_tasks.exe
 *  Chạy : ./sim_tasks.exe 12        (số lần chạy mỗi phần, mặc định 10)
 *  Tự kiểm (cố ý làm sai, sim phải FAIL): -DSIM_NO_I2C_MUTEX, -DSIM_NO_STATE_MUTEX,
 *  -DSIM_COUNTING_SEM
 * ============================================================ */
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include "config.h"
#include "app_logic.h"

static std::atomic<int> g_fail{0};
static std::mutex g_printMutex;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; std::lock_guard<std::mutex> _lk(g_printMutex); \
    std::printf("    FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

/* ############################################################
 *  PHẦN 1 — TASK 1, 2, 3
 * ############################################################ */
namespace t123 {
static const int LED1_HALF_MS  = LED1_PERIOD_MS / 2;
static const int SENSOR_MS     = SENSOR_PERIOD_MS;
static const int BTN_DEB_COUNT = BTN_DEBOUNCE_COUNT;

// Mỗi lần đọc phải là số đo mới: chu kỳ đọc > thời gian 1 lần đo của BH1750
static_assert(SENSOR_PERIOD_MS > BH1750_MEAS_TIME_MS, "SENSOR_PERIOD_MS phai > BH1750_MEAS_TIME_MS");


/* Mô hình vTaskDelayUntil của FreeRTOS: mốc wake tuyệt đối */
static uint32_t delayUntil(uint32_t &lastWake, uint32_t inc, uint32_t now) {
    lastWake += inc;
    return (lastWake > now) ? lastWake : now;   // thời điểm task chạy lại
}

/* ---------------- Task 1 ---------------- */
static void simTask1(std::mt19937 &rng, int &outToggles, double &outDriftDelay) {
    std::uniform_int_distribution<int> work(0, 8);       // thời gian xử lý (ms)
    std::uniform_int_distribution<int> preempt(0, 30);   // bị task khác chiếm (ms)
    const int N = 3000;                                  // 3000 lần đảo = 300 s
    uint32_t t = 0, lastWake = 0, prevToggle = 0;
    uint32_t logMark = 0u - (uint32_t)LED1_HALF_MS;      // giống firmware: xLastWake - xHalfPeriod
    uint32_t tDelay = 0;                                 // mô hình vTaskDelay để so sánh
    for (int i = 0; i < N; ++i) {
        uint32_t toggleAt = t;                           // đảo LED lúc task vừa chạy
        if (i > 0) {
            CHECK(toggleAt - prevToggle == (uint32_t)LED1_HALF_MS,
                  "T1 lan %d: khoang cach %u ms (ky vong %d)", i,
                  (unsigned)(toggleAt - prevToggle), LED1_HALF_MS);
        }
        prevToggle = toggleAt;
        // Giống firmware: cứ T1_LOG_EVERY_N lần đảo thì log thời gian đã trôi
        if ((i + 1) % T1_LOG_EVERY_N == 0) {
            CHECK(toggleAt - logMark == (uint32_t)(T1_LOG_EVERY_N * LED1_HALF_MS),
                  "T1 log lan %d: %u ms (ky vong %d)", i + 1, (unsigned)(toggleAt - logMark),
                  T1_LOG_EVERY_N * LED1_HALF_MS);
            logMark = toggleAt;
        }
        int w = work(rng) + ((rng() % 10 == 0) ? preempt(rng) : 0);
        t = delayUntil(lastWake, LED1_HALF_MS, t + w);
        tDelay += w + LED1_HALF_MS;                      // vTaskDelay: trôi theo w
    }
    outToggles = N;
    outDriftDelay = (double)tDelay - (double)N * LED1_HALF_MS;
}

/* ---------------- Task 2 ---------------- */
// Kịch bản cảm biến, đổi theo từng lần chạy
enum Scenario { SC_HEALTHY = 0, SC_SPORADIC = 1, SC_UNPLUGGED = 2, SC_INIT_FAIL = 3 };
static const char *scenarioName(int sc) {
    switch (sc) {
        case SC_HEALTHY:   return "cam bien tot";
        case SC_SPORADIC:  return "loi doc rai rac";
        case SC_UNPLUGGED: return "rut day tung dot";
        default:           return "init BH1750 loi";
    }
}

struct T2Result { int reads, errors, maxWorkMs, busOps, maxHeld; };

// (a) + (b): luồng số đo và chu kỳ. Mô phỏng đúng thứ tự thân vTask2_Sensor.
static void simTask2Flow(std::mt19937 &rng, int scenario, T2Result &out) {
    std::uniform_real_distribution<float> luxDist(0.0f, BH1750_LUX_MAX);
    std::uniform_int_distribution<int> busBusy(0, 120);   // bus user khác (vd OLED) đang giữ bus (ms)
    std::uniform_int_distribution<int> readOk(1, 3);      // đọc 2 byte I2C thành công (ms)
    const int I2C_TIMEOUT_MS = 50;                        // Wire timeout khi cảm biến không trả lời
    const float NaN = std::numeric_limits<float>::quiet_NaN();

    const bool ready = (scenario != SC_INIT_FAIL);        // g_bh1750Ready trong firmware
    app::LightState st = {0.0f, false, 0, 0};             // g_state.light
    float    lastGood = 0.0f;
    uint32_t expectErr = 0;
    bool     unplugged = false;
    int      maxWork = 0;

    uint32_t t = BH1750_MEAS_TIME_MS, lastWake = t, prevRead = 0;   // vTaskDelay đầu task
    const int N = 600;                                    // 600 lần đọc = 300 s
    for (int k = 0; k < N; ++k) {
        if (k > 0) CHECK(t - prevRead == (uint32_t)SENSOR_MS, "T2 chu ky %u ms (ky vong %d)",
                         (unsigned)(t - prevRead), SENSOR_MS);
        prevRead = t;

        // --- đọc cảm biến (trong xI2CMutex) ---
        float reading = BH1750_ERR_NOT_READY;
        int   work = 0;
        if (ready) {
            work += (rng() % 4 == 0) ? busBusy(rng) : 0;  // chờ lấy xI2CMutex
            if (scenario == SC_UNPLUGGED && rng() % 40 == 0) unplugged = !unplugged;
            bool fail = unplugged || (scenario == SC_SPORADIC && rng() % 5 == 0);
            if (fail) {
                reading = -1.0f;                          // thư viện trả -1 khi đọc I2C thất bại
                work += I2C_TIMEOUT_MS;
            } else {
                reading = luxDist(rng);
                work += readOk(rng);
                if (scenario == SC_SPORADIC) {            // thỉnh thoảng nhận giá trị rác
                    uint32_t g = rng() % 50;
                    if (g == 0) reading = NaN;
                    else if (g == 1) reading = BH1750_LUX_MAX * 1.5f;
                    else if (g == 2) reading = BH1750_LUX_MAX;   // biên trên: vẫn hợp lệ
                    else if (g == 3) reading = 0.0f;             // biên dưới: vẫn hợp lệ
                }
            }
        }

        // --- cập nhật dữ liệu chung (trong xStateMutex) ---
        bool expectValid = (reading >= 0.0f && reading <= BH1750_LUX_MAX);
        app::lightUpdate(st, reading, BH1750_LUX_MAX);
        if (expectValid) lastGood = reading; else expectErr++;

        CHECK(st.seq == (uint32_t)(k + 1), "T2 seq=%u (ky vong %d)", st.seq, k + 1);
        CHECK(st.valid == expectValid, "T2 lan %d: valid=%d (reading=%.1f)", k + 1, st.valid, reading);
        CHECK(st.lux == lastGood, "T2 lan %d: lux=%.1f (ky vong giu %.1f)", k + 1, st.lux, lastGood);
        CHECK(st.errCount == expectErr, "T2 errCount=%u (ky vong %u)", st.errCount, expectErr);
        CHECK(st.lux >= 0.0f && st.lux <= BH1750_LUX_MAX, "T2 lux luu tru ngoai dai: %.1f", st.lux);

        CHECK(work < SENSOR_MS, "T2 xu ly %d ms >= chu ky", work);
        if (work > maxWork) maxWork = work;
        t = delayUntil(lastWake, SENSOR_MS, t + work);
    }
    if (scenario == SC_INIT_FAIL)
        CHECK(st.errCount == (uint32_t)N && !st.valid, "T2 init loi: moi lan doc phai la loi");
    if (scenario == SC_HEALTHY)
        CHECK(st.errCount == 0 && st.valid, "T2 cam bien tot: khong duoc co loi");
    out.reads = N; out.errors = (int)st.errCount; out.maxWorkMs = maxWork;
}

// (c): 2 luồng thật chạy song song theo đúng thứ tự khóa của firmware.
//   Luồng A = Task 2 : [xI2CMutex: giao dịch đọc] -> nhả -> [xStateMutex: lightUpdate] -> nhả
//   Luồng B = bus user khác (Task 6 sau này): [xStateMutex: copy] -> nhả -> [xI2CMutex: giao dịch ghi] -> nhả
static thread_local int t_held = 0;          // số mutex luồng hiện tại đang giữ
static std::atomic<int> g_maxHeld{0};

struct SimLock {
    std::mutex &m; bool enabled;
    SimLock(std::mutex &mm, bool en) : m(mm), enabled(en) {
        if (enabled) m.lock();
        int h = ++t_held, prev = g_maxHeld.load();
        while (h > prev && !g_maxHeld.compare_exchange_weak(prev, h)) {}
    }
    ~SimLock() { --t_held; if (enabled) m.unlock(); }
};

static void simTask2Threads(std::mt19937 &rng, T2Result &out) {
#ifdef SIM_NO_I2C_MUTEX
    const bool useI2CMutex = false;
#else
    const bool useI2CMutex = true;
#endif
#ifdef SIM_NO_STATE_MUTEX
    const bool useStateMutex = false;
#else
    const bool useStateMutex = true;
#endif
    const int N = 20000;
    const int PACE = 32;
    const uint32_t MOD = 50000;                 // < BH1750_LUX_MAX nên luôn là lux hợp lệ
    std::mutex i2cMutex, stateMutex;
    std::atomic<int> busOwner{0}, busCollisions{0}, tornReads{0}, busOps{0};
    std::atomic<bool> go{false};                // vạch xuất phát: 2 luồng bắt đầu cùng lúc
    std::atomic<int>  aDone{0}, bDone{0};       // số vòng mỗi luồng đã chạy
    app::LightState shared = {0.0f, false, 0, 0};
    g_maxHeld = 0;
    uint32_t seedA = rng();

    // Một giao dịch I2C gồm nhiều bước; nếu luồng khác chen vào giữa -> va chạm bus
    auto busTransaction = [&](int me, int steps) {
        busOwner = me;
        for (int i = 0; i < steps; ++i) {
            std::this_thread::yield();
            if (busOwner != me) busCollisions++;
        }
        busOwner = 0;
        busOps++;
    };

    // Mỗi luồng chạy đến khi CẢ HAI đều đủ N vòng -> luôn có tranh chấp suốt bài thử
    std::thread taskA([&] {
        std::mt19937 r(seedA);
        while (!go) std::this_thread::yield();
        while (aDone < N || bDone < N) {
            { SimLock lk(i2cMutex, useI2CMutex); busTransaction(1, 2); }
            {
                SimLock lk(stateMutex, useStateMutex);
                // 1/8 số lần là lỗi đọc; còn lại lux = (seq sau cập nhật) % MOD
                float reading = (r() % 8 == 0) ? -1.0f : (float)((shared.seq + 1) % MOD);
                app::LightState next = shared;
                app::lightUpdate(next, reading, BH1750_LUX_MAX);
                // Ghi từng phần + nhường CPU ở giữa: mô phỏng task bị preempt ngay
                // giữa vùng găng. Không có mutex thì luồng B sẽ đọc phải dữ liệu rách.
                shared.seq = next.seq; shared.errCount = next.errCount;
                std::this_thread::yield();
                shared.lux = next.lux; shared.valid = next.valid;
            }
            aDone++;
            // Giữ nhịp: không chạy vượt luồng kia quá PACE vòng (std::mutex không công bằng)
            while (aDone > bDone + PACE && bDone < N) std::this_thread::yield();
        }
    });
    std::thread taskB([&] {
        uint32_t lastSeq = 0;
        while (!go) std::this_thread::yield();
        while (aDone < N || bDone < N) {
            app::LightState c;
            { SimLock lk(stateMutex, useStateMutex); c = shared; }   // copy rồi nhả ngay
            // Bất biến: seq không giảm, errCount <= seq, valid => lux khớp với seq
            bool ok = (c.seq >= lastSeq) && (c.errCount <= c.seq) &&
                      (!c.valid || c.lux == (float)(c.seq % MOD));
            if (!ok) tornReads++;
            lastSeq = c.seq;
            { SimLock lk(i2cMutex, useI2CMutex); busTransaction(2, 4); }
            bDone++;
            while (bDone > aDone + PACE && aDone < N) std::this_thread::yield();
        }
    });
    go = true;
    taskA.join(); taskB.join();

    CHECK(busCollisions == 0, "T2 bus I2C: %d lan 2 giao dich chong nhau", (int)busCollisions);
    CHECK(tornReads == 0, "T2 race: %d lan doc du lieu khong nhat quan", (int)tornReads);
    CHECK(g_maxHeld == 1, "T2 giu %d mutex cung luc (chi duoc 1)", (int)g_maxHeld);
    CHECK(aDone >= N && bDone >= N, "T2 luong chua chay du: A=%d B=%d", (int)aDone, (int)bDone);
    CHECK(shared.seq == (uint32_t)aDone, "T2 seq=%u (ky vong %d)", shared.seq, (int)aDone);
    out.busOps = busOps; out.maxHeld = g_maxHeld;
}

/* ---------------- Task 3 ---------------- */
struct Seg { uint32_t start, end; };   // khoảng thời gian nút "được nhấn thật"

static void simTask3(std::mt19937 &rng, int &outPresses, int &outGlitches, int &outMaxLatency) {
    std::uniform_int_distribution<int> hold(80, 1500), gap(80, 1000), bounceMs(0, 15),
                                       glitchLen(1, 35);
    const uint32_t T_END = 120000;            // 120 s
    // signal[t] = true khi nút đang ở mức "nhấn" (sau khi tính dội + nhiễu)
    std::vector<uint8_t> sig(T_END + 1, 0);
    std::vector<Seg> presses;
    int glitches = 0;
    uint32_t t = 200 + rng() % 100;
    while (t + 3000 < T_END) {
        if (rng() % 5 == 0) {                 // 20%: xung nhiễu ngắn, không phải nhấn thật
            uint32_t L = glitchLen(rng);
            for (uint32_t i = t; i < t + L; ++i) sig[i] = 1;
            glitches++;
            t += L + gap(rng);
            continue;
        }
        uint32_t h = hold(rng), b1 = bounceMs(rng), b2 = bounceMs(rng);
        for (uint32_t i = t; i < t + h; ++i) sig[i] = 1;
        for (uint32_t i = t; i < t + b1; ++i) sig[i] = rng() & 1;           // dội khi nhấn
        for (uint32_t i = t + h; i < t + h + b2; ++i) sig[i] = rng() & 1;   // dội khi nhả
        presses.push_back({t, t + h});
        t += h + b2 + gap(rng);
    }

    app::Debouncer db;
    app::debounceInit(db, BTN_DEB_COUNT, false);
    bool led2 = false;
    std::vector<uint32_t> toggles;
    for (uint32_t ts = rng() % BTN_POLL_MS; ts <= T_END; ts += BTN_POLL_MS) {
        if (app::debounceUpdate(db, sig[ts] != 0)) { led2 = !led2; toggles.push_back(ts); }
    }

    CHECK(toggles.size() == presses.size(), "T3 so lan dao=%zu, so lan nhan=%zu",
          toggles.size(), presses.size());
    int maxLat = 0;
    for (size_t i = 0; i < toggles.size() && i < presses.size(); ++i) {
        int lat = (int)toggles[i] - (int)presses[i].start;
        CHECK(toggles[i] >= presses[i].start && toggles[i] <= presses[i].end,
              "T3 lan dao %zu luc %u ngoai khoang nhan [%u,%u]", i,
              (unsigned)toggles[i], (unsigned)presses[i].start, (unsigned)presses[i].end);
        // trễ tối đa = dội 15 ms + (BTN_DEB_COUNT) chu kỳ quét
        CHECK(lat <= 15 + (BTN_DEB_COUNT + 1) * BTN_POLL_MS, "T3 tre %d ms qua lon", lat);
        if (lat > maxLat) maxLat = lat;
    }
    CHECK(led2 == (presses.size() % 2 == 1), "T3 trang thai LED2 cuoi sai");
    outPresses = (int)presses.size(); outGlitches = glitches; outMaxLatency = maxLat;
}

/* ---------------- Kiểm tra biên ---------------- */
static void edgeCases() {
    const float M = BH1750_LUX_MAX;
    CHECK(app::luxIsValid(0.0f, M), "lux 0 phai hop le");
    CHECK(app::luxIsValid(-0.0f, M), "lux -0.0 phai hop le");
    CHECK(app::luxIsValid(M, M), "lux = max phai hop le");
    CHECK(!app::luxIsValid(std::nextafter(M, 2 * M), M), "lux > max phai khong hop le");
    CHECK(!app::luxIsValid(-1.0f, M), "ma loi -1 phai khong hop le");
    CHECK(!app::luxIsValid(BH1750_ERR_NOT_READY, M), "ma loi chua khoi tao phai khong hop le");
    CHECK(!app::luxIsValid(-0.001f, M), "so am nho phai khong hop le");
    CHECK(!app::luxIsValid(std::numeric_limits<float>::quiet_NaN(), M), "NaN phai khong hop le");
    CHECK(!app::luxIsValid(std::numeric_limits<float>::infinity(), M), "+inf phai khong hop le");
    CHECK(!app::luxIsValid(-std::numeric_limits<float>::infinity(), M), "-inf phai khong hop le");

    app::LightState s = {0.0f, false, 0, 0};
    app::lightUpdate(s, -1.0f, M);        // lỗi ngay lần đầu: chưa có số đo nào
    CHECK(!s.valid && s.lux == 0.0f && s.seq == 1 && s.errCount == 1, "loi ngay lan doc dau");
    app::lightUpdate(s, 123.5f, M);
    CHECK(s.valid && s.lux == 123.5f && s.seq == 2 && s.errCount == 1, "doc tot sau loi");
    app::lightUpdate(s, -1.0f, M);
    CHECK(!s.valid && s.lux == 123.5f && s.seq == 3 && s.errCount == 2, "loi phai giu lux cu");
    app::lightUpdate(s, 0.0f, M);         // trời tối hoàn toàn vẫn là số đo hợp lệ
    CHECK(s.valid && s.lux == 0.0f && s.errCount == 2, "0 lx la hop le");

    app::Debouncer d; app::debounceInit(d, 3, true);   // khởi động khi đang giữ nút
    bool ev = false;
    for (int i = 0; i < 10; ++i) ev |= app::debounceUpdate(d, true);
    CHECK(!ev, "khoi dong khi dang giu nut khong duoc sinh su kien");
    for (int i = 0; i < 3; ++i) app::debounceUpdate(d, false);
    int cnt = 0;
    for (int i = 0; i < 3; ++i) cnt += app::debounceUpdate(d, true);
    CHECK(cnt == 1, "sau khi nha roi nhan lai phai co dung 1 su kien (cnt=%d)", cnt);
}

static int run(int runs) {
    int startFail = g_fail;
    edgeCases();
    std::printf("Edge cases: %s\n\n", (g_fail - startFail) ? "FAIL" : "PASS");
    std::printf("%-3s %-6s | %-17s | %-16s | %-37s | %-21s | %s\n", "Run", "Seed",
                "Kich ban T2", "T1 toggles,drift*",
                "T2 doc,loi,xuly max | busOps,maxHeld", "T3 nhan,nhieu,tre max", "KQ");
    for (int r = 1; r <= runs; ++r) {
        int before = g_fail;
        uint32_t seed = 1000u + 7919u * (uint32_t)r;
        std::mt19937 rng(seed);
        int scenario = (r - 1) % 4;
        int t1n; double drift; int t3p, t3g, t3lat;
        T2Result t2 = {0, 0, 0, 0, 0};
        simTask1(rng, t1n, drift);
        simTask2Flow(rng, scenario, t2);
        simTask2Threads(rng, t2);
        simTask3(rng, t3p, t3g, t3lat);
        bool ok = (g_fail == before);
        std::printf("%-3d %-6u | %-17s | %5d,%+7.0f ms | %4d,%4d,%4d ms | %9d,%2d       | %4d,%4d,%4d ms     | %s\n",
                    r, seed, scenarioName(scenario), t1n, drift,
                    t2.reads, t2.errors, t2.maxWorkMs, t2.busOps, t2.maxHeld,
                    t3p, t3g, t3lat, ok ? "PASS" : "FAIL");
    }
    std::printf("\n* drift = do troi neu dung vTaskDelay thay vTaskDelayUntil (DelayUntil: 0 ms)\n");
    int fails = g_fail - startFail;
    std::printf("TONG KET PHAN 1: %d run, %d loi -> %s\n", runs, fails, fails ? "FAIL" : "ALL PASS");
    return fails;
}

} // namespace t123

/* ############################################################
 *  PHẦN 2 — TASK 4, 5, 6
 * ############################################################ */
namespace t456 {
static const uint32_t LED3_ON_MS = (uint32_t)LED3_ON_TIME_S * LED3_STEP_MS;


/* ---------------- Mô hình binary semaphore (1 luồng, cho phần A) ---------------- */
struct SimBinSem {
    int tokens = 0;
    bool give() {
#ifdef SIM_COUNTING_SEM
        tokens++; return true;              // cố ý sai: đếm không giới hạn
#else
        if (tokens >= 1) return false;      // đã có token -> xSemaphoreGive trả pdFALSE
        tokens = 1; return true;
#endif
    }
    bool tryTake() { if (tokens > 0) { tokens--; return true; } return false; }
};

/* ---------------- A) Dòng thời gian Task 4 + 5 + 6 ---------------- */
enum Scenario { SC_SPARSE = 0, SC_MASH = 1, SC_LONG_HOLD = 2, SC_MIXED = 3 };
static const char *scenarioName(int sc) {
    switch (sc) {
        case SC_SPARSE:    return "nhan thua (>3s)";
        case SC_MASH:      return "nhan don dap";
        case SC_LONG_HOLD: return "giu nut lau";
        default:           return "hon hop + nhieu";
    }
}

struct TimelineResult { int presses, accepted, rejected, cycles, t6Samples; };

static void simTimeline(std::mt19937 &rng, int scenario, TimelineResult &out) {
    int holdMin = 80, holdMax = 400, gapMin = 80, gapMax = 400, glitchPct = 0;
    switch (scenario) {
        case SC_SPARSE:    gapMin = 3500; gapMax = 8000; break;
        case SC_MASH:      holdMax = 200; break;
        case SC_LONG_HOLD: holdMin = 2000; holdMax = 7000; gapMax = 2000; break;
        default:           holdMax = 1500; gapMax = 5000; glitchPct = 20; break;
    }
    std::uniform_int_distribution<int> hold(holdMin, holdMax), gap(gapMin, gapMax),
                                       bounceMs(0, 15), glitchLen(1, 35);
    const uint32_t T_END = 180000;                        // 180 s
    std::vector<uint8_t> sig(T_END + 1, 0);               // 1 = BTN2 đang ở mức "nhấn"
    int presses = 0;
    uint32_t t = 300 + rng() % 200;
    while (t + 9000 < T_END) {
        if (glitchPct > 0 && (int)(rng() % 100) < glitchPct) {   // xung nhiễu, không phải nhấn thật
            uint32_t L = glitchLen(rng);
            for (uint32_t i = t; i < t + L; ++i) sig[i] = 1;
            t += L + 80 + rng() % 300;
            continue;
        }
        uint32_t h = hold(rng), b1 = bounceMs(rng), b2 = bounceMs(rng);
        for (uint32_t i = t; i < t + h; ++i) sig[i] = 1;
        for (uint32_t i = t; i < t + b1; ++i) sig[i] = rng() & 1;           // dội khi nhấn
        for (uint32_t i = t + h; i < t + h + b2; ++i) sig[i] = rng() & 1;   // dội khi nhả
        presses++;
        t += h + b2 + gap(rng);
    }

    // ----- Mô hình theo tick: thứ tự trong 1 tick = T4, T5 (prio 3) rồi mới T6 (prio 1) -----
    app::Debouncer db; app::debounceInit(db, BTN_DEBOUNCE_COUNT, false);
    SimBinSem sem;
    const uint32_t phase4 = rng() % BTN_POLL_MS, phase6 = rng() % OLED_PERIOD_MS;

    std::vector<uint32_t> giveTimes; std::vector<uint8_t> giveAccepted;
    std::vector<uint32_t> cycleStart, cycleEnd;
    bool     led3On = false;            // chân PIN_LED3
    uint8_t  led3Remain = 0;            // g_state.led3Remain
    uint8_t  stepsLeft = 0;
    uint32_t nextWake = 0;
    int      t6Samples = 0;
    char     line[OLED_LINE_BUF], expect[OLED_LINE_BUF];

    for (uint32_t now = 0; now <= T_END || led3On || sem.tokens > 0; ++now) {
        // --- Task 4: quét nút, cấp semaphore ---
        if (now % BTN_POLL_MS == phase4) {
            bool raw = (now <= T_END) && sig[now] != 0;
            if (app::debounceUpdate(db, raw)) {
                giveTimes.push_back(now);
                giveAccepted.push_back(sem.give() ? 1 : 0);
            }
        }
        // --- Task 5: đang đếm ngược thì chờ đến mốc vTaskDelayUntil ---
        if (led3On && now == nextWake) {
            stepsLeft--;
            if (stepsLeft > 0) { led3Remain = stepsLeft; nextWake += LED3_STEP_MS; }
            else { led3On = false; led3Remain = 0; cycleEnd.push_back(now); }
        }
        // --- Task 5: đang chờ ở xSemaphoreTake ---
        if (!led3On && sem.tryTake()) {
            led3On = true; stepsLeft = LED3_ON_TIME_S; led3Remain = stepsLeft;
            nextWake = now + LED3_STEP_MS; cycleStart.push_back(now);
        }
        // --- Task 6: chụp dữ liệu, tạo dòng LED3 ---
        if (now % OLED_PERIOD_MS == phase6) {
            t6Samples++;
            app::formatLed3Line(line, sizeof(line), led3Remain);
            if (led3On) {
                uint32_t left = cycleStart.back() + LED3_ON_MS - now;       // ms còn lại
                unsigned expRemain = (left + LED3_STEP_MS - 1) / LED3_STEP_MS;
                std::snprintf(expect, sizeof(expect), "LED3: %us", expRemain);
                CHECK(led3Remain >= 1 && led3Remain <= LED3_ON_TIME_S, "T6 remain=%u ngoai 1..%d", led3Remain, LED3_ON_TIME_S);
            } else {
                std::snprintf(expect, sizeof(expect), "LED3: OFF");
            }
            CHECK(std::strcmp(line, expect) == 0, "T6 luc %u ms: hien \"%s\", ky vong \"%s\"", (unsigned)now, line, expect);
        }
    }

    // ----- Lời giải mẫu (tính theo sự kiện, độc lập với mô hình tick) -----
    std::vector<uint32_t> oracleStart; std::vector<uint8_t> oracleAccepted;
    {
        bool pending = false, any = false; uint32_t busyUntil = 0;
        for (uint32_t g : giveTimes) {
            if (pending && busyUntil < g) { oracleStart.push_back(busyUntil); busyUntil += LED3_ON_MS; pending = false; }
            if (!pending && (!any || g >= busyUntil)) { oracleStart.push_back(g); busyUntil = g + LED3_ON_MS; any = true; oracleAccepted.push_back(1); }
            else if (!pending) { pending = true; oracleAccepted.push_back(1); }
            else oracleAccepted.push_back(0);
        }
        if (pending) oracleStart.push_back(busyUntil);
    }

    // ----- Kiểm tra -----
    CHECK((int)giveTimes.size() == presses, "T4: %zu lan cap semaphore, %d lan nhan that", giveTimes.size(), presses);
    CHECK(cycleStart.size() == cycleEnd.size(), "T5: so lan bat (%zu) khac so lan tat (%zu)", cycleStart.size(), cycleEnd.size());
    CHECK(cycleStart == oracleStart, "T5: thoi diem bat LED3 khac loi giai mau (%zu so voi %zu luot)", cycleStart.size(), oracleStart.size());
    CHECK(giveAccepted == oracleAccepted, "T4: ket qua xSemaphoreGive khac loi giai mau");
    int accepted = 0;
    for (uint8_t a : giveAccepted) accepted += a;
    CHECK((int)cycleStart.size() == accepted, "T5: %zu luot LED3, %d token duoc cap", cycleStart.size(), accepted);
    for (size_t i = 0; i < cycleStart.size() && i < cycleEnd.size(); ++i) {
        CHECK(cycleEnd[i] - cycleStart[i] == LED3_ON_MS, "T5 luot %zu: sang %u ms (ky vong %u)", i,
              (unsigned)(cycleEnd[i] - cycleStart[i]), (unsigned)LED3_ON_MS);
        if (i > 0) CHECK(cycleStart[i] >= cycleEnd[i - 1], "T5 luot %zu chong len luot truoc", i);
    }
    // Mỗi lần nhấn đều dẫn tới LED3 sáng trong vòng 3 s kể từ lúc cấp
    for (uint32_t g : giveTimes) {
        bool served = false;
        for (uint32_t s : cycleStart) if (s + LED3_ON_MS > g && s <= g + LED3_ON_MS) { served = true; break; }
        CHECK(served, "T5: lan nhan luc %u ms khong lam LED3 sang", (unsigned)g);
    }
    int rejected = (int)giveTimes.size() - accepted;
    if (scenario == SC_SPARSE) CHECK(rejected == 0 && (int)cycleStart.size() == presses, "nhan thua: moi lan nhan phai duoc 1 luot");
    if (scenario == SC_MASH)   CHECK(rejected > 0, "nhan don dap: phai co lan nhan bi bo qua");
    out.presses = presses; out.accepted = accepted; out.rejected = rejected;
    out.cycles = (int)cycleStart.size(); out.t6Samples = t6Samples;
}

/* ---------------- B) Định dạng dòng OLED ---------------- */
static void simFormat(std::mt19937 &rng) {
    std::uniform_real_distribution<float> luxDist(0.0f, BH1750_LUX_MAX);
    char line[OLED_LINE_BUF];
    for (int i = 0; i < 2000; ++i) {
        float lux = (i == 0) ? 0.0f : (i == 1) ? BH1750_LUX_MAX : (i == 2) ? 9.95f : luxDist(rng);
        app::formatLightLine(line, sizeof(line), lux, true);
        float back = -1.0f;
        CHECK(std::strlen(line) <= OLED_LINE_CHARS, "dong lux \"%s\" dai hon %d ky tu", line, OLED_LINE_CHARS);
        CHECK(std::sscanf(line, "LX:%f", &back) == 1 && std::fabs(back - lux) <= 0.051f,
              "dong lux \"%s\" khong khop %.3f", line, lux);
    }
    app::formatLightLine(line, sizeof(line), 123.4f, false);
    CHECK(std::strcmp(line, "LX: ---") == 0, "lux khong hop le phai hien \"LX: ---\", dang hien \"%s\"", line);
    app::formatLed2Line(line, sizeof(line), true);
    CHECK(std::strcmp(line, "LED2: ON") == 0, "LED2 ON hien \"%s\"", line);
    app::formatLed2Line(line, sizeof(line), false);
    CHECK(std::strcmp(line, "LED2: OFF") == 0, "LED2 OFF hien \"%s\"", line);
    for (int r = 0; r <= 255; ++r) {
        app::formatLed3Line(line, sizeof(line), (uint8_t)r);
        CHECK(std::strlen(line) <= OLED_LINE_CHARS, "dong LED3 \"%s\" dai hon %d ky tu", line, OLED_LINE_CHARS);
        unsigned back = 999;
        if (r == 0) CHECK(std::strcmp(line, "LED3: OFF") == 0, "LED3 remain=0 hien \"%s\"", line);
        else CHECK(std::sscanf(line, "LED3: %us", &back) == 1 && back == (unsigned)r, "LED3 remain=%d hien \"%s\"", r, line);
    }
    char tiny[4];                                   // bộ đệm quá nhỏ: phải cắt, không tràn
    app::formatLed2Line(tiny, sizeof(tiny), false);
    CHECK(std::strlen(tiny) == 3, "bo dem nho phai bi cat con 3 ky tu");
    CHECK(std::strlen(OLED_TITLE) * 6 * OLED_TITLE_SIZE <= OLED_WIDTH, "tieu de OLED rong hon man hinh");
    CHECK(OLED_LINE_CHARS * 6 * OLED_TEXT_SIZE <= OLED_WIDTH, "OLED_LINE_CHARS khong vua man hinh");
    CHECK(OLED_LINE3_Y + 8 * OLED_TEXT_SIZE <= OLED_HEIGHT, "dong 3 tran khoi man hinh");
}

/* ---------------- C) 4 luồng thật ---------------- */
// Binary semaphore thật cho đa luồng (give không chặn, take chặn đến khi có token)
struct RealBinSem {
    std::mutex m; std::condition_variable cv; bool token = false;
    bool give() { std::lock_guard<std::mutex> lk(m); if (token) return false; token = true; cv.notify_one(); return true; }
    void take() { std::unique_lock<std::mutex> lk(m); cv.wait(lk, [&] { return token; }); token = false; }
};

static thread_local int t_held = 0;            // số mutex luồng hiện tại đang giữ
static std::atomic<int> g_maxHeld{0};
struct SimLock {
    std::mutex &m; bool enabled;
    SimLock(std::mutex &mm, bool en) : m(mm), enabled(en) {
        if (enabled) m.lock();
        int h = ++t_held, prev = g_maxHeld.load();
        while (h > prev && !g_maxHeld.compare_exchange_weak(prev, h)) {}
    }
    ~SimLock() { --t_held; if (enabled) m.unlock(); }
};

struct SharedState { app::LightState light; bool led2On; uint8_t led3Remain; };   // giống SystemState
struct ThreadResult { int accepted, cycles, busOps, maxHeld; };

static void simThreads(std::mt19937 &rng, ThreadResult &out) {
#ifdef SIM_NO_I2C_MUTEX
    const bool useI2CMutex = false;
#else
    const bool useI2CMutex = true;
#endif
#ifdef SIM_NO_STATE_MUTEX
    const bool useStateMutex = false;
#else
    const bool useStateMutex = true;
#endif
    const int N = 10000, PACE = 32, GIVES = 3000;
    const uint32_t MOD = 50000;
    std::mutex i2cMutex, stateMutex;
    RealBinSem sem;
    SharedState shared = {{0.0f, false, 0, 0}, false, 0};
    std::atomic<int> busOwner{0}, busCollisions{0}, tornReads{0}, busOps{0};
    std::atomic<int> aDone{0}, bDone{0}, accepted{0}, cycles{0};
    std::atomic<bool> go{false}, stop{false}, finished{false};
    g_maxHeld = 0;
    uint32_t seed2 = rng(), seed4 = rng();

    auto busTransaction = [&](int me, int steps) {       // 1 giao dịch I2C nhiều bước
        busOwner = me;
        for (int i = 0; i < steps; ++i) { std::this_thread::yield(); if (busOwner != me) busCollisions++; }
        busOwner = 0; busOps++;
    };

    std::thread task2([&] {                              // Task 2: I2C -> nhả -> state -> nhả
        std::mt19937 r(seed2);
        while (!go) std::this_thread::yield();
        while (aDone < N || bDone < N) {
            { SimLock lk(i2cMutex, useI2CMutex); busTransaction(2, 2); }
            {
                SimLock lk(stateMutex, useStateMutex);
                float reading = (r() % 8 == 0) ? -1.0f : (float)((shared.light.seq + 1) % MOD);
                app::LightState next = shared.light;
                app::lightUpdate(next, reading, BH1750_LUX_MAX);
                shared.light.seq = next.seq; shared.light.errCount = next.errCount;
                std::this_thread::yield();               // mô phỏng bị preempt giữa vùng găng
                shared.light.lux = next.lux; shared.light.valid = next.valid;
            }
            aDone++;
            while (aDone > bDone + PACE && bDone < N) std::this_thread::yield();
        }
    });
    std::thread task6([&] {                              // Task 6: state (copy) -> nhả -> I2C -> nhả
        uint32_t lastSeq = 0;
        char l1[OLED_LINE_BUF], l2[OLED_LINE_BUF], l3[OLED_LINE_BUF];
        while (!go) std::this_thread::yield();
        while (aDone < N || bDone < N) {
            SharedState c;
            { SimLock lk(stateMutex, useStateMutex); c = shared; }
            bool ok = (c.light.seq >= lastSeq) && (c.light.errCount <= c.light.seq) &&
                      (!c.light.valid || c.light.lux == (float)(c.light.seq % MOD)) &&
                      (c.led3Remain <= LED3_ON_TIME_S);
            if (!ok) tornReads++;
            lastSeq = c.light.seq;
            app::formatLed2Line(l1, sizeof(l1), c.led2On);
            app::formatLightLine(l2, sizeof(l2), c.light.lux, c.light.valid);
            app::formatLed3Line(l3, sizeof(l3), c.led3Remain);
            { SimLock lk(i2cMutex, useI2CMutex); busTransaction(6, 4); }
            bDone++;
            while (bDone > aDone + PACE && aDone < N) std::this_thread::yield();
        }
    });
    std::thread task4([&] {                              // Task 4: cấp semaphore
        std::mt19937 r(seed4);
        while (!go) std::this_thread::yield();
        for (int i = 0; i < GIVES; ++i) {
            if (sem.give()) accepted++;
            int spins = (int)(r() % 4);
            for (int k = 0; k < spins; ++k) std::this_thread::yield();
        }
    });
    std::thread task5([&] {                              // Task 5: take -> đếm ngược trong stateMutex
        while (!go) std::this_thread::yield();
        for (;;) {
            sem.take();
            if (stop) break;
            for (int remain = LED3_ON_TIME_S; remain >= 0; --remain) {
                SimLock lk(stateMutex, useStateMutex);
                shared.led3Remain = (uint8_t)remain;
            }
            cycles++;
        }
    });
    std::thread watchdog([&] {                           // phát hiện deadlock
        for (int i = 0; i < 3000 && !finished; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!finished) { std::printf("    FAIL: DEADLOCK - cac luong khong ket thuc sau 30 s\n"); std::exit(2); }
    });

    go = true;
    task4.join(); task2.join(); task6.join();
    for (int i = 0; i < 2000 && cycles < accepted; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    stop = true;
    while (!sem.give()) std::this_thread::yield();       // đánh thức Task 5 để thoát
    task5.join();
    finished = true; watchdog.join();

    CHECK(busCollisions == 0, "bus I2C: %d lan 2 giao dich chong nhau", (int)busCollisions);
    CHECK(tornReads == 0, "race: %d lan Task 6 doc du lieu khong nhat quan", (int)tornReads);
    CHECK(g_maxHeld == 1, "giu %d mutex cung luc (chi duoc 1)", (int)g_maxHeld);
    CHECK(cycles == accepted, "semaphore: %d token duoc cap nhung Task 5 chay %d luot", (int)accepted, (int)cycles);
    CHECK(accepted >= 1 && accepted <= GIVES, "semaphore: so token %d bat thuong", (int)accepted);
    CHECK(shared.led3Remain == 0, "led3Remain cuoi cung = %u (ky vong 0)", shared.led3Remain);
    out.accepted = accepted; out.cycles = cycles; out.busOps = busOps; out.maxHeld = g_maxHeld;
}

static int run(int runs) {
    int startFail = g_fail;
    std::printf("%-3s %-6s | %-16s | %-32s | %-11s | %-27s | %s\n", "Run", "Seed", "Kich ban BTN2",
                "T4/T5: nhan,nhan token,bo,luot", "T6 lan chup",
                "Luong: token,luot,bus,giu", "KQ");
    for (int r = 1; r <= runs; ++r) {
        int before = g_fail;
        uint32_t seed = 2000u + 104729u * (uint32_t)r;
        std::mt19937 rng(seed);
        int scenario = (r - 1) % 4;
        TimelineResult tl = {0, 0, 0, 0, 0};
        ThreadResult th = {0, 0, 0, 0};
        simTimeline(rng, scenario, tl);
        simFormat(rng);
        simThreads(rng, th);
        bool ok = (g_fail == before);
        std::printf("%-3d %-6u | %-16s | %5d, %5d, %5d, %5d       | %6d      | %5d, %5d, %6d, %d   | %s\n",
                    r, seed % 1000000u, scenarioName(scenario), tl.presses, tl.accepted, tl.rejected, tl.cycles,
                    tl.t6Samples, th.accepted, th.cycles, th.busOps, th.maxHeld, ok ? "PASS" : "FAIL");
    }
    int fails = g_fail - startFail;
    std::printf("\nTONG KET PHAN 2: %d run, %d loi -> %s\n", runs, fails, fails ? "FAIL" : "ALL PASS");
    return fails;
}

} // namespace t456

int main(int argc, char **argv) {
    int runs = (argc > 1) ? std::atoi(argv[1]) : 10;
    std::printf("================ PHAN 1: TASK 1, 2, 3 ================\n");
    int f1 = t123::run(runs);
    std::printf("\n================ PHAN 2: TASK 4, 5, 6 ================\n");
    int f2 = t456::run(runs);
    std::printf("\nTONG KET CHUNG: %d run x 2 phan, %d loi -> %s\n", runs, f1 + f2,
                (f1 + f2) ? "FAIL" : "ALL PASS");
    return (f1 + f2) ? 1 : 0;
}
