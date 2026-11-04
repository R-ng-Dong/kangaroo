# Bản tin thiết bị máy lọc nước: tổng hợp

Tài liệu cho team firmware: **mọi bản tin** máy lọc nước gửi lên và nhận về cho cảnh báo, báo cáo định
kỳ, xác nhận và cấu hình.

---

## 1. Toàn cảnh

```
CHIỀU LÊN (máy → cloud)
  service/RAL/alerts    sự kiện lỗi: xuất hiện hoặc hết
  service/RAL/report    ảnh chụp định kỳ: state + extra + danh sách lỗi đang mở
  …/rpt/state           bản tin state hiện có

CHIỀU XUỐNG (cloud → máy): bản tin JSON trên topic lệnh của chính máy
  alert.ack             xác nhận đã nhận một sự kiện lỗi
  config.set            cấu hình vận hành
```

| Bản tin | Chiều | Topic | Khi nào | Retained |
|---|---|---|---|---|
| `alerts` | lên | `service/RAL/alerts` | lỗi xuất hiện hoặc hết; gửi lại tới khi được xác nhận | không |
| `report` | lên | `service/RAL/report` | sau khi kết nối, và định kỳ | không |
| state | lên | `…/rpt/state` (bản tin state hiện có) | theo luật phần `state` (mục 7) | không |
| `alert.ack` | xuống | topic lệnh của máy | sau khi cloud nhận và lưu một sự kiện `alerts` | không |
| `config.set` | xuống | topic lệnh của máy | máy báo phiên bản cấu hình cũ hoặc chưa có | chỉ phần `core` |

Topic lệnh là topic mà app và cloud vẫn gửi lệnh điều khiển tới máy. Bản tin đi xuống là JSON, có
trường `type`; bản tin không parse được hoặc có `type` máy không biết thì bỏ qua.

Nguyên tắc lớn nhất: **máy tự bảo đảm "ít nhất một lần"**. Mỗi sự kiện lỗi được máy gửi lại theo thang
tần suất cho tới khi cloud xác nhận. Xác nhận chỉ nghĩa là **đã nhận và đã lưu**, không phải đã xử lý
xong.

---

## 2. Quy ước chung cho bản tin đi lên

- QoS 1, `retain = false`.
- Payload JSON UTF-8, không khoảng trắng thừa. `alerts` tối đa **2 KB**, `report` tối đa **4 KB**. Bộ
  đệm MQTT của firmware phải ≥ 4 KB.
- `v`: phiên bản hợp đồng, hiện là `2`. Trường lạ bị bỏ qua ở mọi phía, nên thêm trường không làm hỏng
  gì. Đổi nghĩa hoặc đổi kiểu một trường đã có thì phải tăng `v`.
- `mac`: MAC của **máy được nói tới**, 12 ký tự hex thường, không dấu phân cách. Lấy theo máy được nói
  tới chứ không theo máy gửi, để về sau một máy có thể gửi thay máy khác.
- Không đọc được một giá trị thì **bỏ nó** khỏi payload, không gửi `0` hay `-1`. Với cloud, `0` là số đo
  thật.
- Máy không cần gửi giờ: cloud lấy thời điểm broker nhận bản tin.

---

## 3. Bản tin `alerts`

Mỗi bản tin mang **một sự kiện**: một lỗi cụ thể xuất hiện, hoặc lỗi đó hết. Không gửi danh sách.

```json
{
  "v": 2,
  "mac": "10003bb613a0",
  "deviceId": "6ae17f40db42c019d7379806",
  "aid": "a3f9c21d",
  "code": "ER00052",
  "state": "active",
  "tier": 1,
  "info": { "value": 312, "limit": 300 }
}
```

| Trường | Bắt buộc | Ghi chú |
|---|---|---|
| `v` | có | `2` |
| `mac` | có | mục 2 |
| `deviceId` | nếu có | id thiết bị trên nền tảng. Cloud khoá theo `mac`; trường này chỉ để đối chiếu |
| `aid` | có | định danh **lần lỗi này**, 8 ký tự hex (mục 4) |
| `code` | có | `ER` + 5 chữ số (mục 5) |
| `state` | có | `active`: lỗi xuất hiện · `cleared`: lỗi đã hết |
| `tier` | có | bậc gửi lại máy đang ở cho sự kiện này: `1`, `2` hoặc `3` |
| `info` | không | số đo kèm theo, map phẳng, ≤ 64 byte |

