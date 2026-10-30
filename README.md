# G29 Desk Haptics

Faz o Logitech G29 tremer a mesa com a vibração que o jogo mandaria para um controle Xbox, enquanto você joga de mouse e teclado.

```
Jogo ──rumble──> controle Xbox 360 virtual (ViGEmBus) ──> g29haptic ──> camada de segurança ──> motor do G29 ──> mesa
```

**Estado atual: protótipo de console (etapas 2 a 4).** Já tem a ponte de rumble, o teste de motor e todas as proteções. A interface gráfica (etapa 6) e o modo de áudio (etapa 5) vêm em seguida.

---

## 1. Instalação (uma vez)

1. **Driver da Logitech:** instale o **G HUB**. Deixe o G29 com a fonte ligada na tomada e no USB.
   - No G HUB, deixe a **força do Force Feedback no padrão** durante os testes. Ela multiplica a força deste programa.
2. **Driver do controle virtual:** instale o **ViGEmBus** (instalador assinado em
   [github.com/nefarius/ViGEmBus/releases](https://github.com/nefarius/ViGEmBus/releases)) e reinicie o PC.
   - O projeto ViGEm foi aposentado em 2023 e não recebe mais atualizações, mas o driver continua funcionando no Windows 10/11 e é usado por muitos programas (DS4Windows, Sunshine e outros).

## 2. Compilar

**Opção A: GitHub (sem instalar nada).** Envie esta pasta para um repositório seu. O arquivo `.github/workflows/build.yml` compila automaticamente. Para baixar o `.exe`: *Actions → última execução → Artifacts → g29haptic*.

**Opção B: no seu PC.** Instale o Visual Studio 2022 (ou mais novo) com a carga de trabalho *"Desenvolvimento para desktop com C++"*, que já inclui o CMake. Depois, num terminal dentro da pasta:

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

O executável fica em `build\Release\g29haptic.exe`. O ViGEmClient é baixado sozinho pelo CMake.

## 3. Procedimento de teste seguro (faça nesta ordem)

1. **Volante livre:** mãos fora do aro, nada encostado nele.
2. **Ver se o G29 aparece:**
   ```
   g29haptic list
   ```
   O G29 deve aparecer marcado com `<- G29`.
3. **Teste de motor com intensidade mínima:**
   ```
   g29haptic test --gain 10
   ```
   O programa toca: pulso grave, pulso agudo, oscilação suave e 5 impactos. Observe e escute.
4. **Subir aos poucos**, só se a vibração ficou fraca demais: `--gain 15`, depois `--gain 20`. O limite rígido é 40%.
5. **Teste em segundo plano** (confirma que funciona com o jogo em foco):
   ```
   g29haptic test --gain 10 --delay 5
   ```
   Durante os 5 s de espera, clique em outra janela. Se vibrar mesmo assim, o modo segundo plano funciona.
6. **Avalie a mesa:** coloque a mão na mesa, não no volante. A transmissão depende da braçadeira e do tipo de mesa. Aperte bem a fixação.

**Parada de emergência:** `Ctrl + Alt + End` funciona mesmo com o jogo em tela cheia. `Ctrl + C` também para. Fechar a janela zera as forças antes de sair.

## 4. Usar a ponte durante o jogo

```
g29haptic bridge --gain 15
```

1. Abra o `g29haptic bridge` **antes** do jogo (alguns jogos só procuram controles ao iniciar).
2. Jogue normalmente com mouse e teclado.
3. A linha de status mostra, em tempo real:
   - `Rumble jogo: grave X agudo Y`: o que o jogo está mandando (0-255);
   - `G29: a% b%`: o que vai ao motor depois da segurança;
   - `eventos`: quantas mensagens de vibração chegaram.

### Se nada vibrar no jogo

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| `eventos 0` o tempo todo | O jogo não está mandando rumble para o controle virtual | Ver os itens abaixo |
| O jogo usa o Steam Input | A Steam pode estar intermediando o controle | Steam → jogo → Propriedades → Controle → teste **desativar o Steam Input** |
| O jogo só vibra o dispositivo que você usou por último | Decisão do próprio jogo: com mouse e teclado ativos, ele não manda rumble | Não há como capturar o que não é enviado. Use o modo áudio (próxima etapa) |
| `eventos` sobe, mas nada treme | Intensidade baixa demais para vencer o atrito das engrenagens | Suba o `--gain` aos poucos |

Um jeito rápido de confirmar se o jogo vibra o controle: aperte qualquer botão de um controle de verdade conectado. Se o jogo vibrar o controle real mas não mandar nada com mouse e teclado, ele é do tipo "só o dispositivo ativo".

## 5. Proteções do hardware (sempre ligadas)

Ficam em `src/safety.h`, cobertas por `tests/safety_test.cpp`:

- **Só vibração de média zero** (seno). O aro oscila poucos graus e nunca é girado para um lado.
- **Teto de intensidade:** escolhido por você (`--gain`, padrão 10%), com limite rígido de 40% do torque nominal.
- **Rampas:** de zero ao máximo em 30 ms e do máximo a zero em 150 ms. Nunca há salto instantâneo.
- **Corte de vibração travada:** se o jogo deixa o mesmo valor parado por mais de 3 s (travou, crashou), a força vai a zero. Ajuste com `--max-hold`.
- **Limite de carga média:** a média de ~10 s é mantida em no máximo 15% do torque nominal. Em vibração contínua forte, o programa reduz a força sozinho.
- **Perda do dispositivo** (desconectou, outro programa assumiu): a força é zerada na hora e o programa tenta recuperar a cada 1 s.
- **Saída do programa** (Ctrl+C, fechar janela, emergência): zera os efeitos e reseta o FFB antes de liberar o volante.
- O firmware não é alterado.

## 6. Limitações conhecidas

- **Depende do jogo mandar rumble.** A ponte repassa tudo o que o jogo envia ao controle Xbox. Jogos que param de vibrar quando você usa mouse e teclado não enviam nada; para eles o modo áudio vai ser a alternativa.
- **Gatilhos do Xbox One** (impulse triggers) não são capturados. O controle virtual é um Xbox 360, que tem dois motores.
- **Jogos que usam o próprio volante** (corrida) podem pedir acesso exclusivo ao G29 e entrar em conflito com este programa.
- **Física diferente:** os motores do Xbox são pesos excêntricos; o G29 é um motor de torque com engrenagens. Por isso o rumble é traduzido em dois senos (grave 20 Hz, agudo 40 Hz, ajustáveis com `--low-hz` e `--high-hz`). Há ruído de engrenagem, e frequências altas chegam atenuadas à mesa.
- **Ainda não testado com hardware real.** O código foi compilado contra os cabeçalhos do Windows e a camada de segurança passou nos testes automatizados, mas o comportamento do motor e da mesa só se confirma no seu G29. Comece pelo passo 3.

## 7. Estrutura do código

| Arquivo | Função |
|---|---|
| `src/ffb_device.*` | DirectInput: encontra o G29, abre em modo exclusivo/segundo plano, cria os efeitos senoidais |
| `src/rumble_bridge.*` | ViGEmBus: cria o controle Xbox 360 virtual e recebe o rumble do jogo |
| `src/safety.h` | Teto, rampas, corte de travamento, limite de carga média |
| `src/main.cpp` | Comandos `list` / `test` / `bridge`, loop de 100 Hz, parada de emergência |
| `tests/safety_test.cpp` | Testes automáticos da segurança (não precisam do volante) |

## Próximas etapas

- **Etapa 5:** modo áudio (WASAPI loopback), reserva automática quando o jogo não envia rumble.
- **Etapa 6:** interface gráfica com slider de intensidade, monitor em tempo real e botão de emergência.
