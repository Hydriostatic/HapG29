// ffb_device.cpp — implementação do acesso DirectInput ao G29.
#include "ffb_device.h"

#include <cmath>
#include <cstdio>
#include <cwchar>

namespace haptics {

namespace {

constexpr unsigned short kLogitechVid = 0x046D;
constexpr unsigned short kG29Pid = 0xC24F;

template <class T> void safeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

struct EnumCtx {
    std::vector<FfbDeviceInfo>* out;
};

BOOL CALLBACK enumCallback(LPCDIDEVICEINSTANCEW inst, LPVOID user) {
    auto* ctx = static_cast<EnumCtx*>(user);
    FfbDeviceInfo d;
    d.instance = inst->guidInstance;
    d.name = inst->tszProductName;
    // Para dispositivos HID, guidProduct.Data1 = MAKELONG(VID, PID).
    d.vid = LOWORD(inst->guidProduct.Data1);
    d.pid = HIWORD(inst->guidProduct.Data1);
    d.looksLikeG29 = (d.vid == kLogitechVid && d.pid == kG29Pid) ||
                     d.name.find(L"G29") != std::wstring::npos;
    ctx->out->push_back(d);
    return DIENUM_CONTINUE;
}

bool createDirectInput(IDirectInput8W** di, std::wstring& err) {
    HRESULT hr = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                                    IID_IDirectInput8W,
                                    reinterpret_cast<void**>(di), nullptr);
    if (FAILED(hr)) { err = L"DirectInput8Create falhou: " + hrToString(hr); return false; }
    return true;
}

} // namespace

