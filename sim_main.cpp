// Verilator + SDL2 "virtual Nexys A7 / Nexys 4 DDR" front-end.
//
//  - Top module must be called FPGA and have the ports from the board XDC.
//  - Top of window : VGA monitor (640x480, reconstructed from VGA_HS/VGA_VS/RGB)
//  - Below         : 8-digit multiplexed 7-segment display
//  - Below         : 16 LEDs, 16 switches
//  - Bottom        : push buttons + the two RGB LEDs
//
// Keyboard:
//   SW15..SW8 : Q W E R T Y U I      (left to right, like on the board)
//   SW7..SW0  : A S D F G H J K
//   BTNU/D/L/R: arrow keys          BTNC: Space
//   CPU_RESETN: Backspace (held = reset asserted, i.e. pin driven low)
//   Esc       : quit
// Mouse: click switches to toggle them, hold buttons to press them.

#include <SDL2/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "VFPGA.h"
#include "verilated.h"

// ============================ configuration ============================
constexpr double CLK_HZ = 100e6;  // board clock
constexpr int VGA_DIV = 2;        // sample VGA every N sys clocks (100MHz/4 = 25MHz pixel clk)
constexpr int VGA_W = 800, VGA_H = 600;
// Offset (in pixel clocks / lines) from the *leading edge of sync* to first active pixel.
// 640x480@60: HS = 96 sync + 48 back porch, VS = 2 sync + 33 back porch.
constexpr int H_OFF = 120 + 64;
constexpr int V_OFF = 6 + 23;
constexpr bool HS_ACTIVE_LOW = true;
constexpr bool VS_ACTIVE_LOW = true;

constexpr int IO_SAMPLE = 13;          // sample LEDs/7seg every N clocks (odd => avoids aliasing)
constexpr double SEG_GAIN = 8.0;       // brightness boost so 1/8-duty multiplexed digits look fully lit
constexpr double SIM_BUDGET_S = 0.012; // max wall time spent simulating per frame

constexpr int WIN_W = 800, WIN_H = 960;
// =======================================================================

static VFPGA* top;

// ----------------------------- VGA capture -----------------------------
static uint32_t fb[VGA_W * VGA_H];
static int hcount = 0, vcount = 0;
static bool prev_hs = false, prev_vs = false;

