#include "hal/buttons.hh"
#include "hal/lib.hh"
#include "hal/log.hh"
#include "hal/radio.hh"
#include "hal/timer.hh"
#include "hal/ui.hh"

/// Ao receber mensagens em parâmetros com mensagens demoradas, o receptor
/// possui um delay variável, cuja fonte não pude verificar ainda. Nos
/// parâmetros mais demorados, com 62.5kHz e SF12, o delay máximo reportado foi
/// de 50-60ms, mas em geral parece ser +-3% do time on air.
#define RX_TIMING_ERROR 54800

/// O delay aguardado antes de iniciar o relógio do transmissor
#define TX_DELAY (RX_TIMING_ERROR + 8000)

/// A quantidade de tempo, em microsegundos, disponibilizado para qualquer
/// processamento além do experimento principal.
#define BUDGET TX_DELAY + 100000

/// Define a quantidade de mensagens enviadas para cada combinação de
/// parâmetros.
#define MESSAGES_PER_TEST 50

/// A mensagem enviada durante o experimento.
const uint8_t _message[] = "Mensagem!";
const uint8_t _messageLength = sizeof(_message) / sizeof(char);
bool _hasSD = false;

/// Determina o índice do teste atual. Usado para calcular o
/// SF, CR, largura de banda e potência de transmissão do teste.
uint32_t _currentTest = 0;

/// Contagem de pacotes recebidos com sucesso, corruptos ou perdidos.
uint32_t _testsOk = 0;
uint32_t _testsCorrupt = 0;
uint32_t _testsLost = 0;

/// Define os parâmetros atuais da transmissão.
/// Os parâmetros iniciais serão utilizados na sincronização inicial
/// dos dispositivos.
radio_parameters_t _parameters = radio_parameters_t {
    .power = 22,
    .frequency = 915.0,
    .preambleLength = 8,
    .bandwidth = 62.5,
    .sf = 12,
    .cr = 8,
    .crc = true,
    .invertIq = false,
    .boostedRxGain = true,
    .packetLength = 0,
    .syncWord = 0xAE,
};

/// Define o cargo atual deste aparelho.
enum role_t {
    kUnspecified,
    kTx,
    kRx,
} _role = kUnspecified;

void setup() {
    Serial.begin(115200);

    halInit();
    radioInit();
    timerInit();
    uiSetup();

    // Tentar inicializar o logger no cartão SD
    _hasSD = logInit("/log.txt");
    logPrintf("Modulo iniciado");
}

/// Atualiza os parâmetros atuais do teste de acordo com o índice do teste
/// atual. Retorna `true` se o índice do teste era inválido.
bool updateTestParameters() {
    const uint8_t power[] = { 5, 14, 22 };
    const uint8_t sf[] = { 7, 8, 9, 10, 11, 12 };
    const uint8_t cr[] = { 5, 8 };
    const float bw[] = { 62.5, 125.0, 250.0 };

    // Tamanho dos vetores dos parâmetros
    const size_t powerSize = sizeof(power) / sizeof(uint8_t);
    const size_t sfSize = sizeof(sf) / sizeof(uint8_t);
    const size_t crSize = sizeof(cr) / sizeof(uint8_t);
    const size_t bwSize = sizeof(bw) / sizeof(float);

    bool wasInvalidTest = _currentTest == 0;

    // Limita o índice do teste para índices válidos
    _currentTest %= bwSize * crSize * sfSize * powerSize;

    // Caso o índice do teste esteja acima do limite, será `true`
    wasInvalidTest = !wasInvalidTest && _currentTest == 0;

    uint32_t index = _currentTest;

    _parameters.bandwidth = bw[index % bwSize];
    index /= bwSize;

    _parameters.cr = cr[index % crSize];
    index /= crSize;

    _parameters.sf = sf[index % sfSize];
    index /= sfSize;

    _parameters.power = power[index % powerSize];
    index /= powerSize;

    // Atualizar parâmetros no radiotransmissor
    _parameters.syncWord = 0xEA;
    _parameters.boostedRxGain = false;
    radioSetParameters(_parameters);
    return wasInvalidTest;
}

int printMinimalTime(char* buffer, size_t size, uint64_t time) {
    // Minimizar o tamanho da string de tempo
    const char* unit = "us";
    float fTime = time;

    for (uint8_t i = 0; i < 3 && fTime >= 1000; i++) {
        const char* units[] = { "ms", "s", "m" };
        fTime /= 1000;
        unit = units[i];
    }

    // Imprimir tempo mínimo
    return snprintf(buffer, size, "%.1f %s", fTime, unit);
}

