#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdarg.h>
#include "config.h"
#include "app_logic.h"

struct SystemState {
    app::LightState light;
    bool            led2On;
    uint8_t         led3Remain;
};

static SystemState       g_state      = {{0.0f, false, 0, 0}, false, 0};
static SemaphoreHandle_t xStateMutex  = NULL;
static SemaphoreHandle_t xSerialMutex = NULL;
static SemaphoreHandle_t xI2CMutex    = NULL;
static SemaphoreHandle_t semButton2   = NULL;

static BH1750 g_lightMeter(BH1750_ADDR);
static bool   g_bh1750Ready = false;

static Adafruit_SSD1306 g_display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
static bool             g_oledReady = false;

static void logf(const char *tag, const char *fmt, ...) {
    char buf[LOG_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(LOG_MUTEX_WAIT_MS)) == pdTRUE) {
        Serial.printf("[%7lu ms][%s] %s\n", (unsigned long)millis(), tag, buf);
        xSemaphoreGive(xSerialMutex);
    }
}

static void vTask1_Blink(void *pvParameters) {
    (void)pvParameters;
    const TickType_t xHalfPeriod = pdMS_TO_TICKS(LED1_PERIOD_MS / 2);
    TickType_t xLastWake = xTaskGetTickCount();
    TickType_t xLogMark  = xLastWake - xHalfPeriod;
    uint32_t   toggles   = 0;
    bool       on        = false;

    for (;;) {
        on = !on;
        digitalWrite(PIN_LED1, on ? HIGH : LOW);

        if (++toggles % T1_LOG_EVERY_N == 0) {
            TickType_t now = xTaskGetTickCount();
            logf("T1", "LED1 da dao %lu lan, %d lan gan nhat mat %lu ms",
                 (unsigned long)toggles, T1_LOG_EVERY_N,
                 (unsigned long)((now - xLogMark) * portTICK_PERIOD_MS));
            xLogMark = now;
        }
        vTaskDelayUntil(&xLastWake, xHalfPeriod);
    }
}

static void vTask2_Sensor(void *pvParameters) {
    (void)pvParameters;
    const TickType_t xPeriod = pdMS_TO_TICKS(SENSOR_PERIOD_MS);

    vTaskDelay(pdMS_TO_TICKS(BH1750_MEAS_TIME_MS));
    TickType_t xLastWake = xTaskGetTickCount();

    for (;;) {
        float reading = BH1750_ERR_NOT_READY;

        if (g_bh1750Ready) {
            xSemaphoreTake(xI2CMutex, portMAX_DELAY);
            reading = g_lightMeter.readLightLevel();
            xSemaphoreGive(xI2CMutex);
        }

        xSemaphoreTake(xStateMutex, portMAX_DELAY);
        app::lightUpdate(g_state.light, reading, BH1750_LUX_MAX);
        app::LightState light = g_state.light;
        xSemaphoreGive(xStateMutex);

        if (light.valid) {
            logf("T2", "Lan doc %lu: anh sang = %.1f lx",
                 (unsigned long)light.seq, light.lux);
        } else {
            logf("T2", "Lan doc %lu: LOI doc BH1750 (ma %.0f, tong %lu lan loi)",
                 (unsigned long)light.seq, reading, (unsigned long)light.errCount);
        }
        vTaskDelayUntil(&xLastWake, xPeriod);
    }
}

