# Yêu cầu firmware: bản tin `report` và `alerts` (v2)

Máy lọc nước gửi lên cloud hai loại bản tin. Nguyên tắc lớn nhất của phiên bản này: **thiết bị tự
bảo đảm "ít nhất một lần"**. Một cảnh báo được gửi lại theo thang tần suất cho đến khi server
**xác nhận đã nhận** bằng một bản tin đi xuống. Nhờ vậy:

- một bản tin mất trên đường đi không làm mất thông tin;
- server không cần hàng đợi gửi lại: bản lặp của máy chính là cơ chế gửi lại;
- "xác nhận" chỉ có nghĩa **đã nhận và đã lưu**, không phải **đã xử lý xong**. Người dùng được báo
  khi nào là việc của cloud, không liên quan tới xác nhận này.

| Topic | Khi nào | Chở gì |
|---|---|---|
| `service/RAL/alerts` | **ngay khi** một lỗi xuất hiện hoặc hết, rồi gửi lại theo thang tần suất tới khi được xác nhận | từng **sự kiện** lỗi |
| `service/RAL/report` | **định kỳ** (mặc định 24 giờ), và sau khi kết nối | ảnh chụp `state` + `extra` + danh sách lỗi đang mở |

`report` lo chuỗi số liệu đều theo thời gian (thống kê, biểu đồ) và là lưới an toàn khi sự kiện bị
mất. `alerts` lo tốc độ: lỗi mới, hoặc lỗi vừa hết, về tới cloud trong vài giây.

Danh mục mã lỗi: **Bảng mã lỗi và cảnh báo** (mục 10) do Kangaroo ban hành.

---

## 1. Quy ước chung

- Thiết bị chỉ publish hai topic này; bản tin đi xuống nhận ở topic lệnh
  (mục 5).
- Payload là JSON UTF-8, không khoảng trắng thừa. Giới hạn: `alerts` **tối đa 2 KB**, `report`
  **tối đa 4 KB**. Bộ đệm MQTT của firmware phải ≥ 4 KB.
- `v`: phiên bản hợp đồng, hiện là `2`. Trường lạ bị bỏ qua ở mọi phía, nên được thêm trường mà
  không hỏng gì; đổi nghĩa hoặc đổi kiểu một trường đã có thì phải tăng `v`.
- `mac`: MAC của **máy được nói tới**, 12 ký tự hex thường, không dấu phân cách. Lấy theo máy được
  nói tới, không theo máy gửi, nên về sau một máy có thể gửi thay máy khác.
- Không đọc được một giá trị thì **bỏ nó** khỏi payload, **không** gửi `0` hay `-1`. Với cloud, `0`
  là một số đo thật.
- Thời gian: cloud lấy theo lúc nhận được bản tin, nên máy không cần gửi giờ.

---

## 2. Ba dạng nội dung

### 2.1 `state` — như bản tin state

Chỉ có trong `report`. Giống bản tin state đang dùng, **chỉ giữ phần trạng thái**. Ví dụ của một máy
lọc nước (giá trị lấy từ bản tin state thật):

```json
"state": {
  "1": {
    "45057": [45057, 768, 768, 512, 0, 0, 0],
    "45061": [45061, 1, 2, 515, 65488, 1, 49320, 53],
    "45569": [45569, 0, 2],
    "46096": [46096, 0, 0]
  },
  "2": { "160": [160, 48], "63233": [63233, 0, 355, 0, 0] },
  "3": { "160": [160, 36] },
  "4": { "1": [1, 1], "20": [20, 90] },
  "5": { "1": [1, 1], "20": [20, 12] }
}
```

Dạng: `"<elementId>": { "<attributeId>": [attributeId, giá trị, giá trị, …] }`.

- Cách mã hoá attribute và đơn vị **giống hệt bản tin state**: không có quy ước mới nào.
- **Bỏ**: `eventTime`, `userId`, id thiết bị, `eid`, `changes`.
- `state` là **ảnh chụp đầy đủ** mọi element và attribute tại lúc gửi.
- Giá trị cộng dồn (tổng lượng nước, tổng điện năng…) chỉ tăng, không reset khi mất điện hay cập
  nhật firmware.

**Attribute của máy lọc nước mà cloud sẽ đọc** (theo *Tài liệu bản tin Máy lọc nước*):

