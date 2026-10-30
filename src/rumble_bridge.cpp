// rumble_bridge.cpp — controle Xbox 360 virtual via ViGEmClient.
#include "rumble_bridge.h"

#include <chrono>

namespace haptics {

double RumbleBridge::nowSeconds() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

RumbleBridge::~RumbleBridge() { stop(); }

bool RumbleBridge::start(std::wstring& err) {
    client_ = vigem_alloc();
    if (!client_) { err = L"vigem_alloc falhou (memória)."; return false; }

    VIGEM_ERROR r = vigem_connect(client_);
    if (!VIGEM_SUCCESS(r)) {
        err = (r == VIGEM_ERROR_BUS_NOT_FOUND)
            ? L"Driver ViGEmBus não encontrado. Instale o ViGEmBus e reinicie o PC."
            : L"Falha ao conectar ao ViGEmBus.";
        vigem_free(client_); client_ = nullptr;
        return false;
    }

    pad_ = vigem_target_x360_alloc();
    r = vigem_target_add(client_, pad_);
    if (!VIGEM_SUCCESS(r)) {
        err = L"Falha ao criar o controle Xbox 360 virtual.";
        stop();
        return false;
    }

    r = vigem_target_x360_register_notification(client_, pad_, &RumbleBridge::onNotification, this);
    if (!VIGEM_SUCCESS(r)) {
        err = L"Falha ao registrar o callback de vibração.";
        stop();
        return false;
    }
    return true;
}

void RumbleBridge::stop() {
    if (client_ && pad_) {
        vigem_target_x360_unregister_notification(pad_);
        vigem_target_remove(client_, pad_);
    }
    if (pad_) { vigem_target_free(pad_); pad_ = nullptr; }
    if (client_) { vigem_disconnect(client_); vigem_free(client_); client_ = nullptr; }
    motors_ = 0;
}

VOID CALLBACK RumbleBridge::onNotification(PVIGEM_CLIENT, PVIGEM_TARGET,
                                           UCHAR largeMotor, UCHAR smallMotor,
                                           UCHAR, LPVOID userData) {
    auto* self = static_cast<RumbleBridge*>(userData);
    self->motors_.store((static_cast<uint32_t>(largeMotor) << 8) | smallMotor,
                        std::memory_order_relaxed);
    self->events_.fetch_add(1, std::memory_order_relaxed);
    self->lastEventAt_.store(nowSeconds(), std::memory_order_relaxed);
}

RumbleState RumbleBridge::snapshot() const {
    RumbleState s;
    const uint32_t m = motors_.load(std::memory_order_relaxed);
    s.largeMotor = static_cast<uint8_t>((m >> 8) & 0xFF);
    s.smallMotor = static_cast<uint8_t>(m & 0xFF);
    s.events = events_.load(std::memory_order_relaxed);
    s.lastEventAt = lastEventAt_.load(std::memory_order_relaxed);
    return s;
}

int RumbleBridge::userIndex() const {
    if (!client_ || !pad_) return -1;
    ULONG idx = 0;
    if (!VIGEM_SUCCESS(vigem_target_x360_get_user_index(client_, pad_, &idx))) return -1;
    return static_cast<int>(idx);
}

} // namespace haptics
