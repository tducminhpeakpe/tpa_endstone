// tpa.cpp
//
// Plugin TPA cho Endstone — cài đặt chính.
//
// Thiết kế:
//   * Mỗi yêu cầu chờ có MỘT task hết hạn (runTaskLater) — không dùng vòng quét
//     toàn cục mỗi giây, tránh task thừa (yêu cầu về hiệu năng).
//   * Sau khi chấp nhận, MỘT task đếm ngược (runTaskTimer, 20 ticks/giây) điều
//     khiển toàn bộ quá trình dịch chuyển; bị hủy ngay khi người chơi di chuyển
//     hoặc thoát máy chủ.
//   * PlayerMoveEvent là sự kiện tần suất cao nên được xử lý O(1): kiểm tra
//     nhanh xem người chơi có đang đếm ngược không, rồi mới so sánh tọa độ block.
//   * Các task giữ tham chiếu yếu (weak_ptr) tới yêu cầu — không tạo chu trình
//     tham chiếu, không rò rỉ bộ nhớ.
//   * Toàn bộ chạy trên main thread (Endstone gọi lệnh/sự kiện/task sync tuần
//     tự) nên không cần mutex.

#include "tpa.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace {

constexpr std::uint64_t kTicksPerSecond = 20;  // Endstone: 20 ticks = 1 giây

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Hiển thị usage gọn cho người chơi: "/tpa [player: string]" → "/tpa [player]"
// (chuỗi usage gốc phải giữ nguyên cú pháp "[tên: kiểu]" cho parser của Endstone).
std::string prettyUsage(std::string usage)
{
    size_t pos = 0;
    while ((pos = usage.find(": ", pos)) != std::string::npos) {
        const size_t close = usage.find_first_of("]>", pos);
        if (close == std::string::npos) {
            break;
        }
        usage.erase(pos, close - pos);
        pos = close;
    }
    return usage;
}

}  // namespace

// ---------------------------------------------------------------------------
// Khóa định danh ổn định cho player: XUID không thay đổi kể cả khi đổi tên,
// dùng làm khóa an toàn cho các bảng yêu cầu. Nếu XUID rỗng (một số client
// không gửi) thì fallback sang tên viết thường.
// ---------------------------------------------------------------------------
std::string TpaPlugin::playerKey(const endstone::Player &player)
{
    const std::string xuid = player.getXuid();
    if (!xuid.empty()) {
        return xuid;
    }
    return toLower(player.getName());
}

// ---------------------------------------------------------------------------
// onEnable: tạo config.yml nếu chưa có, đăng ký sự kiện.
// ---------------------------------------------------------------------------
void TpaPlugin::onEnable()
{
    // 1. Cấu hình: tạo thư mục dữ liệu + config.yml mặc định (lần chạy đầu).
    std::error_code ec;
    std::filesystem::create_directories(getDataFolder(), ec);
    const std::filesystem::path config_path = getDataFolder() / "config.yml";

    std::string error;
    if (!config_.loadFromFile(config_path, error)) {
        getLogger().error("Không thể đọc config.yml ({}). Dùng cấu hình mặc định.", error);
    }
    else {
        getLogger().info("Đã tải config.yml: request-timeout={}s, teleport-delay={}s, countdown={}",
                         config_.request_timeout, config_.teleport_delay, config_.countdown);
    }

    // 2. Sự kiện.
    //    PlayerQuitEvent: hủy mọi yêu cầu liên quan khi một bên thoát máy chủ.
    //    PlayerMoveEvent: hủy dịch chuyển khi người chơi di chuyển (đang đếm ngược).
    //    ignore_cancelled = true cho move: nếu sự kiện bị plugin khác hủy thì
    //    coi như không có di chuyển.
    registerEvent<endstone::PlayerQuitEvent>(
        [this](endstone::PlayerQuitEvent &event) { onPlayerQuit(event); });

    registerEvent<endstone::PlayerMoveEvent>(
        [this](endstone::PlayerMoveEvent &event) { onPlayerMove(event); },
        endstone::EventPriority::Normal, /*ignore_cancelled=*/true);

    getLogger().info("TPA v{} đã được bật.", getDescription().getVersion());
}

// ---------------------------------------------------------------------------
// onDisable: hủy mọi task + xóa toàn bộ trạng thái.
// ---------------------------------------------------------------------------
void TpaPlugin::onDisable()
{
    manager_.clear();          // hủy task hết hạn + đếm ngược, xóa mọi yêu cầu
    tpa_disabled_.clear();
    getServer().getScheduler().cancelTasks(*this);  // phòng hờ mọi task còn sót
    getLogger().info("TPA đã bị tắt.");
}