static void vTask3_Button1(void *pvParameters) {
    (void)pvParameters;
    app::Debouncer db;
    app::debounceInit(db, BTN_DEBOUNCE_COUNT, digitalRead(PIN_BTN1) == LOW);
    bool led2 = false;

    for (;;) {
        bool pressed = (digitalRead(PIN_BTN1) == LOW);
        if (app::debounceUpdate(db, pressed)) {
            led2 = !led2;
            digitalWrite(PIN_LED2, led2 ? HIGH : LOW);

            xSemaphoreTake(xStateMutex, portMAX_DELAY);
            g_state.led2On = led2;
            xSemaphoreGive(xStateMutex);

            logf("T3", "Nhan BTN1 -> LED2 = %s", led2 ? "ON" : "OFF");
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

static void vTask4_Button2(void *pvParameters) {
    (void)pvParameters;
    app::Debouncer db;
    app::debounceInit(db, BTN_DEBOUNCE_COUNT, digitalRead(PIN_BTN2) == LOW);

    for (;;) {
        bool pressed = (digitalRead(PIN_BTN2) == LOW);
        if (app::debounceUpdate(db, pressed)) {
            if (xSemaphoreGive(semButton2) == pdTRUE) {
                logf("T4", "Nhan BTN2 -> cap semaphore");
            } else {
                logf("T4", "Nhan BTN2 -> semaphore dang co san, lan nhan nay khong duoc tinh them");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

static void vTask5_Led3(void *pvParameters) {
    (void)pvParameters;
    const TickType_t xStep = pdMS_TO_TICKS(LED3_STEP_MS);

    for (;;) {
        if (xSemaphoreTake(semButton2, portMAX_DELAY) == pdTRUE) {
            TickType_t xStart    = xTaskGetTickCount();
            TickType_t xLastWake = xStart;

            digitalWrite(PIN_LED3, HIGH);
            logf("T5", "Nhan duoc semaphore -> LED3 ON trong %d s", LED3_ON_TIME_S);

            for (uint8_t remain = LED3_ON_TIME_S; remain > 0; --remain) {
                xSemaphoreTake(xStateMutex, portMAX_DELAY);
                g_state.led3Remain = remain;
                xSemaphoreGive(xStateMutex);

                vTaskDelayUntil(&xLastWake, xStep);
            }

            digitalWrite(PIN_LED3, LOW);

            xSemaphoreTake(xStateMutex, portMAX_DELAY);
            g_state.led3Remain = 0;
            xSemaphoreGive(xStateMutex);

            logf("T5", "LED3 OFF sau %lu ms, cho semaphore tiep",
                 (unsigned long)((xTaskGetTickCount() - xStart) * portTICK_PERIOD_MS));
        }
    }
}

static void vTask6_Display(void *pvParameters) {
    (void)pvParameters;
    const TickType_t xPeriod = pdMS_TO_TICKS(OLED_PERIOD_MS);
    TickType_t xLastWake = xTaskGetTickCount();
    uint32_t   refreshes = 0;
    char lineLed2[OLED_LINE_BUF];
    char lineLight[OLED_LINE_BUF];
    char lineLed3[OLED_LINE_BUF];

    for (;;) {
        xSemaphoreTake(xStateMutex, portMAX_DELAY);
        SystemState snapshot = g_state;
        xSemaphoreGive(xStateMutex);

        app::formatLed2Line(lineLed2, sizeof(lineLed2), snapshot.led2On);
        app::formatLightLine(lineLight, sizeof(lineLight), snapshot.light.lux, snapshot.light.valid);
        app::formatLed3Line(lineLed3, sizeof(lineLed3), snapshot.led3Remain);

        if (g_oledReady) {
            g_display.clearDisplay();
            g_display.setTextSize(OLED_TITLE_SIZE);
            g_display.setCursor(0, OLED_TITLE_Y);
            g_display.print(OLED_TITLE);
            g_display.setTextSize(OLED_TEXT_SIZE);
            g_display.setCursor(0, OLED_LINE1_Y);
            g_display.print(lineLed2);
            g_display.setCursor(0, OLED_LINE2_Y);
            g_display.print(lineLight);
            g_display.setCursor(0, OLED_LINE3_Y);
            g_display.print(lineLed3);

            xSemaphoreTake(xI2CMutex, portMAX_DELAY);
            g_display.display();
            xSemaphoreGive(xI2CMutex);
        }

        if (++refreshes % T6_LOG_EVERY_N == 0) {
            logf("T6", "OLED %s, lan ve %lu: %s | %s | %s",
                 g_oledReady ? "OK" : "LOI", (unsigned long)refreshes,
                 lineLed2, lineLight, lineLed3);
        }
        vTaskDelayUntil(&xLastWake, xPeriod);
    }
}

static void createTaskOrHalt(TaskFunction_t fn, const char *name,
                             uint32_t stackBytes, UBaseType_t prio) {
    if (xTaskCreatePinnedToCore(fn, name, stackBytes, NULL, prio, NULL, APP_CORE) != pdPASS) {
        logf("SYS", "LOI: khong tao duoc task %s (thieu heap)", name);
        for (;;) { delay(HALT_DELAY_MS); }
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(SERIAL_WAIT_MS);

    Serial.println("\n===== ESP32-S3 N16R8 - FreeRTOS Task 1..6 =====");
    Serial.printf("Chip  : %s rev %u, %u nhan @ %u MHz\n",
                  ESP.getChipModel(), (unsigned)ESP.getChipRevision(),
                  (unsigned)ESP.getChipCores(), (unsigned)ESP.getCpuFreqMHz());
    Serial.printf("Flash : %u MB | PSRAM: %u byte | Heap trong: %u byte\n",
                  (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                  (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreeHeap());
    Serial.printf("Tick  : %u Hz\n", (unsigned)configTICK_RATE_HZ);

    pinMode(PIN_LED1, OUTPUT);
    pinMode(PIN_LED2, OUTPUT);
    pinMode(PIN_LED3, OUTPUT);
    digitalWrite(PIN_LED1, LOW);
    digitalWrite(PIN_LED2, LOW);
    digitalWrite(PIN_LED3, LOW);
    pinMode(PIN_BTN1, INPUT_PULLUP);
    pinMode(PIN_BTN2, INPUT_PULLUP);

    xStateMutex  = xSemaphoreCreateMutex();
    xSerialMutex = xSemaphoreCreateMutex();
    xI2CMutex    = xSemaphoreCreateMutex();
    semButton2   = xSemaphoreCreateBinary();
    if (xStateMutex == NULL || xSerialMutex == NULL || xI2CMutex == NULL || semButton2 == NULL) {
        Serial.println("LOI: khong tao duoc mutex / semaphore");
        for (;;) { delay(HALT_DELAY_MS); }
    }

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    g_bh1750Ready = g_lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, BH1750_ADDR, &Wire);
    if (g_bh1750Ready) {
        logf("SYS", "BH1750 san sang tai dia chi 0x%02X", BH1750_ADDR);
    } else {
        logf("SYS", "LOI: khong khoi tao duoc BH1750 (0x%02X), Task 2 se bao du lieu khong hop le",
             BH1750_ADDR);
    }

    Wire.beginTransmission(OLED_ADDR);
    bool oledFound = (Wire.endTransmission() == 0);
    g_oledReady = oledFound && g_display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, true, false);
    if (g_oledReady) {
        g_display.setTextColor(SSD1306_WHITE);
        g_display.clearDisplay();
        g_display.display();
        logf("SYS", "OLED san sang tai dia chi 0x%02X", OLED_ADDR);
    } else {
        logf("SYS", "LOI: khong thay OLED (0x%02X), Task 6 chi ghi log, khong ve man hinh", OLED_ADDR);
    }

    createTaskOrHalt(vTask1_Blink,   "T1_Blink",   T1_STACK, T1_PRIO);
    createTaskOrHalt(vTask2_Sensor,  "T2_Sensor",  T2_STACK, T2_PRIO);
    createTaskOrHalt(vTask3_Button1, "T3_Btn1",    T3_STACK, T3_PRIO);
    createTaskOrHalt(vTask4_Button2, "T4_Btn2",    T4_STACK, T4_PRIO);
    createTaskOrHalt(vTask5_Led3,    "T5_Led3",    T5_STACK, T5_PRIO);
    createTaskOrHalt(vTask6_Display, "T6_Display", T6_STACK, T6_PRIO);

    logf("SYS", "Da tao Task 1..6 tren core %d", APP_CORE);
}

void loop() {
    vTaskDelete(NULL);
}