Bản tin không mang mức độ của lỗi: máy chỉ gửi `code`.

**Khi nào gửi:**

- Ngay khi một mã xuất hiện (`active`) hoặc hết (`cleared`), sau debounce (mục 5).
- Không gửi khi không có gì thay đổi. `info` đổi mà không có mã xuất hiện hay hết thì không gửi.
- Tối đa 1 bản tin mỗi 10 giây trên toàn topic. Nhiều sự kiện cùng tới hạn thì gửi lần lượt, cách nhau
  ≥ 10 giây; thang gửi lại đếm theo **số lần đã gửi**, không theo thời gian trôi qua.
- Không cần gửi khi vừa kết nối: `report` lo việc đó.

---

## 4. `aid` và thang gửi lại

**`aid`: 8 ký tự hex, gồm hai nửa.**

- 4 ký tự đầu: **sinh ngẫu nhiên một lần khi máy khởi động**, giữ trong RAM.
- 4 ký tự sau: **bộ đếm trong RAM**, tăng 1 cho mỗi sự kiện mới trong phiên chạy.

Trong một phiên chạy bộ đếm không lặp; khác phiên thì nửa ngẫu nhiên khác. Không cần lưu flash.

- Lỗi đang kéo dài thì **giữ nguyên `aid`** cho mọi lần gửi `active`.
- Lỗi hết thì gửi `cleared` với **cùng `aid`**. Lần tái phát sau là `aid` mới.
- Khi gửi `cleared` của một `aid`, **bỏ luôn `active` cùng `aid` khỏi hàng chờ**, kể cả khi nó chưa
  được xác nhận. Không bỏ thì `active` cũ sẽ làm cloud mở lại lỗi máy đã báo là hết.

**Thang gửi lại.** Mỗi sự kiện (`aid` + `state`) được gửi tới khi nhận được xác nhận:

| Bậc | Chu kỳ (mặc định) | Số lần tối đa (mặc định) | Khoá cấu hình |
|---|---|---|---|
| 1 | 15 s | 5 | `alert.tier1.intervalMs`, `alert.tier1.maxSends` |
| 2 | 60 s | 5 | `alert.tier2.intervalMs`, `alert.tier2.maxSends` |
| 3 | 100 phút | không giới hạn | `alert.tier3.intervalMs` |

- Bắt đầu ở bậc 1; hết số lần tối đa thì sang bậc kế. Mỗi sự kiện có thang riêng.
- Nhận xác nhận đúng `(aid, code, state)` thì **dừng ngay** sự kiện đó.
- `cleared` cũng đi theo thang này.
- Hàng chờ tối đa **8** sự kiện chưa được xác nhận. Quá thì bỏ sự kiện cũ nhất; `report` sẽ bù lại.
- Đang mất kết nối thì không xếp thêm lần gửi mới; bản đã trong hàng chờ giữ nguyên.

---

## 5. Debounce và bảng mã

Một mã chỉ **xuất hiện** khi điều kiện kéo dài liên tục ≥ `T_on`, và chỉ **hết** khi điều kiện đã hết
liên tục ≥ `T_off`. Mọi con số dưới đây chỉnh được qua phần cấu hình `alert` (mục 10).

| Nhóm mã | Điều kiện | `T_on` / `T_off` |
|---|---|---|
| Chất lượng nước (ER00051, 52, 66) | TDS vượt `alert.tdsIn.maxPpm`, `alert.tdsOut.maxPpm`, `alert.tdsOut.warnPpm` | `alert.quality.onSec` / `offSec`, mặc định 60 / 300 s |
| Rò nước, cạn nước (ER00053, 54) | | `alert.leak.onSec` / `offSec`, mặc định 10 / 60 s |
| Chạy quá thời gian (ER00055, 68, 70) | quá `alert.pump.maxRunMin`, `alert.hot.maxRunMin`, `alert.cold.maxRunMin` | xuất hiện khi vượt, hết khi dừng |
| Cảm biến hỏng (ER00067, 69) | | `alert.sensor.onSec` / `offSec`, mặc định 30 / 60 s |
| Tuổi lõi lọc (ER00056–65) | % còn lại chạm `alert.filter.warnPct` hoặc `alert.filter.criticalPct` | xuất hiện khi vượt, hết khi bộ đếm được đặt lại |

