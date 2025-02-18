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
#define RX_TIMING_ERROR 55000

/// O delay aguardado antes de iniciar o relógio do transmissor
#define TX_DELAY (RX_TIMING_ERROR + 8000)

/// A quantidade de tempo, em microsegundos, disponibilizado para qualquer
/// processamento além do experimento principal.
#define BUDGET (TX_DELAY + 80000)

/// A quantidade de tempo, em microsegundos, reservado apenas para escrita no
/// cartão SD.
#define SD_BUDGET 2000000

/// Define a quantidade de mensagens enviadas para cada combinação de
/// parâmetros.
#define MESSAGES_PER_TEST 50

/// A mensagem enviada durante o experimento.
uint8_t _message[] = "XMensagem";
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

    logPrintf("Modulo iniciado\n");
}

/// Atualiza os parâmetros atuais do teste de acordo com o índice do teste
/// atual. Retorna `true` se o índice do teste era inválido.
bool updateTestParameters() {
    const uint8_t power[] = { 5, 10, 17, 22 };
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
    kPing,
};

protocol_state_t _protoState = kUninitialized;
uint32_t _messageIndex = 0;
uint64_t _begin = 0;

/// Executa após um cargo ser selecionado no menu.
void syncLoop() {
    // Tentar inicializar o logger no cartão SD
    _hasSD = logInit("/log.txt");

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
    timerResync(totalBudget, timedLoop);

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

/// Setada com "true" quando a função `timedLoop` estourar seu budget máximo de
/// tempo.
bool _timerLatch = false;

struct msg_result_t {
    int64_t startTime;
    int64_t loraEndTime;
    int64_t endTime;
    int64_t period;
    int64_t nextAlarm;
    uint8_t message[_messageLength];
    uint8_t length;
    int16_t rssi;
    float snr;
    radio_error_t error;
};

/// Armazena o resultado de cada mensagem em um teste.
msg_result_t _results[MESSAGES_PER_TEST];

/// Executa o experimento principal para o receptor e transmissor.
///
/// A função deve iniciar apenas quando o timeout do timer sincronizado acabar.
/// Desta forma, o receptor e o transmissor se mantém sincronizados durante
/// o experimento.
void timedLoop() {
    _nextAlarm = timerNextTick();
    _currentPeriod = timerPeriod();

    const uint64_t toa = radioTransmitTime(_parameters, _messageLength);
    const uint64_t totalBudget = toa + BUDGET;

    radio_error_t error = kNone;

    _operationBegin = timerTime();

    // Armazenar dados iniciais da mensagem atual
    msg_result_t& result = _results[_messageIndex];
    result.length = _messageLength;
    result.startTime = _operationBegin;
    result.nextAlarm = _nextAlarm;
    result.period = _currentPeriod;
    _message[0] = _messageIndex;

    if (_role == kRx) {
        // Usamos o ToA do pacote completo como o timeout para a recepção.
        // Note que o timeout do receptor é interrompido após o receptor
        // detectar o header completo de um pacote, logo, caso um pacote seja
        // detectado no final deste timeout, o processo de recepção de um pacote
        // pode ultrapassar o budget de tempo máximo pra função `timedLoop`.
        error = radioRecv(
            result.message, &result.length,
            radioTransmitTime(_parameters, _messageLength) + TX_DELAY);
    } else if (_role == kTx) {
        // Enviar mensagem e imprimir status da transmissão no Serial
        error = radioSend(_message, _messageLength);
    }

    _operationEnd = timerTime();
    result.loraEndTime = _operationEnd;

    // Armazenar resultados do teste na memória
    result.error = error;
    result.rssi = radioRSSI();
    result.snr = radioSNR();

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

    // Iniciar período de escrita do cartão SD após a mensagem final do
    // parâmetro atual.
    if (_messageIndex == MESSAGES_PER_TEST) {
        timerResync(SD_BUDGET, sdLoop);
    }

    _timedEnd = timerTime();
    _timerLatch |= (_timedEnd - _operationBegin) > _currentPeriod;
    result.endTime = _timedEnd;

    // Imprimir informações de timing para debugging
    logDebugPrintf(
        "%lld,%lld,%lld / lora_excess: %lld, budget_used: %lld, toa: %llu, "
        "period: %llu, alarm: %lld\n",
        _operationBegin, _operationEnd, _timedEnd,
        (_operationEnd - _operationBegin) - toa, (_timedEnd - _operationEnd),
        toa, _currentPeriod, _nextAlarm);
}

/// Armazena os dados no vetor `_results` no cartão SD.
void sdLoop() {
    _nextAlarm = timerNextTick();
    _currentPeriod = timerPeriod();
    _operationBegin = timerTime();

    _resultMessage = "(escrevendo...)";

    // Imprimir rótulo dos dados na primeira combinação
    if (_currentTest == 0) {
        if (_role == kRx) {
            logPrintf(
                "Start Time,Rx End Time,End Time,Period,Alarm,Parameter "
                "Index,Message Index,Tx Power "
                "(dBm),Spreading Factor,Coding Rate,Bandwidth (kHz),RSSI "
                "(dBm),SNR (dB),Status,Message\n");
        } else {
            logPrintf(
                "Start Time,Tx End Time,End Time,Period,Alarm,Parameter "
                "Index,Message Index,Tx Power "
                "(dBm),Spreading Factor,Coding Rate,Bandwidth (kHz),Status\n");
        }
    }

    for (size_t i = 0; i < MESSAGES_PER_TEST; i++) {
        const msg_result_t& result = _results[i];

        if (_role == kRx) {
            // Imprimir todas as informações para resultados do receptor
            logPrintf(
                "%llu,%llu,%llu,%llu,%llu,%u,%u,%hhd,%hhu,%hhu,%f,%hi,%f,%u,",
                result.startTime, result.loraEndTime, result.endTime,
                result.period, result.nextAlarm, _currentTest, i,
                _parameters.power, _parameters.sf, _parameters.cr,
                _parameters.bandwidth, result.rssi, result.snr, result.error);

            for (size_t i = 0; i < result.length; i++) {
                logPrintf("%02x", result.message[i]);
            }

            logPrintf("\n");
        } else if (_role == kTx) {
            // Imprimir poucas informações para o transmissor (não possui
            // RSSI/SNR)
            logPrintf("%llu,%llu,%llu,%llu,%llu,%u,%u,%hhu,%hhu,%hhu,%f,%u\n",
                      result.startTime, result.loraEndTime, result.endTime,
                      result.period, result.nextAlarm, _currentTest, i,
                      _parameters.power, _parameters.sf, _parameters.cr,
                      _parameters.bandwidth, result.error);
        }
    }

    // Resetar vetor de resultados
    memset(_results, 0, sizeof(_results));

    logFlush();

    // Resetar parâmetros de teste
    _messageIndex = 0;
    _currentTest++;

    // Marcar o timer para resincronização e iniciar próximo teste
    _protoState = updateTestParameters() ? kFinished : kRunning;
    timerResync(radioTransmitTime(_parameters, _messageLength) + BUDGET,
                timedLoop);

    // Finalizar log do receptor após o último teste
    if (_protoState == kFinished) {
        logClose();
        timerStop();
    }

    _operationEnd = _timedEnd = timerTime();
    _timerLatch |= (_timedEnd - _operationBegin) > _currentPeriod;

    // Imprimir informações de timing para debugging
    logDebugPrintf(
        "%lld,%lld,%lld / budget_used: %lld, period: %llu, alarm: %lld\n",
        _operationBegin, _operationEnd, _timedEnd,
        (_timedEnd - _operationBegin), _currentPeriod, _nextAlarm);
}

/// Executa quando todos os testes forem finalizados.
void finishedLoop() {
    uiClear();
    drawTestOverlay(NULL, false, false);
    uiAlign(kCenter);

    uiText(0, 15, "Testes finalizados!");
    uiFinish();
}

/// Executa um simples loop, em que um receptor contínuamente recebe
/// mensagens de um transmissor no mesmo parâmetro.
void pingLoop() {
    static radio_error_t error = kUnknown;

    uiLoop();

    // Inicializar parâmetros de ping
    radioSetParameters({
        .power = 22,
        .frequency = 915.0,
        .preambleLength = 8,
        .bandwidth = 62.5,
        .sf = 12,
        .cr = 8,
        .crc = true,
        .invertIq = false,
        .boostedRxGain = false,
        .packetLength = 0,
        .syncWord = 0xEE,
    });

    // Desenhar interface antes da operação LoRa
    uiClear();
    drawTestOverlay(NULL, _role == kRx, true);

    const char* statusText;
    switch (error) {
    case kNone:
        statusText = "(ok)";
        break;
    case kTimeout:
        statusText = "(t.out)";
        break;
    case kHeader:
    case kCrc:
        statusText = "(crc/h)";
        break;
    default:
        statusText = "(n/a)";
        break;
    }

    // Desenhar status do ping
    char buffer[256] = { 0 };
    snprintf(buffer, 256, "%s %s",
             _role == kRx ? "Esperando ping..." : "Enviando ping...",
             statusText);

    uiAlign(kCenter);
    uiText(0, 15, buffer);
    uiFinish();

    uint8_t message[] = { _currentTest };
    uint8_t length = sizeof(message) / sizeof(uint8_t);

    // Enviar/receber ping dependendo do cargo selecionado
    if (_role == kRx) {
        error = radioRecv(message, &length, 5000000);
        _currentTest = message[0];
    } else if (_role == kTx) {
        _currentTest++;
        error = radioSend(message, length);
        delay(1000);
    }
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

        if (uiButton(-30, 20, "Transmissor"))
            _role = kTx;

        if (uiButton(30, 20, "Receptor"))
            _role = kRx;

        if (uiButton(-30, 30, "Tx. Ping")) {
            _protoState = kPing;
            _role = kTx;
        }

        if (uiButton(30, 30, "Rx. Ping")) {
            _protoState = kPing;
            _role = kRx;
        }

        uiFinish();

        // Esperar o suficiente para uma taxa de 20FPS
        uint32_t time = millis() - start;
        delay(time >= rate ? 0 : rate - time);
        return;
    }

    if (_protoState == kUninitialized)
        return syncLoop();
    else if (_protoState == kPing)
        return pingLoop();
    else if (_protoState == kRunning) {
        static uint32_t _renderFrame = 0;

        // Renderizar a cada 4 execuções do `loop`
        if (!radioBusy() && _renderFrame++ == 12) {
            _renderFrame = 0;

            // Desenhar interface, exibindo o RSSI e SNR apenas para o
            // receptor
            uiLoop();

            // Parar o experimento caso o usuário aperte o botão durante
            // a transmissão.
            if (uiButtonState()) {
                Serial.println("Parando...");
                logClose();
                timerStop();
                _protoState = kFinished;
                return;
            }

            uiClear();
            drawTestOverlay(NULL, _role == kRx, false);
            uiAlign(kLeft);

            // Imprimir uma barra de progresso com as atividades
            // executadas entre os timeouts do timer.
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

            // Barra semi-transparente expressando o tempo total
            // consumido esperando.
            uiRect(5 + pctFunctionEnd * barWidth, 15,
                   (pctNow - pctFunctionEnd) * barWidth, 14, kDither, kWhite);

            uiText(10, 15, _timerLatch ? "(desync!)" : _resultMessage, kBlack);

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