| Thông tin | Element | Attribute | Cấu trúc |
|---|---|---|---|
| Chất lượng nước đầu vào (TDS vào) | 2 | 160 | `[attr, tds ppm]` |
| Chất lượng nước đầu ra (TDS ra) | 3 | 160 | `[attr, tds ppm]` |
| Bầu nóng / bầu lạnh bật tắt | 4 / 5 | 1 | `[attr, onoff]` |
| Nhiệt độ đặt bầu nóng (40–90 °C) / bầu lạnh (0–15 °C) | 4 / 5 | 20 | `[attr, temp °C]` |
| Tuổi thọ lõi lọc, lõi k ở element k | 1 → 5 | 45568 | `[attr, productId, expiredAt, usedTime]` |
| Lỗi / cảnh báo | 1 | 46096 | `[attr, bitmask]`: bit n ứng với mã ER000(51+n) |
| Lưu lượng nước | 1 | 45569 | `[attr, tổng nước (4B)]` |
| Điện năng (tổng và tiết kiệm) | 2 | 63233 | `[attr, total (4B Wh), saved (4B Wh)]` |
| Phiên bản firmware | 1 | 45057 | `[attr, …]` |
| Kết nối mạng | 1 | 45061 | `[attr, …]` |

Ba thông tin theo chiều app → máy không nằm trong `report`: thay lõi lọc (attribute 62624), tiết
kiệm điện (63232), sục rửa (63236).

Giá trị 4 byte được gửi thành hai số 16 bit liên tiếp, cao rồi thấp: ví dụ `[63233, 0, 355, 0, 0]`
là tổng 355 Wh, tiết kiệm 0. Attribute nào có giá trị 4 byte đều đi theo quy ước này.

### 2.2 `extra` — thứ ngoài hệ attribute

```json
"extra": { "locationId": "6ac262893e68478edf110594", "mainboard": "2.1.0", "uptime": 86400, "configVersion": 7 }
```

Map phẳng (khoá → số, chuỗi hoặc true/false). Cái gì đã là attribute thì để ở `state`, không lặp ở
`extra`. Ví dụ phiên bản firmware của máy lọc nước đã là attribute 45057 nên **không** đưa `fw` vào
`extra`. Các khoá cloud đọc: `locationId`, `mainboard`, `iotBoard`, `uptime` (giây), và `configVersion`
(phiên bản cấu hình máy đang áp, mục 5.2). Server đối chiếu `configVersion` của máy với phiên
bản hiện tại khi nhận report; lệch thì mới gửi `config.set` xuống riêng máy đó. Khoá khác lưu
nguyên. Giới hạn: tối đa 32 khoá, tên khoá ≤ 32 ký tự, giá trị chuỗi ≤ 64 ký tự, cả `extra` ≤ 512
byte, không lồng object, không mảng.

### 2.3 `alerts` — sự kiện lỗi, không phải danh sách

Mỗi bản tin `alerts` mang **một sự kiện**: một lỗi cụ thể xuất hiện, hoặc lỗi đó hết. Không gửi
danh sách. Mỗi lần xảy ra là một lần lỗi riêng, có một `aid` riêng (mục 4).

---

## 3. Topic `service/RAL/alerts`

### Payload

```json
{
  "v": 2,
  "mac": "10003bb613a0",
  "deviceId": "6ae17f40db42c019d7379806",
  "aid": "a3f9c21d",
  "code": "ER00052",
  "severity": "error",
  "state": "active",
  "tier": 1,
  "info": { "value": 312, "limit": 300 }
}
```

| Trường | Bắt buộc | Ghi chú |
|---|---|---|
| `v` | có | `2` |
| `mac` | có | mục 1 |
| `deviceId` | có | id thiết bị trên nền tảng|
| `aid` | có | định danh của **lần lỗi này**, 8 ký tự hex sinh ngẫu nhiên (mục 4) |
| `code` | có | `ER` + 5 chữ số |
| `severity` | có | `error` / `warning`, cố định theo bảng mã (mục 10) |
| `state` | có | `active`: lỗi xuất hiện · `cleared`: lỗi đã hết |
| `tier` | có | bậc gửi lại mà máy đang ở cho sự kiện này: `1` / `2` / `3` (mục 4) |
| `info` | không | số đo kèm theo, phẳng, ≤ 64 byte |

## 4. `aid` và thang gửi lại

**`aid` — định danh một lần lỗi.** 8 ký tự hex, gồm hai nửa:

- **4 ký tự đầu: sinh ngẫu nhiên một lần khi máy khởi động** (giữ trong RAM).
- **4 ký tự sau: bộ đếm RAM**, tăng 1 cho mỗi sự kiện mới trong phiên chạy.

Lý do hai nửa chứ không một kiểu:

- Bộ đếm thuần không được: mất điện thì quay về 0, chuỗi `aid` lặp lại y như cũ, server sẽ tưởng
  lần lỗi mới là lần cũ.
- Random thuần cũng không được: trong một phiên chạy không có gì chặn hai lần lỗi trùng `aid`.
  Nếu trùng đúng lúc `cleared` của lần cũ còn trong hàng chờ, server coi `active` của lần mới là
  bản trùng rồi đóng lỗi bằng `cleared` muộn — lỗi đang thật sự mở bị đánh dấu đã xử lý.
- Hai nửa thì trong một phiên chạy bộ đếm không bao giờ lặp; khác phiên thì nửa random khác.
  Không cần lưu flash: mất điện chỉ làm mất bộ đếm, không làm chuỗi lặp lại.
- Cùng một lỗi đang kéo dài thì **giữ nguyên `aid`** cho mọi lần gửi `active`.
- Lỗi hết thì gửi `cleared` với **cùng `aid`**, rồi bỏ nó. Lần sau tái phát là `aid` mới.
- Khi gửi `cleared` của một `aid`, **bỏ luôn `active` cùng `aid` khỏi hàng chờ**, kể cả khi nó chưa
  được xác nhận — `cleared` đã thay nó. Không bỏ thì `active` cũ còn chạy sẽ khiến server mở lại
  một lỗi mà máy đã báo là hết.

**Thang gửi lại — mỗi sự kiện (`aid` + `state`) gửi cho tới khi được xác nhận:**

| Bậc | Chu kỳ | Số lần tối đa |
|---|---|---|
| 1 | 15 s | 5 |
| 2 | 60 s | 5 |
| 3 | 6000 s (100 phút) | không giới hạn |

- Bắt đầu từ bậc 1; hết số lần tối đa thì sang bậc kế tiếp. Mỗi sự kiện có thang riêng.
- Nhận được xác nhận đúng `(aid, code, state)` thì **dừng ngay** mọi lần gửi của sự kiện đó.
- Sự kiện `cleared` cũng đi theo thang này: nếu máy hết lỗi đúng lúc mất mạng, `cleared` sẽ được
  gửi lại ở bậc 3 tới khi server xác nhận.
- Các con số này do server cấu hình được (mục 5), và máy phải kẹp giới hạn an toàn cho mọi giá trị
  nhận về.
- Giữ tối đa 8 sự kiện chưa được xác nhận trong hàng chờ. Quá thì bỏ sự kiện cũ nhất: `report`
  (mục 7) sẽ bù lại trạng thái.
- Đang mất kết nối thì không xếp hàng thêm lần gửi mới; bản đã trong hàng chờ giữ nguyên.

### Khi nào gửi

- Ngay khi một mã xuất hiện (`active`) hoặc hết (`cleared`), sau debounce.
- Không gửi khi không có gì thay đổi. Giá trị trong `info` đổi mà không có mã xuất hiện hay hết
  thì không gửi.
- Tối đa 1 bản tin mỗi 10 giây trên toàn topic. Nhiều sự kiện cùng tới hạn thì gửi từng bản tin
  cách nhau ≥ 10 giây; thang đếm theo **số lần đã gửi**, không phải thời gian trôi qua.
- Không cần gửi khi vừa kết nối: `report` lo việc đó.

---

## 5. Bản tin đi xuống: xác nhận và cấu hình

Máy nhận trên **topic lệnh của chính nó** (topic cmd).
Có hai loại:

### 5.1 Xác nhận (`alert.ack`)

```json
{ "type": "alert.ack", "aid": "a3f9c21d", "code": "ER00052", "state": "active" }
```

- Nhận đúng `(aid, code, state)` đang trong hàng chờ → dừng gửi sự kiện đó. `type` hoặc các trường
  không khớp → bỏ qua, không ảnh hưởng hàng chờ.
- Xác nhận **chỉ nghĩa là server đã nhận và lưu**. Lỗi chưa được xử lý thì vẫn là đang mở.
- Không retained. Mất bản xác nhận không sao: lần gửi lặp kế tiếp của máy sẽ được xác nhận lại.

### 5.2 Cấu hình (`config.set`) — retained, có phiên bản

```json
{
  "type": "config.set",
  "configVersion": 7,
  "values": {
    "alert.tier1.intervalMs": 15000,
    "alert.tier1.maxSends": 5,
    "alert.tier2.intervalMs": 60000,
    "alert.tier2.maxSends": 5,
    "alert.tier3.intervalMs": 6000000,
    "report.intervalMs": 86400000
  }
}
```