**Lõi lọc:**

- Mỗi lõi chỉ báo **mức cao nhất** đang đúng: lõi 1 còn dưới 10% thì có `ER00061`, **không** kèm
  `ER00056`.
- Số lõi trong mã là số khe theo cấu hình model.
- Khi nhận cấu hình lõi mới từ app (khách quét QR lõi), máy đặt lại bộ đếm của khe đó và gửi `cleared`
  cho mã của khe đó.
- Đổi ngưỡng khi lỗi đang mở: lỗi chỉ hết theo luật `T_off` của ngưỡng mới, không tự đóng ngay.

**Bảng mã:**

| Mã | Nội dung |
|---|---|
| ER00051 | Chất lượng nước đầu vào không đảm bảo |
| ER00052 | Chất lượng nước đầu ra không đảm bảo |
| ER00053 | Rò nước *(chưa dùng)* |
| ER00054 | Cạn nước trong bình chứa *(chưa dùng)* |
| ER00055 | Bơm RO chạy quá thời gian (mặc định 60 phút) |
| ER00056–60 | Lõi lọc 1–5 còn dưới mức cảnh báo (mặc định 30%, tức đã dùng 70%) |
| ER00061–65 | Lõi lọc 1–5 còn dưới mức nguy cấp (mặc định 10%, tức đã dùng 90%) |
| ER00066 | Cảnh báo chất lượng nước đầu ra |
| ER00067 | Cảm biến bầu nóng hỏng |
| ER00068 | Bầu nóng chạy liên tục quá thời gian (mặc định 60 phút) |
| ER00069 | Cảm biến bầu lạnh hỏng |
| ER00070 | Bầu lạnh chạy liên tục quá thời gian (mặc định 120 phút) |
| ER00071–74 | Dự phòng, Kangaroo khai báo sau |

ER00053, ER00054 chưa cần gửi. ER00071–74 chỉ gửi khi đã có khai báo.

---

## 6. Bản tin `report`

```json
{
  "v": 2,
  "mac": "10003bb613a0",
  "state": {
    "2": { "160": [160, 48] },
    "3": { "160": [160, 36] }
  },
  "extra": {
    "locationId": "6ac262893e68478edf110594",
    "uptime": 86400,
    "configVersion": 1793606400,
    "cfg.state": 1793606400,
    "cfg.alert": 1793520000,
    "cfg.general": 0
  },
  "alerts": [
    { "code": "ER00052", "aid": "a3f9c21d" }
  ]
}
```

| Trường | Bắt buộc | Ghi chú |
|---|---|---|
| `v`, `mac` | có | |
| `state` | nên có | ảnh chụp **đầy đủ** trạng thái máy lúc gửi (mục 6.1) |
| `extra` | không | mục 6.2 |
| `alerts` | có | danh sách lỗi **đang mở** lúc gửi, mỗi phần tử `code`, `aid` (giữ `aid` hiện tại). `[]` = không còn lỗi nào. Đây là lưới an toàn để cloud đối chiếu |

`report` không đi theo thang gửi lại và không cần xác nhận. Bản này mất thì bản sau mang trạng thái đúng.

**Khi nào gửi:**

| Thời điểm | Ghi chú |
|---|---|
| Sau khi kết nối (hoặc kết nối lại) broker | chờ ngẫu nhiên **0–30 giây** rồi gửi |
| Định kỳ theo `report.intervalMs` | mỗi lần ±60 giây ngẫu nhiên. Mặc định của firmware **24 giờ**; cloud đặt **15 phút** qua `config.set` vì biểu đồ dựng theo điểm 15 phút |

- Luôn gửi, kể cả khi không có lỗi (`alerts: []`).
- Độ trễ ngẫu nhiên là bắt buộc: sau một lần mất điện cả khu, hàng nghìn máy kết nối lại cùng lúc.
- Hai report cách nhau tối thiểu 1 phút.
- Mất kết nối thì không lưu report cũ để gửi bù; kết nối lại chỉ gửi report hiện tại.

### 6.1 `state`

Ảnh chụp **đầy đủ** trạng thái hiện tại của máy, đúng định dạng bản tin state mà SDK đang tạo; không có gì
mới phải mã hoá. Ví dụ ở trên rút gọn. Chỉ lấy phần trạng thái, không kèm thời điểm, id người dùng hay
danh sách thay đổi. Giá trị cộng dồn (tổng nước, tổng điện) chỉ tăng, không reset khi mất điện hay cập
nhật firmware.

