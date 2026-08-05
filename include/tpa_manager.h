// tpa_manager.h
//
// Mô hình dữ liệu của plugin TPA:
//
//   - TpaRequest : một yêu cầu TPA đang tồn tại (đang chờ xử lý hoặc đang
//                  đếm ngược để dịch chuyển).
//   - TpaManager : kho lưu trữ tất cả yêu cầu, đảm bảo các ràng buộc:
//                      * mỗi người gửi chỉ có 1 yêu cầu đang gửi,
//                      * mỗi người nhận chỉ có 1 yêu cầu đang chờ,
//                      * mỗi người chỉ có tối đa 1 lượt dịch chuyển đang đếm ngược.
//
// Toàn bộ thao tác chạy trên main thread (lệnh, sự kiện, task sync của
// Endstone đều tuần tự trên luồng chính) nên KHÔNG cần khóa (mutex).
//
// Vòng đời của một yêu cầu:
//   sendRequest()  → thêm vào outgoing_ + incoming_
//   chấp nhận      → resolve() (gỡ khỏi bảng chờ) + thêm vào teleporting_
//   dịch chuyển xong / bị hủy do di chuyển / hết hạn / từ chối / hủy / thoát
//                  → resolve() (gỡ khỏi MỌI bảng + hủy task liên quan)

#pragma once

#include <endstone/endstone.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Một yêu cầu TPA. `requester` gửi yêu cầu đến `target`:
//   - here == false (lệnh /tpa):   requester dịch chuyển đến chỗ target;
//   - here == true  (lệnh /tpahere): target dịch chuyển đến chỗ requester.
//
// Bất biến quan trọng: khi request còn nằm trong TpaManager thì CẢ HAI người
// chơi đều đang online (mọi trường hợp thoát máy đều gỡ request ngay trong
// PlayerQuitEvent), do đó con trỏ Player* trong request luôn hợp lệ.
struct TpaRequest {
    std::string requester_key;   // khóa định danh người gửi (XUID, fallback: tên thường)
    std::string requester_name;  // tên hiển thị người gửi
    std::string target_key;      // khóa định danh người nhận
    std::string target_name;     // tên hiển thị người nhận
    bool here = false;           // true = tpahere (người nhận dịch chuyển đến người gửi)

    endstone::Player *requester_player = nullptr;  // con trỏ hợp lệ theo bất biến trên
    endstone::Player *target_player = nullptr;

    std::shared_ptr<endstone::Task> expiry_task;     // task tự hết hạn (timeout)
    std::shared_ptr<endstone::Task> countdown_task;  // task đếm ngược trước khi dịch chuyển

    // Người được dịch chuyển (phải đứng yên) và người giữ vị trí đích.
    [[nodiscard]] const std::string &moverKey() const { return here ? target_key : requester_key; }
    [[nodiscard]] const std::string &moverName() const { return here ? target_name : requester_name; }
    [[nodiscard]] const std::string &destinationKey() const { return here ? requester_key : target_key; }
    [[nodiscard]] const std::string &destinationName() const { return here ? requester_name : target_name; }
    [[nodiscard]] endstone::Player *moverPlayer() const { return here ? target_player : requester_player; }
    [[nodiscard]] endstone::Player *destinationPlayer() const { return here ? requester_player : target_player; }

    // Yêu cầu này có liên quan đến player `key` không (gửi hoặc nhận).
    [[nodiscard]] bool involves(std::string_view key) const
    {
        return requester_key == key || target_key == key;
    }

    // Trả về "bên còn lại" khi một bên đã rời đi.
    [[nodiscard]] std::string_view otherKey(std::string_view key) const
    {
        return requester_key == key ? std::string_view(target_key) : std::string_view(requester_key);
    }
};

class TpaManager {
public:
    // ---- tra cứu ----
    // Yêu cầu đang gửi bởi `requester_key` (dùng cho /tpacancel).
    [[nodiscard]] std::shared_ptr<TpaRequest> findOutgoing(std::string_view requester_key) const
    {
        const auto it = outgoing_.find(std::string(requester_key));
        return it != outgoing_.end() ? it->second : nullptr;
    }

    // Yêu cầu đang chờ xử lý bởi `target_key` (dùng cho /tpaccept, /tpdeny).
    [[nodiscard]] std::shared_ptr<TpaRequest> findIncoming(std::string_view target_key) const
    {
        const auto it = incoming_.find(std::string(target_key));
        return it != incoming_.end() ? it->second : nullptr;
    }