// ---------------------------------------------------------------------------
// Điều phối lệnh. Quyền sử dụng lệnh được khai báo trong ENDSTONE_PLUGIN và
// được Endstone kiểm tra tự động (PluginCommand::execute → testPermission).
// ---------------------------------------------------------------------------
bool TpaPlugin::onCommand(endstone::CommandSender &sender, const endstone::Command &command,
                          const std::vector<std::string> &args)
{
    const std::string &name = command.getName();

    // Lệnh admin có thể chạy từ console.
    if (name == "tpareload") {
        handleReload(sender);
        return true;
    }

    // Các lệnh còn lại chỉ dành cho người chơi.
    auto *player = dynamic_cast<endstone::Player *>(&sender);
    if (player == nullptr) {
        sender.sendMessage(config_.format("only-player"));
        return true;
    }

    // Kiểm tra tham số cho lệnh cần đối số.
    if (name == "tpa" || name == "tpahere") {
        if (args.size() != 1) {
            sendUsage(sender, command);
            return true;
        }
    }

    if (name == "tpa") {
        handleTpa(*player, args, /*here=*/false);
    }
    else if (name == "tpahere") {
        handleTpa(*player, args, /*here=*/true);
    }
    else if (name == "tpaccept") {
        handleTpAccept(*player);
    }
    else if (name == "tpdeny") {
        handleTpDeny(*player);
    }
    else if (name == "tpacancel") {
        handleTpCancel(*player);
    }
    else if (name == "tptoggle") {
        handleTpToggle(*player);
    }
    else {
        return false;  // lệnh không thuộc plugin
    }
    return true;
}

// ---------------------------------------------------------------------------
// /tpa <player> và /tpahere <player>
// ---------------------------------------------------------------------------
void TpaPlugin::handleTpa(endstone::Player &player, const std::vector<std::string> &args, bool here)
{
    // Tìm người chơi theo tên rút gọn.
    std::vector<std::string> matches;
    endstone::Player *target = findPlayer(args[0], matches);
    if (target == nullptr) {
        if (matches.empty()) {
            send(player, "player-not-found", {{"player", args[0]}});
        }
        else {
            // Nhiều người trùng khớp → yêu cầu nhập rõ hơn, không chọn bừa.
            std::string list;
            for (size_t i = 0; i < matches.size(); ++i) {
                if (i > 0) {
                    list += ", ";
                }
                list += matches[i];
            }
            send(player, "player-ambiguous", {{"player", args[0]}, {"matches", list}});
        }
        return;
    }

    sendRequest(player, *target, here);
}

// ---------------------------------------------------------------------------
// Tạo yêu cầu TPA với đầy đủ kiểm tra ràng buộc.
// ---------------------------------------------------------------------------
void TpaPlugin::sendRequest(endstone::Player &requester, endstone::Player &target, bool here)
{
    const std::string requester_key = playerKey(requester);
    const std::string target_key = playerKey(target);

    // 1. Không gửi cho chính mình.
    if (requester_key == target_key) {
        send(requester, "cannot-request-self");
        return;
    }

    // 2. Người nhận đã tắt nhận TPA.
    if (tpa_disabled_.contains(target_key)) {
        send(requester, "target-tpa-off", {{"player", target.getName()}});
        return;
    }

    // 3. Không gửi trùng lặp đến cùng một người (kiểm tra trước để báo đúng lý do).
    if (manager_.hasRequestFrom(target_key, requester_key)) {
        send(requester, "request-duplicate", {{"player", target.getName()}});
        return;
    }

    // 4. Mỗi người gửi chỉ có một yêu cầu đang gửi.
    if (manager_.findOutgoing(requester_key)) {
        send(requester, "request-pending");
        return;
    }

    // 5. Mỗi người nhận chỉ xử lý một yêu cầu đang chờ.
    if (manager_.findIncoming(target_key)) {
        send(requester, "target-has-request", {{"player", target.getName()}});
        return;
    }

    // Tạo yêu cầu.
    auto request = std::make_shared<TpaRequest>();
    request->requester_key = requester_key;
    request->requester_name = requester.getName();
    request->target_key = target_key;
    request->target_name = target.getName();
    request->here = here;
    request->requester_player = &requester;
    request->target_player = &target;
    manager_.add(request);

    // Thông báo cho cả hai phía.
    const std::string timeout_str = std::to_string(config_.request_timeout);
    send(requester, "request-sent", {{"player", target.getName()}, {"time", timeout_str}});
    send(target, here ? "request-received-here" : "request-received",
         {{"player", requester.getName()}, {"time", timeout_str}});

    // Task hết hạn: đúng một task cho mỗi yêu cầu, chạy đúng lúc hết hạn.
    // Giữ weak_ptr để tránh chu trình tham chiếu request ↔ task.
    request->expiry_task = getServer().getScheduler().runTaskLater(
        *this,
        [weak = std::weak_ptr<TpaRequest>(request), this]() {
            if (auto req = weak.lock(); req && isEnabled()) {
                expireRequest(req);
            }
        },
        static_cast<std::uint64_t>(config_.request_timeout) * kTicksPerSecond);
}

