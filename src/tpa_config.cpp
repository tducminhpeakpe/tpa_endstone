// tpa_config.cpp
//
// Cài đặt TpaConfig: nạp/gỡ cấu hình từ config.yml và định dạng tin nhắn.

#include "tpa_config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace {

// Tin nhắn + prefix MẶC ĐỊNH, được phân tích từ defaultYaml() đúng một lần
// (lười — lazy static). Khi nạp config, plugin bắt đầu từ bộ mặc định này rồi
// mới overlay các key mà admin khai báo trong file — config thiếu key nào thì
// dùng tin nhắn mặc định của key đó, không bao giờ hiện chuỗi lỗi "missing".
struct DefaultMessages {
    std::string prefix;
    std::unordered_map<std::string, std::string> messages;
};

const DefaultMessages &defaultMessages()
{
    static const DefaultMessages defaults = [] {
        DefaultMessages d;
        const YAML::Node root = YAML::Load(TpaConfig::defaultYaml());
        if (root["messages"]) {
            const YAML::Node msgs = root["messages"];
            if (msgs["prefix"]) {
                d.prefix = msgs["prefix"].as<std::string>("");
            }
            for (const auto &entry : msgs) {
                const std::string key = entry.first.as<std::string>();
                if (key != "prefix" && entry.second.IsScalar()) {
                    d.messages[key] = entry.second.as<std::string>();
                }
            }
        }
        return d;
    }();
    return defaults;
}

// Chuyển mã màu dạng &x (vd &a, &c, &l, &r...) thành §x — màu chuẩn của Bedrock.
// QUAN TRỌNG: § phải là chuỗi UTF-8 2 byte (0xC2 0xA7), KHÔNG phải byte đơn 0xA7:
//   * client Bedrock nhận diện mã màu qua ký tự U+00A7 (UTF-8 = C2 A7);
//   * console Endstone (spdlog TextFormatter) cũng chỉ nhận diện dạng C2 A7 để
//     chuyển sang ANSI khi terminal hỗ trợ.
// Byte đơn 0xA7 là UTF-8 không hợp lệ → client/console bỏ qua → mất màu.
std::string translateColorCodes(std::string text)
{
    constexpr std::string_view color_chars = "0123456789abcdefklmnor";
    constexpr std::string_view section_utf8 = "\xC2\xA7";  // "§" trong UTF-8
    size_t pos = 0;
    while ((pos = text.find('&', pos)) != std::string::npos) {
        // Chỉ chuyển khi có ký tự màu/format hợp lệ theo sau (tránh nuốt ký tự & thường).
        if (pos + 1 < text.size() && color_chars.find(text[pos + 1]) != std::string_view::npos) {
            text.replace(pos, 1, section_utf8);
            pos += section_utf8.size();
        }
        else {
            ++pos;
        }
    }
    return text;
}

}  // namespace

