#include <M5Unified.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "SCSCL.h"   // FEETECH シリアルバスサーボ (SCS0009) ドライバ

#include <cstdio>
#include <cmath>

static constexpr int DEFAULT_SECONDS = 90 * 60;

static constexpr int FACE_CX = 160;
static constexpr int FACE_CY = 60;
static constexpr int FACE_R  = 42;

static constexpr int TIME_BAND_Y0 = 110;
static constexpr int TIME_BAND_H  = 70;
static constexpr int TIME_CENTER_Y = 145;

static constexpr int BTN_TOP = 180;
static constexpr int BTN_H   = 60;

// ---- サーボ (首振り) 設定 ----------------------------------------------
// StackChan (CoreS3) の首は FEETECH SCS0009 シリアルバスサーボ2個。
// PWM ではなく UART1 (1Mbps, TX=GPIO6 / RX=GPIO7) でコマンドを送る。
// 値は M5Stack 公式 BSP (StackChan-BSP) と同一。
static constexpr uart_port_t SERVO_UART    = UART_NUM_1;
static constexpr int         SERVO_BAUD    = 1000000;
static constexpr int         SERVO_TX_GPIO = 6;
static constexpr int         SERVO_RX_GPIO = 7;

static constexpr int SERVO_YAW_ID   = 1;   // 水平 (左右)
static constexpr int SERVO_PITCH_ID = 2;   // 垂直 (上下)

// 中心位置 (BSP のデフォルト値)。実機を校正している場合はこの値を合わせること。
static constexpr int YAW_ZERO_POS   = 460;
static constexpr int PITCH_ZERO_POS = 620;

// SCS0009 は 1step ≒ 0.3125度。step = 角度[度] * 16 / 5。
// 首振り: 垂直サーボを基準角の上下にゆっくり振る (うなずき)。水平は中央で固定。
static constexpr double SERVO_SWAY_AMPLITUDE_DEG = 15.0;  // 上下の振れ幅 (度)
static constexpr double SERVO_SWAY_PERIOD_S      = 2.5;   // 首振り周期 (秒)
static constexpr double PITCH_NEUTRAL_DEG        = 35.0;  // 上下の基準角 (推奨 5〜85度内)

// サーボ電源 (VM) は PY32 IO エクスパンダ (I2C 0x6F) の pin0 = VM_EN で投入する。
// これを High にしないとサーボに給電されず、一切動かない。
static constexpr uint8_t PY32_ADDR        = 0x6F;
static constexpr uint8_t PY32_REG_GPIO_M  = 0x03;  // 方向 (1=出力)
static constexpr uint8_t PY32_REG_GPIO_O  = 0x05;  // 出力レベル
static constexpr uint8_t PY32_REG_GPIO_PU = 0x09;  // プルアップ
static constexpr uint8_t PY32_VM_EN_PIN   = 0;

static SCSCL g_scs;

// 通知音の音量 (0〜255)。
static constexpr uint8_t VOL_SMALL = 60;   // 残り 15/5/1 分の小さい通知音
static constexpr uint8_t VOL_LOUD  = 180;  // 終了直前・時間切れの音

enum FaceMood {
    MOOD_NORMAL,
    MOOD_SERIOUS,
    MOOD_PANIC,
    MOOD_URGENT,    // 残り1分
    MOOD_SURPRISED,
};

static int     remaining_seconds = DEFAULT_SECONDS;
static bool    running           = false;
static bool    time_up           = false;
static int64_t last_tick_us      = 0;
static bool    touch_was_down    = false;

static int      last_drawn_seconds = -1;
static FaceMood last_drawn_mood    = static_cast<FaceMood>(-1);
static int      last_drawn_running = -1;
static bool     first_draw         = true;

static FaceMood compute_mood() {
    if (time_up) return MOOD_SURPRISED;
    if (remaining_seconds <= 1 * 60)  return MOOD_URGENT;
    if (remaining_seconds <= 5 * 60)  return MOOD_PANIC;
    if (remaining_seconds <= 15 * 60) return MOOD_SERIOUS;
    return MOOD_NORMAL;
}

static uint16_t face_color_for(FaceMood m) {
    switch (m) {
        case MOOD_SERIOUS:   return TFT_ORANGE;
        case MOOD_PANIC:     return TFT_RED;
        case MOOD_URGENT:    return TFT_MAGENTA;
        case MOOD_SURPRISED: return TFT_CYAN;
        default:             return TFT_YELLOW;
    }
}

