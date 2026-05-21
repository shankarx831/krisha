# macOS Spoke: CoreAudio HAL Wrapper

The macOS spoke leverages the CoreAudio HAL (Hardware Abstraction Layer) plugin architecture to create high-performance virtual proxy output devices.

## Architecture Overview

```
Application audio
    │
    ▼
┌──────────────────────────┐
│ macOS CoreAudio (HAL)    │
│ routes output to proxy   │
└───────────┬──────────────┘
            │
            ▼
┌──────────────────────────┐
│ KrishaDriver.driver   │  <-- HAL Plugin (runs inside coreaudiod)
│                          │
│  - OnWriteMixedOutput()  │
│  - Linear interpolation  │
│  - Ring buffer write     │
└───────────┬──────────────┘
            │ shared memory (mmap)
            │ /tmp/krisha-<uid>
            ▼
┌──────────────────────────┐
│ KrishaHost            │  <-- Headless Swift Host Process
│                          │
│  - Ring buffer read      │
│  - DSP engine run        │
│  - Hardware audio write  │
└──────────────────────────┘
```

## Core Components

1.  **Plugin Runtime (`packages/driver/src/Plugin.cpp`)**:
    *   Creates output-only proxy devices matching desired devices listed in `/tmp/krisha-devices.txt`.
    *   Implements the `UniversalAudioHandler` for stream IO.
    *   Leverages the **`libASPL`** helper library to wrap the complex CoreAudio C API structures with high-level C++ objects.
2.  **Shared Memory Ring Buffer (`packages/driver/include/RFSharedAudio.h`)**:
    *   Implements a single-producer (driver), single-consumer (host) lock-free circular ring buffer.
    *   Uses 64-bit atomic monotonic read and write indices to prevent wrapping issues.
    *   Manages overruns and underruns gracefully.
3.  **Liveness & Heartbeat Protocol**:
    *   The driver writes to a `driver_heartbeat` atomic variable during every audio IO callback.
    *   The host writes to a `host_heartbeat` variable on a 1-second timer.
    *   If either side is dead for more than 5 seconds, the proxy device is torn down or reset.

## Build and Install

```bash
# Build the HAL driver
cd packages/driver
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Install the driver
./install.sh
sudo killall coreaudiod
```