- **Retained**: máy nhận bản cấu hình mới nhất ngay khi subscribe lại. Không cần lỗi đang mở mới
  nhận được cấu hình.
- Máy nhớ `configVersion` (lưu flash). Nhận bản có phiên bản **lớn hơn** phiên bản đang dùng thì áp
  và lưu lại; bằng hoặc nhỏ hơn thì bỏ qua.
- **Kẹp giới hạn an toàn cho mọi giá trị**: mọi chu kỳ ≥ 5 s; `maxSends` ≥ 1; `report.intervalMs`
  trong khoảng 60 s đến 24 h. Giá trị ngoài khoảng thì dùng giá trị giới hạn gần nhất. Cấu hình sai
  không bao giờ được làm máy tê liệt.
- Khoá lạ bỏ qua.
- Giá trị mặc định khi chưa từng nhận cấu hình: đúng bảng ở mục 4 và mục 7.

---

## 6. Topic `service/RAL/report`

### Payload

```json
{
  "v": 2,
  "mac": "10003bb613a0",
  "state": {
    "1": {
      "45057": [45057, 768, 768, 512, 0, 0, 0],
      "45569": [45569, 0, 2],
      "46096": [46096, 0, 0]
    },
    "2": { "160": [160, 48], "63233": [63233, 0, 355, 0, 0] },
    "3": { "160": [160, 36] },
    "4": { "1": [1, 1], "20": [20, 90] },
    "5": { "1": [1, 1], "20": [20, 12] }
  },
  "extra": { "locationId": "6ac262893e68478edf110594", "uptime": 86400, "configVersion": 7 },
  "alerts": [
    { "code": "ER00052", "severity": "error", "aid": "a3f9c21d" }
  ]
}
```

| Trường | Bắt buộc | Ghi chú |
|---|---|---|
| `v`, `mac` | có | |
| `state` | nên có | ảnh chụp đầy đủ, mục 2.1 |
| `extra` | không | mục 2.2 |
| `alerts` | có | danh sách **đang mở** tại lúc gửi: mỗi phần tử `code`, `severity`, `aid` (giữ `aid` hiện tại của lỗi). `[]` = không còn lỗi nào. Đây là lưới an toàn: cloud đối chiếu với các lỗi đang lưu để đóng cái thiếu, mở cái thiếu |

`report` không đi theo thang gửi lại và không cần xác nhận. Bản này bị mất thì bản kế tiếp mang
trạng thái đúng.

### Khi nào gửi

| Thời điểm | Ghi chú |
|---|---|
| Sau khi kết nối (hoặc kết nối lại) broker thành công | chờ ngẫu nhiên **0–30 giây** rồi gửi |
| Định kỳ, mặc định mỗi **24 giờ** | mỗi lần cộng/trừ ngẫu nhiên tới **±60 giây**; chỉnh được qua `report.intervalMs` (mục 5.2) |

- Luôn gửi, kể cả khi không có lỗi nào (`alerts: []`).
- Một lần mỗi ngày là đủ có chủ ý: các chỉ số của máy lọc nước thay đổi rất chậm, có khi cả tháng
  mới đổi. Bản tin này giữ cho "lần báo gần nhất" không bị quá cũ và là mẫu thưa cho thống kê dài
  hạn. Nó **không** thay bản tin state mà app đang dùng.
- Lưới an toàn thật sự cho cảnh báo là thang gửi lại của `alerts` (mục 4), không phải `report`.
- Độ trễ ngẫu nhiên là bắt buộc: sau một lần mất điện cả khu, hàng nghìn máy kết nối lại cùng lúc.
- Hai report cách nhau tối thiểu 1 phút.
- Đang mất kết nối thì không lưu report cũ để gửi bù. Kết nối lại chỉ gửi report hiện tại.

---

## 7. Ví dụ một ngày

| Lúc | Máy | Bản tin | Cloud |
|---|---|---|---|
| 08:00:12 | kết nối, không lỗi | `report` có `state`, `alerts: []` | cập nhật trạng thái |
| 08:15 | chất lượng nước đầu ra kém ≥ 60 s | `alerts` `aid A active ER00052 tier 1` | **mở** ER00052, báo người dùng, gửi `alert.ack` |
| 08:15:20 | nhận ack | ngừng sự kiện `aid A active` | |
| 09:20 | lõi 2 quá 90% | `alerts` `aid B active ER00062 tier 1` | **mở** ER00062, ack |
| 09:25 | khách thay lõi 2, máy đặt lại bộ đếm | `alerts` `aid B cleared ER00062 tier 1` | **đóng** ER00062, ack |
| 09:40 | mất wifi; TDS về bình thường | `alerts` `aid A cleared ER00052` không gửi được | không đổi |
| 09:41 | kết nối lại | gửi lại `aid A cleared` theo thang | **đóng** ER00052, ack |
| 08:00 hôm sau | | `report` `alerts: []` | đối chiếu: mọi lỗi đã đóng, khớp |