int printMinimalPeriod(char* buffer, size_t size, uint64_t start,
                       uint64_t end) {
    // Minimizar o tamanho da string de tempo
    const char* unit = "us";
    float fStart = start;
    float fEnd = end;

    for (uint8_t i = 0; i < 3 && fEnd >= 1000; i++) {
        const char* units[] = { "ms", "s", "m" };
        fStart /= 1000;
        fEnd /= 1000;
        unit = units[i];
    }

    // Imprimir tempo mínimo
    return snprintf(buffer, size, "%.1f/%.1f %s", fStart, fEnd, unit);
}

/// Desenha o overlay com os parâmetros e resultados da transmissão atual.
void drawTestOverlay(const char* title, bool drawQuality, bool drawToA) {
    char buffer[1024];

    // Desenhar textos estáticos
    uiAlign(kCenter);
    if (title == NULL)
        uiText(0, 0, _hasSD ? "SD" : "SER");

    uiAlign(kLeft);

    // Desenhar título fallback caso nenhum titulo tenha sido passado
    if (title == NULL) {
        snprintf(buffer, 1024, "Teste %u", _currentTest);
        uiText(0, 0, buffer);
    } else {
        uiText(0, 0, title);
    }

    int16_t paramsY = drawQuality ? 42 : 52;

    uiText(0, paramsY, "Band.");
    uiText(0, paramsY + 10, "RSSI");
    uiText(128 - 55, paramsY + 10, "SNR");

    // Escrever valor do spreading factor atual
    snprintf(buffer, 1024, "SF%d", _parameters.sf);
    uiText(128 - 55, paramsY, buffer);

    if (drawToA) {
        // Desenhar tempo de transmissão alinhado com a largura de banda
        const uint64_t toa = radioTransmitTime(_parameters, _messageLength);
        uiText(0, paramsY - 10, "ToA");

        uiAlign(kRight);
        printMinimalTime(buffer, 1024, toa);
        uiText(60, paramsY - 10, buffer);
    } else {
        // Printar quantia de pacotes ok, corruptos e perdidos
        uiAlign(kCenter);
        snprintf(buffer, 1024, "(%u/%u/%u)", _testsOk, _testsCorrupt,
                 _testsLost);
        uiText(0, paramsY - 10, buffer);
    }

    // Escrever valor do coding rate atual
    uiAlign(kRight);
    snprintf(buffer, 1024, "CR%d", _parameters.cr);
    uiText(0, paramsY, buffer);

    // Escrever valor da potência
    snprintf(buffer, 1024, "(%d dBm)", _parameters.power);
    uiText(0, 0, buffer);

    // Escrever valor da largura de banda atual
    if ((_parameters.bandwidth - (int)_parameters.bandwidth) == 0)
        snprintf(buffer, 1024, "%.0fkHz", _parameters.bandwidth);
    else
        snprintf(buffer, 1024, "%.1fkHz", _parameters.bandwidth);

    uiText(60, paramsY, buffer);

    // Escrever RSSI atual
    snprintf(buffer, 1024, "%hddBm", radioRSSI());
    uiText(60, paramsY + 10, buffer);

    // Escrever SNR atual
    snprintf(buffer, 1024, "%.0fdB", radioSNR());
    uiText(0, paramsY + 10, buffer);
}

/// Descreve os possíveis estados do protocolo de comunicação.
enum protocol_state_t {
    kUninitialized,
    kRunning,
    kFinished,
};

protocol_state_t _protoState = kUninitialized;
uint32_t _messageIndex = 0;
uint64_t _begin = 0;

