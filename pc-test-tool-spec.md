# Đặc tả tool PC test thiết bị Kangaroo

> Có thể đưa nguyên file này cho công cụ sinh giao diện và mã ứng dụng. Nguồn bản tin chi tiết là [test-msg.md](test-msg.md); khi hai tài liệu khác nhau, đối chiếu trực tiếp với `main/factory_test_udp.c`.

## 1. Mục tiêu

Tạo ứng dụng desktop Windows, giao diện tiếng Việt, để kỹ thuật viên test một hoặc nhiều thiết bị Kangaroo trong cùng mạng LAN với PC. Ứng dụng gửi bản tin JSON qua UDP tới firmware nhánh `test-product`, hiển thị phản hồi và lưu kết quả từng phép test. Ưu tiên Python + PySide6 để có thể đóng gói thành `.exe`, nhưng có thể dùng công nghệ desktop tương đương nếu vẫn đáp ứng giao tiếp UDP và các tiêu chí bên dưới.

Tool chạy cục bộ, không cần tài khoản hoặc cloud. Kỹ thuật viên nhập IP hoặc tìm thiết bị bằng broadcast, chọn thiết bị theo MAC rồi chạy test thủ công. Không giả định rằng `code: 0` của lệnh UART đồng nghĩa phần cứng đã đổi trạng thái.

**Luồng firmware test hiện tại:** khi khởi động, thiết bị tự kết nối Wi-Fi nhà máy được cấu hình trong `main/main.c`. PC cần ở cùng mạng đó; tool tìm IP qua UDP broadcast hoặc nhập thủ công, chạy `wifiReport` để xác nhận kết nối rồi tiếp tục các phép test. Không cần quét BLE hay gửi SSID/mật khẩu từ PC để bắt đầu test UDP.

## 2. Bố cục giao diện

Một cửa sổ có năm vùng rõ ràng:

1. **Wi-Fi nhà máy:** hướng dẫn PC kết nối cùng mạng Wi-Fi với thiết bị; hiển thị trạng thái đang chờ thiết bị nhận IP và nút tìm thiết bị. Không yêu cầu nhập mật khẩu thiết bị vào tool.
2. **Kết nối LAN:** chọn card mạng PC, IP thiết bị hoặc địa chỉ broadcast, UDP port mặc định `8181`, nút **Tìm thiết bị**, danh sách thiết bị tìm thấy với MAC, IP, SSID, RSSI, thời điểm thấy gần nhất. Cho phép nhập IP và MAC thủ công. Sau khi chọn thiết bị, mọi lệnh dùng MAC đó thay cho `alldevice`.
3. **Test PCBA:** các nhóm Wi-Fi, BLE, ADC TDS, trạng thái bơm, Output và LED RGB. Mỗi nhóm có nút chạy, giá trị đo, trạng thái và thông báo lỗi. Input hiển thị `pump_active` dạng bật/tắt. Output dùng một ô chọn `index` 0–7 và một lựa chọn `state` bật/tắt để gửi lệnh.
4. **Test UART:** nút bật test mode; các form Reset, Set Mode, Set Filter Time, Set Device ID; nút Get All và bảng dữ liệu trả về. Các nút có tác động lên thiết bị phải cho người vận hành xem tham số trước khi gửi; Reset cần hộp xác nhận.
5. **Nhật ký và kết quả:** bảng thời gian, IP/MAC, RPC id/method, trạng thái, thời gian phản hồi; ô xem JSON gửi/nhận nguyên văn; nút sao chép, xóa log và xuất CSV/JSON. Có ô mã ca test hoặc mã sản phẩm và tên người vận hành để đưa vào báo cáo.

Trạng thái hiển thị thống nhất: `Chưa chạy`, `Đang chờ`, `Thành công`, `Lỗi`, `Không phản hồi`, `Chờ xác nhận thực tế`. Không tô xanh “đạt” cho lệnh UART chỉ vì nhận `code: 0`; cho phép kỹ thuật viên đánh dấu đạt sau khi quan sát thiết bị hoặc đối chiếu `getAll`.

## 3. Wi-Fi mặc định của firmware test

Trong chế độ test, thiết bị tự kết nối SSID `TTS` bằng mật khẩu đặt trong `main/main.c` sau khi khởi tạo platform. Firmware giữ cấu hình này trong RAM, không ghi đè thông tin Wi-Fi đã lưu trong flash. PC phải kết nối tới cùng mạng `TTS` để gửi UDP. Log `Connecting to factory Wi-Fi` chỉ cho biết đã bắt đầu kết nối; log `Factory Wi-Fi ready, IP: ...` mới xác nhận thiết bị có IP. Nếu không thấy thiết bị qua broadcast, cho phép nhập IP từ log và thử `wifiReport` trực tiếp.