// ---------------------------------------------------------------------------
// config.yml mặc định — được ghi ra thư mục dữ liệu của plugin ở lần chạy đầu.
// ĐÂY LÀ NGUỒN DUY NHẤT của file mặc định; sửa ở đây rồi biên dịch lại plugin.
// ---------------------------------------------------------------------------
const std::string &TpaConfig::defaultYaml()
{
    static const std::string yaml = R"(# ============================================================
#  Cấu hình plugin TPA (Endstone)
#  Sau khi sửa, gõ /tpareload để áp dụng mà không cần restart server.
# ============================================================

# Tùy chọn chung
options:
  # Hiện đếm ngược mỗi giây ("Dịch chuyển sau X giây...") khi đang chờ dịch chuyển
  countdown: true

# Thời gian (tính bằng giây)
time:
  # Yêu cầu TPA tự hết hạn sau bao nhiêu giây
  request-timeout: 60
  # Người được dịch chuyển phải đứng yên bao nhiêu giây trước khi dịch chuyển.
  # Đặt 0 để dịch chuyển ngay lập tức.
  teleport-delay: 3

# ----------------------------------------------------------------------------
# Tin nhắn.
#
# Placeholder được hỗ trợ:
#   {player}   tên người gửi yêu cầu   {target}  tên người nhận yêu cầu
#   {seconds}  số giây đứng yên        {time}    thời gian hết hạn (giây)
#   {usage}    cú pháp lệnh            {matches} danh sách người chơi trùng khớp
#   {error}    thông báo lỗi cấu hình
#
# Màu sắc dùng ký hiệu & (vd &a xanh, &c đỏ, &e vàng, &l đậm, &r reset).
# Để trống một tin nhắn (giá trị rỗng) nếu muốn tắt thông báo đó.
# ----------------------------------------------------------------------------
messages:
  # Tiền tố thêm vào trước mọi tin nhắn (để trống nếu không muốn)
  prefix: "&6[2s2c] "

  # Lỗi chung
  usage: "&4Lệnh Sai. Nhập /help để xem tất cả lệnh."
  only-player: "&4Lệnh này chỉ dành cho người chơi."
  player-not-found: "&6Không tìm thấy người chơi '{player}'."
  player-ambiguous: "&6Có nhiều người chơi khớp với '{player}', vui lòng nhập rõ hơn: {matches}"
  config-error: "&4Lỗi cấu hình: {error}"

  # Gửi yêu cầu
  cannot-request-self: "&6Bạn không thể gửi yêu cầu TPA cho chính mình."
  target-tpa-off: "&6{player} đã tắt nhận yêu cầu TPA."
  request-pending: "&6Bạn đã có một yêu cầu TPA đang chờ. Hãy dùng /tpacancel để hủy."
  request-duplicate: "&6Bạn đã gửi yêu cầu TPA đến {player} rồi, hãy chờ hết hạn hoặc hủy bằng /tpacancel."
  target-has-request: "&6{player} đang có một yêu cầu TPA khác đang chờ."
  request-sent: "&6Đã gửi yêu cầu TPA đến {player}. Yêu cầu hết hạn sau {time} giây."
  request-received: "&6{player} muốn dịch chuyển đến bạn. Gõ /tpaccept để đồng ý hoặc /tpdeny để từ chối (hết hạn sau {time} giây)."
  request-received-here: "&6{player} mời bạn dịch chuyển đến chỗ họ. Gõ /tpaccept để đồng ý hoặc /tpdeny để từ chối (hết hạn sau {time} giây)."

  # Chấp nhận / từ chối / hủy
  request-accepted-sender: "&6{player} đã chấp nhận yêu cầu TPA của bạn."
  request-accepted-target: "&6Bạn đã chấp nhận yêu cầu TPA của {player}."
  # Gửi cho NGƯỜI ĐƯỢC DỊCH CHUYỂN khi bắt đầu đếm ngược (/tpa: người gửi, /tpahere: người nhận)
  countdown-start: "&6Đứng yên! Sắp dịch chuyển trong {seconds} giây..."
  request-denied-sender: "&6{player} đã từ chối yêu cầu TPA của bạn."
  request-denied-target: "&6Bạn đã từ chối yêu cầu TPA của {player}."
  request-cancelled-sender: "&6Bạn đã hủy yêu cầu TPA."
  request-cancelled-target: "&6{player} đã hủy yêu cầu TPA."
  no-request: "&6Bạn không có yêu cầu TPA nào đang chờ."
  no-outgoing-request: "&6Bạn không có yêu cầu TPA nào để hủy."

  # Hết hạn / thoát máy chủ
  request-expired-sender: "&6Yêu cầu TPA đến {player} đã hết hạn."
  request-expired-target: "&6Yêu cầu TPA từ {player} đã hết hạn."
  player-quit: "&6{player} đã thoát máy chủ, yêu cầu TPA đã bị hủy."

  # Dịch chuyển
  countdown: "&6Đứng yên! Dịch chuyển sau {seconds} giây..."
  teleport-cancelled: "&6Đã hủy dịch chuyển vì bạn đã di chuyển."
  teleport-cancelled-target: "&6Đã hủy dịch chuyển vì {player} đã di chuyển."
  teleport-success: "&6Đã dịch chuyển đến {player}."
  teleport-success-target: "&6{player} đã dịch chuyển đến bạn."
  teleport-failed: "&6Không thể dịch chuyển lúc này, hãy thử lại."

  # Bật/tắt nhận TPA
  tpa-enabled: "&6Bạn đã bật nhận yêu cầu TPA."
  tpa-disabled: "&6Bạn đã tắt nhận yêu cầu TPA. Người khác sẽ không thể gửi yêu cầu đến bạn."

  # Reload
  config-reloaded: "&6Đã tải lại cấu hình."
)";
    return yaml;
}