// ---------------------------------------------------------------------------
// /tpaccept — chấp nhận yêu cầu đang chờ, bắt đầu đếm ngược dịch chuyển.
// ---------------------------------------------------------------------------
void TpaPlugin::handleTpAccept(endstone::Player &player)
{
    const auto request = manager_.findIncoming(playerKey(player));
    if (!request) {
        send(player, "no-request");
        return;
    }
    acceptRequest(request);
}

void TpaPlugin::acceptRequest(const std::shared_ptr<TpaRequest> &request)
{
    // Thông báo cả hai phía.
    if (request->requester_player) {
        send(*request->requester_player, "request-accepted-sender", {{"player", request->target_name}});
    }
    if (request->target_player) {
        send(*request->target_player, "request-accepted-target", {{"player", request->requester_name}});
    }

    // Hết giai đoạn "chờ xử lý": gỡ khỏi bảng chờ + hủy task hết hạn.
    manager_.resolve(request);

    // Bắt đầu giai đoạn "đứng yên rồi dịch chuyển".
    startCountdown(request);
}

// ---------------------------------------------------------------------------
// /tpdeny — từ chối yêu cầu đang chờ.
// ---------------------------------------------------------------------------
void TpaPlugin::handleTpDeny(endstone::Player &player)
{
    const auto request = manager_.findIncoming(playerKey(player));
    if (!request) {
        send(player, "no-request");
        return;
    }
    denyRequest(request);
}

void TpaPlugin::denyRequest(const std::shared_ptr<TpaRequest> &request)
{
    if (request->requester_player) {
        send(*request->requester_player, "request-denied-sender", {{"player", request->target_name}});
    }
    if (request->target_player) {
        send(*request->target_player, "request-denied-target", {{"player", request->requester_name}});
    }
    manager_.resolve(request);
}

// ---------------------------------------------------------------------------
// /tpacancel — hủy yêu cầu mình đã gửi.
// ---------------------------------------------------------------------------
void TpaPlugin::handleTpCancel(endstone::Player &player)
{
    const auto request = manager_.findOutgoing(playerKey(player));
    if (!request) {
        send(player, "no-outgoing-request");
        return;
    }
    cancelRequest(request);
}

void TpaPlugin::cancelRequest(const std::shared_ptr<TpaRequest> &request)
{
    if (request->requester_player) {
        send(*request->requester_player, "request-cancelled-sender");
    }
    if (request->target_player) {
        send(*request->target_player, "request-cancelled-target", {{"player", request->requester_name}});
    }
    manager_.resolve(request);
}

// ---------------------------------------------------------------------------
// Yêu cầu hết hạn (task hết hạn kích hoạt).
// ---------------------------------------------------------------------------
void TpaPlugin::expireRequest(const std::shared_ptr<TpaRequest> &request)
{
    if (request->requester_player) {
        send(*request->requester_player, "request-expired-sender", {{"player", request->target_name}});
    }
    if (request->target_player) {
        send(*request->target_player, "request-expired-target", {{"player", request->requester_name}});
    }
    manager_.resolve(request);
}

