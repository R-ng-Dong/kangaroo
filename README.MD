# Demo 18/09/26
1. Quét thiết bị
Xem thêm **[core_flow.png](core_flow.png)** để hiểu rõ luồng hoạt động.
2. Truyền bản tin App->Device

   - Ứng dụng/cloud gửi lệnh xuống thiết bị.
   - Thiết bị nhận dữ liệu qua các event của IOT Core.

   Mô tả chung cho tất cả event trong hệ thống:

   - `IOT_EVENT_STATE`: xử lý thay đổi trạng thái thiết bị, ví dụ `STATE_SET`, `STATE_GET`.
   - `IOT_EVENT_DEVICE`: xử lý vòng đời thiết bị, kết nối mạng, kết nối cloud, tham gia/rời thiết bị, và thay đổi thuộc tính thiết bị.
   - `IOT_EVENT_SMART`: xử lý automation, trigger, schedule, bind/unbind, sync-on-off, condition group.
   - `IOT_EVENT_OS`: xử lý reboot, OTA và các sự kiện hệ thống cấp thấp.
   - `IOT_EVENT_MESH`, `IOT_EVENT_SETTING`, `IOT_EVENT_IR`: xử lý các event mở rộng theo mô hình mạng, cấu hình và điều khiển IR.

   Luồng xử lý chung của mọi event:

   App / Cloud
       |
       v
   IOT Core dispatch event
       |
       v
   eventCallback()
       |
       +--> switch (event->category)
              |
              +--> IOT_EVENT_STATE -> IOT_ExCoreEventHandleState() 
              +--> IOT_EVENT_DEVICE -> IOT_ExCoreEventHandleDevice() 
              +--> IOT_EVENT_SMART -> IOT_ExCoreEventHandleSmart() 
              +--> IOT_EVENT_OS -> IOT_ExCoreEventHandleOs() 

   Mỗi event đều tuân theo quy trình chung:
   1. Nhận event từ Core.
   2. Phân loại theo `category` và `type`.
   3. Xác định target: EID, element, attribute, smid, ...
   4. Cập nhật dữ liệu/DAO hoặc thực thi hành động tương ứng.
   5. Gửi ACK / báo trạng thái / phản hồi lại cloud nếu cần.

   ⟹ Toàn bộ luồng dữ liệu của hệ thống đều đi qua `eventCallback()` và phân loại theo `IOT_EVENT_*`. Đây là cơ chế tổng quát để ứng dụng nhận, xử lý và phản hồi mọi loại lệnh từ cloud hoặc hệ thống.

3. Reset thiết bị
`RootDeviceFactoryReset`: Xóa toàn bộ thông tin mạng sau đó khởi động lại 

## Build (ESP-IDF)

This build method uses `idf.py` (CMake-based).

### Prerequisites

1. Install ESP-IDF v5.x with the `esp32c3` target:
   `git clone -b v5.1.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf`
   then `~/esp/esp-idf/install.sh esp32c3`
2. `build.sh` auto-activates the ESP-IDF environment (no manual `source export.sh` needed)
3. CH340 driver may be required for USB-serial: https://www.wch-ic.com/downloads/CH341SER_EXE.html

### Build script

`build.sh` (Linux/macOS) does the whole cycle: it finds the ESP-IDF install
and drives `idf.py`. (`build.bat` / `build.ps1` are minimal stubs;
`build_idf.bat` is a fuller Windows alternative.)

```
./build.sh [action] [options] [-- <idf.py args>]
```

| Action | Effect |
|--------|--------|
| *(none)*, `build` | Incremental build, then print the size summary |
| `rebuild` | `fullclean`, drop `sdkconfig`, then build |
| `clean` | Remove the build directory |
| `flash` | Build incrementally, then flash |
| `monitor` | Open the serial monitor (no build) |
| `menuconfig` | Open the config menu |
| `reconfigure` | Regenerate the build configuration |

| Option | Effect |
|--------|--------|
| `--idf <path>` | Use a specific ESP-IDF installation |
| `--port <dev>` | Serial port for `flash`/`monitor`, e.g. `--port /dev/ttyUSB0` (default: auto-detected) |
| `--release` | Also apply `sdkconfig.defaults.release`, regenerating `sdkconfig` if it was generated for debug |
| `-B, --build-dir <dir>` | Build directory (default: `build`) |
| `--list` | Show the ESP-IDF installs that were found |
| `-h, --help` | Show the built-in help |

Examples:

```sh
./build.sh                            # incremental debug build + size summary
./build.sh --release rebuild          # clean release build
./build.sh --port /dev/ttyUSB0 flash  # build, then flash to a fixed port
./build.sh --port /dev/ttyUSB0 monitor
./build.sh -B build_test build        # build into a separate directory
```

`rebuild` clears `sdkconfig` as well as `build/`. IDF only reads the
defaults files when `sdkconfig` is absent, so without that an edit to
`sdkconfig.defaults` / `sdkconfig.defaults.esp32c3` /
`sdkconfig.defaults.release` would be silently ignored by the one action
meant to start over. Switching between debug and `--release` likewise
regenerates `sdkconfig` automatically.

### Serial monitor

`monitor` exits on **Ctrl+C**. IDF Monitor's own default is Ctrl+], and it
forwards Ctrl+C to the target, which is surprising for a serial console, so
`esp-idf-monitor.cfg` in the project root remaps it. A literal Ctrl+C can still
be sent to the target with Ctrl+T then Ctrl+C.

### Manual build

```sh
idf.py build
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor     # Ctrl+C to exit, see above
```