Tool không cần truy cập BLE để cấu hình Wi-Fi. Method UDP `bleReport` ở bước test sau đó là phép quét BLE do **thiết bị** thực hiện, khác với việc PC quét BLE để tìm thiết bị.

## 4. Giao thức UDP

- Thiết bị lắng nghe IPv4 UDP trên port `8181` và trả lời về **IP/port nguồn** của gói yêu cầu. Dùng **một socket UDP lâu sống** cho cả gửi và nhận; không tạo socket nhận với port khác. Luồng nhận chạy nền để giao diện không bị treo.
- JSON UTF-8, mỗi datagram là một đối tượng JSON. Gói yêu cầu tối đa 1024 byte theo bộ đệm firmware.
- Yêu cầu dùng payload RPC dạng ThingsBoard: `{"id":1,"method":"wifiReport","params":{"mac":"alldevice"}}`. `id` là số nguyên 0–4294967295 do tool cấp; `params` luôn là object. `mac` là 12 ký tự hex viết thường, không có dấu `:`, hoặc `alldevice` khi tìm thiết bị. Các tham số lệnh khác đặt trong `params`.
- Phản hồi: `{"id":1,"result":{"mac":"aabbccddeeff","code":0,...}}`. `code = 0` là thành công ở mức lệnh; `code = -1` có `error`. Thiết bị lặp lại đúng `id` của yêu cầu. Format RPC này chạy trên UDP nội bộ, không tự kết nối máy chủ ThingsBoard; trong MQTT ThingsBoard, request ID nằm ở topic.
- `input` trả trạng thái bơm tại thời điểm đọc; firmware không gửi sự kiện input tự động. Muốn cập nhật, tool gửi lại RPC `input` với `id` mới.
- Ghép phản hồi bằng `id`, MAC trong `result` và IP nguồn. Cấp `id` khác nhau cho các lệnh đang chờ; firmware xử lý tuần tự trên một tác vụ UDP, vì vậy vẫn nên gửi từng lệnh cho mỗi thiết bị. Khi tìm bằng broadcast, nhận nhiều phản hồi cùng `id` và gộp theo MAC.
- Mặc định chờ 2 giây cho lệnh thường, ít nhất 5 giây cho `bleReport` vì firmware quét 3 giây. Timeout phải cấu hình được. Lệnh đọc có thể cho phép thử lại bằng nút riêng; **không tự gửi lại** Reset, Set Mode, Set Filter Time, Set Device ID, Output hoặc LED RGB.
- Khi không có phản hồi, hiển thị `Không phản hồi` với gợi ý kiểm tra IP, cùng mạng Wi-Fi, port 8181 và MAC đích. Không suy ra `code: -1` vì thiết bị không gửi mã lỗi khi JSON thiếu trường RPC bắt buộc hoặc MAC sai.
- Tìm kiếm broadcast chỉ là tiện ích. Nếu mạng chặn broadcast, người vận hành vẫn phải dùng được IP thủ công. Thiết bị phải nhận IP từ Wi-Fi nhà máy trước khi PC có thể gửi UDP.

## 5. Form và dữ liệu từng lệnh

| Nhóm UI | RPC method | Đầu vào trong `params` | Hiển thị `result` |
| --- | --- | --- | --- |
| Wi-Fi | `wifiReport` | Không | `connected`, `ssid`, `rssi` dBm, `channel`, `ip`; `ip` có thể vắng mặt |
| BLE | `bleReport` | Không | `devices`, `detected`, `bestRssi` dBm nếu có; vòng chờ 3 giây |
| ADC | `adc` | Không | `raw1`, `raw2` (0–4095) từ hai kênh ADC TDS trên bo ESP32-C3 |
| Input | `input` | Nút Đọc trạng thái bơm | `pump_active` true/false từ `PumpIsActive()` |
| Output | `output` | Chọn một `index` nguyên 0–7 và `state` 0/1 | Echo `index`, `state`, `code` |
| LED RGB | `lc8823` | Color picker + hex `#RRGGBB`, đổi thành số nguyên `rgb` 0–16777215 | Echo `rgb`, `code` |
| Reset | `reset` | Hộp xác nhận | `code`; cảnh báo kết nối có thể gián đoạn sau khi gửi |
| Chế độ | `setMode` | `mode`: 0 nóng 100°C, 1 lạnh 20°C, 2 tiết kiệm điện | Echo `mode`, `code` |
| Thời gian lọc | `setFilterTime` | `filterIdx`: 0 lõi 1, 1 lõi 2, 2 lõi 3, 3 RO, 4 Post; `time`: số phút nguyên 0–65535 | Echo `filterIdx`, `time`, `code` |
| Test mode | `testMode` | Không | `code`; có thể cần trước lệnh Output |
| Device ID | `setDevId` | `setDevId`: số nguyên 0–4294967295 | Echo `setDevId`, `code` |
| Trạng thái tổng | `getAll` | Không | `system_run`, `system_time`, `setting`, 5 `filter_time_*`, `dev_id`, `input_stt`, `output_stt`, `switch_stt`, `led_stt`, `raw1`, `raw2` |

