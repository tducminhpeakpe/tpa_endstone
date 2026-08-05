// tpa_config.h
//
// Cấu trúc cấu hình của plugin TPA + bộ nạp config.yml.
//
// Endstone (C++) chưa có API config sẵn như Bukkit nên plugin tự đọc config.yml
// bằng thư viện yaml-cpp (MIT, chỉ biên dịch vào file plugin, không ảnh hưởng
// tới runtime của máy chủ). Nếu config.yml chưa tồn tại, plugin tự tạo file
// mặc định kèm chú thích để quản trị viên dễ chỉnh sửa.
//
// Toàn bộ tin nhắn của plugin nằm trong config.yml dưới nhánh `messages`.
// Mỗi tin nhắn có thể chứa placeholder: {player}, {target}, {seconds}, {time},
// {usage}, {matches}, {error} và màu theo cú pháp &x (tự động chuyển sang §x —
// § được gửi dưới dạng UTF-8 2 byte 0xC2 0xA7, đúng chuẩn Endstone/client
// Bedrock nhận diện, nên màu hiện đúng trong game lẫn console).
// Tin nhắn để trống (rỗng) sẽ bị plugin bỏ qua — dùng để tắt bớt thông báo.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

// Một cặp placeholder → giá trị, dùng khi định dạng tin nhắn.
using TpaPh = std::pair<std::string_view, std::string>;

class TpaConfig {
public:
    // ---------------------------------------------------------------
    // Thời gian (giây)
    // ---------------------------------------------------------------
    int request_timeout = 60;  // yêu cầu tự hết hạn sau bao nhiêu giây
    int teleport_delay = 3;    // phải đứng yên bao nhiêu giây trước khi dịch chuyển

    // ---------------------------------------------------------------
    // Tùy chọn
    // ---------------------------------------------------------------
    bool countdown = true;  // hiện đếm ngược mỗi giây trong lúc chờ dịch chuyển

    // ---------------------------------------------------------------
    // Tin nhắn (key → template)
    // ---------------------------------------------------------------
    std::string prefix;                                  // tiền tố tự động thêm vào mọi tin nhắn
    std::unordered_map<std::string, std::string> messages;

    // Nạp cấu hình từ file. Nếu file chưa tồn tại → tạo file mặc định trước.
    // Trả về false và gán `error` nếu YAML bị hỏng (lúc đó plugin giữ nguyên
    // cấu hình cũ/mặc định, không crash server).
    bool loadFromFile(const std::filesystem::path &path, std::string &error);

    // Nạp cấu hình từ chuỗi YAML (dùng chung cho loadFromFile và test).
    bool loadFromString(const std::string &yaml, std::string &error);

    // Nội dung config.yml mặc định (nguồn duy nhất của file mặc định).
    static const std::string &defaultYaml();

    // Định dạng tin nhắn theo `key`:
    //   1. thay các placeholder {name} bằng giá trị tương ứng,
    //   2. thêm tiền tố `prefix` (nếu có),
    //   3. chuyển mã màu &x → §x.
    // Trả về chuỗi rỗng nếu tin nhắn bị cấu hình trống (plugin sẽ bỏ qua gửi)
    // hoặc chuỗi đánh dấu [tpa:key] nếu thiếu key (giúp admin phát hiện lỗi cấu hình).
    [[nodiscard]] std::string format(const std::string &key,
                                     std::initializer_list<TpaPh> placeholders = {}) const;
};
