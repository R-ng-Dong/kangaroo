# Đặc tả tool PC test thiết bị Kangaroo

> Có thể đưa nguyên file này cho công cụ sinh giao diện và mã ứng dụng. Nguồn bản tin chi tiết là [test-msg.md](test-msg.md); khi hai tài liệu khác nhau, đối chiếu trực tiếp với `main/factory_test_udp.c`.

## 1. Mục tiêu

Tạo ứng dụng desktop Windows, giao diện tiếng Việt, để kỹ thuật viên test một hoặc nhiều thiết bị Kangaroo. PC tự quét BLE để phát hiện thiết bị, sau đó dùng JSON qua UDP trên cùng mạng LAN để chạy các phép test còn lại. Ưu tiên Python + PySide6 để có thể đóng gói thành `.exe`, nhưng có thể dùng công nghệ desktop tương đương nếu đáp ứng cả BLE và UDP.

Tool chạy cục bộ, không cần tài khoản hoặc cloud. Kỹ thuật viên nhập IP hoặc tìm thiết bị bằng broadcast, chọn thiết bị theo MAC rồi chạy test thủ công. Không giả định rằng `code: 0` của lệnh UART đồng nghĩa phần cứng đã đổi trạng thái.

**Luồng firmware test hiện tại:** `main.c` gọi `FactoryTestUdpStart()` khi thiết bị chưa provisioned. Thiết bị kết nối Wi-Fi nhà máy được cấu hình trong `main/factory_test_udp.c`; tool PC tìm IP qua UDP broadcast hoặc nhập thủ công rồi chạy các phép test. Firmware phát BLE provisioning với tên scan response `KGTEST_<MAC Wi-Fi STA>`.

## 2. Bố cục giao diện

Một cửa sổ có năm vùng rõ ràng:

1. **BLE và Wi-Fi nhà máy:** nút quét BLE bằng adapter PC, danh sách tên BLE provisioning của thiết bị và RSSI do PC đo; sau đó kết nối PC cùng Wi-Fi với thiết bị. Không yêu cầu nhập mật khẩu thiết bị vào tool.
2. **Kết nối LAN:** chọn card mạng PC, IP thiết bị hoặc địa chỉ broadcast, UDP port mặc định `8181`, nút **Tìm thiết bị**, danh sách thiết bị tìm thấy với MAC, IP, SSID, RSSI, thời điểm thấy gần nhất. Cho phép nhập IP và MAC thủ công. Sau khi chọn thiết bị, mọi lệnh dùng MAC đó thay cho `alldevice`.
3. **Test PCBA:** các nhóm Wi-Fi, BLE, ADC TDS, trạng thái bơm, Output và LED RGB. Mỗi nhóm có nút chạy, giá trị đo, trạng thái và thông báo lỗi. Input hiển thị `pumpActive` dạng bật/tắt. Output dùng một ô chọn `index` 0–7 và một lựa chọn `state` bật/tắt để gửi lệnh.
4. **Test UART:** nút bật test mode; các form Reset, Set Mode, Set Filter Time, Set Device ID; nút Get All và bảng dữ liệu trả về. Các nút có tác động lên thiết bị phải cho người vận hành xem tham số trước khi gửi; Reset cần hộp xác nhận.
5. **Nhật ký và kết quả:** bảng thời gian, IP/MAC, method yêu cầu/phản hồi, trạng thái, thời gian phản hồi; ô xem JSON gửi/nhận nguyên văn; nút sao chép, xóa log và xuất CSV/JSON. Có ô mã ca test hoặc mã sản phẩm và tên người vận hành để đưa vào báo cáo.

Trạng thái hiển thị thống nhất: `Chưa chạy`, `Đang chờ`, `Thành công`, `Lỗi`, `Không phản hồi`, `Chờ xác nhận thực tế`. Không tô xanh “đạt” cho lệnh UART chỉ vì nhận phản hồi thành công; cho phép kỹ thuật viên đánh dấu đạt sau khi quan sát thiết bị hoặc đối chiếu `getAllState`.

## 3. Wi-Fi mặc định của firmware test

Trong chế độ test, `FactoryTestUdpStart()` kết nối SSID `TTS` bằng mật khẩu đặt trong `main/factory_test_udp.c` sau khi khởi tạo platform. Firmware giữ cấu hình này trong RAM, không ghi đè thông tin Wi-Fi đã lưu trong flash. PC phải kết nối tới cùng mạng `TTS` để gửi UDP. Log `Connecting to factory Wi-Fi` chỉ cho biết đã bắt đầu kết nối; log `Factory Wi-Fi ready, IP: ...` mới xác nhận thiết bị có IP. Nếu không thấy thiết bị qua broadcast, cho phép nhập IP từ log và thử `scanDevWifi` trực tiếp.

