# Plugin TPA cho Endstone (C++20)

Plugin **TPA** (Teleport Request) viết bằng **C++20** cho máy chủ
[Endstone](https://endstone.dev) — nền tảng server Minecraft Bedrock.

- Chỉ dùng **API chính thức của Endstone** (đã kiểm tra biên dịch với header
  Endstone `v0.11.6` thật).
- Toàn bộ tin nhắn & thời gian cấu hình qua **config.yml** (tự tạo ở lần chạy đầu).
- Tìm người chơi bằng **tên rút gọn**, không phân biệt hoa/thường.
- Tối ưu hiệu năng: không dùng vòng quét task thừa, sự kiện `PlayerMoveEvent`
  xử lý O(1), không rò rỉ bộ nhớ.

---

## Tính năng

| Lệnh | Mô tả |
| --- | --- |
| `/tpa <player>` | Gửi yêu cầu dịch chuyển đến vị trí người chơi khác |
| `/tpahere <player>` | Mời người chơi khác dịch chuyển đến vị trí của bạn || `/tpaccept` | Chấp nhận yêu cầu TPA đang chờ |
| `/tpdeny` | Từ chối yêu cầu TPA đang chờ |
| `/tpacancel` (alias `/tpcancel`) | Hủy yêu cầu TPA mình đã gửi |
| `/tptoggle` | Bật/tắt việc nhận yêu cầu TPA |
| `/tpareload` | Tải lại config.yml ngay lập tức (không cần restart) |

Quy tắc:

- Yêu cầu tự **hết hạn** sau thời gian cấu hình (mặc định **60 giây**) — cả hai
  phía đều được thông báo.
- Sau khi chấp nhận, người được dịch chuyển phải **đứng yên** (mặc định
  **3 giây**); có đếm ngược mỗi giây (bật/tắt được). Di chuyển → hủy dịch chuyển.
- Mỗi người gửi chỉ có **1 yêu cầu đang gửi**; mỗi người nhận chỉ xử lý
  **1 yêu cầu chờ**; không gửi trùng lặp; không gửi cho chính mình.
- Người gửi hoặc người nhận **thoát máy chủ** → yêu cầu tự hủy, phía còn lại
  được thông báo.
- `/tptoggle` tắt → người khác không thể gửi yêu cầu đến bạn (có thông báo
  cho người gửi).

> **Ghi chú cú pháp lệnh**: tham số của `/tpa` và `/tpahere` được khai báo là
> `[player: string]` (tham số **tùy chọn**, kiểu **text thô**) — đúng cú pháp
> usage mà Endstone parse thành tham số Bedrock. Nhờ vậy:
> - `/tpa` không có tên → **không** báo "Syntax error" của game, plugin hiện
    >   tin nhắn hướng dẫn;
> - `/tpa ste` → plugin nhận nguyên chuỗi `ste` và tự tìm theo tên rút gọn
    >   (không bị kiểu "player" của game chặn/validate);
> - Nếu khai báo kiểu `/tpa <player>` (thiếu `: type`), Endstone hiểu đó là
    >   **enum chỉ nhận đúng chữ "player"** → gõ tên thật sẽ báo lỗi syntax.

---

## Cấu trúc dự án

```
tpa-plugin/
├── CMakeLists.txt              # Build plugin (FetchContent: endstone + yaml-cpp)
├── README.md
├── include/
│   ├── tpa.h                   # Lớp plugin (lệnh, sự kiện, luồng xử lý)
│   ├── tpa_config.h            # Cấu hình config.yml + định dạng tin nhắn
│   └── tpa_manager.h           # Mô hình yêu cầu TPA + kho lưu trữ
├── src/
│   ├── tpa.cpp                 # Cài đặt chính (ENDSTONE_PLUGIN, lệnh, sự kiện)
│   └── tpa_config.cpp          # Nạp/ghi config.yml, tin nhắn mặc định
└── tests/
    ├── build.sh                # Build & chạy test (chạy trên stub API)
    ├── stubs/endstone/endstone.hpp  # Stub API Endstone — CHỈ cho test
    └── test_main.cpp           # 17 kịch bản test logic (70 check)
```

---

## Yêu cầu & Build

| Nền tảng | Trình biên dịch | Ghi chú |
| --- | --- | --- |
| Windows | **clang-cl** (Endstone yêu cầu), CMake ≥ 3.29 | Build **Release/RelWithDebInfo** (Endstone cấm MSVC Debug — ABI) |
| Linux | **Clang** (`-stdlib=libc++`), CMake ≥ 3.15 | |

Plugin build ra thư viện **`endstone_tpa.dll`** (Windows) hoặc
**`endstone_tpa.so`** (Linux) — đặt vào thư mục `plugins/` của máy chủ.

```bash
# Phiên bản Endstone API mặc định là 0.11 (khớp server 0.11.x)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# Kết quả: build/endstone_tpa.so  (hoặc .dll trên Windows)
```

> Muốn build cho phiên bản Endstone khác: `cmake -B build -DENDSTONE_API_VERSION=0.10 ..`

Plugin chỉ phụ thuộc 2 thư viện ngoài, cả hai đều **biên dịch tĩnh vào file
plugin** (không ảnh hưởng runtime máy chủ):

- **yaml-cpp** (MIT) — đọc `config.yml`. Cần thiết vì Endstone C++ chưa có API
  cấu hình như Bukkit.
- **Endstone SDK** — chính là API chính thức.

---

## Cấu hình (config.yml)

File được **tự tạo** tại `plugins/tpa/config.yml` ở lần chạy đầu tiên, kèm chú
thích đầy đủ. Sau khi sửa, gõ `/tpareload`.

```yaml
options:
  countdown: true          # đếm ngược mỗi giây khi chờ dịch chuyển

time:
  request-timeout: 60      # yêu cầu tự hết hạn (giây)
  teleport-delay: 3        # thời gian đứng yên trước khi dịch chuyển (giây; 0 = ngay)

messages:
  prefix: "&e[TPA]&r "     # tiền tố mọi tin nhắn (để trống nếu không muốn)
  request-sent: "&aĐã gửi yêu cầu TPA đến &e{player}&a..."
  ...
```

### Placeholder

| Placeholder | Ý nghĩa |
| --- | --- |
| `{player}` | Tên người gửi yêu cầu |
| `{target}` | Tên người nhận yêu cầu |
| `{seconds}` | Số giây đứng yên (đếm ngược) |
| `{time}` | Thời gian hết hạn (giây) |
| `{usage}` | Cú pháp lệnh (tin nhắn `usage`) |
| `{matches}` | Danh sách người chơi trùng khớp (tin nhắn `player-ambiguous`) |
| `{error}` | Thông báo lỗi cấu hình (tin nhắn `config-error`) |

Mẹo:

- Màu sắc dùng ký hiệu `&` (`&a` xanh, `&c` đỏ, `&e` vàng, `&l` đậm, `&r` reset…) — tự
  động chuyển sang mã `§` của Bedrock. `§` được gửi đúng dạng **UTF-8 (0xC2 0xA7)**
  nên màu hiện chính xác: trong game (client Bedrock) và trên console Endstone
  (chuyển sang ANSI khi terminal hỗ trợ). Có thể viết thẳng `§a` trong config nếu
  file được lưu dưới dạng UTF-8.
- **Tắt một thông báo**: đặt giá trị tin nhắn đó thành `""` (rỗng).
- **Config thiếu key nào** thì plugin dùng tin nhắn mặc định của key đó — chỉ cần
  ghi đè phần muốn thay đổi.

### Quyền (permission)

| Quyền | Mặc định | Lệnh |
| --- | --- | --- |
| `tpa.command.tpa` | True | `/tpa` |
| `tpa.command.tpahere` | True | `/tpahere` |
| `tpa.command.tpaccept` | True | `/tpaccept` |
| `tpa.command.tpdeny` | True | `/tpdeny` |
| `tpa.command.tpacancel` | True | `/tpacancel` |
| `tpa.command.tptoggle` | True | `/tptoggle` |
| `tpa.command.reload` | Operator | `/tpareload` |

Mặc định mọi người chơi đều dùng được; muốn hạn chế thì sửa `default_` trong
`src/tpa.cpp` (khối `ENDSTONE_PLUGIN`) thành `Operator`/`False` rồi build lại,
hoặc cấp/thu hồi qua hệ thống quyền của Endstone.

---

## Chạy test

Test logic chạy trên **stub của Endstone API** (mô phỏng đúng signature API 0.11
mà plugin dùng) nên không cần server hay compiler Clang — chỉ cần g++ ≥ 13
(hỗ trợ `<format>`) và Git:

```bash
bash tests/build.sh
# Kết quả: 70/70 check đạt — ALL TESTS PASSED
```

17 kịch bản bao gồm: luồng `/tpa` và `/tpahere` đầy đủ (gửi → chấp nhận → đếm
ngược → teleport), hủy khi di chuyển, xoay người không hủy, hết hạn, từ chối,
hủy, ràng buộc (tự gửi cho mình / trùng lặp / quá 1 yêu cầu), tắt/bật nhận TPA,
thoát máy chủ giữa chừng, tìm tên rút gọn (khớp phần đầu, trùng → yêu cầu nhập
rõ hơn, khớp chính xác thắng), teleport thất bại, config tùy biến (delay 0,
timeout 5s), reload config, tin nhắn rỗng = tắt thông báo, và PlayerMoveEvent
do chính teleport gây ra không tự hủy.

---

## Ghi chú thiết kế

- **Không có timer quét toàn cục**: mỗi yêu cầu có đúng 1 task hết hạn
  (`runTaskLater`) và tối đa 1 task đếm ngược (`runTaskTimer`) — chỉ tồn tại khi
  cần, bị hủy ngay khi kết thúc.
- **PlayerMoveEvent tần suất cao** được xử lý O(1): tra bảng `teleporting_`
  trước, chỉ so sánh tọa độ **block** khi cần (bỏ qua xoay người/đầu).
- **Không rò rỉ bộ nhớ**: task giữ `std::weak_ptr<TpaRequest>` thay vì
  `shared_ptr` — không tạo chu trình tham chiếu request ↔ task.
- **Đơn luồng**: lệnh, sự kiện và task sync của Endstone đều chạy tuần tự trên
  main thread → không cần mutex.
- **Teleport không tự hủy chính nó**: trạng thái "đang dịch chuyển" được gỡ
  TRƯỚC khi gọi `teleport()` để `PlayerMoveEvent` phát sinh bởi chính teleport
  không hủy lượt dịch chuyển.
- **Định danh bằng XUID** (fallback: tên thường) — yêu cầu không bị lẫn kể cả
  khi có tên trùng hoặc người chơi đổi tên.
- Trạng thái `/tptoggle` lưu trong RAM (reset khi restart server) — có thể mở
  rộng ghi ra file nếu cần.
