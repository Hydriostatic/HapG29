// rumble_bridge.h — a "ponte": cria um controle Xbox 360 virtual (ViGEmBus)
// e recebe os comandos de vibração que o jogo envia para ele.
//
// O jogo vê um controle Xbox real conectado. Quando ele chama
// XInputSetState (ou equivalente), o driver entrega os valores dos dois
// motores (large = grave, small = agudo) no callback deste módulo.
// Nada é injetado no jogo: funciona inclusive com anticheat.
#pragma once

#include <windows.h>
#include <ViGEm/Client.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace haptics {

struct RumbleState {
    uint8_t large = 0;     // motor grave (0-255)
    uint8_t small = 0;     // motor agudo (0-255)
    uint64_t events = 0;   // quantas notificações chegaram desde o início
    double lastEventAt = -1.0; // segundos (relógio do programa); -1 = nunca
};

class RumbleBridge {
public:
    RumbleBridge() = default;
    ~RumbleBridge();
    RumbleBridge(const RumbleBridge&) = delete;
    RumbleBridge& operator=(const RumbleBridge&) = delete;

    bool start(std::wstring& err);
    void stop();

    RumbleState snapshot() const;
    // Slot XInput (0-3) que o Windows deu ao controle virtual; -1 se desconhecido.
    int userIndex() const;

    static double nowSeconds();

private:
    static VOID CALLBACK onNotification(PVIGEM_CLIENT client, PVIGEM_TARGET target,
                                        UCHAR largeMotor, UCHAR smallMotor,
                                        UCHAR ledNumber, LPVOID userData);

    PVIGEM_CLIENT client_ = nullptr;
    PVIGEM_TARGET pad_ = nullptr;
    // Empacotado num atômico para leitura sem trava no loop principal.
    std::atomic<uint32_t> motors_{0};
    std::atomic<uint64_t> events_{0};
    std::atomic<double> lastEventAt_{-1.0};
};

} // namespace haptics
