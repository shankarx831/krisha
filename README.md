# KRISHA
**Kernel-level Reactive Integration for System Headless Audio**
> Architected and Developed by Shankar | Powered by the Radioform C++ DSP Engine

[![DSP Unit Tests](https://github.com/torteous44/radioform/actions/workflows/dsp_tests.yml/badge.svg)](https://github.com/torteous44/radioform/actions/workflows/dsp_tests.yml)
[![Build Spokes](https://github.com/torteous44/radioform/actions/workflows/build_spokes.yml/badge.svg)](https://github.com/torteous44/radioform/actions/workflows/build_spokes.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

KRISHA is an industry-grade, system-wide parametric equalizer engineered for extreme battery efficiency (**0.0% idle CPU overhead**) and ultra-low latency. Expanding upon the core Radioform C++ DSP Engine, Shankar designed and developed KRISHA as a 0.0% idle CPU, cross-platform hub-and-spoke architecture featuring native Windows, macOS, Linux, and Android drivers.

---

## 🏛 Architecture Overview: Hub & Spoke

KRISHA is split into a **Frozen C++ DSP Core (The Hub)** that encapsulates the complex mathematical logic, and **Platform Audio Hooks (The Spokes)** that feed audio buffers through the engine and present headless reactive user interfaces.

```mermaid
graph TD
    subgraph UI_Spokes ["Headless UI & Controller Spokes"]
        MacUI["macOS SwiftUI App"]
        WinUI["WinUI 3 Tray App"]
        LinUI["GTK4/Cairo App"]
        AndUI["Jetpack Compose App"]
    end

    subgraph Audio_Spokes ["OS Audio Driver Spokes"]
        MacSpoke["CoreAudio HAL Device"]
        WinSpoke["Windows sAPO COM"]
        LinSpoke["PipeWire Sink Daemon"]
        AndSpoke["Android DynamicsProcessing"]
    end

    subgraph Core_Hub ["Core DSP Hub (C++)"]
        Engine["DSP Engine Context"]
        Parser["AutoEq Text Parser"]
        Biquads["Biquad Banches"]
        Limiter["Soft Limiter"]
    end

    MacUI -->|VNode Monitor| MacSpoke
    WinUI -->|Memory-Mapped IPC| WinSpoke
    LinUI -->|Signals/Sockets| LinSpoke
    AndUI -->|JNI Bridge| AndSpoke

    MacSpoke -->|Static Link| Core_Hub
    WinSpoke -->|Static Link| Core_Hub
    LinSpoke -->|Static Link| Core_Hub
    AndSpoke -->|JNI Mapping| Core_Hub
```

### 1. The C++ DSP Hub
*   **Realtime-Safe Biquad Cascades**: Implements up to 10 biquad band processing filters (Peak, Shelf, Pass, Notch) written in lock-free, heap-allocation-free C++.
*   **Twin Preamp Smoother**: Provides click-free and zipper-free Left/Right preamp balance changes using a second-order exponential ramp evaluated over a 10ms boundary.
*   **Logarithmic Curve Evaluator**: Computes complex vector magnitude responses thread-safely:
    $$|H| = \sqrt{\frac{N_{real}^2 + N_{imag}^2}{D_{real}^2 + D_{imag}^2}}$$
*   **AutoEq Text Parser**: A pure, zero-dependency tokenizing parser capable of reading standard `ParametricEQ.txt` streams on any target platform.

### 2. The Platform Audio Spokes
*   **macOS Spoke**: Leverages the macOS CoreAudio Hardware Abstraction Layer (HAL) plugin architecture to construct system-wide virtual output proxies communicating via a lock-free shared memory ring buffer.
*   **Windows sAPO Spoke**: Implements native `IAudioProcessingObject` and `IAudioProcessingObjectRT` COM interfaces in `RadioformAPO.cpp` to process IEEE float streams directly inside Windows `audiodg.exe`.
*   **Linux PipeWire Spoke**: Creates a real-time virtual sink daemon at `packages/spokes/linux/main.c` that hooks into PipeWire streams with zero allocations or blocking system calls inside the hot audio thread.
*   **Android JNI Spoke**: Connects parsed AutoEq arrays straight to Android's high-performance `DynamicsProcessing` engine using highly efficient JNI boundaries.

---

## ⚡ The "0.0% Idle CPU" Achievement

Conventional equalizers utilize dispatch timers or periodic polls to monitor preset changes or refresh UI states, introducing battery drain and thread scheduling wakeups. KRISHA enforces a **purely event-driven reactive event loop** across all platform spokes to sleep at **0.0% CPU** when idle:

| Platform | IPC / System Monitor Mechanism | Idle CPU |
| :--- | :--- | :---: |
| **macOS** | Kernel VNode listener via `DispatchSource.makeFileSystemObjectSource` tracking `.write` events. | **0.0%** |
| **Windows** | Native Win32 tray notification message loop + Named `MemoryMappedFile` and `EventWaitHandle` signaling. | **0.0%** |
| **Linux** | GLib's `g_main_context_wakeup` event loops suspending GTK4/Cairo contexts inside kernel space. | **0.0%** |
| **Android** | Event-driven Kotlin state flow mapping + non-blocking background Coroutines. | **0.0%** |

---

## 📈 Advanced DSP & Algorithmic Optimizations

1.  **Zero-Branching 0.0 dB Bypass**: Precalculates and caches active biquad indices during preset updates. Flat or 0.0 dB bands are completely skipped in the render loop without branching overhead, eliminating CPU branch misprediction penalties.
2.  **Hardware Denormal Suppression**: Denormal (subnormal) floating-point numbers can cause 10x-100x instruction stalls on modern x86/ARM CPUs. Radioform enables hardware Flush-to-Zero (FTZ) and Denormals-Are-Zero (DAZ) CPU flags during thread initialization:
    ```cpp
    #if defined(__x86_64__) || defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ & DAZ
    #endif
    ```
3.  **Off-Thread 120-Step Logarithmic Graphing**: Rather than executing transfer function magnitude calculations on the main UI rendering thread, 120 logarithmic steps (20Hz to 20,000Hz) are evaluated on a background thread/queue with a 16ms debounce throttle.

---

## 📂 Repository Layout

```
radioform/
├── apps/                 # Headless Native UI applications
│   ├── mac/              # macOS SwiftUI Menu Bar app
│   ├── windows/          # WinUI 3 Tray app & P/Invoke graph
│   ├── linux/            # GTK4 / Cairo vector UI
│   └── android/          # Jetpack Compose / Coroutine UI
├── packages/             # Low-level system integration Spokes
│   ├── dsp/              # Core C++ DSP Hub & Test suites
│   ├── driver/           # macOS HAL Driver plugin
│   ├── host/             # Swift CoreAudio bridge host
│   └── spokes/           # Spokes wrapping the DSP Hub
│       ├── macOS/
│       ├── windows/      # Windows sAPO COM wrappers
│       ├── linux/        # PipeWire C sink daemon
│       └── android/      # JNI bridge wrappers
├── dist/                 # Release and DMG builds
└── tools/                # Automated packaging and codesign utilities
```

---

## 🛠 Compilation and Build Instructions

First, ensure you have a modern compiler, `cmake` (version 3.20+), and your platform's native development kits installed.

### 1. Compile C++ Core DSP & Run Unit Tests (All Platforms)
```bash
cd packages/dsp
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
# Execute the 39 unit tests
./tests/radioform_dsp_tests
```

### 2. Build macOS Release Bundle (`.app` + virtual HAL Driver)
```bash
# Performs versioning, compiles C++ core, HAL drivers, Swift Host, and packages dist/Radioform.app
make build
```

### 3. Build Windows sAPO Spoke (Visual Studio / CMake)
```bash
cd packages/spokes/windows
cmake -B build -S .
cmake --build build --config Release
```

### 4. Build Linux PipeWire Sink Spoke
```bash
cd packages/spokes/linux
cmake -B build -S .
cmake --build build --config Release
```

### 5. Build Android JNI Spoke
```bash
cd packages/spokes/android
cmake -B build -S .
cmake --build build --config Release
```

---

## 📜 License

KRISHA is released under the **GNU General Public License v3.0**. See the `LICENSE` file for details.