/// Executa após um cargo ser selecionado no menu.
void syncLoop() {
    uiLoop();

    // Inicializar parâmetros padrão de transmissão
    radioSetParameters(_parameters);

    // Desenhar interface antes da operação LoRa
    uiClear();
    drawTestOverlay(NULL, _role == kRx, true);

    uiAlign(kCenter);
    uiText(0, 15, _role == kRx ? "Esperando sync..." : "Enviando sync...");
    uiFinish();

    radio_error_t error = kNone;
    uint8_t message[] = { _currentTest };
    uint8_t length = sizeof(message) / sizeof(uint8_t);

    if (_role == kRx) {
        error = radioRecv(message, &length);
    } else if (_role == kTx) {
        error = radioSend(message, length);
    }

    // Tentar novamente caso a transmissão tenha falhado.
    if (error != kNone) {
        Serial.print("Erro na sincronização, e");
        Serial.println(error);
        delay(1000);
        return;
    }

    // Atualizar parâmetros do teste e iniciar o experimento.
    _currentTest = message[0];
    updateTestParameters();

    const auto totalBudget =
        radioTransmitTime(_parameters, _messageLength) + BUDGET;

    // Esperar `TX_DELAY` microsegundos do budget do transmissor
    // para garantir que os receptores começam a receber antes do
    // transmissor começar a enviar.
    timerStart(totalBudget + (_role == kTx ? TX_DELAY : 0), timedLoop);
    timerResync(totalBudget);

    _begin = timerTime();
    _protoState = kRunning;
}

const char* _resultMessage = "(...)";

/// Variáveis de timing, usadas para desenhar a barra de progresso na UI.
int64_t _nextAlarm = 0;
uint64_t _currentPeriod = 1;
int64_t _operationBegin = 0;
int64_t _operationEnd = 0;
int64_t _timedEnd = 0;

/// Setada com "true" quando a função `timedLoop` estourar seu budget máximo de tempo.
bool _timerLatch = false;

/// Executa o experimento principal para o receptor e transmissor.
///
/// A função deve iniciar apenas quando o timeout do timer sincronizado acabar.
/// Desta forma, o receptor e o transmissor se mantém sincronizados durante
/// o experimento.
void timedLoop() {
    _nextAlarm = timerNextTick();
    _currentPeriod = timerPeriod();

    const uint64_t toa = radioTransmitTime(_parameters, _messageLength);
    radio_error_t error = kNone;

    _operationBegin = timerTime();
    if (_role == kRx) {
        // Receber mensagem e imprimir status da operação no Serial
        uint8_t message[_messageLength];
        uint8_t length = _messageLength;

        // O timeout do receptor é interrompido após o receptor detectar o
        // header completo de um pacote. Por isso, o timeout usado no
        // experimento é o time on air de um pacote de tamanho 0 (simulando o
        // tempo de transmissão do header) + 5% desse tempo para compensar os
        // erros de timing do receptor (leia `RX_TIMING_ERROR` acima)
        error = radioRecv(message, &length,
                          TX_DELAY - RX_TIMING_ERROR +
                              radioTransmitTime(_parameters, 0) * 1.2);
        _operationEnd = timerTime();

        logPrintf(
            "%llu,%u,%u,%hhd dB,SF%hhu,CR%hhu,%f kHz,%hi dBm,%f dB,%.*s,%u\n",
            timerTime() - _begin, _currentTest, _messageIndex,
            _parameters.power, _parameters.sf, _parameters.cr,
            _parameters.bandwidth, radioRSSI(), radioSNR(), length,
            (size_t)message, error);
    } else if (_role == kTx) {
        // Enviar mensagem e imprimir status da transmissão no Serial
        error = radioSend(_message, _messageLength);
        _operationEnd = timerTime();

        logPrintf("%llu,%u,%u,%hhu dB,SF%hhu,CR%hhu,%f kHz,%u\n",
                  timerTime() - _begin, _currentTest, _messageIndex,
                  _parameters.power, _parameters.sf, _parameters.cr,
                  _parameters.bandwidth, error);
    }

    // Atualizar mensagem no display com erro apropriado
    switch (error) {
    case kNone:
        _resultMessage = "(ok)";
        _testsOk++;
        break;
    case kTimeout:
        _resultMessage = "(t.out)";
        _testsLost++;
        break;
    case kHeader:
    case kCrc:
        _resultMessage = "(crc/h)";
        _testsCorrupt++;
        break;
    case kUnknown:
        _resultMessage = "(err)";
        _testsLost++;
        break;
    }

    _messageIndex++;

    // Iniciar nova linha de testes após `MESSAGES_PER_TEST` mensagens
    if (_messageIndex == MESSAGES_PER_TEST) {
        _messageIndex = 0;
        _currentTest++;

        // Marcar o timer para resincronização e iniciar próximo teste
        _protoState = updateTestParameters() ? kFinished : kRunning;
        timerResync(radioTransmitTime(_parameters, _messageLength) + BUDGET);

        // Finalizar log do receptor após o último teste
        if (_protoState == kFinished) {
            logClose();
            timerStop();
        }
    }

    _timedEnd = timerTime();
    _timerLatch |= (_timedEnd - _operationBegin) > _currentPeriod;

    // Imprimir informações de timing para debugging
    logDebugPrintf(
        "%lld,%lld,%lld / lora_excess: %lld, budget_used: %lld, toa: %llu, "
        "period: %llu, alarm: %lld\n",
        _operationBegin, _operationEnd, _timedEnd,
        (_operationEnd - _operationBegin) - toa, (_timedEnd - _operationEnd),
        toa, _currentPeriod, _nextAlarm);
}

