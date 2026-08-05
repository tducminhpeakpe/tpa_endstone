// tpa.h
//
// Plugin TPA cho Endstone (C++20).
//
// Lệnh:   /tpa <player>  /tpahere <player>  /tpaccept  /tpdeny  /tpacancel  /tptoggle  /tpareload
//
// Toàn bộ tin nhắn và thời gian nằm trong config.yml (xem tpa_config.h).
// Mọi xử lý đều trên main thread: lệnh, sự kiện và task sync của Endstone chạy
// tuần tự, nên không cần khóa đồng bộ.

#pragma once

#include <endstone/endstone.hpp>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "tpa_config.h"
#include "tpa_manager.h"

class TpaPlugin : public endstone::Plugin {
public:
    // ----------------------------- vòng đời plugin -----------------------------
    void onEnable() override;
    void onDisable() override;

    // ----------------------------- lệnh -----------------------------
    bool onCommand(endstone::CommandSender &sender, const endstone::Command &command,
                   const std::vector<std::string> &args) override;

    // ----------------------------- sự kiện -----------------------------
    void onPlayerQuit(endstone::PlayerQuitEvent &event);
    void onPlayerMove(endstone::PlayerMoveEvent &event);

private:
    // ----------------------------- xử lý lệnh -----------------------------
    void handleTpa(endstone::Player &player, const std::vector<std::string> &args, bool here);
    void handleTpAccept(endstone::Player &player);
    void handleTpDeny(endstone::Player &player);
    void handleTpCancel(endstone::Player &player);
    void handleTpToggle(endstone::Player &player);
    void handleReload(endstone::CommandSender &sender);

    // ----------------------------- luồng xử lý yêu cầu -----------------------------
    void sendRequest(endstone::Player &requester, endstone::Player &target, bool here);
    void acceptRequest(const std::shared_ptr<TpaRequest> &request);
    void denyRequest(const std::shared_ptr<TpaRequest> &request);
    void cancelRequest(const std::shared_ptr<TpaRequest> &request);
    void expireRequest(const std::shared_ptr<TpaRequest> &request);
    void startCountdown(const std::shared_ptr<TpaRequest> &request);
    void finishTeleport(const std::shared_ptr<TpaRequest> &request);

    // ----------------------------- trợ giúp -----------------------------
    // Tìm người chơi theo tên (không phân biệt hoa/thường, ưu tiên khớp phần đầu).
    // Trả về nullptr nếu không tìm thấy; khi đó `matches` chứa danh sách các
    // ứng viên trùng khớp (rỗng = không ai, nhiều phần tử = nhập rõ hơn).
    endstone::Player *findPlayer(const std::string &query, std::vector<std::string> &matches) const;

    // Gửi tin nhắn cấu hình theo key; bỏ qua nếu tin nhắn bị cấu hình trống.
    void send(endstone::Player &player, const std::string &key,
              std::initializer_list<TpaPh> placeholders = {}) const;

    // Gửi cú pháp lệnh (lấy từ command usages khai báo trong ENDSTONE_PLUGIN).
    void sendUsage(endstone::CommandSender &sender, const endstone::Command &command) const;

    // Khóa định danh ổn định của player: XUID nếu có, ngược lại tên viết thường.
    static std::string playerKey(const endstone::Player &player);

    // ----------------------------- trạng thái -----------------------------
    TpaConfig config_;                                     // cấu hình (config.yml)
    TpaManager manager_;                                   // kho yêu cầu
    std::unordered_set<std::string> tpa_disabled_;         // player đã tắt nhận TPA (theo key)
};
