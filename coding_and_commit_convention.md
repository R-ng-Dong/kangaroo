# C/C++ Embedded Coding & Commit Convention

---

## 1. Naming Conventions

| Element | Convention | Example |
|---|---|---|
| Files | `snake_case.c` / `snake_case.h` | `uart_driver.c` |
| Variables | `snake_case` | `sensor_value` |
| Functions | `SnakeCase` (PascalCase) | `ReadSensor()`, `InitUart()` |
| Constants / Macros | `UPPER_SNAKE_CASE` | `MAX_BUFFER_SIZE` |
| Types (struct/enum/typedef) | `PascalCase` | `UartConfig`, `ErrorCode` |
| Project type aliases | `UI8`, `SI8`, `UI16`, `SI16`, `UI32`, `SI32`, `UI64`, `SI64` | `UI8 count;` instead of `uint8_t count;` |
| Enum members | `UPPER_SNAKE_CASE` | `ERROR_TIMEOUT` |
| Global variables | `g_` prefix | `g_system_state` |
| Static / file-local variables | `s_` prefix | `s_retry_count` |
| Boolean-like values | `UI8` with `TRUE` / `FALSE` | `UI8 connected = TRUE;` |
| Loop / counter variables | **Meaningful names only** — no `i`, `j`, `k` | `profile_index`, `retry_count`, `byte_pos` |

---

## 2. Formatting

- Indentation: 4 spaces, no tabs
- Brace style: K&R (opening brace on same line)
- Max line length: 100 characters
- Pointer alignment: `int *p` (asterisk next to variable name)
- Always use braces for `if` / `for` / `while`, even for single statements
- One statement per line
- Consistent spacing around operators and after commas
- **Yoda conditions**: literal/constant on the left side of comparisons to prevent accidental assignment (e.g. `if (NULL == ptr)` instead of `if (ptr == NULL)`)

```c
if (NULL == ptr) {        // GOOD: literal on left
if (0 == count) {         // GOOD
if (MAX_SIZE > len) {     // GOOD
if (ptr == NULL) {        // BAD: variable on left
if (count == 0) {         // BAD
```

---

## 3. Header Files

- Include guards: `#ifndef` / `#define` / `#endif` (preferred over `#pragma once` for older/varied embedded toolchains)
- Include order, grouped and alphabetized within each group:
  1. Standard library headers (`<stdint.h>`, `<stddef.h>` — **never** `<stdbool.h>`)
  2. Vendor/SDK/HAL headers
  3. Project headers (`"common.h"` for type aliases)
- Each file includes only what it directly uses (no relying on transitive includes)

```c
#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>

#include "common.h"

#include "stm32_hal.h"

#include "error_codes.h"

#endif // UART_DRIVER_H
```

---

## 4. Memory & Resource Management (No Dynamic Allocation)

- **No `malloc` / `free`, no `new` / `delete`** anywhere in the codebase
- All buffers statically or stack allocated, with fixed, compile-time-known sizes
- Every buffer sized via a named constant — no magic numbers
- No unbounded-depth recursion (stack budget is fixed and limited)
- If C++ is used: no dynamically-allocating STL containers (`std::vector`, `std::string`, etc.) — prefer `std::array` or fixed-size custom containers
- Document worst-case stack usage for functions with deep call chains

---

## 5. Error Handling

- Return codes only — no exceptions
- Centralized `ErrorCode` enum shared across modules
- `assert()` for debug-build sanity checks; explicit return-code checks for production error paths
- ISR functions: no blocking calls, no error propagation via return value — use flags/queues instead

---

## 6. Comments & Documentation

- Doxygen-style comments (`/** ... */`) for all public functions and types
- Tagged TODOs: `// TODO(username): description`
- Tagged FIXMEs: `// FIXME(username): description`
- Any magic number tied to a register or hardware behavior must cite the datasheet section

---

## 7. Language Feature Rules

