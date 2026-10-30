// main.cpp — G29 Desk Haptics (protótipo de console, etapas 2 a 4).
//
// Comandos:
//   g29haptic list                      lista dispositivos com Force Feedback
//   g29haptic test   [opções]           sequência de teste do motor (Modo C)
//   g29haptic bridge [opções]           ponte rumble Xbox -> G29 (Modo A)
//
// Opções:
//   --gain N      intensidade máxima em % do torque nominal (1-40, padrão 10)
//   --device N    índice do dispositivo (ver 'list'); padrão: detecta o G29
//   --low-hz F    frequência do canal grave  (padrão 20 Hz)
//   --high-hz F   frequência do canal agudo  (padrão 40 Hz)
//   --delay S     (test) espera S segundos antes de começar
//   --max-hold S  (bridge) corta vibração parada no mesmo valor após S s (padrão 3)
//
// Parada de emergência: Ctrl+Alt+End (funciona com o jogo em foco) ou Ctrl+C.

#include "ffb_device.h"
#include "rumble_bridge.h"
#include "safety.h"

#include <windows.h>
#include <timeapi.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace haptics;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kHotkeyId = 1;

std::atomic<bool> g_stopRequested{false};
std::atomic<bool> g_mainLoopDone{false};

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

BOOL WINAPI consoleHandler(DWORD type) {
    g_stopRequested = true;
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        // O Windows encerra o processo quando este handler retorna:
        // dá tempo para o loop principal zerar e liberar o volante.
        for (int i = 0; i < 50 && !g_mainLoopDone; ++i) Sleep(10);
    }
    return TRUE;
}

// Janela oculta: o DirectInput exige uma janela de nível superior para o
// SetCooperativeLevel, e ela também recebe a tecla de emergência global.
HWND createHiddenWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"G29DeskHapticsHidden";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, wc.lpszClassName, L"G29 Desk Haptics", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                           nullptr, nullptr, wc.hInstance, nullptr);
}

struct Options {
    std::string command;
    double gainPercent = 10.0;
    int device = -1;
    double lowHz = 20.0;
    double highHz = 40.0;
    double delay = 0.0;
    double maxHold = 3.0;
};

void printUsage() {
    std::printf(
        "G29 Desk Haptics 0.1 — vibração de controle Xbox no motor do Logitech G29\n\n"
        "Uso:\n"
        "  g29haptic list\n"
        "  g29haptic test   [--gain N] [--delay S] [--device N] [--low-hz F] [--high-hz F]\n"
        "  g29haptic bridge [--gain N] [--max-hold S] [--device N] [--low-hz F] [--high-hz F]\n\n"
        "  --gain N   intensidade máxima em %% do torque nominal (1-%d, padrão 10)\n\n"
        "Parada de emergência: Ctrl+Alt+End (mesmo com o jogo em foco) ou Ctrl+C.\n",
        static_cast<int>(kHardCeiling * 100));
}

bool parseArgs(int argc, char** argv, Options& o) {
    if (argc < 2) return false;
    o.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        auto need = [&](const char* name) -> const char* {
            if (std::strcmp(argv[i], name) != 0) return nullptr;
            if (i + 1 >= argc) { std::fprintf(stderr, "Falta valor para %s\n", name); std::exit(2); }
            return argv[++i];
        };
        if (const char* v = need("--gain")) o.gainPercent = std::atof(v);
        else if (const char* v2 = need("--device")) o.device = std::atoi(v2);
        else if (const char* v3 = need("--low-hz")) o.lowHz = std::atof(v3);
        else if (const char* v4 = need("--high-hz")) o.highHz = std::atof(v4);
        else if (const char* v5 = need("--delay")) o.delay = std::atof(v5);
        else if (const char* v6 = need("--max-hold")) o.maxHold = std::atof(v6);
        else { std::fprintf(stderr, "Opção desconhecida: %s\n", argv[i]); return false; }
    }
    const double maxGain = kHardCeiling * 100.0;
    if (o.gainPercent < 1.0) o.gainPercent = 1.0;
    if (o.gainPercent > maxGain) {
        std::printf("[segurança] --gain limitado a %.0f%%.\n", maxGain);
        o.gainPercent = maxGain;
    }
    o.lowHz = std::fmin(std::fmax(o.lowHz, 5.0), 100.0);
    o.highHz = std::fmin(std::fmax(o.highHz, 5.0), 100.0);
    o.maxHold = std::fmin(std::fmax(o.maxHold, 0.5), 30.0);
    return true;
}

