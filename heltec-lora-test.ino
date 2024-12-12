#include "hal/buttons.hh"
#include "hal/lib.hh"
#include "hal/log.hh"
#include "hal/radio.hh"
#include "hal/timer.hh"
#include "hal/ui.hh"

/// A quantidade de tempo, em microsegundos, disponibilizado para qualquer
/// processamento além do experimento principal
#define BUDGET 240000

/// O delay aguardado antes de iniciar o relógio do transmissor
#define TX_DELAY 80000

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
    .power = 23,
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
    SPI.begin(SCK, MISO, MOSI, SS);

    halInit();
    radioInit();
    timerInit();
    uiSetup();

    // Tentar inicializar o logger no cartão SD
    _hasSD = logInit("/log.txt");
}

/// Atualiza os parâmetros atuais do teste de acordo com o índice do teste
/// atual. Retorna `true` se o índice do teste era inválido.
bool updateTestParameters() {
    const uint8_t power[] = { 5, 14, 23 };
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
        const uint64_t toa = radioTransmitTime(_messageLength);
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

    // Desenhar interface antes da operação LoRa
    uiClear();
    drawTestOverlay(NULL, _role == kRx, true);

    uiAlign(kCenter);
    uiText(0, 15, _role == kRx ? "Esperando sync..." : "Enviando sync...");
    uiFinish();

    // Inicializar parâmetros padrão de transmissão
    radioSetParameters(_parameters);

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

    // Esperar `TX_DELAY` microsegundos do budget do transmissor
    // para garantir que os receptores começam a receber antes do
    // transmissor começar a enviar.
    if (_role == kTx) {
        uint32_t start = micros();
        while (micros() - start < TX_DELAY);
    }

    // Atualizar parâmetros do teste e iniciar o experimento.
    _currentTest = message[0];
    updateTestParameters();

    timerStart(radioTransmitTime(_messageLength) + BUDGET);
    _begin = timerTime();
    _protoState = kRunning;
}

/// Executa o experimento principal para o receptor e transmissor.
///
/// A função deve iniciar apenas quando o timeout do timer sincronizado acabar.
/// Desta forma, o receptor e o transmissor se mantém sincronizados durante
/// o experimento.
void timedLoop() {
    static const char* _waitMessage = "(...)";
    static uint32_t _renderFrame = 0;

    const bool isTimerDone = timerFlag();

    // Desenhar interface, exibindo o RSSI e SNR apenas para o receptor
    uiLoop();
    uiClear();
    drawTestOverlay(NULL, _role == kRx, false);
    uiAlign(kCenter);

    // Renderizar interface do budget uma vez após o tick do timer, e
    // a cada oitava execução durante a espera do timer.
    if (isTimerDone || _renderFrame++ == 8) {
        _renderFrame = 0;

        uiAlign(kLeft);

        // Mostrar "(...)" antes de realizar uma operação LoRa
        if (isTimerDone)
            _waitMessage = "(...)";

        uiText(5, 15, _waitMessage);

        // Imprimir o restante do budget até o próximo timer
        int64_t now = timerTime();
        int64_t nextTick = timerNextTick();
        uint64_t period = timerPeriod();

        uiAlign(kRight);

        char buffer[256];
        printMinimalPeriod(buffer, 256, period - (nextTick - now), period);

        uiText(5, 15, buffer);
        uiFinish();
    }

    // Aguardar o próximo timeout do timer.
    if (!isTimerDone)
        return;

    radio_error_t error = kNone;

    if (_role == kRx) {
        const uint64_t toa = radioTransmitTime(_messageLength);

        // Receber mensagem e imprimir status da operação no Serial
        uint8_t message[_messageLength];
        uint8_t length = _messageLength;

        // O receptor espera o `TX_DELAY` + 100ms para compensar quaisquer delay
        // vindo da biblioteca RadioLib, somado com possíveis inconsistências no
        // timing devido à execução de código do display e cartão SD entre as
        // operações LoRa.
        //
        // Note que essas inconsistências não implicam na dessincronização dos
        // timers, visto que a resincronização é feita no callback do timer para
        // garantir o mínimo erro.
        error = radioRecv(message, &length, toa + TX_DELAY + 100000);

        logPrintf("%llu,%u,%u,%hhd dB,SF%hhu,CR%hhu,%f kHz,%hi dBm,%f dB,%u\n",
                  timerTime() - _begin, _currentTest, _messageIndex,
                  _parameters.power, _parameters.sf, _parameters.cr,
                  _parameters.bandwidth, radioRSSI(), radioSNR(), error);
    } else if (_role == kTx) {
        // Enviar mensagem e imprimir status da transmissão no Serial
        error = radioSend(_message, _messageLength);

        logPrintf("%llu,%u,%u,%hhu dB,SF%hhu,CR%hhu,%f kHz,%u\n",
                  timerTime() - _begin, _currentTest, _messageIndex,
                  _parameters.power, _parameters.sf, _parameters.cr,
                  _parameters.bandwidth, error);
    }

    // Atualizar mensagem no display com erro apropriado
    switch (error) {
    case kNone:
        _waitMessage = "(ok)";
        _testsOk++;
        break;
    case kTimeout:
        _waitMessage = "(t.out)";
        _testsLost++;
        break;
    case kHeader:
    case kCrc:
        _waitMessage = "(crc/h)";
        _testsCorrupt++;
        break;
    case kUnknown:
        _waitMessage = "(err)";
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
        timerResync(radioTransmitTime(_messageLength) + BUDGET);

        // Finalizar log do receptor após o último teste
        if (_protoState == kFinished && _role == kRx)
            logClose();
    }
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
    else if (_protoState == kRunning)
        return timedLoop();
    else
        return finishedLoop();
}
