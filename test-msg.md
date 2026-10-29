# Bản tin test UDP theo dạng RPC ThingsBoard

Firmware test giữ IPv4 UDP port `8181`. Payload lệnh dùng cấu trúc RPC `method`/`params` của ThingsBoard. UDP không có MQTT topic để mang request ID, nên giao thức test bổ sung `id` trong JSON và lặp lại `id` trong phản hồi. Đây là **định dạng RPC tương thích cho tool PC qua UDP**, không phải kết nối MQTT/HTTP trực tiếp tới máy chủ ThingsBoard.

Tham chiếu: [ThingsBoard MQTT RPC](https://thingsboard.io/docs/reference/mqtt-api/rpc/) và [ThingsBoard HTTP RPC](https://thingsboard.io/docs/reference/http-api/rpc/).

## Quy ước

- PC và thiết bị cùng mạng Wi-Fi. Thiết bị tự kết nối SSID nhà máy từ `main/main.c`; đợi log `Factory Wi-Fi ready, IP: ...`.
- Mỗi UDP datagram chứa một JSON UTF-8; yêu cầu tối đa 1024 byte. Thiết bị trả lời về IP/port nguồn.
- Yêu cầu RPC: `{"id":1,"method":"wifiReport","params":{"mac":"alldevice"}}`.
- `id`: số nguyên không âm 0–4294967295 do tool PC cấp; duy nhất trong các lệnh đang chờ. Dùng cùng `id` để ghép phản hồi. Khi broadcast, nhiều thiết bị có thể trả về cùng `id`.
- `method`: tên lệnh trong bảng dưới. `params` luôn là object và luôn có `mac`; `mac` là 12 ký tự hex thường không dấu `:`, hoặc `alldevice` khi tìm thiết bị.
- Phản hồi RPC: `{"id":1,"result":{"mac":"aabbccddeeff","code":0,...}}`. `code: 0` là lệnh cục bộ thành công hoặc UART đã gửi, chưa xác nhận phần cứng đã đổi trạng thái. `code: -1` có `error`.
- JSON sai, thiếu `id`/`method`/`params`/`mac` hoặc MAC không khớp: thiết bị im lặng. Tham số lệnh sai trả `invalid_argument`; method không biết trả `unknown_command`.
- `input` là lệnh đọc trạng thái bơm tại thời điểm yêu cầu. Firmware không gửi sự kiện input tự động.
- Giao thức cũ `process/data/cmd/type` không còn được firmware chấp nhận.

## Danh sách method

| Method          | Params thêm                          | Kết quả trong `result`                              |
| --------------- | ------------------------------------ | --------------------------------------------------- |
| `wifiReport`    | Không                                | `connected`, `ssid`, `rssi`, `channel`, `ip` nếu có |
| `bleReport`     | Không                                | `devices`, `detected`, `bestRssi` nếu có            |
| `adc`           | Không                                | `raw1`, `raw2` của hai kênh ADC TDS cục bộ          |
| `input`         | Không                                | `pump_active`                                       |
| `output`        | `index` 0–7, `state` 0/1             | Echo `index`, `state`                               |
| `lc8823`        | `rgb` 0–16777215                     | Echo `rgb`                                          |
| `reset`         | Không                                | `code`                                              |
| `setMode`       | `mode` 0/1/2                         | Echo `mode`                                         |
| `setFilterTime` | `filterIdx` 0–4, `time` 0–65535 phút | Echo `filterIdx`, `time`                            |
| `testMode`      | Không                                | `code`                                              |
| `setDevId`      | `setDevId` 0–4294967295              | Echo `setDevId`                                     |
| `getAll`        | Không                                | Các trường trạng thái từ khung UART                 |

## Ví dụ từng lệnh

Mỗi cặp JSON dưới đây lần lượt là yêu cầu và phản hồi mẫu. MAC, số đo và IP chỉ là ví dụ.

### Wi-Fi

```json
{"id":1,"method":"wifiReport","params":{"mac":"alldevice"}}
{"id":1,"result":{"mac":"aabbccddeeff","code":0,"connected":true,"ssid":"TTS","rssi":-54,"channel":6,"ip":"192.168.1.50"}}
```

### BLE

```json
{"id":2,"method":"bleReport","params":{"mac":"aabbccddeeff"}}
{"id":2,"result":{"mac":"aabbccddeeff","code":0,"devices":3,"detected":true,"bestRssi":-61}}
```

Quét mất khoảng 3 giây. `devices` đếm bản tin quảng bá, không phải số MAC duy nhất. Khi không thấy tín hiệu: `code: 0`, `devices: 0`, `detected: false`, không có `bestRssi`.

### ADC

```json
{"id":3,"method":"adc","params":{"mac":"aabbccddeeff"}}
{"id":3,"result":{"mac":"aabbccddeeff","raw1":1234,"raw2":2345,"code":0}}
```

`raw1`/`raw2` là giá trị ADC 12 bit (0–4095) của TDS 0/TDS 1 trên bo ESP32-C3, không phải dữ liệu từ bo UART. Lần test đầu gọi `TdsTaskStop()`, cấu hình GPIO0/GPIO10 cho TDS 0 và GPIO4/GPIO5 cho TDS 1 làm output, rồi khởi tạo ADC channel 3/channel 2 ở 12 dB, 12 bit. Khi đọc, `pulse1=1`, `pulse2=0` cho mỗi cặp; sau khi đọc, cả bốn chân về 0. Lỗi khởi tạo trả `adc_init_failed`; lỗi đọc hoặc đặt mức GPIO trả `adc_read_failed`.

### Input: trạng thái bơm

```json
{"id":4,"method":"input","params":{"mac":"aabbccddeeff"}}
{"id":4,"result":{"mac":"aabbccddeeff","pump_active":true,"code":0}}
```

`pump_active` lấy trực tiếp từ `PumpIsActive()` với chân bơm cấu hình active-low. Tool PC gửi lại lệnh `input` khi cần đọc trạng thái mới; không có đăng ký hay telemetry input.

### Output

```json
{"id":5,"method":"output","params":{"mac":"aabbccddeeff","index":0,"state":1}}
{"id":5,"result":{"mac":"aabbccddeeff","index":0,"state":1,"code":0}}
```

`state`: 0 = tắt, 1 = bật. Có thể cần bật `testMode` trước.

### LED RGB

```json
{"id":6,"method":"lc8823","params":{"mac":"aabbccddeeff","rgb":16711680}}
{"id":6,"result":{"mac":"aabbccddeeff","rgb":16711680,"code":0}}
```

`rgb` là số `0xRRGGBB`. Ví dụ trên là màu đỏ.

### Reset

```json
{"id":7,"method":"reset","params":{"mac":"aabbccddeeff"}}
{"id":7,"result":{"mac":"aabbccddeeff","code":0}}
```

### Chế độ

```json
{"id":8,"method":"setMode","params":{"mac":"aabbccddeeff","mode":1}}
{"id":8,"result":{"mac":"aabbccddeeff","mode":1,"code":0}}
```

`mode`: 0 = nóng 100°C; 1 = lạnh 20°C; 2 = tiết kiệm điện.

### Thời gian lọc

```json
{"id":9,"method":"setFilterTime","params":{"mac":"aabbccddeeff","filterIdx":3,"time":120}}
{"id":9,"result":{"mac":"aabbccddeeff","filterIdx":3,"time":120,"code":0}}
```

`filterIdx`: 0 lõi 1, 1 lõi 2, 2 lõi 3, 3 RO, 4 Post. `time` tính bằng phút.

### Bật test mode

```json
{"id":10,"method":"testMode","params":{"mac":"aabbccddeeff"}}
{"id":10,"result":{"mac":"aabbccddeeff","code":0}}
```

### Device ID

```json
{"id":11,"method":"setDevId","params":{"mac":"aabbccddeeff","setDevId":12345}}
{"id":11,"result":{"mac":"aabbccddeeff","setDevId":12345,"code":0}}
```

### Đọc toàn bộ trạng thái UART

```json
{"id":12,"method":"getAll","params":{"mac":"aabbccddeeff"}}
{"id":12,"result":{"mac":"aabbccddeeff","system_run":100,"system_time":200,"setting":3,"filter_time_1":10,"filter_time_2":20,"filter_time_3":30,"filter_time_ro":40,"filter_time_post":50,"dev_id":12345,"input_stt":5,"output_stt":1,"switch_stt":2,"led_stt":4,"raw1":1234,"raw2":2345,"code":0}}
```

`getAll` lấy dữ liệu MLN qua các API trong `water_profile.h`; điều khiển MLN dùng `water_controller.h`. `raw1`/`raw2` ở đây là giá trị bo UART gửi về, khác với `raw1`/`raw2` ADC cục bộ của method `adc`. Chưa nhận đủ khung UART loại 1 và A: `no_uart_data`.

Khi UART1 đã chuyển sang luồng test echo, bộ phân tích khung MLN của luồng chính không chạy; `getAll` trả `no_uart_data` để tránh dùng dữ liệu cũ.

## Phản hồi lỗi

```json
{"id":13,"result":{"mac":"aabbccddeeff","code":-1,"error":"invalid_argument"}}
{"id":14,"result":{"mac":"aabbccddeeff","code":-1,"error":"no_uart_data"}}
{"id":15,"result":{"mac":"aabbccddeeff","code":-1,"error":"unknown_command"}}
```

Các lỗi khác: `wifi_not_connected`, `ble_unavailable`, `ble_callback_failed`, `ble_scan_failed`, `ble_scan_stop_failed`, `adc_init_failed`, `adc_read_failed`, `uart_send_failed`, `led_failed`. Các ví dụ chưa được kiểm thử trên thiết bị thật.

### 1. ADC

- Tắt hàm TDS: TdsTaskStop()
- Init GPIO cho 2 chân pull và cho logic ngược
- Init 2 ADC: AdcOneshotInit
- Đọc ADC: AdcOneshotRead

### 2. UART

- Khi bắt đầu luồng test, `TestUartInit()` hủy UART1 của luồng chính, cấu hình lại UART1 và chạy task nhận riêng cho test. Luồng UART chính không xử lý dữ liệu test.
- Tool PC gửi byte trực tiếp vào UART1 của thiết bị (115200 baud, 8N1). Mỗi byte nhận được được gửi lại qua UART1 theo đúng thứ tự; dữ liệu có thể được chia thành nhiều lần ghi nếu đến theo nhiều đợt.
- Echo UART là dữ liệu nhị phân nguyên gốc, không đóng gói JSON và không đi qua UDP. Nối TX của tool với RX của thiết bị, RX của tool với TX của thiết bị và nối chung GND.

### 3. RGB

## Timeline: 05/11