// Fonte de sinal: recebe o tempo (s) e devolve os alvos 0..1 de cada canal.
// Retorna false quando terminou.
using Source = std::function<bool(double t, double& low, double& high)>;
// Linha de status opcional, chamada a cada 250 ms.
using StatusFn = std::function<void(double t, double outLow, double outHigh, double dutyAvg, bool held)>;

// Loop de 100 Hz: fonte -> limitador de segurança -> motor.
int runLoop(FfbDevice& dev, const SafetyConfig& cfg, const Source& source, const StatusFn& status) {
    ChannelLimiter chLow, chHigh;
    DutyMonitor duty;
    double outLow = 0.0, outHigh = 0.0;
    bool lost = false;
    double lastRecover = 0.0, lastStatus = -1.0;
    int exitCode = 0;

    const double start = RumbleBridge::nowSeconds();
    double prev = start;

    timeBeginPeriod(1);
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && msg.wParam == kHotkeyId) {
                dev.stopAll();
                std::printf("\n\n*** PARADA DE EMERGÊNCIA (Ctrl+Alt+End) ***\n");
                g_stopRequested = true;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_stopRequested) break;

        const double now = RumbleBridge::nowSeconds();
        const double dt = std::fmin(now - prev, 0.1);
        prev = now;
        const double t = now - start;

        double low = 0.0, high = 0.0;
        if (!source(t, low, high)) break;

        const double scale = duty.update(DutyMonitor::demandOf(low, high, cfg), dt, cfg);
        outLow = chLow.step(low, now, dt, cfg, scale);
        outHigh = chHigh.step(high, now, dt, cfg, scale);

        if (!lost) {
            if (!dev.setLevels(outLow, outHigh)) {
                lost = true;
                std::printf("\n[erro] controle do G29 perdido (desconectado ou outro programa assumiu). "
                            "Força zerada; tentando recuperar...\n");
                chLow.reset(); chHigh.reset(); duty.reset();
                outLow = outHigh = 0.0;
            }
        } else if (now - lastRecover > 1.0) {
            lastRecover = now;
            if (dev.tryRecover()) { lost = false; std::printf("\n[ok] G29 recuperado.\n"); }
        }

        if (status && now - lastStatus >= 0.25) {
            lastStatus = now;
            status(t, outLow, outHigh, duty.average(), chLow.heldCut() || chHigh.heldCut());
        }
        Sleep(10);
    }
    timeEndPeriod(1);
    dev.stopAll();
    return exitCode;
}

int cmdList() {
    std::vector<FfbDeviceInfo> list;
    std::wstring err;
    if (!FfbDevice::listDevices(list, err)) { std::printf("[erro] %s\n", toUtf8(err).c_str()); return 1; }
    if (list.empty()) {
        std::printf("Nenhum dispositivo com Force Feedback encontrado.\n"
                    "Verifique: fonte do G29 ligada, cabo USB, G HUB (ou LGS) instalado.\n");
        return 1;
    }
    std::printf("Dispositivos com Force Feedback:\n");
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& d = list[i];
        std::printf("  [%zu] %s  (VID %04X, PID %04X)%s\n", i, toUtf8(d.name).c_str(), d.vid, d.pid,
                    d.looksLikeG29 ? "  <- G29" : "");
    }
    return 0;
}

