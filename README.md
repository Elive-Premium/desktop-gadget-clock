# Clock Gadget

A beautiful desktop clock gadget built with EFL (Enlightenment Foundation Libraries).

## Features
- Transparent, borderless window
- Smooth hover animations
- Close button appears on mouse hover
- Draggable window

## Building

### Prerequisites
- EFL development libraries
- Meson build system
- Ninja

### Compile
```bash
meson setup build
cd build
meson compile
./src/clock-gadget --help
```
