// ffb_device.h — acesso ao Force Feedback do G29 via DirectInput 8.
//
// Usa só a API documentada do Windows (dinput8). Não depende do SDK da
// Logitech; precisa apenas do driver oficial (G HUB ou LGS) instalado.
#pragma once

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>

#include <string>
#include <vector>

namespace haptics {

struct FfbDeviceInfo {
    GUID instance{};
    std::wstring name;
    unsigned short vid = 0;
    unsigned short pid = 0;
    bool looksLikeG29 = false;
};

class FfbDevice {
public:
    FfbDevice() = default;
    ~FfbDevice();
    FfbDevice(const FfbDevice&) = delete;
    FfbDevice& operator=(const FfbDevice&) = delete;

    // Lista todos os controladores com Force Feedback conectados.
    static bool listDevices(std::vector<FfbDeviceInfo>& out, std::wstring& err);

    // Abre o dispositivo em modo EXCLUSIVO + BACKGROUND (exigido para FFB
    // continuar com o jogo em primeiro plano). index < 0 = escolhe o G29.
    bool open(HWND window, int index, std::wstring& err);

    // Cria os dois efeitos senoidais (grave e agudo), parados em zero.
    bool createChannels(double lowHz, double highHz, std::wstring& err);

    // Define a amplitude de cada canal (fração do nominal, 0..1).
    // Retorna false se o dispositivo foi perdido.
    bool setLevels(double low, double high);

    // Tenta recuperar o dispositivo após perda (desconexão, outro app).
    bool tryRecover();

    // Para tudo imediatamente (parada de emergência).
    void stopAll();
    void close();

    const FfbDeviceInfo& info() const { return info_; }
    const std::wstring& warning() const { return warning_; }
    int channelCount() const { return (low_ ? 1 : 0) + (high_ ? 1 : 0); }

private:
    bool createSine(double hz, IDirectInputEffect** out, std::wstring& err);
    bool setMagnitude(IDirectInputEffect* e, double hz, double level);

    IDirectInput8W* di_ = nullptr;
    IDirectInputDevice8W* dev_ = nullptr;
    IDirectInputEffect* low_ = nullptr;
    IDirectInputEffect* high_ = nullptr;
    double lowHz_ = 20.0, highHz_ = 40.0;
    double lastLow_ = -1.0, lastHigh_ = -1.0;
    FfbDeviceInfo info_;
    std::wstring warning_;
};

std::wstring hrToString(HRESULT hr);

} // namespace haptics