// ---------------------------------------------------------------------------
// Đếm ngược trước khi dịch chuyển (teleport-delay giây, mặc định 3s).
// Nếu người chơi di chuyển, PlayerMoveEvent sẽ hủy task này.
// ---------------------------------------------------------------------------
void TpaPlugin::startCountdown(const std::shared_ptr<TpaRequest> &request)
{
    endstone::Player *mover = request->moverPlayer();
    if (mover == nullptr) {
        manager_.resolve(request);  // phòng hờ (không thể xảy ra do bất biến)
        return;
    }

    // Đánh dấu đang dịch chuyển → PlayerMoveEvent sẽ theo dõi người này.
    manager_.beginTeleport(request);

    const int delay = config_.teleport_delay;
    if (delay <= 0) {
        finishTeleport(request);  // cấu hình cho phép dịch chuyển ngay
        return;
    }

    // Báo NGƯỜI ĐƯỢC DỊCH CHUYỂN phải đứng yên (với /tpa là người gửi,
    // với /tpahere là người nhận — xem TpaRequest::moverPlayer()).
    send(*mover, "countdown-start", {{"seconds", std::to_string(delay)}});

    if (config_.countdown) {
        // Một task lặp mỗi giây: đếm ngược delay, 0 → dịch chuyển.
        // `remaining` bắt đầu = delay: hiện delay-1, delay-2... rồi dịch chuyển
        // đúng sau `delay` giây.
        int remaining = delay;
        request->countdown_task = getServer().getScheduler().runTaskTimer(
            *this,
            [weak = std::weak_ptr<TpaRequest>(request), this, remaining]() mutable {
                auto req = weak.lock();
                if (!req || !isEnabled()) {
                    return;
                }
                if (--remaining <= 0) {
                    finishTeleport(req);
                }
                else if (req->moverPlayer()) {
                    send(*req->moverPlayer(), "countdown", {{"seconds", std::to_string(remaining)}});
                }
            },
            kTicksPerSecond, kTicksPerSecond);
    }
    else {
        // Không đếm ngược: vẫn chờ đủ thời gian đứng yên, chỉ không hiện tin nhắn.
        request->countdown_task = getServer().getScheduler().runTaskLater(
            *this,
            [weak = std::weak_ptr<TpaRequest>(request), this]() {
                if (auto req = weak.lock(); req && isEnabled()) {
                    finishTeleport(req);
                }
            },
            static_cast<std::uint64_t>(delay) * kTicksPerSecond);
    }
}

// ---------------------------------------------------------------------------
// Dịch chuyển người chơi đến đích.
// ---------------------------------------------------------------------------
void TpaPlugin::finishTeleport(const std::shared_ptr<TpaRequest> &request)
{
    // QUAN TRỌNG: gỡ trạng thái đang dịch chuyển TRƯỚC khi teleport — bản thân
    // teleport cũng phát sinh PlayerMoveEvent; nếu không gỡ trước, sự kiện đó
    // sẽ bị coi là "di chuyển" và hủy chính lượt dịch chuyển này.
    manager_.resolve(request);

    endstone::Player *mover = request->moverPlayer();
    endstone::Player *destination = request->destinationPlayer();
    if (mover == nullptr || destination == nullptr) {
        return;  // phòng hờ (không thể xảy ra do bất biến)
    }

    if (mover->teleport(destination->getLocation())) {
        send(*mover, "teleport-success", {{"player", request->destinationName()}});
        send(*destination, "teleport-success-target", {{"player", request->moverName()}});
    }
    else {
        send(*mover, "teleport-failed");
        send(*destination, "teleport-failed");
    }
}

// ---------------------------------------------------------------------------
// PlayerMoveEvent: hủy dịch chuyển nếu người đang đếm ngược di chuyển.
// Sự kiện này chạy rất thường xuyên nên giữ tối ưu:
//   1) tra bảng O(1) — không có yêu cầu thì thoát ngay;
//   2) chỉ so sánh tọa độ BLOCK (bỏ qua xoay người/đầu gây nhiễu).
// ---------------------------------------------------------------------------
void TpaPlugin::onPlayerMove(endstone::PlayerMoveEvent &event)
{
    const auto request = manager_.findTeleporting(playerKey(event.getPlayer()));
    if (!request) {
        return;  // không ai đang dịch chuyển ở đây — thoát nhanh
    }

    const endstone::Location &from = event.getFrom();
    const endstone::Location &to = event.getTo();
    if (from.getBlockX() == to.getBlockX() && from.getBlockY() == to.getBlockY() &&
        from.getBlockZ() == to.getBlockZ()) {
        return;  // chỉ đổi hướng nhìn, chưa rời ô hiện tại
    }

    // Đã di chuyển sang ô khác → hủy dịch chuyển.
    manager_.resolve(request);  // hủy task đếm ngược + gỡ khỏi bảng

    if (endstone::Player *mover = request->moverPlayer()) {
        send(*mover, "teleport-cancelled");
    }
    if (endstone::Player *destination = request->destinationPlayer();
        destination != nullptr && destination != request->moverPlayer()) {
        send(*destination, "teleport-cancelled-target", {{"player", request->moverName()}});
    }
}