    // Đã có yêu cầu từ `requester_key` đến `target_key` chưa (chống trùng lặp).
    [[nodiscard]] bool hasRequestFrom(std::string_view target_key, std::string_view requester_key) const
    {
        const auto it = incoming_.find(std::string(target_key));
        return it != incoming_.end() && it->second->requester_key == requester_key;
    }

    // Yêu cầu đang đếm ngược của `mover_key` (dùng cho PlayerMoveEvent, O(1)).
    [[nodiscard]] std::shared_ptr<TpaRequest> findTeleporting(std::string_view mover_key) const
    {
        const auto it = teleporting_.find(std::string(mover_key));
        return it != teleporting_.end() ? it->second : nullptr;
    }

    // ---- thay đổi ----
    // Thêm yêu cầu mới vào bảng chờ (outgoing + incoming).
    void add(const std::shared_ptr<TpaRequest> &request)
    {
        outgoing_[request->requester_key] = request;
        incoming_[request->target_key] = request;
    }

    // Chuyển yêu cầu sang giai đoạn đếm ngược dịch chuyển.
    void beginTeleport(const std::shared_ptr<TpaRequest> &request)
    {
        teleporting_[request->moverKey()] = request;
    }

    // Gỡ yêu cầu khỏi MỌI bảng và hủy mọi task liên quan.
    // Đây là điểm dọn dẹp duy nhất — mọi đường kết thúc của request đều đi qua đây.
    void resolve(const std::shared_ptr<TpaRequest> &request)
    {
        if (const auto it = outgoing_.find(request->requester_key);
            it != outgoing_.end() && it->second == request) {
            outgoing_.erase(it);
        }
        if (const auto it = incoming_.find(request->target_key);
            it != incoming_.end() && it->second == request) {
            incoming_.erase(it);
        }
        if (const auto it = teleporting_.find(request->moverKey());
            it != teleporting_.end() && it->second == request) {
            teleporting_.erase(it);
        }

        cancelTasks(request);
    }

    // Gỡ mọi yêu cầu liên quan đến player `key` (khi player thoát máy chủ).
    // Trả về danh sách các yêu cầu bị ảnh hưởng để plugin thông báo cho bên còn lại.
    std::vector<std::shared_ptr<TpaRequest>> removeAllInvolving(std::string_view key)
    {
        std::vector<std::shared_ptr<TpaRequest>> affected;
        for (const auto &[k, req] : outgoing_) {
            if (req->involves(key)) affected.push_back(req);
        }
        for (const auto &[k, req] : incoming_) {
            if (req->involves(key)) affected.push_back(req);
        }
        for (const auto &[k, req] : teleporting_) {
            if (req->involves(key)) affected.push_back(req);
        }
        for (const auto &req : affected) {
            resolve(req);  // resolve nhiều lần an toàn (đã kiểm tra identity khi xóa)
        }
        return affected;
    }

    // Dọn toàn bộ trạng thái (khi plugin tắt).
    void clear()
    {
        for (auto &[k, req] : outgoing_) cancelTasks(req);
        for (auto &[k, req] : incoming_) cancelTasks(req);
        for (auto &[k, req] : teleporting_) cancelTasks(req);
        outgoing_.clear();
        incoming_.clear();
        teleporting_.clear();
    }

private:
    // Hủy task hết hạn + task đếm ngược của request (nếu có).
    static void cancelTasks(const std::shared_ptr<TpaRequest> &request)
    {
        if (request->expiry_task) {
            request->expiry_task->cancel();
            request->expiry_task.reset();
        }
        if (request->countdown_task) {
            request->countdown_task->cancel();
            request->countdown_task.reset();
        }
    }

    // requester_key → request (tối đa 1 / người gửi)
    std::unordered_map<std::string, std::shared_ptr<TpaRequest>> outgoing_;
    // target_key → request (tối đa 1 / người nhận)
    std::unordered_map<std::string, std::shared_ptr<TpaRequest>> incoming_;
    // mover_key → request đang đếm ngược (tối đa 1 / người đang dịch chuyển)
    std::unordered_map<std::string, std::shared_ptr<TpaRequest>> teleporting_;
};