static void draw_face(FaceMood mood) {
    auto &lcd = M5.Display;
    const int cx = FACE_CX;
    const int cy = FACE_CY;
    const int r  = FACE_R;

    lcd.fillRect(cx - r - 6, cy - r - 6, 2 * r + 12, 2 * r + 12, TFT_BLACK);

    const uint16_t fc = face_color_for(mood);

    lcd.fillCircle(cx, cy, r, fc);
    lcd.drawCircle(cx, cy, r, TFT_WHITE);

    const int eye_dx = r / 3;
    const int eye_dy = r / 4;
    const int elx = cx - eye_dx;
    const int erx = cx + eye_dx;
    const int ey  = cy - eye_dy;

    switch (mood) {
        case MOOD_NORMAL: {
            lcd.fillCircle(elx, ey, 4, TFT_BLACK);
            lcd.fillCircle(erx, ey, 4, TFT_BLACK);

            const int mr = r / 2;
            const int my = cy + r / 8;
            lcd.fillCircle(cx, my, mr,     TFT_BLACK);
            lcd.fillCircle(cx, my, mr - 3, fc);
            lcd.fillRect(cx - mr - 1, my - mr - 1, 2 * mr + 2, mr + 1, fc);
            break;
        }
        case MOOD_SERIOUS: {
            lcd.fillCircle(elx, ey, 3, TFT_BLACK);
            lcd.fillCircle(erx, ey, 3, TFT_BLACK);
            lcd.drawLine(elx - 6, ey - 9, elx + 6, ey - 5, TFT_BLACK);
            lcd.drawLine(erx - 6, ey - 5, erx + 6, ey - 9, TFT_BLACK);
            const int my = cy + r / 3;
            lcd.fillRect(cx - r / 3, my, 2 * (r / 3), 3, TFT_BLACK);
            break;
        }
        case MOOD_PANIC: {
            lcd.drawCircle(elx, ey, 7, TFT_BLACK);
            lcd.drawCircle(erx, ey, 7, TFT_BLACK);
            lcd.fillCircle(elx, ey, 2, TFT_BLACK);
            lcd.fillCircle(erx, ey, 2, TFT_BLACK);
            const int my = cy + r / 3;
            lcd.fillEllipse(cx, my, r / 3, r / 6, TFT_BLACK);
            lcd.fillCircle(cx + r - 4, cy - r / 2 + 4, 3, TFT_BLUE);
            break;
        }
        case MOOD_URGENT: {
            // 残り1分: ギュッとつぶった目 (><) と叫ぶ口で焦り MAX
            const int e = 7;
            lcd.drawWideLine(elx - e, ey - e, elx + e, ey + e, 3, TFT_BLACK);
            lcd.drawWideLine(elx - e, ey + e, elx + e, ey - e, 3, TFT_BLACK);
            lcd.drawWideLine(erx - e, ey - e, erx + e, ey + e, 3, TFT_BLACK);
            lcd.drawWideLine(erx - e, ey + e, erx + e, ey - e, 3, TFT_BLACK);
            const int my = cy + r / 3;
            lcd.fillEllipse(cx, my, r / 4, r / 5, TFT_BLACK);  // 大きく開いた口
            lcd.fillCircle(cx - r + 2, cy - r / 3,     3, TFT_CYAN);  // 汗
            lcd.fillCircle(cx + r - 2, cy - r / 3 + 4, 3, TFT_CYAN);
            break;
        }
        case MOOD_SURPRISED: {
            lcd.fillCircle(elx, ey, 7, TFT_WHITE);
            lcd.fillCircle(erx, ey, 7, TFT_WHITE);
            lcd.drawCircle(elx, ey, 7, TFT_BLACK);
            lcd.drawCircle(erx, ey, 7, TFT_BLACK);
            lcd.fillCircle(elx, ey, 3, TFT_BLACK);
            lcd.fillCircle(erx, ey, 3, TFT_BLACK);
            const int my = cy + r / 3;
            lcd.fillCircle(cx, my, r / 4, TFT_BLACK);
            break;
        }
    }
}