Tool dùng adapter BLE của PC để quét tên `KGTEST_<MAC Wi-Fi STA>` (ví dụ `KGTEST_10003bb87464`) và đo RSSI. Tên nằm trong scan response, nên cần thu cả scan response. Firmware hiện bỏ qua method UDP `scanDevBle`, không gửi `scanDevBleRsp`. Tool tách MAC từ tên BLE, rồi đối chiếu với `scanDevWifiRsp.params.data.mac` sau khi có UDP. Tool không gửi SSID/mật khẩu cho thiết bị qua BLE.

## 4. Giao thức UDP

- Thiết bị lắng nghe IPv4 UDP trên port `8181` và trả lời về **IP/port nguồn** của gói yêu cầu. Dùng **một socket UDP lâu sống** cho cả gửi và nhận; không tạo socket nhận với port khác. Luồng nhận chạy nền để giao diện không bị treo.
- JSON UTF-8, mỗi datagram là một đối tượng JSON. Gói yêu cầu tối đa 1024 byte theo bộ đệm firmware.
- Yêu cầu dùng payload RPC dạng ThingsBoard: `{"id":1,"method":"scanDevWifi","params":{}}`. `id` là số nguyên 0–4294967295 tùy chọn; `params` luôn là object. `mac` cũng tùy chọn; nếu gửi, dùng 12 ký tự hex thường không dấu `:` hoặc `alldevice` khi tìm thiết bị.
- Phản hồi: `{"method":"scanDevWifiRsp","params":{"data":{"mac":"aabbccddeeff","connected":true}}}` cho `scanDevWifi`. Các method khác trả `<method>Rsp`; lỗi có `params.code = -1` và `params.error`. Phản hồi không mang `id`.
- `testInput` trả trạng thái bơm tại thời điểm đọc; firmware không gửi sự kiện input tự động. Muốn cập nhật, tool gửi lại RPC `testInput`.
- Ghép phản hồi bằng `method` và IP nguồn; khi có `params.data.mac`, dùng thêm MAC để phân biệt thiết bị. Vì phản hồi không có `id`, chỉ gửi một lệnh đang chờ cho mỗi thiết bị. Khi tìm bằng broadcast, nhận nhiều `scanDevWifiRsp` và gộp theo MAC.
- Mặc định chờ 2 giây cho lệnh UDP; timeout phải cấu hình được. Quét BLE chạy độc lập trên PC và có thời gian chờ riêng. Lệnh đọc có thể cho phép thử lại bằng nút riêng; **không tự gửi lại** Reset, Set Mode, Set Filter Time, Set Device ID, Output hoặc LED RGB.
- Khi không có phản hồi, hiển thị `Không phản hồi` với gợi ý kiểm tra IP, cùng mạng Wi-Fi, port 8181 và MAC đích. Không suy ra `code: -1` vì thiết bị không gửi mã lỗi khi JSON thiếu trường RPC bắt buộc hoặc MAC sai.
- Tìm kiếm broadcast chỉ là tiện ích. Nếu mạng chặn broadcast, người vận hành vẫn phải dùng được IP thủ công. Thiết bị phải nhận IP từ Wi-Fi nhà máy trước khi PC có thể gửi UDP.

## 5. Form và dữ liệu từng lệnh

| Nhóm UI                | RPC method                     | Đầu vào trong `params`                                                               | Hiển thị `params.data`                                                                                                                    |
| ---------------------- | ------------------------------ | ------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| Wi-Fi                  | `scanDevWifi`                  | Không                                                                                | `connected`, `ssid`, `rssi` dBm, `channel`, `ip`; `ip` có thể vắng mặt                                                                    |
| Loại thiết bị          | `getTypeDev`                   | Không                                                                                | `type`: 0 máy tiêu chuẩn, 1 máy nóng/lạnh                                                                                                 |
| BLE                    | Quét trực tiếp bằng adapter PC | Không                                                                                | PC hiển thị tên và RSSI BLE; không có phản hồi UDP                                                                                        |
| ADC                    | `testAdc`                      | Không                                                                                | `raw1`, `raw2` (0–4095) từ hai kênh ADC TDS trên bo ESP32-C3                                                                              |
| Input                  | `testInput`                    | Nút Đọc trạng thái bơm                                                               | `pumpActive` true/false từ `PumpIsActive()`                                                                                               |
| Output                 | `output`                       | Chọn một `index` nguyên 0–7 và `state` 0/1                                           | Echo `index`, `state`; xem `params.code`                                                                                                  |
| LED RGB                | `testLed`                      | `color`: `R`, `G` hoặc `B`                                                           | Xem `params.code` trong `testLedRsp`                                                                                                      |
| Reset                  | `reset`                        | Hộp xác nhận                                                                         | Xem `params.code`; cảnh báo kết nối có thể gián đoạn sau khi gửi                                                                          |
| Chế độ                 | `setMode`                      | `mode`: 0 nóng 100°C, 1 lạnh 20°C, 2 tiết kiệm điện                                  | Echo `mode`; xem `params.code`                                                                                                            |
| Thời gian lọc          | `setFilterTime`                | `filterIdx`: 0 lõi 1, 1 lõi 2, 2 lõi 3, 3 RO, 4 Post; `time`: số phút nguyên 0–65535 | Echo `filterIdx`, `time`; xem `params.code`                                                                                               |
| Test mode              | `testMode`                     | Không                                                                                | Xem `params.code`; có thể cần trước lệnh Output                                                                                           |
| Device ID              | `setDevId`                     | `devId`: số nguyên 0–4294967295                                                      | Echo `devId`; lỗi xem `params.code`                                                                                                       |
| Trạng thái tổng / UART | `getAllState`                  | Không                                                                                | `productId`, `firmwareVersion`, `mainboardVersion` nếu có khung A; khung UART hợp lệ gần nhất, trạng thái từng loại khung và bộ đếm khung |