// ---------------------------------------------------------------------------
// Nạp cấu hình từ chuỗi YAML. Chỉ đọc các key đã biết; key thiếu/không hợp lệ
// sẽ giữ giá trị mặc định (hoặc giá trị cũ) — plugin luôn khởi động được.
// ---------------------------------------------------------------------------
bool TpaConfig::loadFromString(const std::string &yaml, std::string &error)
{
    try {
        const YAML::Node root = YAML::Load(yaml);

        // time
        if (root["time"]) {
            const YAML::Node time = root["time"];
            if (time["request-timeout"]) {
                request_timeout = std::max(1, time["request-timeout"].as<int>(request_timeout));
            }
            if (time["teleport-delay"]) {
                teleport_delay = std::max(0, time["teleport-delay"].as<int>(teleport_delay));
            }
        }

        // options
        if (root["options"] && root["options"]["countdown"]) {
            countdown = root["options"]["countdown"].as<bool>(countdown);
        }

        // messages: bắt đầu từ bộ mặc định, overlay config của admin lên trên.
        // (config thiếu key nào → dùng tin nhắn mặc định của key đó;
        //  admin muốn TẮT một tin nhắn thì đặt giá trị rỗng "")
        const DefaultMessages &def = defaultMessages();
        prefix = def.prefix;
        messages = def.messages;
        if (root["messages"]) {
            const YAML::Node msgs = root["messages"];
            if (msgs["prefix"]) {
                prefix = msgs["prefix"].as<std::string>(prefix);
            }
            for (const auto &entry : msgs) {
                const std::string key = entry.first.as<std::string>();
                if (key == "prefix" || !entry.second.IsScalar()) {
                    continue;
                }
                messages[key] = entry.second.as<std::string>();
            }
        }

        return true;
    }
    catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

bool TpaConfig::loadFromFile(const std::filesystem::path &path, std::string &error)
{
    // Tạo file mặc định nếu chưa tồn tại (lần chạy đầu tiên).
    if (!std::filesystem::exists(path)) {
        std::ofstream out(path);
        if (!out) {
            error = "không thể tạo file " + path.string();
            return false;
        }
        out << defaultYaml();
    }

    std::ifstream in(path);
    if (!in) {
        error = "không thể mở file " + path.string();
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return loadFromString(content, error);
}

// ---------------------------------------------------------------------------
// Định dạng tin nhắn: placeholder → tiền tố → màu.
// ---------------------------------------------------------------------------
std::string TpaConfig::format(const std::string &key, std::initializer_list<TpaPh> placeholders) const
{
    const auto it = messages.find(key);
    std::string out = (it != messages.end()) ? it->second : ("[tpa:missing:" + key + "]");

    if (!out.empty()) {
        // Thay placeholder {name} → giá trị.
        for (const auto &[name, value] : placeholders) {
            const std::string needle = "{" + std::string(name) + "}";
            size_t pos = 0;
            while ((pos = out.find(needle, pos)) != std::string::npos) {
                out.replace(pos, needle.size(), value);
                pos += value.size();
            }
        }

        // Tiền tố.
        if (!prefix.empty()) {
            out = prefix + out;
        }

        // Màu &x → §x.
        out = translateColorCodes(std::move(out));
    }

    return out;
}
