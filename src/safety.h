// safety.h — camada de proteção entre "o que se pede" e "o que vai ao motor".
//
// Toda saída para o G29 passa por aqui. Os valores são frações do torque
// nominal do DirectInput (0.0 a 1.0, onde 1.0 = DI_FFNOMINALMAX).
//
// Proteções:
//  1. Teto de saída (ganho escolhido pelo usuário), com teto rígido absoluto.
//  2. Rampa de ataque/decaimento: nenhuma mudança é instantânea.
//  3. Trava de comando parado: se o mesmo valor não-zero fica parado por
//     muito tempo (jogo travou, crashou), a saída decai para zero.
//  4. Limitador de ciclo de trabalho: média móvel de ~10 s da saída. Se o
//     motor ficar energizado demais em média, a força é reduzida.
#pragma once

#include <algorithm>
#include <cmath>

namespace haptics {

// Nunca ultrapassado, independentemente de argumentos.
constexpr double kHardCeiling = 0.40;

struct SafetyConfig {
    double maxOutput     = 0.10;  // ganho do usuário (fração do nominal)
    double attackSeconds = 0.030; // 0 -> 100% do nominal em 30 ms
    double releaseSeconds = 0.150; // 100% -> 0 em 150 ms
    double holdTimeout   = 3.0;   // s com o mesmo valor não-zero antes de cortar
    double dutyTau       = 10.0;  // constante de tempo da média (s)
    double dutyBudget    = 0.15;  // média máxima permitida (fração do nominal)
};

inline double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

// Mede a média de energização pedida ao motor e devolve um fator de redução.
// Mede a DEMANDA (antes da redução), não a saída: assim, em vibração
// contínua, a média real converge exatamente para dutyBudget.
class DutyMonitor {
public:
    // demand: saída somada que seria enviada sem redução (fração do nominal).
    static double demandOf(double low, double high, const SafetyConfig& c) {
        return (clamp01(low) + clamp01(high)) * std::min(c.maxOutput, kHardCeiling);
    }
    double update(double level, double dt, const SafetyConfig& c) {
        const double a = std::min(1.0, dt / c.dutyTau);
        ema_ += (level - ema_) * a;
        return ema_ > c.dutyBudget ? c.dutyBudget / ema_ : 1.0;
    }
    double average() const { return ema_; }
    void reset() { ema_ = 0.0; }
private:
    double ema_ = 0.0;
};

// Um canal (um "motor" lógico: grave ou agudo).
class ChannelLimiter {
public:
    // target: 0..1 (intensidade pedida pelo jogo/teste, relativa)
    // now: tempo atual (s); dt: passo (s); scale: fator do DutyMonitor.
    // Retorna a saída final em fração do nominal (já limitada).
    double step(double target, double now, double dt,
                const SafetyConfig& c, double scale) {
        target = clamp01(target);

        if (std::fabs(target - lastTarget_) > 0.004) {
            lastTarget_ = target;
            changedAt_ = now;
        }
        held_ = target > 0.0 && (now - changedAt_) > c.holdTimeout;
        if (held_) target = 0.0;

        const double ceiling = std::min(c.maxOutput, kHardCeiling);
        const double goal = target * ceiling * clamp01(scale);

        const double up = dt / c.attackSeconds;
        const double down = dt / c.releaseSeconds;
        if (goal > out_) out_ = std::min(goal, out_ + up);
        else             out_ = std::max(goal, out_ - down);
        return out_;
    }
    bool heldCut() const { return held_; }
    void reset() { out_ = 0.0; lastTarget_ = 0.0; changedAt_ = 0.0; held_ = false; }
private:
    double out_ = 0.0;
    double lastTarget_ = 0.0;
    double changedAt_ = 0.0;
    bool held_ = false;
};

} // namespace haptics