### 6.2 `extra`

Map **phẳng** (khoá → số, chuỗi hoặc true/false), không lồng object, không mảng. Tối đa 32 khoá, tên
khoá ≤ 32 ký tự, giá trị chuỗi ≤ 64 ký tự, cả `extra` ≤ 512 byte. Cái gì đã có trong `state` thì không
lặp ở `extra`.

| Khoá | Ý nghĩa |
|---|---|
| `locationId` | id vị trí lắp đặt trên nền tảng, nếu máy biết |
| `mainboard`, `iotBoard` | phiên bản phần cứng hoặc firmware từng bo |
| `uptime` | giây từ lần khởi động gần nhất |
| `configVersion` | phiên bản phần `core` đang áp dụng |
| `cfg.state`, `cfg.alert`, `cfg.general` | phiên bản từng phần cấu hình mới đang áp dụng; phần chưa từng nhận ghi **`0`** |

⚠️ Máy hiểu cấu hình theo phần thì **luôn gửi đủ ba khoá `cfg.*`** trong mọi `report`, kể cả khi cả ba
đều `0`. Có các khoá này là cách máy báo "tôi hiểu `scope`": cloud chỉ gửi các phần `state`, `alert`,
`general` cho máy có chúng. Máy không gửi khoá `cfg.*` nào được coi là chưa hiểu `scope` và chỉ nhận
`core`, vì nó sẽ hiểu nhầm một phần khác là `core`.

Khoá khác cloud lưu nguyên.

---

## 7. Bản tin state: khi nào gửi

Năm nhóm số đo dưới đây gửi bằng **bản tin state hiện có** (qua SDK, như hiện nay), theo luật của phần cấu hình `state`. Các
thuộc tính điều khiển và trạng thái (bật/tắt, khoá trẻ em, chế độ…) **vẫn gửi ngay khi đổi** như hiện
nay, để app phản hồi tức thì.

### 7.1 Quy tắc chung

1. Máy đọc liên tục (TDS 2 lần/giây, bản tin mainboard 1 lần/giây), giữ **giá trị mới nhất**, và chỉ
   **đẩy lên** theo luật ở mục 7.2.
2. **Khoảng chờ không bỏ thay đổi.** Thay đổi trong khoảng chờ được giữ lại; hết khoảng chờ thì gửi
   giá trị mới nhất, gộp thành 1 lần. Nếu lúc đó giá trị đã quay về gần giá trị đã gửi (dưới ngưỡng)
   thì không gửi.
3. Nước và điện luôn gửi **số tích luỹ** (tổng trong ngày + tổng từ đầu), không gửi phần tăng thêm.
4. Một lần gửi định kỳ cũng tính là một lần gửi. Chu kỳ định kỳ tính **từ lần gửi gần nhất** của nhóm.

Thuật toán dùng chung cho TDS, nước, điện:

```
lastSent, lastSentAt, pending = false

mỗi lần đọc giá trị mới v:
  nếu |v - lastSent| >= ngưỡng:
      nếu now - lastSentAt >= minGap: gửi(v)
      ngược lại: pending = true
  nếu pending và now - lastSentAt >= minGap:
      pending = false
      nếu |latest - lastSent| >= ngưỡng: gửi(latest)
  nếu đến hạn định kỳ: gửi(latest)

gửi(x): publish state; lastSent = x; lastSentAt = now
```

### 7.2 Luật từng nhóm

| Nhóm | Áp dụng | Gửi khi thay đổi | Khoảng chờ tối thiểu | Gửi định kỳ |
|---|---|---|---|---|
| **TDS vào / ra** | RO thường, RO nóng lạnh | TDS vào **hoặc** ra đổi ≥ ngưỡng (bên dưới). Luôn gửi **cả cặp** | `state.tds.minGapSec` | bơm chạy: `state.tds.runPeriodSec`; bơm dừng: `state.tds.idlePeriodSec` |
| **Nhiệt độ bầu nóng / lạnh** | RO nóng lạnh | khi **chuyển trạng thái nhiệt** (bên dưới). Dao động khi bầu tự đóng cắt: không gửi. Bầu đang tắt: không gửi chuyển trạng thái | không, gửi ngay | `state.temp.periodSec` |
| **Tuổi thọ lõi lọc** (5 lõi) | RO thường, RO nóng lạnh | % còn lại của **bất kỳ** lõi giảm thêm `state.filter.stepPct` → gửi **cả 5 lõi** | không | không |
| **Lượng nước** | RO thường, RO nóng lạnh | tăng ≥ `state.water.deltaMl` ml | `state.water.minGapSec` | không |
| **Điện năng** | RO nóng lạnh | tăng ≥ `state.energy.deltaWh` Wh. Gửi tổng trong ngày **theo bộ phận** (bơm, bình nóng, bình lạnh, điện phân, bo mạch) + tổng từ đầu | `state.energy.minGapSec` | không |