bool openWheel(FfbDevice& dev, HWND hwnd, const Options& o) {
    std::wstring err;
    if (!dev.open(hwnd, o.device, err)) { std::printf("[erro] %s\n", toUtf8(err).c_str()); return false; }
    std::printf("Volante: %s (VID %04X, PID %04X)\n", toUtf8(dev.info().name).c_str(),
                dev.info().vid, dev.info().pid);
    if (!dev.createChannels(o.lowHz, o.highHz, err)) {
        std::printf("[erro] %s\n", toUtf8(err).c_str());
        return false;
    }
    if (!dev.warning().empty()) std::printf("[aviso] %s\n", toUtf8(dev.warning()).c_str());
    std::printf("Motor pronto: %d canal(is), grave %.0f Hz, agudo %.0f Hz, intensidade máx. %.0f%%\n",
                dev.channelCount(), o.lowHz, o.highHz, o.gainPercent);
    return true;
}

int cmdTest(FfbDevice& dev, const Options& o, const SafetyConfig& cfg) {
    for (double left = o.delay; left > 0.0 && !g_stopRequested; left -= 1.0) {
        std::printf("Começando em %.0f s... (troque para outra janela para testar em segundo plano)\r", left);
        std::fflush(stdout);
        Sleep(1000);
    }
    std::printf("\n");

    struct Stage { double begin, end; const char* name; };
    static const Stage stages[] = {
        {0.0, 0.3, "Pulso curto grave (300 ms)"},
        {1.3, 1.6, "Pulso curto agudo (300 ms)"},
        {2.6, 4.6, "Oscilação suave (2 s, sobe e desce)"},
        {5.6, 6.9, "Sequência de 5 impactos"},
    };
    int announced = -1;

    Source src = [&](double t, double& low, double& high) {
        for (int i = 0; i < 4; ++i)
            if (t >= stages[i].begin && t < stages[i].end && announced < i) {
                announced = i;
                std::printf("-> %s\n", stages[i].name);
            }
        low = high = 0.0;
        if (t < 0.3) low = 1.0;
        else if (t >= 1.3 && t < 1.6) high = 1.0;
        else if (t >= 2.6 && t < 4.6) low = 0.5 * (1.0 - std::cos(2.0 * kPi * (t - 2.6) / 2.0));
        else if (t >= 5.6 && t < 6.9) {
            const double k = std::fmod(t - 5.6, 0.25);
            if (k < 0.08) { low = 1.0; high = 0.5; }
        }
        return t < 7.5;
    };
    int rc = runLoop(dev, cfg, src, nullptr);
    std::printf("Teste concluído. Sentiu na mesa? Se quase nada, repita com --gain 15, depois 20 (máx. %.0f).\n",
                kHardCeiling * 100);
    return rc;
}

int cmdBridge(FfbDevice& dev, const SafetyConfig& cfg) {
    RumbleBridge bridge;
    std::wstring err;
    if (!bridge.start(err)) { std::printf("[erro] %s\n", toUtf8(err).c_str()); return 1; }

    std::printf("Controle Xbox 360 virtual conectado (slot XInput %d).\n"
                "Abra o jogo e jogue normalmente com mouse e teclado.\n"
                "Ctrl+Alt+End = parada de emergência | Ctrl+C = sair\n\n", bridge.userIndex() + 1);

    // Os motores do Xbox não têm limiar; o motor do G29 tem atrito.
    // Valores pequenos ganham um piso para serem sentidos.
    auto shape = [](double v) { return v < 0.01 ? 0.0 : 0.2 + 0.8 * v; };

    Source src = [&](double, double& low, double& high) {
        const RumbleState s = bridge.snapshot();
        low = shape(s.largeMotor / 255.0);
        high = shape(s.smallMotor / 255.0) * 0.6;
        return true;
    };

    StatusFn status = [&](double t, double outLow, double outHigh, double dutyAvg, bool held) {
        const RumbleState s = bridge.snapshot();
        const char* note;
        if (s.events == 0)
            note = t > 20.0 ? "nenhum rumble recebido: o jogo não está vibrando o controle" : "aguardando rumble do jogo...";
        else if (held)
            note = "vibração parada há muito tempo: cortada por segurança";
        else
            note = (s.largeMotor || s.smallMotor) ? "vibrando" : "ok";
        std::printf("\rRumble jogo: grave %3u agudo %3u | G29: %4.1f%% %4.1f%% | média %4.1f%% | eventos %llu | %-60s",
                    s.largeMotor, s.smallMotor, outLow * 100.0, outHigh * 100.0, dutyAvg * 100.0,
                    static_cast<unsigned long long>(s.events), note);
        std::fflush(stdout);
    };

    int rc = runLoop(dev, cfg, src, status);
    bridge.stop();
    std::printf("\nPonte encerrada.\n");
    return rc;
}

