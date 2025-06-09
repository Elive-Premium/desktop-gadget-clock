# Elive Clock

A beautiful desktop clock built with EFL (Enlightenment Foundation Libraries).

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

### Compile and Run
```bash
meson setup build && meson compile -C build
./build/src/clock-gadget --seconds
```