Không hardcode ngưỡng đạt của RSSI/ADC. Cho phép nhập ngưỡng trong phần cài đặt ca test; nếu chưa có ngưỡng, chỉ hiển thị số đo và để kỹ thuật viên xác nhận. `devices` của BLE là **số kết quả quảng bá**, có thể lặp MAC thiết bị phát; không hiển thị là số thiết bị duy nhất. `code: 0, detected: false` nghĩa là quét chạy nhưng chưa thấy tín hiệu.

ADC trả một giá trị raw cho mỗi TDS sau khi firmware dừng tác vụ TDS, cấu hình bốn chân Pulse và hai kênh ADC. Chân Pulse chỉ ở mức đối nghịch trong lúc lấy mẫu và được trả về 0 sau đó. Chưa có ngưỡng đạt cố định. `pump_active` là trạng thái bơm active-low tại thời điểm gọi; tool có thể cho phép đọc lại thủ công. `index` 0–7 của lệnh output là miền giá trị firmware chấp nhận, không khẳng định cả tám output đều có phần cứng trên sản phẩm.

## 6. Luồng làm việc đề nghị

1. Kết nối PC vào cùng mạng Wi-Fi `TTS`, đợi thiết bị nhận IP; chọn card mạng, tìm hoặc nhập IP thiết bị và chạy `wifiReport` để xác nhận IP/MAC. Nếu dùng broadcast, hiển thị mọi thiết bị trả lời rồi chọn đúng MAC.
2. Chạy `bleReport`, `adc`, đọc `input`; cho phép người vận hành thao tác vật lý và đánh dấu kết quả. BLE không thấy thiết bị chuẩn cần trạng thái `Chờ xác nhận`, không tự kết luận phần cứng lỗi.
3. Khi cần test bo điều khiển, gửi `testMode`, sau đó thử Output, Set Mode, Set Filter Time hoặc Set Device ID. Đọc `getAll` sau lệnh ghi và so sánh trường liên quan nếu phản hồi UART hỗ trợ. Không tự gửi Reset trong chuỗi test.
4. Kết thúc phiên, xuất báo cáo gồm thời gian, mã ca, người vận hành, IP/MAC, từng bản tin, số đo, kết quả tự động và xác nhận thủ công. Không lưu mật khẩu Wi-Fi trong báo cáo.

## 7. Xử lý lỗi và chế độ demo

Các `error` firmware có thể trả: `invalid_argument`, `unknown_command`, `no_uart_data`, `wifi_not_connected`, `ble_unavailable`, `ble_callback_failed`, `ble_scan_failed`, `ble_scan_stop_failed`, `adc_init_failed`, `adc_read_failed`, `uart_send_failed`, `led_failed`. Hiển thị tên lỗi và diễn giải tiếng Việt; giữ nguyên JSON thô để tra cứu.

Có **Chế độ demo** chạy không cần thiết bị, dùng phản hồi mẫu từ [test-msg.md](test-msg.md) để xem giao diện và thử xuất báo cáo. Chế độ này phải có nhãn rõ ràng `DEMO`; kết quả demo không được đưa vào báo cáo thiết bị thật.

## 8. Tiêu chí hoàn thành cho công cụ sinh giao diện

- Mọi form trong bảng trên tạo đúng JSON và chặn tham số ngoài phạm vi trước khi gửi.
- Một socket UDP nhận được phản hồi lệnh mà không khóa giao diện; `bleReport` có thời gian chờ phù hợp.
- Chọn đúng thiết bị khi có nhiều phản hồi broadcast; lệnh sau khám phá dùng MAC cụ thể.
- Phân biệt timeout, lỗi firmware, thao tác UART đã gửi và kết quả phần cứng đã xác nhận.
- Có nhật ký JSON thô, bảng kết quả và xuất CSV/JSON; thử được giao diện bằng chế độ demo.