Không hardcode ngưỡng đạt của RSSI/ADC. Cho phép nhập ngưỡng trong phần cài đặt ca test; nếu chưa có ngưỡng, chỉ hiển thị số đo và để kỹ thuật viên xác nhận. RSSI BLE là số đo của adapter PC; `advertising:true` từ UDP chỉ xác nhận firmware đã gọi phát quảng bá, chưa chứng minh PC thu được tín hiệu.

ADC trả một giá trị raw cho mỗi TDS sau khi firmware dừng tác vụ TDS, cấu hình bốn chân Pulse và hai kênh ADC. Chân Pulse chỉ ở mức đối nghịch trong lúc lấy mẫu và được trả về 0 sau đó. Chưa có ngưỡng đạt cố định. `pumpActive` là trạng thái bơm active-low tại thời điểm gọi; tool có thể cho phép đọc lại thủ công. `index` 0–7 của lệnh output là miền giá trị firmware chấp nhận, không khẳng định cả tám output đều có phần cứng trên sản phẩm.

## 6. Luồng làm việc đề nghị

1. PC quét BLE provisioning và ghi RSSI đo tại PC. Nếu không thấy, báo không tìm thấy BLE và cho phép quét lại. Dùng MAC trong tên `KGTEST_<MAC>` để phân biệt các thiết bị; xác nhận lại MAC qua UDP sau đó.
2. Kết nối PC vào cùng mạng Wi-Fi `TTS`, đợi thiết bị nhận IP; chọn card mạng, tìm hoặc nhập IP thiết bị và chạy `scanDevWifi` để xác nhận IP/MAC. So MAC trả về với MAC trong tên BLE.
3. Chạy `testAdc`, đọc `testInput`; cho phép người vận hành thao tác vật lý và đánh dấu kết quả.
   `getAllStateRsp.params.code: 0` khi UART1 đã nhận ít nhất một khung hợp lệ loại 1 hoặc A. Tool hiển thị các trường của từng loại khung đã nhận và hai bộ đếm sequence; đọc lại để xác nhận khung UART tiếp tục cập nhật. Không còn phép thử echo byte thô.
4. Khi cần test bo điều khiển, gửi `testMode`, sau đó thử Output, Set Mode, Set Filter Time hoặc Set Device ID. Đọc `getAllState` sau lệnh ghi và so sánh trường liên quan nếu phản hồi UART hỗ trợ. Không tự gửi Reset trong chuỗi test.
5. Kết thúc phiên, xuất báo cáo gồm thời gian, mã ca, người vận hành, IP/MAC, từng bản tin, số đo, kết quả tự động và xác nhận thủ công. Không lưu mật khẩu Wi-Fi trong báo cáo.

## 7. Xử lý lỗi và chế độ demo

Các `error` firmware có thể trả: `invalid_argument`, `unknown_command`, `no_uart_data`, `wifi_not_connected`, `ble_advertise_failed`, `adc_init_failed`, `adc_read_failed`, `uart_send_failed`, `led_failed`. Hiển thị tên lỗi và diễn giải tiếng Việt; giữ nguyên JSON thô để tra cứu.

Có **Chế độ demo** chạy không cần thiết bị, dùng phản hồi mẫu từ [test-msg.md](test-msg.md) để xem giao diện và thử xuất báo cáo. Chế độ này phải có nhãn rõ ràng `DEMO`; kết quả demo không được đưa vào báo cáo thiết bị thật.

## 8. Tiêu chí hoàn thành cho công cụ sinh giao diện

- Mọi form trong bảng trên tạo đúng JSON và chặn tham số ngoài phạm vi trước khi gửi.
- Tool PC quét BLE độc lập với UDP và đo RSSI; một socket UDP nhận phản hồi mà không khóa giao diện.
- Chọn đúng thiết bị khi có nhiều phản hồi broadcast; lệnh sau khám phá dùng MAC cụ thể.
- Phân biệt timeout, lỗi firmware, thao tác UART đã gửi và kết quả phần cứng đã xác nhận.
- Có nhật ký JSON thô, bảng kết quả và xuất CSV/JSON; thử được giao diện bằng chế độ demo.