- **Ngưỡng TDS** = `max(state.tds.deltaPct % × giá trị đã gửi, state.tds.floorPpm)`, tính riêng cho vào
  và ra. Đã gửi 36 ppm: 5% là 1,8 ppm, nhỏ hơn sàn 2 ppm, nên ngưỡng là 2 ppm.
- **Chuyển trạng thái nhiệt:** đang làm nóng/lạnh → **sẵn sàng** khi chạm điểm cài đặt; **sẵn sàng** →
  chưa sẵn sàng khi bình nóng thấp hơn điểm đặt ≥ `state.temp.hotBandC` °C, hoặc bình lạnh cao hơn điểm
  đặt ≥ `state.temp.coldBandC` °C.
- **RO thường** không đo điện theo bộ phận: chỉ gửi điện năng một lần lúc chốt ngày (mục 7.4).

Ước tính cho 1 máy RO nóng lạnh dùng khoảng 12 lít và 2 kWh mỗi ngày: TDS 40–50, nhiệt độ 5–10, lõi
1–2, nước ≤ 12, điện khoảng 10 bản tin/ngày. Cần đo lại khi chạy thử thật.

### 7.3 Khi người dùng mở app

Máy biết app đang mở qua **kênh MQTT riêng giữa app và máy** (đã có, ngoài phạm vi tài liệu này). Cloud
không gửi tín hiệu này, chỉ cấp hai tham số.

1. App báo đang mở: gửi **ngay** state của cả 5 nhóm, bỏ qua khoảng chờ.
2. Trong lúc app còn mở, TDS và nhiệt độ gửi mỗi `state.live.intervalSec` giây. Lõi lọc, nước, điện
   chỉ gửi một lần ở bước 1.
3. App báo đóng thì trở lại luật thường. Kênh app–máy không báo đóng thì máy coi app đã đóng sau
   `state.live.ttlSec` giây kể từ tín hiệu cuối cùng từ app.

### 7.4 Chốt ngày

Lúc 00:00 giờ địa phương (theo `general.tzOffsetMin`), ngay trước khi đặt các bộ đếm "trong ngày" (nước,
điện) về 0, máy gửi state một lần với tổng của ngày vừa hết. Chốt ngày cần đồng hồ đúng: máy đồng bộ
giờ (SNTP) sau khi có mạng. Chưa đồng bộ được thì **không** chốt ngày, cứ cộng tiếp; cloud vẫn tính được
từ tổng từ đầu.

---

## 8. `alert.ack`

```json
{"type":"alert.ack","aid":"7e570001","code":"ER00052","state":"active"}
```

- Khớp đúng `(aid, code, state)` đang trong hàng chờ → dừng gửi sự kiện đó. Không khớp → bỏ qua.
- Không retained. Mất bản xác nhận không sao: lần gửi lặp kế tiếp được xác nhận lại.
- Cloud gửi xác nhận cho **mọi** sự kiện đã lưu, kể cả bản trùng và bản cũ đến trễ, để máy luôn dừng.

---

## 9. `config.set`

### 9.1 Dạng bản tin

Phần `core` (**không có `scope`**):

```json
{
  "type": "config.set",
  "configVersion": 1793606400,
  "values": {
    "alert.tier1.intervalMs": 15000,
    "alert.tier1.maxSends": 5,
    "alert.tier2.intervalMs": 60000,
    "alert.tier2.maxSends": 5,
    "alert.tier3.intervalMs": 6000000,
    "report.intervalMs": 900000
  }
}
```

Các phần mới, có `scope`:

```json
{
  "type": "config.set",
  "scope": "general",
  "configVersion": 1793606400,
  "values": { "general.tzOffsetMin": 420 }
}
```

