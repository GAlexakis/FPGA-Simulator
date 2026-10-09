TOP        = FPGA
RTL        = $(wildcard rtl/*.v rtl/*.sv FPGA.v)
SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LIBS   = $(shell sdl2-config --libs)

all: obj_dir/V$(TOP)

obj_dir/V$(TOP): $(RTL) sim_main.cpp
	verilator -Wall -Wno-fatal --cc --exe --build -j 0 -O3 --top-module $(TOP) \
		$(RTL) sim_main.cpp \
		-CFLAGS "-O2 $(SDL_CFLAGS)" -LDFLAGS "$(SDL_LIBS)"

run: all
	./obj_dir/V$(TOP)

install:
	sudo apt update
	sudo apt install verilator libsdl2-dev g++ make

clean:
	rm -rf obj_dir

.PHONY: all run clean install