Bản tin lúc 09:40 bị mất nhưng không sao: sự kiện `cleared` được gửi lại ở bậc 3 tới khi có ack, và
`report` hôm sau cũng nói không còn lỗi.

---

## 8. Cloud xử lý thế nào (tổng quát)

- **Hai topic đi chung một luồng theo từng máy**, xử lý theo thứ tự máy gửi. Bản tin cũ đến trễ
  không mở lại lỗi đã đóng (mốc so sánh là lúc server nhận).
- **Sự kiện `active`:** chưa có lỗi đang mở cùng mã → mở lỗi, thông báo người dùng. Có lỗi cùng mã:
  cùng `aid` → bản trùng; khác `aid` → làm mới (cập nhật `aid`, lần thấy gần nhất), **không** tạo
  lỗi thứ hai.
- **Sự kiện `cleared`:** lỗi đang mở cùng mã và `aid` → đóng (đã xử lý). `aid` khác → đã hết từ
  trước, chỉ xác nhận.
- **Máy offline không đóng lỗi nào** (chỉ sự kiện `cleared` hoặc `report` của chính máy đó đóng).
- **Xác nhận được gửi cho mọi sự kiện đã lưu**, bản trùng bao gồm, và **sau khi đã lưu** — để bản
  xác nhận mất thì lần lặp kế tiếp được xác nhận lại. Xác nhận không phải "đã xử lý".
- **`report.alerts`** là phép đối chiếu: lỗi đang lưu mà không có trong danh sách → đóng; mã có
  trong danh sách mà chưa mở → mở. `report.state` và `extra` lưu theo thời gian, ý nghĩa attribute
  do cloud đọc theo danh mục.
- Bản tin sai định dạng, hoặc MAC không có trong hệ thống, bị bỏ qua (không xác nhận: không có máy
  nào để nhận).

Chi tiết lưu trữ và API phía cloud sẽ thiết kế riêng.

---

## 9. Bảng mã và `severity`

| Mã | Nội dung | `severity` |
|---|---|---|
| ER00051 | Chất lượng nước đầu vào không đảm bảo | `warning` |
| ER00052 | Chất lượng nước đầu ra không đảm bảo | `error` |
| ER00053 | Rò nước *(chưa dùng)* | `error` |
| ER00054 | Cạn nước trong bình chứa *(chưa dùng)* | `error` |
| ER00055 | Bơm RO hoạt động quá 60 phút | `error` |
| ER00056–60 | Lõi lọc 1–5 quá 70% tuổi thọ | `warning` |
| ER00061–65 | Lõi lọc 1–5 quá 90% tuổi thọ | `warning` |
| ER00066 | Cảnh báo chất lượng nước đầu ra | `warning` |
| ER00067 | Cảm biến bầu nóng hỏng | `warning` |
| ER00068 | Bầu nóng chạy liên tục quá 1 giờ | `warning` |
| ER00069 | Cảm biến bầu lạnh hỏng | `warning` |
| ER00070 | Bầu lạnh chạy liên tục quá 2 giờ | `warning` |
| ER00071–74 | Dự phòng, Kangaroo khai báo sau | theo khai báo |

- ER00053, ER00054 đang "chưa dùng đến": chưa cần gửi.
- ER00071–74 chỉ gửi khi đã có khai báo.

---

## 10. Tham số hệ thống (máy đọc, server ghi)

| Khoá | Mặc định | Khoảng máy phải kẹp |
|---|---|---|
| `alert.tier1.intervalMs` | 15000 | ≥ 5000 |
| `alert.tier1.maxSends` | 5 | ≥ 1 |
| `alert.tier2.intervalMs` | 60000 | ≥ 5000 |
| `alert.tier2.maxSends` | 5 | ≥ 1 |
| `alert.tier3.intervalMs` | 6000000 | ≥ 5000 |
| `report.intervalMs` | 86400000 | 60000 – 604800000 (7 ngày) |

Giá trị chỉ đổi khi server gửi `config.set` có `configVersion` lớn hơn (mục 5.2).
