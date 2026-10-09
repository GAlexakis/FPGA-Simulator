# Verilator + SDL2 Virtual FPGA Board

Run and interact with your Verilog/SystemVerilog designs in real time on a virtual **Digilent Nexys A7 / Nexys 4 DDR**-style board, without owning the hardware.

The design is simulated with [Verilator](https://www.veripool.org/verilator/) and wrapped in a C++ front-end that uses [SDL2](https://www.libsdl.org/) to draw the board in a window. Press keys (or click) to flip switches and push buttons, and watch the LEDs, the 7-segment display and a live VGA monitor respond as they would on the real board.

```
+--------------------------------------+
|                                      |
|          VGA monitor 640x480         |
|     (rebuilt from HS/VS + RGB)       |
|                                      |
+--------------------------------------+
|   8 digit multiplexed 7-segment      |
+--------------------------------------+
|   LED15 ......................LED0   |
|   SW15  ......................SW0    |
+--------------------------------------+
|        [U]                           |
|   [L]  [C]  [R]    [RESET]  RGB16/17 |
|        [D]                           |
+--------------------------------------+
```

## Features

- Top module uses the **same port names as the board's XDC constraints**, so a design that works here can be built for the board with the stock constraints file.
- **VGA output** reconstructed from `VGA_HS`, `VGA_VS`, `VGA_R/G/B` (4 bits per channel), so sync timing bugs show up just like on a real monitor.
- **Multiplexed 7-segment display**: segment brightness is computed from duty cycle, so a properly refreshed display looks steady and a badly refreshed one flickers or looks dim.
- **LED duty-cycle sampling**: PWM-dimmed LEDs and the RGB LEDs show the right brightness and mixed colours.
- **Keyboard and mouse input** for 16 switches, 5 push buttons and the active-low `CPU_RESETN` button.
- Live **simulation speed** readout in the window title.
- Resizable window with automatic scaling.

## Requirements

- Verilator 4.x or 5.x
- SDL2 development files
- A C++ compiler (g++ or clang++) and `make`

### Install on Debian / Ubuntu (including WSL)

```bash
sudo apt install verilator libsdl2-dev g++ make
```

### Install on Fedora

```bash
sudo dnf install verilator SDL2-devel gcc-c++ make
```

### Install on macOS (Homebrew)

```bash
brew install verilator sdl2
```

### WSL notes

- **Windows 11 / recent Windows 10 with WSL2:** WSLg is built in and the window opens with no extra setup. If it does not, run `wsl --update` in PowerShell, then `wsl --shutdown` and reopen your distro.
- **Older setups:** install an X server such as VcXsrv (enable "Disable access control") and set `DISPLAY` (for WSL2: `export DISPLAY=$(grep nameserver /etc/resolv.conf | awk '{print $2}'):0`).
- Keep the project inside the Linux filesystem (`~/fpga_sim`), not under `/mnt/c`, or builds will be very slow.

## Quick start

```bash
git clone https://github.com/GAlexakis/FPGA-Simulator.git
cd FPGA-Simulator
make run
```

The included demo design (`rtl/`) shows a tic-tac-toe game (`https://github.com/GAlexakis/IC-TicTacToe.git`).

## Controls

### Keyboard

| Input | Key |
|---|---|
| SW15 ... SW8 | `Q` `W` `E` `R` `T` `Y` `U` `I` (left to right) |
| SW7 ... SW0 | `A` `S` `D` `F` `G` `H` `J` `K` (left to right) |
| BTNU / BTND / BTNL / BTNR | Arrow keys |
| BTNC | `Space` |
| CPU_RESETN | `Backspace` (held = pin driven low) |
| Quit | `Esc` |

Switch keys **toggle** the switch on each press (like a slide switch). Buttons are **momentary**.

### Mouse

- Click a switch to toggle it.
- Hold the left mouse button on a button to press it.

## Using your own design

1. Put your `.v` / `.sv` files in `rtl/`. They are all picked up automatically by the Makefile.
2. Instantiate your top module inside `FPGA.v` and connect the ports below.
3. Run `make run`.

### Required top-level ports

```verilog
module FPGA (
    input  wire        CLK100MHZ,
    input  wire [15:0] SW,
    output wire [15:0] LED,
    output wire        LED16_B, LED16_G, LED16_R,
    output wire        LED17_B, LED17_G, LED17_R,
    output wire        CA, CB, CC, CD, CE, CF, CG, DP,
    output wire [7:0]  AN,
    input  wire        CPU_RESETN, BTNC, BTNU, BTNL, BTNR, BTND,
    output wire [3:0]  VGA_R, VGA_G, VGA_B,
    output wire        VGA_HS, VGA_VS
);
```

| Port | Dir | Description |
|---|---|---|
| `CLK100MHZ` | in | 100 MHz system clock |
| `SW[15:0]` | in | Slide switches |
| `LED[15:0]` | out | LEDs, active high |
| `LED16_*`, `LED17_*` | out | RGB LEDs, active high |
| `CA`..`CG`, `DP` | out | 7-segment cathodes (segments a-g and decimal point), **active low** |
| `AN[7:0]` | out | 7-segment digit enables, **active low**, `AN[7]` is the leftmost digit |
| `CPU_RESETN` | in | Reset button, **active low** (1 when not pressed) |
| `BTNC/U/L/R/D` | in | Push buttons, active high |
| `VGA_R/G/B[3:0]` | out | 4-bit VGA colour channels |
| `VGA_HS`, `VGA_VS` | out | VGA sync, negative polarity |

## Command-line options

```
./obj_dir/VFPGA [--speed X]
```

| Option | Description |
|---|---|
| `--speed X` | Target simulation speed as a multiple of real time. Default `1.0` tries to run at 100 MHz. Use `0.1` to slow things down 10x (e.g. to watch a 1 Hz blink), or a value above 1 to run faster if your machine can keep up. |

Standard Verilator `+verilator+...` runtime arguments are also passed through.

## How it works

The main loop runs once per rendered frame (about 60 per second):

1. **Poll input** and drive `SW`, the buttons and `CPU_RESETN`.
2. **Simulate** `100e6 * frame_time * speed` clock cycles, limited to about 12 ms of wall-clock time per frame.
3. **Render** the VGA frame buffer, the 7-segment display, the LEDs, the switches and the buttons.

During simulation, two samplers run alongside the clock:

### VGA capture

Every 2nd system clock (`VGA_DIV`, giving the standard 50 MHz pixel rate) the testbench samples `VGA_HS`, `VGA_VS` and the colour lines. A falling edge on `HS` resets the horizontal counter and advances the line counter, and a falling edge on `VS` resets the line counter. Pixels are written into a 800x6000 frame buffer at positions offset from the leading sync edges by `H_OFF` (120 sync + 64 back porch) and `V_OFF` (6 sync + 23 back porch). Because the picture is positioned using your sync signals, wrong timing (shifted or missing image, rolling picture) is visible in the window.

### 7-segment and LED duty cycle

Every 13th clock (`IO_SAMPLE`, an odd number to avoid aliasing with power-of-two counters) the testbench records which digit is enabled and which segments are lit, plus the LED outputs. At the end of each frame the accumulated counts become brightness values. For the 7-segment display a gain (`SEG_GAIN`, default 8) is applied so that a digit lit for 1/8 of the time in an 8-digit multiplex looks fully lit, as it does to the eye on the real board.

## Configuration

Most tuning constants are at the top of `sim_main.cpp`:

| Constant | Default | Meaning |
|---|---|---|
| `CLK_HZ` | `100e6` | Board clock frequency used for pacing |
| `VGA_DIV` | `2` | System clocks per VGA pixel (100 MHz / 2 = 50 MHz) |
| `VGA_W`, `VGA_H` | `800`, `600` | Visible VGA resolution |
| `H_OFF`, `V_OFF` | `184`, `29` | Pixels / lines from sync leading edge to first visible pixel |
| `HS_ACTIVE_LOW`, `VS_ACTIVE_LOW` | `true` | Sync polarity |
| `IO_SAMPLE` | `13` | Clocks between LED / 7-segment samples |
| `SEG_GAIN` | `8.0` | 7-segment brightness gain |
| `SIM_BUDGET_S` | `0.012` | Max wall time spent simulating per frame |
| `WIN_W`, `WIN_H` | `800`, `920` | Logical window size |


## Project structure

```
fpga_sim/
  Makefile        Builds the design and testbench with Verilator
  sim_main.cpp    SDL2 front-end, VGA capture, LED/7-seg sampling, input
  FPGA.v          Top module for simulation (isntantiate your design inside)
  rtl/
    *.sv          Demo design (replace with your own)
  README.md
```

Build output goes to `obj_dir/` (created by Verilator, removed with `make clean`).