/// Executa quando todos os testes forem finalizados.
void finishedLoop() {
    uiClear();
    drawTestOverlay(NULL, false, false);
    uiAlign(kCenter);

    uiText(0, 15, "Testes finalizados!");
    uiFinish();
}

void loop() {
    halLoop();

    // Desenhar tela de seleção de cargo antes de iniciar o protocolo
    if (_role == kUnspecified) {
        const uint32_t rate = 1000 / 20;
        uint32_t start = millis();

        uiLoop();
        uiClear();
        drawTestOverlay("Selecione o cargo", false, false);
        uiAlign(kCenter);

        if (uiButton(-30, 30, "Transmissor"))
            _role = kTx;

        if (uiButton(30, 30, "Receptor"))
            _role = kRx;

        uiFinish();

        // Esperar o suficiente para uma taxa de 20FPS
        uint32_t time = millis() - start;
        delay(time >= rate ? 0 : rate - time);
        return;
    }

    if (_protoState == kUninitialized)
        return syncLoop();
    else if (_protoState == kRunning) {
        static uint32_t _renderFrame = 0;

        // Renderizar a cada 4 execuções do `loop`
        if (!radioBusy() && _renderFrame++ == 12) {
            _renderFrame = 0;

            // Desenhar interface, exibindo o RSSI e SNR apenas para o receptor
            uiLoop();

            // Parar o experimento caso o usuário aperte o botão durante a transmissão.
            if (uiEmergencyStop()) {
                Serial.println("Parando...");
                logClose();
                timerStop();
                _protoState = kFinished;
                return;
            }

            uiClear();
            drawTestOverlay(NULL, _role == kRx, false);
            uiAlign(kLeft);

            // Imprimir uma barra de progresso com as atividades executadas
            // entre os timeouts do timer.
            const int16_t barWidth = 128 - 10;
            uiRect(5, 15, barWidth, 14, kStroke, kWhite);

            // Imprimir o restante do budget até o próximo timer
            int64_t now = timerTime();
            int64_t nextTick = timerNextTick();
            uint64_t period = timerPeriod();

            double pctOpBegin = (_begin - _begin) / (double)(_currentPeriod);
            double pctOpEnd =
                (_operationEnd - _operationBegin) / (double)(_currentPeriod);
            double pctFunctionEnd =
                (_timedEnd - _operationBegin) / (double)(_currentPeriod);
            double pctNow =
                (timerTime() - _operationBegin) / (double)(_currentPeriod);

            // Previne barras de progresso inválidas
            pctOpBegin = constrain(pctOpBegin, 0.0, 1.0);
            pctOpEnd = constrain(pctOpEnd, 0.0, 1.0);
            pctFunctionEnd = constrain(pctFunctionEnd, 0.0, 1.0);
            pctNow = constrain(pctNow, 0.0, 1.0);

            // Barra branca expressando o tempo total consumido pela
            // função `timedLoop`
            uiRect(5 + pctOpBegin * barWidth, 15,
                   (pctFunctionEnd - pctOpBegin) * barWidth, 14, kFill, kWhite);

            // Barra semi-branca expressando o tempo total consumido esperando.
            uiRect(5 + pctFunctionEnd * barWidth, 15,
                   (pctNow - pctFunctionEnd) * barWidth, 14, kDither, kWhite);

            uiText(10, 15, _timerLatch ? "(desync!)" : _resultMessage);

            uiAlign(kRight);

            char buffer[256];
            printMinimalPeriod(buffer, 256, period - (nextTick - now), period);

            uiText(10, 15, buffer, kBlack);
            uiFinish();
        }

        yield();
    } else if (_protoState == kFinished)
        return finishedLoop();
}