| Trường | Ghi chú |
|---|---|
| `scope` | `core`, `state`, `alert`, `general`. Không có `scope` = `core` |
| `configVersion` | số nguyên **không dấu 32 bit**. Cloud dùng thời điểm lưu cấu hình tính bằng giây (Unix time), nên số luôn tăng kể cả khi cloud dựng lại dữ liệu. Lưu bằng `uint32`, không dùng kiểu 16 bit |
| `values` | **toàn bộ** khoá của phần đó (ảnh chụp đầy đủ, không phải phần thay đổi) |

Vì sao chia phần: bản tin đi xuống tối đa 1024 byte. Gộp cả 36 khoá là 1005 byte, sát trần; chia ra thì
phần lớn nhất (`state`) khoảng 465 byte.

### 9.2 Máy xử lý thế nào

Tên khoá là **duy nhất toàn máy**, nên máy giữ **một bảng khoá → giá trị phẳng**. `scope` chỉ để đánh
phiên bản và vận chuyển; mọi phần xử lý bằng cùng một hàm:

```
khởi tạo: version[scope] = 0 cho mọi phần chưa từng nhận

nhận config.set:
  scope = msg.scope hoặc "core"
  nếu scope không phải core, state, alert, general: bỏ qua cả bản tin
  nếu msg.configVersion <= version[scope]: bỏ qua
  với mỗi khoá thuộc scope này mà máy biết:
      giá trị = msg.values[khoá] nếu có, không thì mặc định của máy
      kẹp giá trị vào khoảng an toàn (mục 10)
  ghi tất cả cùng lúc vào flash, rồi version[scope] = msg.configVersion
  áp dụng ngay
```

- Khoá lạ bỏ qua. Khoá máy biết mà bản tin thiếu thì về mặc định. `scope` lạ thì bỏ qua cả bản tin.
- Áp dụng **trọn gói**: một phần được áp toàn bộ hoặc không áp gì.
- **Kẹp giá trị**: cấu hình sai không bao giờ được làm máy tê liệt.
- Chưa từng nhận cấu hình thì chạy bằng **mặc định trong firmware**. Máy phải chạy đúng cả khi chưa kết
  nối cloud lần nào.
- Báo phiên bản đang dùng qua `extra` của `report` (mục 6.2).

### 9.3 Retained

Topic lệnh chỉ giữ được **một** bản tin retained. Phần `core` retained, để máy nhận cấu hình mới nhất
ngay khi subscribe lại. Các phần khác **không** retained: máy lưu trong flash, và cloud gửi lại phần nào
máy báo cũ hoặc thiếu.

---

## 10. Bảng khoá cấu hình đầy đủ

Mọi giá trị là số nguyên. Một cấu hình cho mọi model: máy bỏ qua khoá không áp dụng cho phần cứng của nó
(ví dụ RO thường bỏ qua nhiệt độ và điện theo bộ phận).