// ---------------------------------------------------------------------------
// PlayerQuitEvent: mọi yêu cầu liên quan đến người thoát đều bị hủy,
// bên còn lại được thông báo.
// ---------------------------------------------------------------------------
void TpaPlugin::onPlayerQuit(endstone::PlayerQuitEvent &event)
{
    endstone::Player &leaver = event.getPlayer();
    const std::string key = playerKey(leaver);

    const auto affected = manager_.removeAllInvolving(key);
    tpa_disabled_.erase(key);  // trạng thái tắt-TPA không cần giữ cho người đã rời

    for (const auto &request : affected) {
        // Bên còn lại (vẫn online theo bất biến của request).
        endstone::Player *other = (request->requester_key == key) ? request->target_player
                                                                  : request->requester_player;
        if (other != nullptr && other != &leaver) {
            send(*other, "player-quit", {{"player", leaver.getName()}});
        }
    }
}

// ---------------------------------------------------------------------------
// /tptoggle — bật/tắt nhận yêu cầu TPA.
// ---------------------------------------------------------------------------
void TpaPlugin::handleTpToggle(endstone::Player &player)
{
    const std::string key = playerKey(player);
    if (tpa_disabled_.erase(key) > 0) {
        send(player, "tpa-enabled");
    }
    else {
        tpa_disabled_.insert(key);
        send(player, "tpa-disabled");
    }
}

// ---------------------------------------------------------------------------
// /tpareload — nạp lại config.yml ngay lập tức (không cần restart).
// ---------------------------------------------------------------------------
void TpaPlugin::handleReload(endstone::CommandSender &sender)
{
    std::string error;
    if (config_.loadFromFile(getDataFolder() / "config.yml", error)) {
        sender.sendMessage(config_.format("config-reloaded"));
        getLogger().info("Đã tải lại cấu hình: request-timeout={}s, teleport-delay={}s, countdown={}",
                         config_.request_timeout, config_.teleport_delay, config_.countdown);
    }
    else {
        sender.sendMessage(config_.format("config-error", {{"error", error}}));
        getLogger().error("Tải lại cấu hình thất bại: {}", error);
    }
}

// ---------------------------------------------------------------------------
// Tìm người chơi theo tên rút gọn:
//   1) khớp CHÍNH XÁC (không phân biệt hoa/thường) — luôn thắng;
//   2) khớp PHẦN ĐẦU của tên (ưu tiên) — 1 kết quả thì dùng, nhiều kết quả thì báo nhập rõ hơn;
//   3) fallback: khớp CHỨA trong tên — cùng quy tắc;
//   4) không ai → báo không tìm thấy.
// Không bao giờ chọn ngẫu nhiên khi có nhiều người trùng khớp.
// ---------------------------------------------------------------------------
endstone::Player *TpaPlugin::findPlayer(const std::string &query, std::vector<std::string> &matches) const
{
    matches.clear();
    const std::string q = toLower(query);
    if (q.empty()) {
        return nullptr;
    }

    const std::vector<endstone::Player *> online = getServer().getOnlinePlayers();

    // 1) Khớp chính xác.
    for (endstone::Player *player : online) {
        if (toLower(player->getName()) == q) {
            return player;
        }
    }

    // 2) Khớp phần đầu tên.
    std::vector<endstone::Player *> prefix;
    for (endstone::Player *player : online) {
        const std::string name = toLower(player->getName());
        if (name.size() >= q.size() && name.compare(0, q.size(), q) == 0) {
            prefix.push_back(player);
        }
    }
    if (prefix.size() == 1) {
        return prefix.front();
    }
    if (prefix.size() > 1) {
        for (endstone::Player *player : prefix) {
            matches.push_back(player->getName());
        }
        return nullptr;
    }

    // 3) Fallback: khớp chứa trong tên.
    std::vector<endstone::Player *> contains;
    for (endstone::Player *player : online) {
        const std::string name = toLower(player->getName());
        if (name.find(q) != std::string::npos) {
            contains.push_back(player);
        }
    }
    if (contains.size() == 1) {
        return contains.front();
    }
    if (contains.size() > 1) {
        for (endstone::Player *player : contains) {
            matches.push_back(player->getName());
        }
        return nullptr;
    }

    // 4) Không tìm thấy.
    return nullptr;
}

