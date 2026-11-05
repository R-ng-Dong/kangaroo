# Bản tin test UDP theo dạng RPC ThingsBoard

Firmware test giữ IPv4 UDP port `8181`. Payload lệnh và phản hồi dùng cấu trúc `method`/`params` kiểu ThingsBoard. Đây là **định dạng cho tool PC qua UDP**, không phải kết nối MQTT/HTTP trực tiếp tới máy chủ ThingsBoard.

Tham chiếu: [ThingsBoard MQTT RPC](https://thingsboard.io/docs/reference/mqtt-api/rpc/) và [ThingsBoard HTTP RPC](https://thingsboard.io/docs/reference/http-api/rpc/).

## Quy ước

- Ở nhánh test hiện tại, `main.c` gọi `FactoryTestUdpStart()` khi thiết bị chưa provisioned. Thiết bị kết nối SSID `TTS` bằng mật khẩu `TTS123456`; đợi log `Factory Wi-Fi ready, IP: ...` và `Listening on UDP port 8181` trước khi gửi UDP.
- Mỗi UDP datagram chứa một JSON UTF-8; yêu cầu tối đa 1024 byte. Thiết bị trả lời về IP/port nguồn.
- Yêu cầu RPC tìm thiết bị: `{"id":1,"method":"scanDevWifi","params":{}}`.
- `id`: số nguyên không âm 0–4294967295, có thể bỏ qua; phản hồi không chứa `id`. Tool chỉ gửi một lệnh đang chờ cho mỗi thiết bị và ghép theo method phản hồi cùng IP nguồn.
- `method`: tên lệnh trong bảng dưới. `params` luôn là object. `mac` có thể bỏ qua; nếu gửi thì phải là MAC Wi-Fi của thiết bị gồm 12 ký tự hex thường không dấu `:`, hoặc `alldevice` khi tìm thiết bị.
- Phản hồi dùng `<method>Rsp`, ví dụ `scanDevWifiRsp` và `testLedRsp`. Lỗi có `params.code: -1`, `params.error` và `params.data.mac`.
- JSON sai, thiếu `method`/`params` hoặc MAC không khớp: thiết bị im lặng. Tham số lệnh sai trả `invalid_argument`; method không biết trả `unknown_command`.
- `scanDevBle` hiện bị vô hiệu hóa: firmware ghi log RX nhưng không gửi `scanDevBleRsp`. Tool quét BLE trực tiếp qua adapter PC.
- `testInput` là lệnh đọc trạng thái bơm tại thời điểm yêu cầu. Firmware không gửi sự kiện input tự động.
- Giao thức cũ `process/data/cmd/type` không còn được firmware chấp nhận.

## Danh sách method

| Method          | Params thêm                          | Kết quả trong `params.data`                         |
| --------------- | ------------------------------------ | --------------------------------------------------- |
| `scanDevWifi`   | Không                                | `connected`, `ssid`, `rssi`, `channel`, `ip` nếu có |
| `getTypeDev`    | Không                                | `type`: 0 máy tiêu chuẩn, 1 máy nóng/lạnh           |
| `testAdc`       | Không                                | `raw1`, `raw2` từ khung UART A của bo MLN           |
| `testInput`     | Không                                | `pumpActive`                                        |
| `output`        | `index` 0–7, `state` 0/1             | Echo `index`, `state`; xem `params.code`            |
| `testLed`       | `color`: `R`, `G` hoặc `B`           | Kết quả `testLedRsp` có `params.code`               |
| `reset`         | Không                                | Không có trường bổ sung; xem `params.code`          |
| `setMode`       | `mode` 0/1/2                         | Echo `mode`                                         |
| `setFilterTime` | `filterIdx` 0–4, `time` 0–65535 phút | Echo `filterIdx`, `time`                            |
| `testMode`      | Không                                | `mode: 1` khi gửi thành công                        |
| `setDevId`      | `devId` 0–4294967295                 | Echo `devId`                                        |
| `getAllState`   | Không                                | Dữ liệu mới nhất từ khung UART loại 1 và A          |

## Ví dụ từng lệnh

Mỗi cặp JSON dưới đây lần lượt là yêu cầu và phản hồi mẫu. MAC, số đo và IP chỉ là ví dụ.

### Wi-Fi

```json
{
  "id": 1,
  "method": "scanDevWifi",
  "params": {}
}
```

```json
{
  "method": "scanDevWifiRsp",
  "params": {
    "data": {
      "mac": "aabbccddeeff",
      "connected": true,
      "ssid": "TTS",
      "rssi": -54,
      "channel": 6,
      "ip": "192.168.1.50"
    }
  }
}
```

### Loại thiết bị

```json
{"method":"getTypeDev","params":{"mac":"aabbccddeeff"}}
{"method":"getTypeDevRsp","params":{"data":{"mac":"aabbccddeeff","type":1}}}
```

`type` lấy từ `AppGetModel()`: `0` là `WATER_PURIFIER_TYPE_STANDARD`, `1` là `WATER_PURIFIER_TYPE_HOT_COLD`.

### BLE

Tool PC quét trực tiếp bằng adapter BLE, không gửi UDP `scanDevBle`. Tên trong BLE scan response có dạng `KGTEST_<MAC Wi-Fi STA>` với MAC gồm 12 ký tự hex thường, không dấu `:`; ví dụ `KGTEST_10003bb87464`. Tool dùng phần MAC trong tên để đối chiếu với `scanDevWifiRsp.params.data.mac` sau khi thiết bị kết nối mạng. RSSI BLE do PC đo.

### ADC

```json
{
  "id": 3,
  "method": "testAdc",
  "params": {
    "mac": "..."
  }
}
```

```json
{
  "method": "testAdcRsp",
  "params": {
    "data": {
      "mac": "aabbccddeeff",
      "raw1": 1234,
      "raw2": 2345,
      "source": "uart_frame_A",
      "frameTypeASequence": 3
    }
  }
}
```

`raw1`/`raw2` là giá trị ADC 12 bit (0–4095) do bo MLN gửi trong khung UART loại A gần nhất. `testAdc` không đọc chân ADC của ESP32 và không dừng tác vụ TDS. Nếu chưa nhận khung A, phản hồi `code: -1`, `error: "no_uart_data"`. `frameTypeASequence` tăng mỗi khi nhận khung A hợp lệ; tool có thể đọc lại để kiểm tra dữ liệu mới.

### Input: trạng thái bơm

```json
{
  "method": "testInput",
  "params": {
    "mac": "..."
  }
}
```

```json
{
  "method": "testInputRsp",
  "params": {
    "data": {
      "mac": "aabbccddeeff",
      "pumpActive": true
    }
  }
}
```

`pumpActive` lấy trực tiếp từ `PumpIsActive()` với chân bơm cấu hình active-low. Tool PC gửi lại lệnh `testInput` khi cần đọc trạng thái mới; không có đăng ký hay telemetry input.

### Output

```json
{"method":"output","params":{"mac":"aabbccddeeff","index":0,"state":1}}
{"method":"outputRsp","params":{"code":0,"data":{"mac":"aabbccddeeff","index":0,"state":1}}}
```

`state`: 0 = tắt, 1 = bật. Có thể cần bật `testMode` trước.

### LED RGB

```json
{
  "method": "testLed",
  "params": {
    "mac": "...",
    "color": "R"
  }
}
```

```json
{
  "method": "testLedRsp",
  "params": {
    "code": 0
  }
}
```

`color` nhận `R`, `G` hoặc `B`; firmware bật kênh màu tương ứng ở mức 255 và tắt hai kênh còn lại.

### Reset

```json
{
  "method": "reset",
  "params": {
    "mac": "..."
  }
}
```

```json
{
  "method": "resetRsp",
  "params": {
    "code": 0
  }
}
```

### Chế độ

```json
{
  "method": "setMode",
  "params": {
    "mac": "...",
    "mode": 1
  }
}
```

```json
{
  "method": "setModeRsp",
  "params": {
    "data": {
      "mode": 1
    }
  }
}
```

`mode`: 0 = nóng 100°C; 1 = lạnh 20°C; 2 = tiết kiệm điện.

### Thời gian lọc

```json
{
  "method": "setFilterTime",
  "params": {
    "mac": "...",
    "filterIdx": 3,
    "time": 120
  }
}
```

```json
{
  "method": "setFilterTimeRsp",
  "params": {
    "data": {
      "filterIdx": 3,
      "time": 120
    }
  }
}
```

`filterIdx`: 0 lõi 1, 1 lõi 2, 2 lõi 3, 3 RO, 4 Post. `time` tính bằng phút.

### Bật test mode

```json
{
  "method": "testMode",
  "params": {
    "mac": "..."
  }
}
```

```json
{
  "method": "testModeRsp",
  "params": {
    "data": {
      "mode": 1
    }
  }
}
```

### Device ID

```json
{
  "method": "setDevId",
  "params": {
    "mac": "...",
    "devId": 12345
  }
}
```

```json
{
  "method": "setDevIdRsp",
  "params": {
    "data": {
      "devId": 12345
    }
  }
}
```

### Đọc toàn bộ trạng thái UART

```json
{
  "method": "getAllState",
  "params": {
    "mac": "..."
  }
}
```

```json
{
  "method": "getAllStateRsp",
  "params": {
    "code": 0,
    "data": {
      "mac": "aabbccddeeff",
      "uartReceived": true,
      "frameType1Seen": true,
      "frameTypeASeen": true,
      "productId": "00000101C0040003",
      "firmwareVersion": "2.0.0.0",
      "mainboardVersion": "1.3",
      "frameType1Sequence": 10,
      "frameTypeASequence": 3,
      "profileSource": "uart_profile",
      "system_run": 100,
      "system_time": 200,
      "ro_status": 0,
      "ply_status": 0,
      "hot_tank_status": 1,
      "cold_tank_status": 1,
      "setting": 3,
      "hot_tank_temperature": 26,
      "cold_tank_temperature": 26,
      "hot_target_temperature": 90,
      "cold_target_temperature": 12,
      "filter_time_1": 10,
      "filter_time_2": 20,
      "filter_time_3": 30,
      "filter_time_ro": 40,
      "filter_time_post": 50,
      "dev_id": 12345,
      "input_stt": 5,
      "output_stt": 1,
      "switch_stt": 2,
      "led_stt": 4,
      "raw1": 1234,
      "raw2": 2345
    }
  }
}
```

`getAllState` đọc dữ liệu MLN đã được luồng UART1 chính giải mã qua `water_profile.h`; các lệnh điều khiển MLN dùng `water_controller.h`. `raw1`/`raw2` ở đây cùng nguồn UART A với `testAdc`. `productId` là ID được dùng khi khởi tạo IoT Core theo model (`...0004` cho STANDARD, `...0003` cho HOT_COLD); `firmwareVersion` lấy từ `CONFIG_APP_PROJECT_VER` của bản firmware đang biên dịch. `mainboardVersion` ghép byte 0 (Major) và byte 1 (Minor) của `fw_build_ver` trong khung UART A theo dạng `major.minor`; byte 2–7 là thời điểm build `YY MM DD hh mm ss`, không thuộc phiên bản. `getAllStatus` và `getAll` vẫn được chấp nhận như tên lệnh cũ, với cùng nội dung dữ liệu và tên phản hồi tương ứng.

UART1 tiếp tục chạy trong chế độ test. Mỗi lần gọi trả giá trị từ **khung hợp lệ gần nhất**, không đợi một khung mới. `frameType1Sequence` và `frameTypeASequence` tăng khi nhận khung tương ứng; tool có thể gọi lại để xác nhận dữ liệu còn cập nhật. Có ít nhất một loại khung thì `code: 0`; nếu chưa có loại nào, trả `no_uart_data` nhưng vẫn có `productId` và `firmwareVersion`. Trường của loại 1 chỉ xuất hiện sau khi đã nhận khung loại 1; `mainboardVersion`, `dev_id`, `input_stt`, `output_stt`, `switch_stt`, `led_stt`, `raw1`, `raw2` chỉ xuất hiện sau khung loại A.

## Phản hồi lỗi

```json
{
  "method": "outputRsp",
  "params": {
    "code": -1,
    "error": "invalid_argument",
    "data": { "mac": "aabbccddeeff" }
  }
}
```

```json
{
  "method": "getAllStateRsp",
  "params": {
    "code": -1,
    "error": "no_uart_data",
    "data": {
      "mac": "aabbccddeeff"
    }
  }
}
```

```json
{
  "method": "unknownRsp",
  "params": {
    "code": -1,
    "error": "unknown_command",
    "data": { "mac": "aabbccddeeff" }
  }
}
```

Các lỗi khác: `wifi_not_connected`, `ble_advertise_failed`, `uart_send_failed`, `led_failed`. Các ví dụ chưa được kiểm thử trên thiết bị thật.

### 1. ADC

- `testAdc` trả `adc_raw_value_1` và `adc_raw_value_2` từ khung UART loại A gần nhất của bo MLN.
- Nếu chưa nhận khung A, trả `no_uart_data`; không thay đổi GPIO hoặc tác vụ TDS trên ESP32.

### 2. UART

- UART1 giữ nguyên luồng nhận và giải mã khung của `water_profile.c`; chế độ test không hủy hoặc cấu hình lại UART1.
- Tool gửi `getAllState` qua UDP để đọc dữ liệu từ các khung UART hợp lệ gần nhất. `frameType1Sequence` và `frameTypeASequence` giúp kiểm tra có khung mới giữa các lần đọc.
- UART1 không còn echo byte thô; tool không gửi dữ liệu tùy ý trực tiếp vào UART1 để chờ phản hồi echo.

### 3. RGB

## Timeline: 05/11