- Standard: C11
- No dynamic memory (see Section 4)
- **No `goto`**
- `volatile` required for all hardware register variables and any variable shared with an ISR
- Use project type aliases (`UI8`, `SI8`, `UI16`, `SI16`, `UI32`, `SI32`, `UI64`, `SI64`) from `common.h` instead of `uint8_t`, `int32_t`, etc.
- **No `bool`** — use `UI8` with `TRUE`/`FALSE` macros
- **No floating point (`double`)** — use fixed-point integer math instead (e.g. temperature x10 as `SI8`)
- `const`-qualify all read-only data (enables placement in ROM/flash)
- `static_assert` for compile-time checks on struct size/layout and enum ranges

---

## 8. Hardware & Register Access

- All memory-mapped register access goes through defined structs/macros — no raw magic addresses in logic code
- Register definitions centralized per peripheral (e.g., `uart_regs.h`)
- Bit manipulation via named masks/shifts, never inline magic numbers
  - Bad: `reg |= (1 << 3);`
  - Good: `reg |= UART_TX_ENABLE_BIT;`
- Avoid compiler-dependent bit-field struct layouts for registers — use explicit masking instead

---

## 9. Interrupts (ISR) Rules

- ISR bodies kept minimal: no blocking calls, no long loops, no dynamic allocation
- Variables shared between ISR and main code marked `volatile`
- Critical sections explicitly guarded (disable/enable interrupts, or RTOS primitive) — document maximum interrupt-disable duration
- No unbounded loops inside an ISR

---

## 10. Concurrency / RTOS 

- Consistent naming for tasks, semaphores, mutexes, and queues
- Documented priority-inversion handling policy
- Stack size per task documented and justified

---

## 11. Timing & Determinism

- No unbounded busy-wait loops — always bound with a max iteration count or hardware timer timeout
- Worst-case execution time (WCET) documented for time-critical functions

---

## 12. Portability / Compiler

- Compiler/toolchain version pinned per project
- No reliance on undefined or implementation-defined behavior (signed overflow, unaligned access) unless explicitly justified and commented
- Endianness handled explicitly for any byte-level protocol work

---

## 13. Safety / Reliability

- Watchdog timer usage/reset policy documented, if applicable
- MISRA-C compliance level: MISRA-C:2012 
- No reliance on implicit zero-initialization beyond what the standard/linker script guarantees
- Const-qualify all read-only data intended for flash placement

---

## 14. Build & Static Analysis

- Static analysis: ESP-IDF built-in checks via `idf.py build` (compiler warnings `-Wall -Wextra -Werror`)
- Compiler warnings treated as errors: `-Wall -Wextra -Werror`
- Linker script / memory map conventions documented if custom
- Binary size budget tracked per module where flash is constrained
- Build and flash instructions: see [README.md](README.md)

---

## 15. Project Structure & Component Layout

### 15.1 Top-Level Directory Layout

```
project_root/
├── main/                       # ESP-IDF main component (entry point)
├── components/                 # All custom components
│   ├── app/                    # Application business logic
│   ├── app_context/            # Shared application state
│   ├── common/                 # Cross-cutting: types, pin config, error codes
│   ├── drivers/                # Hardware peripheral drivers
│   ├── hal/                    # Hardware abstraction layer
│   ├── protocols/              # Communication protocol implementations
│   └── system/                 # System services (storage, time, watchdog)
├── CMakeLists.txt              # Project-level CMake config
├── sdkconfig.defaults          # Default SDK configuration
├── sdkconfig.defaults.release  # Release build overrides
├── partitions.csv              # Custom partition table
├── build_idf.bat               # Build scripts
├── flash_idf.bat
└── README.md
```

### 15.2 Component Category Taxonomy