int runCommand(const Options& o) {
    if (o.command == "list") return cmdList();
    if (o.command != "test" && o.command != "bridge") { printUsage(); return 2; }

    g_stopRequested = false;
    g_mainLoopDone = false;
    HWND hwnd = createHiddenWindow();
    if (!hwnd) { std::printf("[erro] não foi possível criar a janela interna.\n"); return 1; }
    if (!RegisterHotKey(hwnd, kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_END))
        std::printf("[aviso] Ctrl+Alt+End já está em uso por outro programa; use Ctrl+C para parar.\n");

    SafetyConfig cfg;
    cfg.maxOutput = o.gainPercent / 100.0;
    cfg.holdTimeout = o.maxHold;

    int rc = 1;
    {
        FfbDevice dev;
        if (openWheel(dev, hwnd, o)) {
            SafetyConfig run = cfg;
            if (o.command == "test") run.holdTimeout = 30.0; // o teste tem trechos longos de propósito
            rc = (o.command == "test") ? cmdTest(dev, o, run) : cmdBridge(dev, run);
        }
        // dev.close() no destrutor: zera forças, DISFFC_RESET e libera o volante.
    }
    g_mainLoopDone = true;
    UnregisterHotKey(hwnd, kHotkeyId);
    DestroyWindow(hwnd);
    return rc;
}

// True quando o programa foi aberto com duplo clique (a janela do console
// pertence só a ele e fecharia sozinha ao terminar).
bool ownsConsole() {
    DWORD ids[2];
    return GetConsoleProcessList(ids, 2) <= 1;
}

std::string readLine() {
    char buf[128] = {};
    if (!std::fgets(buf, sizeof(buf), stdin)) return {};
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

double askGain(double def) {
    std::printf("Intensidade em %% (1-%.0f) [Enter = %.0f]: ", kHardCeiling * 100, def);
    std::string s = readLine();
    double v = s.empty() ? def : std::atof(s.c_str());
    if (v < 1.0) v = 1.0;
    if (v > kHardCeiling * 100) v = kHardCeiling * 100;
    return v;
}

// Menu para quem abre o .exe com duplo clique.
void interactiveMenu() {
    for (;;) {
        std::printf(
            "\n===== G29 Desk Haptics =====\n"
            "  1) Listar dispositivos (ver se o G29 aparece)\n"
            "  2) Testar o motor (mãos fora do volante)\n"
            "  3) Testar o motor em segundo plano (5 s para trocar de janela)\n"
            "  4) Iniciar a ponte de rumble (deixe aberto e abra o jogo)\n"
            "  0) Sair\n"
            "Escolha: ");
        std::string c = readLine();
        Options o;
        if (c == "0" || (c.empty() && std::feof(stdin))) return;
        if (c == "1") o.command = "list";
        else if (c == "2") { o.command = "test"; o.gainPercent = askGain(10); }
        else if (c == "3") { o.command = "test"; o.gainPercent = askGain(10); o.delay = 5; }
        else if (c == "4") {
            o.command = "bridge"; o.gainPercent = askGain(15);
            std::printf("Parar: Ctrl+C (volta ao menu) | Emergência: Ctrl+Alt+End\n");
        }
        else continue;
        std::printf("\n");
        runCommand(o);
    }
}

} // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(consoleHandler, TRUE);

    if (argc < 2 && ownsConsole()) {
        interactiveMenu();
        return 0;
    }

    Options o;
    int rc;
    if (!parseArgs(argc, argv, o)) { printUsage(); rc = 2; }
    else rc = runCommand(o);

    if (ownsConsole()) {
        std::printf("\nPressione Enter para fechar...");
        readLine();
    }
    return rc;
}