std::wstring hrToString(HRESULT hr) {
    // Alguns DIERR_* são definidos como valores sem sinal, então compara-se
    // via cast em vez de switch.
    struct Known { HRESULT code; const wchar_t* text; };
    static const Known known[] = {
        { static_cast<HRESULT>(DIERR_INPUTLOST),     L"DIERR_INPUTLOST (dispositivo perdido)" },
        { static_cast<HRESULT>(DIERR_NOTACQUIRED),   L"DIERR_NOTACQUIRED (sem controle do dispositivo)" },
        { static_cast<HRESULT>(DIERR_OTHERAPPHASPRIO), L"DIERR_OTHERAPPHASPRIO (outro programa tem acesso exclusivo)" },
        { static_cast<HRESULT>(DIERR_UNSUPPORTED),   L"DIERR_UNSUPPORTED (efeito não suportado)" },
        { static_cast<HRESULT>(DIERR_DEVICEFULL),    L"DIERR_DEVICEFULL (memória de efeitos cheia)" },
        { static_cast<HRESULT>(DIERR_INVALIDPARAM),  L"DIERR_INVALIDPARAM" },
        { static_cast<HRESULT>(DIERR_NOTEXCLUSIVEACQUIRED), L"DIERR_NOTEXCLUSIVEACQUIRED" },
    };
    for (const Known& k : known)
        if (k.code == hr) return k.text;
    wchar_t buf[32];
    swprintf(buf, 32, L"HRESULT 0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

FfbDevice::~FfbDevice() { close(); }

bool FfbDevice::listDevices(std::vector<FfbDeviceInfo>& out, std::wstring& err) {
    IDirectInput8W* di = nullptr;
    if (!createDirectInput(&di, err)) return false;
    EnumCtx ctx{&out};
    HRESULT hr = di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCallback, &ctx,
                                 DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    di->Release();
    if (FAILED(hr)) { err = L"EnumDevices falhou: " + hrToString(hr); return false; }
    return true;
}

bool FfbDevice::open(HWND window, int index, std::wstring& err) {
    close();
    std::vector<FfbDeviceInfo> list;
    if (!listDevices(list, err)) return false;
    if (list.empty()) {
        err = L"Nenhum dispositivo com Force Feedback encontrado. "
              L"O G29 está ligado na tomada e o G HUB/LGS está instalado?";
        return false;
    }

    int chosen = -1;
    if (index >= 0) {
        if (index >= static_cast<int>(list.size())) { err = L"Índice de dispositivo inválido."; return false; }
        chosen = index;
    } else {
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].looksLikeG29) { chosen = static_cast<int>(i); break; }
        if (chosen < 0) {
            err = L"Há dispositivos com FFB, mas nenhum parece ser o G29. "
                  L"Use 'g29haptic list' e depois --device N.";
            return false;
        }
    }
    info_ = list[chosen];

    if (!createDirectInput(&di_, err)) return false;
    HRESULT hr = di_->CreateDevice(info_.instance, &dev_, nullptr);
    if (FAILED(hr)) { err = L"CreateDevice falhou: " + hrToString(hr); close(); return false; }

    hr = dev_->SetDataFormat(&c_dfDIJoystick);
    if (FAILED(hr)) { err = L"SetDataFormat falhou: " + hrToString(hr); close(); return false; }

    // FFB exige acesso exclusivo. BACKGROUND mantém o acesso com o jogo em foco.
    hr = dev_->SetCooperativeLevel(window, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    if (FAILED(hr)) { err = L"SetCooperativeLevel falhou: " + hrToString(hr); close(); return false; }

    hr = dev_->Acquire();
    if (FAILED(hr)) { err = L"Acquire falhou: " + hrToString(hr); close(); return false; }

    // Estado conhecido: nenhum efeito tocando, motores ligados.
    dev_->SendForceFeedbackCommand(DISFFC_RESET);
    dev_->SendForceFeedbackCommand(DISFFC_SETACTUATORSON);
    return true;
}

bool FfbDevice::createSine(double hz, IDirectInputEffect** out, std::wstring& err) {
    DWORD axes[1] = { DIJOFS_X };
    // Em coordenadas cartesianas a direção é um vetor: com um eixo só, ele
    // precisa ser diferente de zero, senão o driver aplica força nula.
    LONG direction[1] = { 1 };
    DIPERIODIC periodic{};
    periodic.dwMagnitude = 0;
    periodic.lOffset = 0;      // média zero: o aro oscila, nunca gira para um lado
    periodic.dwPhase = 0;
    periodic.dwPeriod = static_cast<DWORD>(1000000.0 / hz); // microssegundos

    DIEFFECT eff{};
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwSamplePeriod = 0;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes = 1;
    eff.rgdwAxes = axes;
    eff.rglDirection = direction;
    eff.lpEnvelope = nullptr;
    eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams = &periodic;
    eff.dwStartDelay = 0;

    HRESULT hr = dev_->CreateEffect(GUID_Sine, &eff, out, nullptr);
    if (FAILED(hr)) { err = L"CreateEffect(GUID_Sine) falhou: " + hrToString(hr); return false; }
    hr = (*out)->Start(1, 0);
    if (FAILED(hr)) { err = L"Start do efeito falhou: " + hrToString(hr); safeRelease(*out); return false; }
    return true;
}

bool FfbDevice::createChannels(double lowHz, double highHz, std::wstring& err) {
    if (!dev_) { err = L"Dispositivo não aberto."; return false; }
    lowHz_ = lowHz;
    highHz_ = highHz;
    if (!createSine(lowHz_, &low_, err)) return false;
    std::wstring err2;
    warning_.clear();
    if (!createSine(highHz_, &high_, err2)) {
        // Um canal já basta; o agudo é somado ao grave em setLevels.
        warning_ = L"canal agudo indisponível (" + err2 + L"); usando um canal só.";
    }
    lastLow_ = lastHigh_ = -1.0;
    return true;
}

bool FfbDevice::setMagnitude(IDirectInputEffect* e, double hz, double level) {
    DIPERIODIC periodic{};
    periodic.dwMagnitude = static_cast<DWORD>(std::lround(level * DI_FFNOMINALMAX));
    periodic.lOffset = 0;
    periodic.dwPhase = 0;
    periodic.dwPeriod = static_cast<DWORD>(1000000.0 / hz);

    DIEFFECT eff{};
    eff.dwSize = sizeof(DIEFFECT);
    eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams = &periodic;
    HRESULT hr = e->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
    return SUCCEEDED(hr);
}

bool FfbDevice::setLevels(double low, double high) {
    if (!dev_ || !low_) return false;
    if (!high_) { low = std::fmin(1.0, low + high); high = 0.0; }

    // Só envia ao USB quando muda o suficiente (menos tráfego, menos CPU).
    bool ok = true;
    if (std::fabs(low - lastLow_) > 0.0005) {
        ok = setMagnitude(low_, lowHz_, low) && ok;
        if (ok) lastLow_ = low;
    }
    if (high_ && std::fabs(high - lastHigh_) > 0.0005) {
        ok = setMagnitude(high_, highHz_, high) && ok;
        if (ok) lastHigh_ = high;
    }
    return ok;
}

bool FfbDevice::tryRecover() {
    if (!dev_) return false;
    if (FAILED(dev_->Acquire())) return false;
    // Após reaquisição os efeitos precisam ser baixados de novo para o volante.
    for (IDirectInputEffect* e : { low_, high_ }) {
        if (!e) continue;
        e->Download();
        e->Start(1, 0);
    }
    lastLow_ = lastHigh_ = -1.0;
    setLevels(0.0, 0.0);
    return true;
}

std::wstring FfbDevice::diagnostics() const {
    if (!dev_) return L"dispositivo não aberto";
    std::wstring out;
    DWORD state = 0;
    if (SUCCEEDED(dev_->GetForceFeedbackState(&state))) {
        out += (state & DIGFFS_ACTUATORSON) ? L"motores LIGADOS" : L"motores DESLIGADOS";
        if (state & DIGFFS_STOPPED) out += L", estado PARADO";
        if (state & DIGFFS_PAUSED) out += L", estado PAUSADO";
        if (state & DIGFFS_POWEROFF) out += L", SEM ENERGIA (fonte desligada?)";
        if (state & DIGFFS_SAFETYSWITCHOFF) out += L", chave de segurança desligada";
        if (state & DIGFFS_DEVICELOST) out += L", dispositivo perdido";
    } else {
        out += L"estado FFB indisponível";
    }
    auto effectState = [](IDirectInputEffect* e) -> std::wstring {
        if (!e) return L"não criado";
        DWORD st = 0;
        HRESULT hr = e->GetEffectStatus(&st);
        if (FAILED(hr)) return L"erro " + hrToString(hr);
        return (st & DIEGES_PLAYING) ? L"tocando" : L"PARADO";
    };
    out += L" | efeito grave: " + effectState(low_) + L" | efeito agudo: " + effectState(high_);
    return out;
}

bool FfbDevice::playTemporarySine(double hz, double level, DWORD ms, std::wstring& err) {
    IDirectInputEffect* e = nullptr;
    if (!createSine(hz, &e, err)) return false;
    const bool ok = setMagnitude(e, hz, level);
    if (!ok) err = L"SetParameters falhou no efeito temporário";
    Sleep(ms);
    setMagnitude(e, hz, 0.0);
    e->Stop();
    e->Unload();
    e->Release();
    return ok;
}

void FfbDevice::stopAll() {
    if (low_) setMagnitude(low_, lowHz_, 0.0);
    if (high_) setMagnitude(high_, highHz_, 0.0);
    if (dev_) dev_->SendForceFeedbackCommand(DISFFC_STOPALL);
    lastLow_ = lastHigh_ = 0.0;
}

void FfbDevice::close() {
    if (dev_) {
        stopAll();
        dev_->SendForceFeedbackCommand(DISFFC_RESET);
    }
    safeRelease(low_);
    safeRelease(high_);
    if (dev_) dev_->Unacquire();
    safeRelease(dev_);
    safeRelease(di_);
}

} // namespace haptics