// ---------------------------------------------------------------------------
// Gửi tin nhắn cấu hình; chuỗi rỗng (cấu hình trống) → bỏ qua.
// ---------------------------------------------------------------------------
void TpaPlugin::send(endstone::Player &player, const std::string &key,
                     std::initializer_list<TpaPh> placeholders) const
{
    const std::string message = config_.format(key, placeholders);
    if (!message.empty()) {
        player.sendMessage(message);
    }
}

void TpaPlugin::sendUsage(endstone::CommandSender &sender, const endstone::Command &command) const
{
    const std::vector<std::string> &usages = command.getUsages();
    const std::string usage = usages.empty() ? "/" + command.getName() : usages.front();
    sender.sendMessage(config_.format("usage", {{"usage", prettyUsage(usage)}}));
}

// ---------------------------------------------------------------------------
// Khai báo plugin: metadata, lệnh và quyền.
// Toàn bộ lệnh mặc định cho phép mọi người chơi (PermissionDefault::True);
// quản trị viên có thể thu hẹp bằng cách đổi default_ thành Operator/False.
// ---------------------------------------------------------------------------
ENDSTONE_PLUGIN("tpa", "1.0.0", TpaPlugin)
{
    description = "TPA plugin for Endstone servers — /tpa, /tpahere, /tpaccept, /tpdeny, /tpacancel, /tptoggle";
    prefix = "TPA";

    // ---------------------------------------------------------------
    // LƯU Ý VỀ CÚ PHÁP USAGE (Endstone parse usage thành tham số Bedrock):
    //   * "/tpa <player>" (thiếu ": type") → bị hiểu là ENUM chỉ nhận đúng
    //     chữ "player" → gõ tên thật hoặc không gõ sẽ báo "Syntax error".
    //   * "string" = kiểu text thô (HardNonTerminal::Id), không validate —
    //     plugin tự tìm người chơi theo tên rút gọn.
    //   * "[...]" = tham số TÙY CHỌN → /tpa không có đối số sẽ chạy vào plugin
    //     (args rỗng) để hiện tin nhắn hướng dẫn, không bị lỗi syntax.
    // ---------------------------------------------------------------
    command("tpa")
        .description("Gửi yêu cầu dịch chuyển đến người chơi khác")
        .usages("/tpa [player: string]")
        .permissions("tpa.command.tpa");

    command("tpahere")
        .description("Mời người chơi khác dịch chuyển đến vị trí của bạn")
        .usages("/tpahere [player: string]")
        .permissions("tpa.command.tpahere");

    command("tpaccept")
        .description("Chấp nhận yêu cầu TPA đang chờ")
        .usages("/tpaccept")
        .permissions("tpa.command.tpaccept");

    command("tpdeny")
        .description("Từ chối yêu cầu TPA đang chờ")
        .usages("/tpdeny")
        .permissions("tpa.command.tpdeny");

    command("tpacancel")
        .description("Hủy yêu cầu TPA bạn đã gửi")
        .usages("/tpacancel")
        .aliases("tpcancel")
        .permissions("tpa.command.tpacancel");

    command("tptoggle")
        .description("Bật hoặc tắt việc nhận yêu cầu TPA")
        .usages("/tptoggle")
        .permissions("tpa.command.tptoggle");

    command("tpareload")
        .description("Tải lại cấu hình TPA từ config.yml")
        .usages("/tpareload")
        .permissions("tpa.command.reload");

    // Quyền — mặc định ai cũng dùng được.
    permission("tpa.command.tpa").description("Cho phép dùng lệnh /tpa").default_(endstone::PermissionDefault::True);
    permission("tpa.command.tpahere").description("Cho phép dùng lệnh /tpahere").default_(endstone::PermissionDefault::True);
    permission("tpa.command.tpaccept").description("Cho phép dùng lệnh /tpaccept").default_(endstone::PermissionDefault::True);
    permission("tpa.command.tpdeny").description("Cho phép dùng lệnh /tpdeny").default_(endstone::PermissionDefault::True);
    permission("tpa.command.tpacancel").description("Cho phép dùng lệnh /tpacancel").default_(endstone::PermissionDefault::True);
    permission("tpa.command.tptoggle").description("Cho phép dùng lệnh /tptoggle").default_(endstone::PermissionDefault::True);

    // Chỉ quản trị viên (op) mới reload được cấu hình.
    permission("tpa.command.reload").description("Cho phép dùng lệnh /tpareload").default_(endstone::PermissionDefault::Operator);
}