static void draw_time() {
    auto &lcd = M5.Display;
    const int w = lcd.width();

    lcd.fillRect(0, TIME_BAND_Y0, w, TIME_BAND_H, TFT_BLACK);

    lcd.setTextDatum(middle_center);

    if (time_up) {
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.setTextSize(5);
        lcd.drawString("TIME UP", w / 2, TIME_CENTER_Y);
        return;
    }

    const int m = remaining_seconds / 60;
    const int s = remaining_seconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);

    const FaceMood mood = compute_mood();
    uint16_t color = TFT_GREEN;
    if (mood == MOOD_URGENT)       color = TFT_MAGENTA;
    else if (mood == MOOD_PANIC)   color = TFT_RED;
    else if (mood == MOOD_SERIOUS) color = TFT_YELLOW;

    lcd.setTextColor(color, TFT_BLACK);
    lcd.setTextSize(7);
    lcd.drawString(buf, w / 2, TIME_CENTER_Y);
}

static void draw_buttons() {
    auto &lcd = M5.Display;
    const int w  = lcd.width();
    const int bw = w / 4;
    const int bh = BTN_H;

    lcd.fillRect(0, BTN_TOP, w, bh, TFT_BLACK);

    const char *labels[4] = {
        "-5min",
        "RESET",
        running ? "PAUSE" : "START",
        "+5min",
    };
    const uint16_t fills[4] = {
        TFT_DARKGREY,
        TFT_PURPLE,
        static_cast<uint16_t>(running ? TFT_MAROON : TFT_DARKGREEN),
        TFT_NAVY,
    };

    for (int i = 0; i < 4; i++) {
        const int x = i * bw;
        lcd.fillRect(x + 4, BTN_TOP + 4, bw - 8, bh - 8, fills[i]);
        lcd.drawRect(x + 1, BTN_TOP + 1, bw - 2, bh - 2, TFT_WHITE);
        lcd.drawRect(x + 2, BTN_TOP + 2, bw - 4, bh - 4, TFT_WHITE);
    }

    lcd.setTextDatum(middle_center);
    lcd.setTextSize(2);
    for (int i = 0; i < 4; i++) {
        const int x = i * bw + bw / 2;
        const int y = BTN_TOP + bh / 2;
        lcd.setTextColor(TFT_WHITE, fills[i]);
        lcd.drawString(labels[i], x, y);
    }
}

static void redraw_all() {
    auto &lcd = M5.Display;
    lcd.fillScreen(TFT_BLACK);
    draw_face(compute_mood());
    draw_time();
    draw_buttons();
}

static void refresh_display() {
    if (first_draw) {
        redraw_all();
        last_drawn_seconds = remaining_seconds;
        last_drawn_mood    = compute_mood();
        last_drawn_running = running ? 1 : 0;
        first_draw = false;
        return;
    }

    const FaceMood mood = compute_mood();
    const bool mood_changed     = (mood != last_drawn_mood);
    const bool seconds_changed  = (remaining_seconds != last_drawn_seconds);
    const bool running_changed  = ((running ? 1 : 0) != last_drawn_running);

    if (mood_changed) {
        draw_face(mood);
        last_drawn_mood = mood;
    }
    if (seconds_changed || mood_changed) {
        draw_time();
        last_drawn_seconds = remaining_seconds;
    }
    if (running_changed) {
        draw_buttons();
        last_drawn_running = running ? 1 : 0;
    }
}

// SCS0009 のサーボ電源 (VM) を PY32 IO エクスパンダ経由で投入する。
// pin0 を「出力 + プルアップ + High」に設定する (BSP の setServoPowerEnabled 相当)。
static void servo_power_on() {
    const uint32_t f = 100000;
    const uint8_t mask = 1 << PY32_VM_EN_PIN;
    M5.In_I2C.bitOn(PY32_ADDR, PY32_REG_GPIO_M,  mask, f);  // 出力に設定
    M5.In_I2C.bitOn(PY32_ADDR, PY32_REG_GPIO_PU, mask, f);  // プルアップ
    M5.In_I2C.bitOn(PY32_ADDR, PY32_REG_GPIO_O,  mask, f);  // High = 電源ON
}

// 角度 [度] を SCS の目標位置 (0〜1023) へ変換して指令する。
//   pos = zero_pos + 角度 * 16 / 5   (1step ≒ 0.3125度)
static void servo_write_deg(int id, int zero_pos, double deg) {
    int pos = zero_pos + static_cast<int>(std::lround(deg * 16.0 / 5.0));
    if (pos < 0)    pos = 0;
    if (pos > 1023) pos = 1023;
    g_scs.WritePos(static_cast<u8>(id), static_cast<u16>(pos), 20, 0);  // Time=20ms, Speed=0
}