| Category | Purpose | Examples |
|----------|---------|----------|
| `app/` | Product-specific business logic | filter, sensor, status_manager, water_profile |
| `app_context/` | Shared global state across modules | app_context (single file) |
| `common/` | Cross-cutting concerns | common.h, error_codes.h, pin_config.h, event_bits.h |
| `drivers/` | Hardware peripheral drivers | adc, ble, gpio, wifi, uart, led, flow_meter |
| `hal/` | Hardware abstraction | clock, platform_init, low_level_utils |
| `protocols/` | Communication implementations | http_ota, kgrm_mqtt, uart_protocol |
| `system/` | System services | nvs_storage, ntp_sync, watchdog, config_manager, timer_manager |

### 15.3 Component Internal Structure

Every component follows the ESP-IDF component convention:

```
component_name/
├── CMakeLists.txt          # Build definition (required)
├── <source>.c              # Implementation file(s)
└── include/
    └── <header>.h          # Public API header (required)
```

**Rules:**
- `CMakeLists.txt` is **always** at component root
- Public headers go in `include/` subdirectory
- All `.c` files at component root — no nested subdirectories within a component
- Header-only components (e.g., `rtos_wrapper`) skip the `.c` file
- Private headers: not used in this project (no `priv_include/` directories)

### 15.4 File Naming Rules

| Rule | Convention | Example |
|------|-----------|---------|
| Directory name | `snake_case` | `device_control/`, `uart_driver/` |
| Source file matches directory | `<dir_name>.c` | `wifi_driver.c` in `wifi_driver/` |
| Header file mirrors source | `<source_name>.h` | `wifi_driver.h` for `wifi_driver.c` |
| Hardware drivers | `<peripheral>_driver.c` | `adc_driver.c`, `gpio_driver.c` |
| Manager modules | `<feature>_manager.c` | `filter_manager.c`, `status_manager.c` |
| Hardware-specific ICs | `<ic_name>.c` | `lc8823.c` (LED driver IC) |
| Abbreviations | Keep uppercase for common acronyms | `adc`, `ble`, `gpio`, `uart`, `ntp`, `nvs`, `ota`, `mqtt` |

### 15.5 CMakeLists.txt Pattern

```cmake
idf_component_register(SRCS "source_file.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES dependency1 dependency2)
```

- `SRCS`: source files (relative to component root)
- `INCLUDE_DIRS`: public include directories (usually `"include"`)
- `PRIV_REQUIRES`: internal dependencies not exposed to dependents

### 15.6 Inconsistencies (Accept Existing)

The following inconsistencies exist and are accepted:
- `_control` vs `_ctrl`: both used (`device_control`, `electrolysis_ctrl`)
- `filter/` contains `filter_manager.c`: directory ≠ file name
- `kgrm_mqtt/` contains `mqtt_client.c`: directory ≠ file name
- `led/` contains `lc8823.c`: named after IC, not peripheral type

---

## Commit Convention

**Format:**
```
[TICKET-ID][TYPE] Overview line

Detail info explaining what changed and why.
```

**Example:**
```
[PROJ-123][FIX] Correct UART baud rate miscalculation

Fixed divisor calculation in InitUart() that caused
incorrect baud rate on 16MHz clock configuration.
Verified against oscilloscope measurement.
```

**Types:**

| Tag | Meaning |
|---|---|
| `FEAT` | New feature |
| `FIX` | Bug fix |
| `REFACTOR` | Restructuring without behavior change |
| `PERF` | Performance improvement |
| `STYLE` | Formatting only, no logic change |
| `DOCS` | Documentation only |
| `TEST` | Test additions or fixes |
| `BUILD` | Build system / Makefile / linker script changes |
| `CHORE` | Misc maintenance |

**Rules:**
- Overview line: imperative mood, no trailing period, concise (≤ 72 chars excluding the tag)
- One blank line between overview and detail
- Detail info wrapped at 72 chars; explain *what* and *why*, not just what
- One logical change per commit
- Ticket ID format: `PROJ-123` 