| Phần | Khoá | Đơn vị | Mặc định | Khoảng máy kẹp |
|---|---|---|---|---|
| core | `alert.tier1.intervalMs` | ms | 15000 | ≥ 5000 |
| core | `alert.tier1.maxSends` | lần | 5 | ≥ 1 |
| core | `alert.tier2.intervalMs` | ms | 60000 | ≥ 5000 |
| core | `alert.tier2.maxSends` | lần | 5 | ≥ 1 |
| core | `alert.tier3.intervalMs` | ms | 6000000 | ≥ 5000 |
| core | `report.intervalMs` | ms | 86400000 (cloud đặt 900000) | 60000 – 604800000 |
| state | `state.tds.deltaPct` | % | 5 | 1 – 50 |
| state | `state.tds.floorPpm` | ppm | 2 | 0 – 50 |
| state | `state.tds.minGapSec` | giây | 60 | 10 – 3600 |
| state | `state.tds.runPeriodSec` | giây | 300 | 60 – 3600 |
| state | `state.tds.idlePeriodSec` | giây | 3600 | 300 – 86400 |
| state | `state.temp.hotBandC` | °C | 5 | 1 – 30 |
| state | `state.temp.coldBandC` | °C | 3 | 1 – 20 |
| state | `state.temp.periodSec` | giây | 3600 | 300 – 86400 |
| state | `state.filter.stepPct` | % | 1 | 1 – 20 |
| state | `state.water.deltaMl` | ml | 1000 | 100 – 100000 |
| state | `state.water.minGapSec` | giây | 900 | 60 – 86400 |
| state | `state.energy.deltaWh` | Wh | 200 | 10 – 10000 |
| state | `state.energy.minGapSec` | giây | 900 | 60 – 86400 |
| state | `state.live.intervalSec` | giây | 60 | 10 – 600 |
| state | `state.live.ttlSec` | giây | 120 | 30 – 1800 |
| alert | `alert.tdsIn.maxPpm` | ppm | _ | 1 – 2000 |
| alert | `alert.tdsOut.maxPpm` | ppm | _ | 1 – 2000 |
| alert | `alert.tdsOut.warnPpm` | ppm | _ | 1 – 2000 |
| alert | `alert.quality.onSec`, `alert.quality.offSec` | giây | 60, 300 | 1 – 3600 |
| alert | `alert.leak.onSec`, `alert.leak.offSec` | giây | 10, 60 | 1 – 3600 |
| alert | `alert.sensor.onSec`, `alert.sensor.offSec` | giây | 30, 60 | 1 – 3600 |
| alert | `alert.pump.maxRunMin` | phút | 60 | 5 – 600 |
| alert | `alert.hot.maxRunMin` | phút | 60 | 5 – 600 |
| alert | `alert.cold.maxRunMin` | phút | 120 | 5 – 600 |
| alert | `alert.filter.warnPct` | % còn lại | 30 | 1 – 99 |
| alert | `alert.filter.criticalPct` | % còn lại | 10 | 1 – 99 |
| general | `general.tzOffsetMin` | phút | 420 | −720 – 840 |

`_`: firmware giữ ngưỡng đang dùng làm mặc định và báo lại con số. Cloud **không gửi** ba khoá này cho
tới khi có giá trị được lưu, nên trong lúc đó máy dùng mặc định của mình.

Hai cặp phải đúng thứ tự: `alert.filter.criticalPct` nhỏ hơn `alert.filter.warnPct`, và
`alert.tdsOut.warnPpm` nhỏ hơn `alert.tdsOut.maxPpm`. Cloud kiểm khi lưu, nhưng một ngưỡng TDS có thể
chưa được lưu (máy dùng mặc định), nên máy vẫn tự kiểm: sai thứ tự thì dùng mặc định cho cả cặp.

Khoá nào firmware không dùng tới thì báo lại, cloud bỏ hoặc sửa phía mình.

---

## 11. Ví dụ một ngày

| Lúc | Máy | Gửi lên | Nhận về |
|---|---|---|---|
| 08:00:05 | kết nối, máy mới | `report`, `extra` có `cfg.state`, `cfg.alert`, `cfg.general` đều `0`, chưa có `configVersion`, `alerts: []` | `config.set` phần `core`, `state`, `alert`, `general` |
| 08:00:06 | áp dụng 4 phần, lưu flash | | |
| 08:10 | bơm chạy, TDS ra 36 → 38 ppm | state TDS (đổi 2 ppm, đạt ngưỡng) | |
| 08:10:20 | TDS ra 38 → 41 | giữ lại (chưa đủ 60 s) | |
| 08:11:00 | hết khoảng chờ | state TDS, cặp vào + ra | |
| 08:15 | TDS ra kém ≥ 60 s | `alerts` `aid A`, `active`, ER00052, `tier 1` | `alert.ack` → ngừng gửi `aid A active` |
| 08:15:05 | | `report` định kỳ 15 phút, `alerts` có ER00052 | |
| 09:00 | khách mở app | state 5 nhóm ngay, rồi TDS + nhiệt độ mỗi 60 s | |
| 09:20 | lõi 2 còn dưới 10% | `alerts` `aid B`, `active`, ER00062 | `alert.ack` |
| 09:25 | khách thay lõi 2, máy đặt lại bộ đếm | `alerts` `aid B`, `cleared`, ER00062 | `alert.ack` |
| 09:40 | mất wifi; TDS về bình thường | `alerts` `aid A`, `cleared` không gửi được | |
| 09:41 | kết nối lại | gửi lại `aid A cleared` theo thang; `report` sau 0–30 s, `alerts: []` | `alert.ack` |
| 23:59:59 | chốt ngày | state nước + điện tổng trong ngày, rồi đặt về 0 | |