// 水平・垂直サーボを基準姿勢へ戻す。
static void servo_center() {
    servo_write_deg(SERVO_YAW_ID,   YAW_ZERO_POS,   0.0);
    servo_write_deg(SERVO_PITCH_ID, PITCH_ZERO_POS, PITCH_NEUTRAL_DEG);
}

// サーボ電源を入れ、UART バスを開いてトルクON、基準姿勢へ。
static void servo_init() {
    servo_power_on();
    vTaskDelay(pdMS_TO_TICKS(200));  // 電源安定待ち

    g_scs.begin(SERVO_UART, SERVO_BAUD, SERVO_TX_GPIO, SERVO_RX_GPIO);

    g_scs.EnableTorque(SERVO_YAW_ID,   1);
    g_scs.EnableTorque(SERVO_PITCH_ID, 1);

    servo_center();
}

// Slowly nod a servo-mounted head while the timer is running.
// esp_timer_get_time() を時間源に sin() で垂直サーボを基準角の上下に振る。
// 水平サーボは中央で固定して「うなずき」に見せる。
static void update_servo_motion() {
    const double t     = static_cast<double>(esp_timer_get_time()) / 1000000.0;
    const double phase = std::sin(2.0 * M_PI * t / SERVO_SWAY_PERIOD_S);  // -1.0 .. 1.0
    servo_write_deg(SERVO_YAW_ID,   YAW_ZERO_POS,   0.0);
    servo_write_deg(SERVO_PITCH_ID, PITCH_ZERO_POS, PITCH_NEUTRAL_DEG + phase * SERVO_SWAY_AMPLITUDE_DEG);
}

static void handle_touch() {
    auto t = M5.Touch.getDetail();
    const bool pressed_now = t.isPressed();

    if (pressed_now && !touch_was_down) {
        const int x  = t.x;
        const int y  = t.y;
        const int w  = M5.Display.width();
        const int bw = w / 4;

        if (y >= BTN_TOP) {
            if (x < bw) {
                remaining_seconds -= 5 * 60;
                if (remaining_seconds < 0) remaining_seconds = 0;
                if (remaining_seconds > 0) time_up = false;
            } else if (x < 2 * bw) {
                remaining_seconds = DEFAULT_SECONDS;
                running = false;
                time_up = false;
                last_tick_us = esp_timer_get_time();
                first_draw = true;  // 次の refresh_display() で画面全体を再描画
            } else if (x < 3 * bw) {
                if (time_up) {
                    remaining_seconds = DEFAULT_SECONDS;
                    time_up = false;
                    running = true;
                } else {
                    running = !running;
                }
                last_tick_us = esp_timer_get_time();
            } else {
                remaining_seconds += 5 * 60;
                time_up = false;
            }
        }
    }

    touch_was_down = pressed_now;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Speaker.begin();
    servo_init();

    last_tick_us = esp_timer_get_time();
    refresh_display();
}

void loop() {
    M5.update();
    handle_touch();

    if (running && !time_up) {
        const int64_t now = esp_timer_get_time();
        if (now - last_tick_us >= 1000000) {
            const int elapsed = static_cast<int>((now - last_tick_us) / 1000000);
            last_tick_us += static_cast<int64_t>(elapsed) * 1000000;
            remaining_seconds -= elapsed;

            if (remaining_seconds <= 0) {
                remaining_seconds = 0;
                running = false;
                time_up = true;
                M5.Speaker.setVolume(VOL_LOUD);
                M5.Speaker.tone(1200, 800);
            } else if (remaining_seconds == 15 * 60 ||
                       remaining_seconds == 5 * 60 ||
                       remaining_seconds == 1 * 60) {
                // 残り 15/5/1 分: 小さい音で通知
                M5.Speaker.setVolume(VOL_SMALL);
                M5.Speaker.tone(880, 150);
            } else if (remaining_seconds <= 10) {
                // 終了直前のカウントダウン
                M5.Speaker.setVolume(VOL_LOUD);
                M5.Speaker.tone(1000, 120);
            }
        }
        update_servo_motion();
    } else {
        // 停止中・時間切れ時は首を基準姿勢で静止させる。
        servo_center();
    }

    refresh_display();
    delay(20);
}