static void vga_sample() {
    bool hs = top->VGA_HS, vs = top->VGA_VS;
    bool hs_act = HS_ACTIVE_LOW ? !hs : hs;
    bool vs_act = VS_ACTIVE_LOW ? !vs : vs;

    if (hs_act && !prev_hs) { hcount = 0; if (vcount < 100000) vcount++; }
    if (vs_act && !prev_vs) { vcount = 0; }
    prev_hs = hs_act;
    prev_vs = vs_act;

    int x = hcount - H_OFF, y = vcount - V_OFF;
    if (x >= 0 && x < VGA_W && y >= 0 && y < VGA_H) {
        uint32_t r = (top->VGA_R & 0xF) * 17;
        uint32_t g = (top->VGA_G & 0xF) * 17;
        uint32_t b = (top->VGA_B & 0xF) * 17;
        fb[y * VGA_W + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    if (hcount < 100000) hcount++;
}

// ------------------------ LED / 7-seg duty sampling ---------------------
// LED channels: 0..15 = LED[15:0], 16..18 = LED16 R,G,B, 19..21 = LED17 R,G,B
static uint32_t seg_acc[8][8];
static uint32_t led_acc[22];
static uint32_t io_total = 0;

static void io_sample() {
    uint8_t an_on = (uint8_t)~top->AN;  // anodes are active low
    uint8_t seg = (uint8_t)~(top->CA | (top->CB << 1) | (top->CC << 2) | (top->CD << 3) |
                             (top->CE << 4) | (top->CF << 5) | (top->CG << 6) | (top->DP << 7));
    io_total++;
    for (int d = 0; d < 8; d++) {
        if (!((an_on >> d) & 1)) continue;
        for (int s = 0; s < 8; s++) seg_acc[d][s] += (seg >> s) & 1;
    }
    uint32_t bits = top->LED | (top->LED16_R << 16) | (top->LED16_G << 17) | (top->LED16_B << 18) |
                    (top->LED17_R << 19) | (top->LED17_G << 20) | (top->LED17_B << 21);
    for (int i = 0; i < 22; i++) led_acc[i] += (bits >> i) & 1;
}

// ------------------------------ simulation ------------------------------
static int vga_ctr = 0, io_ctr = 0;

static inline void tick() {
    top->CLK100MHZ = 0;
    top->eval();
    top->CLK100MHZ = 1;
    top->eval();
    if (++vga_ctr == VGA_DIV) { vga_ctr = 0; vga_sample(); }
    if (++io_ctr == IO_SAMPLE) { io_ctr = 0; io_sample(); }
}

// ------------------------------- inputs ---------------------------------
enum Btn { B_C, B_U, B_L, B_R, B_D, B_RST, NBTN };
static uint16_t sw_state = 0;
static bool btn_kb[NBTN], btn_mouse[NBTN];

static const SDL_Scancode sw_keys[16] = {
    SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_H, SDL_SCANCODE_G,  // SW0..3
    SDL_SCANCODE_F, SDL_SCANCODE_D, SDL_SCANCODE_S, SDL_SCANCODE_A,  // SW4..7
    SDL_SCANCODE_I, SDL_SCANCODE_U, SDL_SCANCODE_Y, SDL_SCANCODE_T,  // SW8..11
    SDL_SCANCODE_R, SDL_SCANCODE_E, SDL_SCANCODE_W, SDL_SCANCODE_Q}; // SW12..15

static int btn_for_key(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_SPACE: return B_C;
        case SDL_SCANCODE_UP: return B_U;
        case SDL_SCANCODE_DOWN: return B_D;
        case SDL_SCANCODE_LEFT: return B_L;
        case SDL_SCANCODE_RIGHT: return B_R;
        case SDL_SCANCODE_BACKSPACE: return B_RST;
        default: return -1;
    }
}

// ------------------------------- layout ---------------------------------
constexpr int SEG_Y = 492+120, SEG_W = 50, SEG_H = 84, SEG_T = 7;
constexpr int LED_Y = 596+120, LED_SZ = 24;
constexpr int SW_Y = 646+120, SW_W = 24, SW_H = 44;
constexpr int BTN_CX = 120, BTN_CY = 770+120, BTN_SZ = 36, BTN_PITCH = 42;

static SDL_Rect sw_rect(int i) { return {(15 - i) * 40 + 8+80, SW_Y, SW_W, SW_H}; }
static SDL_Rect btn_rect(int b) {
    switch (b) {
        case B_C: return {BTN_CX+80 - BTN_SZ / 2, BTN_CY - BTN_SZ / 2, BTN_SZ, BTN_SZ};
        case B_U: return {BTN_CX+80 - BTN_SZ / 2, BTN_CY - BTN_PITCH - BTN_SZ / 2, BTN_SZ, BTN_SZ};
        case B_D: return {BTN_CX+80 - BTN_SZ / 2, BTN_CY + BTN_PITCH - BTN_SZ / 2, BTN_SZ, BTN_SZ};
        case B_L: return {BTN_CX+80 - BTN_PITCH - BTN_SZ / 2, BTN_CY - BTN_SZ / 2, BTN_SZ, BTN_SZ};
        case B_R: return {BTN_CX+80 + BTN_PITCH - BTN_SZ / 2, BTN_CY - BTN_SZ / 2, BTN_SZ, BTN_SZ};
        default:  return {280+80, BTN_CY - 18, 90, 36};  // reset
    }
}
static SDL_Rect rgb_rect(int k) { return {450 + k * 80+80, BTN_CY - 28, 56, 56}; }

static bool inside(const SDL_Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// ------------------------------- drawing --------------------------------
static const uint8_t SEG7[16] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
                                 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};  // bit0=a .. bit6=g

static void fill(SDL_Renderer* r, SDL_Rect rc, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_RenderFillRect(r, &rc);
}
static SDL_Color lerp(SDL_Color a, SDL_Color b, float t) {
    t = std::max(0.f, std::min(1.f, t));
    return {(Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t),
            (Uint8)(a.b + (b.b - a.b) * t), 255};
}

static void draw_digit(SDL_Renderer* r, int x, int y, int w, int h, int t, const float b[8],
                       SDL_Color on, SDL_Color off, bool dp) {
    int half = h / 2;
    SDL_Rect seg[7] = {
        {80+x + t, y, w - 2 * t, t},                       // a
        {80+x + w - t, y + t, t, half - t},                // b
        {80+x + w - t, y + half, t, h - t - half},         // c
        {80+x + t, y + h - t, w - 2 * t, t},               // d
        {80+x, y + half, t, h - t - half},                 // e
        {80+x, y + t, t, half - t},                        // f
        {80+x + t, y + half - t / 2, w - 2 * t, t}};       // g
    int order[7] = {0, 1, 2, 3, 4, 5, 6};
    std::sort(order, order + 7, [&](int p, int q) { return b[p] < b[q]; });  // lit segs drawn last
    for (int i : order) fill(r, seg[i], lerp(off, on, b[i]));
    if (dp) fill(r, {x + w + 4, y + h - t, t, t}, lerp(off, on, b[7]));
}

static void draw_num(SDL_Renderer* r, int cx, int y, int n, SDL_Color bg) {
    char buf[8];
    int len = snprintf(buf, sizeof buf, "%d", n);
    const int cw = 7, gap = 3;
    int x = cx - (len * cw + (len - 1) * gap) / 2;
    for (int i = 0; i < len; i++, x += cw + gap) {
        float b[8] = {0};
        for (int j = 0; j < 7; j++) b[j] = (SEG7[buf[i] - '0'] >> j) & 1;
        draw_digit(r, x, y, cw, 12, 2, b, {200, 200, 200, 255}, bg, false);
    }
}

static void draw_ui(SDL_Renderer* r) {
    const SDL_Color BG = {28, 32, 42, 255};
    fill(r, {0, VGA_H, WIN_W, WIN_H - VGA_H}, BG);

    // ---- brightness from duty cycles ----
    double tot = io_total ? (double)io_total : 1.0;
    float segb[8][8], ledb[22];
    for (int d = 0; d < 8; d++)
        for (int s = 0; s < 8; s++)
            segb[d][s] = (float)std::min(1.0, seg_acc[d][s] * SEG_GAIN / tot);
    for (int i = 0; i < 22; i++) ledb[i] = (float)(led_acc[i] / tot);
    memset(seg_acc, 0, sizeof seg_acc);
    memset(led_acc, 0, sizeof led_acc);
    io_total = 0;

    // ---- 7-segment (AN[7] leftmost, like on the board) ----
    fill(r, {0, VGA_H + 4, WIN_W, 96}, {8, 8, 8, 255});
    for (int c = 0; c < 8; c++) {
        int d = 7 - c;
        draw_digit(r, c * 80 + 10, SEG_Y, SEG_W, SEG_H, SEG_T, segb[d],
                   {255, 40, 30, 255}, {45, 14, 14, 255}, true);
    }

    // ---- LEDs ----
    for (int i = 0; i < 16; i++) {
        int c = 15 - i;
        SDL_Rect rc = {c * 40 + 8+80, LED_Y, LED_SZ, LED_SZ};
        fill(r, {rc.x - 2, rc.y - 2, rc.w + 4, rc.h + 4}, {10, 10, 10, 255});
        fill(r, rc, lerp({50, 40, 15, 255}, {255, 210, 40, 255}, ledb[i]));
        draw_num(r, rc.x + rc.w / 2 - 80, LED_Y + LED_SZ + 6, i, BG);
    }

    // ---- switches ----
    for (int i = 0; i < 16; i++) {
        SDL_Rect rc = sw_rect(i);
        bool on = (sw_state >> i) & 1;
        fill(r, rc, {10, 10, 10, 255});
        SDL_Rect knob = {rc.x + 2, on ? rc.y + 2 : rc.y + rc.h / 2, rc.w - 4, rc.h / 2 - 2};
        fill(r, knob, on ? SDL_Color{80, 220, 110, 255} : SDL_Color{120, 125, 135, 255});
        draw_num(r, rc.x + rc.w / 2 - 80, SW_Y + SW_H + 6, i, BG);
    }

    // ---- buttons ----
    for (int b = 0; b < NBTN; b++) {
        SDL_Rect rc = btn_rect(b);
        bool p = btn_kb[b] || btn_mouse[b];
        SDL_Color base = (b == B_RST) ? SDL_Color{150, 40, 40, 255} : SDL_Color{110, 115, 125, 255};
        SDL_Color lit = (b == B_RST) ? SDL_Color{255, 90, 90, 255} : SDL_Color{240, 240, 245, 255};
        fill(r, {rc.x - 2, rc.y - 2, rc.w + 4, rc.h + 4}, {10, 10, 10, 255});
        fill(r, rc, p ? lit : base);
    }

    // ---- RGB LEDs ----
    for (int k = 0; k < 2; k++) {
        SDL_Rect rc = rgb_rect(k);
        float R = ledb[16 + 3 * k], G = ledb[17 + 3 * k], B = ledb[18 + 3 * k];
        fill(r, {rc.x - 3, rc.y - 3, rc.w + 6, rc.h + 6}, {10, 10, 10, 255});
        fill(r, rc, {(Uint8)(30 + 225 * R), (Uint8)(30 + 225 * G), (Uint8)(30 + 225 * B), 255});
        draw_num(r, rc.x + rc.w / 2 - 80, rc.y + rc.h + 8, 16 + k, BG);
    }
}

// --------------------------------- main ---------------------------------
using clk = std::chrono::steady_clock;
static double secs(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

int main(int argc, char** argv) {
    double speed = 1.0;  // 1.0 = try to run at the real 100 MHz
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--speed") && i + 1 < argc) speed = atof(argv[++i]);

    Verilated::commandArgs(argc, argv);
    top = new VFPGA;
    top->CPU_RESETN = 1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window* win = SDL_CreateWindow("FPGA sim", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       WIN_W, WIN_H, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_RenderSetLogicalSize(ren, WIN_W, WIN_H);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         VGA_W, VGA_H);
    for (auto& p : fb) p = 0xFF000000u;

    printf("Switches : Q W E R T Y U I = SW15..SW8   A S D F G H J K = SW7..SW0 (or click them)\n"
           "Buttons  : arrows = BTNU/D/L/R, Space = BTNC, Backspace = CPU_RESETN (held = low)\n"
           "Esc quits.  Run with --speed X to scale target sim speed (default 1.0 = 100 MHz).\n");

    bool running = true;
    auto last = clk::now(), stat_t = last;
    uint64_t stat_cycles = 0;

    while (running) {
        // ---------- events ----------
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    bool down = e.type == SDL_KEYDOWN;
                    SDL_Scancode sc = e.key.keysym.scancode;
                    if (down && sc == SDL_SCANCODE_ESCAPE) running = false;
                    int b = btn_for_key(sc);
                    if (b >= 0) btn_kb[b] = down;
                    if (down && !e.key.repeat)
                        for (int i = 0; i < 16; i++)
                            if (sc == sw_keys[i]) sw_state ^= (1u << i);
                    break;
                }
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        for (int i = 0; i < 16; i++)
                            if (inside(sw_rect(i), e.button.x, e.button.y)) sw_state ^= (1u << i);
                        for (int b = 0; b < NBTN; b++)
                            if (inside(btn_rect(b), e.button.x, e.button.y)) btn_mouse[b] = true;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT) memset(btn_mouse, 0, sizeof btn_mouse);
                    break;
            }
        }

        top->SW = sw_state;
        top->BTNC = btn_kb[B_C] || btn_mouse[B_C];
        top->BTNU = btn_kb[B_U] || btn_mouse[B_U];
        top->BTNL = btn_kb[B_L] || btn_mouse[B_L];
        top->BTNR = btn_kb[B_R] || btn_mouse[B_R];
        top->BTND = btn_kb[B_D] || btn_mouse[B_D];
        top->CPU_RESETN = !(btn_kb[B_RST] || btn_mouse[B_RST]);  // active low

        // ---------- simulate ----------
        auto now = clk::now();
        double dt = std::min(secs(last, now), 0.05);
        last = now;
        long target = (long)(CLK_HZ * dt * speed), done = 0;
        auto t0 = clk::now();
        while (done < target) {
            long n = std::min(2048L, target - done);
            for (long i = 0; i < n; i++) tick();
            done += n;
            if (secs(t0, clk::now()) > SIM_BUDGET_S) break;
        }
        stat_cycles += done;

        // ---------- render ----------
        SDL_UpdateTexture(tex, nullptr, fb, VGA_W * 4);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_Rect dst = {0, 0, VGA_W, VGA_H};
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        draw_ui(ren);
        SDL_RenderPresent(ren);

        // ---------- stats / pacing ----------
        auto t1 = clk::now();
        if (secs(stat_t, t1) > 0.5) {
            double mhz = stat_cycles / secs(stat_t, t1) / 1e6;
            char title[128];
            snprintf(title, sizeof title, "FPGA sim - %.1f MHz simulated (%.0f%% of real time)", mhz,
                     mhz / (CLK_HZ / 1e6) * 100.0);
            SDL_SetWindowTitle(win, title);
            stat_t = t1;
            stat_cycles = 0;
        }
        double frame = secs(now, t1);
        if (frame < 1.0 / 60) SDL_Delay((Uint32)((1.0 / 60 - frame) * 1000));
    }

    top->final();
    delete top;
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
