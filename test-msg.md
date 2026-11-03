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
| `testAdc`       | Không                                | `raw1`, `raw2` của hai kênh ADC TDS cục bộ          |
| `testInput`     | Không                                | `pumpActive`                                        |
| `output`        | `index` 0–7, `state` 0/1             | Echo `index`, `state`; xem `params.code`            |
| `testLed`       | `color`: `R`, `G` hoặc `B`           | Kết quả `testLedRsp` có `params.code`               |
| `reset`         | Không                                | Không có trường bổ sung; xem `params.code`          |
| `setMode`       | `mode` 0/1/2                         | Echo `mode`                                         |
| `setFilterTime` | `filterIdx` 0–4, `time` 0–65535 phút | Echo `filterIdx`, `time`                            |
| `testMode`      | Không                                | `mode: 1` khi gửi thành công                        |
| `setDevId`      | `devId` 0–4294967295                 | Echo `devId`                                        |
| `getAllStatus`  | Không                                | Các trường trạng thái từ khung UART                 |

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
      "raw2": 2345
    }
  }
}
```

`raw1`/`raw2` là giá trị ADC 12 bit (0–4095) của TDS 0/TDS 1 trên bo ESP32-C3, không phải dữ liệu từ bo UART. Lần test đầu gọi `TdsTaskStop()`, cấu hình GPIO0/GPIO10 cho TDS 0 và GPIO4/GPIO5 cho TDS 1 làm output, rồi khởi tạo ADC channel 3/channel 2 ở 12 dB, 12 bit. Khi đọc, `pulse1=1`, `pulse2=0` cho mỗi cặp; sau khi đọc, cả bốn chân về 0. Lỗi khởi tạo trả `adc_init_failed`; lỗi đọc hoặc đặt mức GPIO trả `adc_read_failed`.

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
  "method": "getAllStatus",
  "params": {
    "mac": "..."
  }
}
```

```json
{
  "method": "getAllStatusRsp",
  "params": {
    "code": 0,
    "data": {
      "mac": "aabbccddeeff",
      "system_run": 100,
      "system_time": 200,
      "setting": 3,
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

`getAllStatus` lấy dữ liệu MLN qua các API trong `water_profile.h`; điều khiển MLN dùng `water_controller.h`. `raw1`/`raw2` ở đây là giá trị bo UART gửi về, khác với `raw1`/`raw2` ADC cục bộ của method `testAdc`.

Khi UART1 đang ở luồng test echo, `getAllStatus` kiểm tra đã nhận dữ liệu UART hay chưa: một khung hợp lệ loại 1 hoặc A trước khi chuyển luồng, hoặc ít nhất một byte do task test nhận sau khi chuyển luồng. Khi có dữ liệu, `params.code: 0` và `params.data.uartReceived: true`. Nếu đã nhận khung loại 1 hoặc A trước khi vào test, phản hồi còn có `profileSource: "before_test"` và các giá trị đã giải mã từ khung đó. Đây là **ảnh chụp trước khi chuyển sang echo**, không phải trạng thái trực tiếp. `uartRxBytes` chỉ đếm byte task test nhận sau khi chuyển luồng, nên có thể bằng 0 dù đã nhận khung trước đó. Ví dụ:

```json
{"method":"getAllStatusRsp","params":{"code":0,"data":{"mac":"aabbccddeeff","uartReceived":true,"frameType1Seen":true,"frameTypeASeen":false,"uartRxBytes":0,"profileSource":"before_test","system_run":949,"system_time":99306,"ro_status":0,"ply_status":0,"hot_tank_status":4,"cold_tank_status":15,"setting":1,"hot_tank_temperature":27,"cold_tank_temperature":27,"hot_target_temperature":90,"cold_target_temperature":12,"filter_time_1":880,"filter_time_2":880,"filter_time_3":880,"filter_time_ro":0,"filter_time_post":0}}}
```

Các trường trạng thái và thời gian ở trên đến từ khung UART loại 1. Nếu đã nhận thêm khung loại A, phản hồi có thêm `dev_id`, `input_stt`, `output_stt`, `switch_stt`, `led_stt`, `raw1`, `raw2`. Nếu chưa nhận khung hợp lệ lẫn byte UART test, trả `no_uart_data`. Khi không ở luồng echo, phản hồi đầy đủ chỉ có sau khi nhận cả khung loại 1 và A.

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
  "method": "getAllStatusRsp",
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

Các lỗi khác: `wifi_not_connected`, `ble_advertise_failed`, `adc_init_failed`, `adc_read_failed`, `uart_send_failed`, `led_failed`. Các ví dụ chưa được kiểm thử trên thiết bị thật.

### 1. ADC

- Tắt hàm TDS: TdsTaskStop()
- Init GPIO cho 2 chân pull và cho logic ngược
- Init 2 ADC: AdcOneshotInit
- Đọc ADC: AdcOneshotRead

### 2. UART

- Khi bắt đầu luồng test, `TestUartInit()` hủy UART1 của luồng chính, cấu hình lại UART1 và chạy task nhận riêng cho test. Luồng UART chính không xử lý dữ liệu test.
- Tool PC gửi byte trực tiếp vào UART1 của thiết bị (115200 baud, 8N1). Mỗi byte nhận được được gửi lại qua UART1 theo đúng thứ tự; dữ liệu có thể được chia thành nhiều lần ghi nếu đến theo nhiều đợt.
- Echo UART là dữ liệu nhị phân nguyên gốc, không đóng gói JSON và không đi qua UDP. Nối TX của tool với RX của thiết bị, RX của tool với TX của thiết bị và nối chung GND.
- `getAllStatusRsp` trong luồng test báo PASS khi UART1 đã nhận dữ liệu; điều này xác nhận đường RX, chưa xác nhận PC đã nhận được byte echo ở đường TX.

### 3. RGB

## Timeline: 05/11
