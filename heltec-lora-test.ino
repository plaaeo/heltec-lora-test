#include "hal/buttons.hh"
#include "hal/lib.hh"
#include "hal/log.hh"
#include "hal/radio.hh"
#include "hal/timer.hh"
#include "hal/ui.hh"

/// A quantidade de tempo, em microsegundos, disponibilizado para qualquer
/// processamento além do experimento principal
#define BUDGET 300000

/// O delay aguardado antes de iniciar o relógio do transmissor
#define TX_DELAY 80000

/// Define a quantidade de mensagens enviadas para cada combinação de
/// parâmetros.
#define MESSAGES_PER_TEST 1

/// A mensagem enviada durante o experimento.
const uint8_t _message[] = "oii";
const uint8_t _messageLength = sizeof(_message) / sizeof(char);

/// Determina o índice do teste atual. Usado para calcular o
/// SF, CR, largura de banda e potência de transmissão do teste.
uint32_t _currentTest = 0;

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

/// Desenha o overlay com os parâmetros e resultados da transmissão atual.
void drawTestOverlay(const char* title, bool drawQuality, bool drawToA) {
    char buffer[1024];

    // Desenhar textos estáticos
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
        // Desenhar tempo de transmissão
        const uint64_t toa = radioTransmitTime(_messageLength);
        uiText(10, paramsY - 10, "ToA");

        uiAlign(kRight);        
        snprintf(buffer, 1024, "%llu us", toa);
        uiText(10, paramsY - 10, buffer);
    } else {
        uiAlign(kRight);
    }

    // Escrever valor do coding rate atual
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

void txLoop() {
    if (_protoState == kRunning && !timerFlag())
        return;

    // O budget de tempo total para transmissão
    const uint64_t totalBudget = radioTransmitTime(_messageLength) + BUDGET;
    const uint64_t budgetStart = timerTime();

    uiLoop();
    uiClear();
    drawTestOverlay(NULL, false, true);
    uiAlign(kCenter);

    switch (_protoState) {
    case kUninitialized: {
        uiText(0, 15, "Sincronizando...");
        uiFinish();

        // Inicializar parâmetros padrão de transmissão
        radioSetParameters(_parameters);

        uint8_t message[] = { _currentTest };
        const auto error = radioSend(message, 1);

        // Tentar novamente caso a transmissão tenha falhado.
        if (error != kNone) {
            Serial.print("[tx] Erro ");
            Serial.println(error);
            delay(1000);
            return;
        }

        // Esperar 80ms do budget para garantir que os receptores
        // começam a receber antes do transmissor começar a enviar.
        uint32_t start = micros();
        while (micros() - start < TX_DELAY);

        // Atualizar parâmetros do teste e iniciar o experimento.
        updateTestParameters();

        timerStart(radioTransmitTime(_messageLength) + BUDGET);
        _begin = timerTime();
        _protoState = kRunning;
        break;
    }
    case kRunning: {
        const uint64_t toa = radioTransmitTime(_messageLength);

        char buffer[1024];
        snprintf(buffer, 1024, "Transmitindo #%u...", _messageIndex + 1);
        uiText(0, 15, buffer);

        uiFinish();

        // Enviar mensagem e imprimir status da transmissão no Serial
        const auto error = radioSend(_message, _messageLength);

        Serial.print(timerTime() - _begin);
        Serial.print(",");
        Serial.print(_currentTest);
        Serial.print(",");
        Serial.print(_messageIndex);
        Serial.print(",SF");
        Serial.print(_parameters.sf);
        Serial.print(",CR");
        Serial.print(_parameters.cr);
        Serial.print(",");
        Serial.print(_parameters.bandwidth);
        Serial.print("kHz,");
        Serial.println(error);

        _messageIndex++;

        // Iniciar nova linha de testes após `MESSAGES_PER_TEST` mensagens
        if (_messageIndex == MESSAGES_PER_TEST) {
            _messageIndex = 0;
            _currentTest++;

            // Marcar o timer para resincronização e iniciar próximo teste
            _protoState = updateTestParameters() ? kFinished : kRunning;
            timerResync(radioTransmitTime(_messageLength) + BUDGET);
        }
        
        // Atualizar interface após receber
        uiClear();
        drawTestOverlay(NULL, true, true);
        uiAlign(kCenter);

        snprintf(buffer, 1024, "Transmitido (e%u) #%u", error, _messageIndex);
        uiText(0, 15, buffer);

        uiFinish();
        break;
    }
    case kFinished: {
        uiText(0, 15, "Testes finalizados!");
        uiFinish();
        break;
    }
    }

    const uint64_t budgetEnd = timerTime();

    Serial.print(budgetEnd-budgetStart);
    Serial.print('/');
    Serial.print(totalBudget);
    Serial.println("ms used budget");
}

void rxLoop() {
    if (_protoState == kRunning && !timerFlag())
        return;

    // O budget de tempo total para transmissão
    const uint64_t totalBudget = radioTransmitTime(_messageLength) + BUDGET;
    const uint64_t budgetStart = timerTime();

    uiLoop();
    uiClear();
    drawTestOverlay(NULL, true, true);
    uiAlign(kCenter);

    switch (_protoState) {
    case kUninitialized: {
        uiText(0, 15, "Aguardando sincronização...");
        uiFinish();

        // Inicializar parâmetros padrão de transmissão
        radioSetParameters(_parameters);

        uint8_t message[] = { _currentTest };
        uint8_t length = 1;
        const auto error = radioRecv(message, &length);

        // Tentar novamente caso a operação tenha falhado.
        if (error != kNone) {
            Serial.print("[rx] Erro ");
            Serial.println(error);
            delay(1000);
            return;
        }

        // Iniciar datalogger
        logInit("/log.txt");

        // Atualizar parâmetros do teste e iniciar o experimento.
        _currentTest = message[0];
        updateTestParameters();

        timerStart(radioTransmitTime(_messageLength) + BUDGET);
        _begin = timerTime();
        _protoState = kRunning;
        break;
    }
    case kRunning: {
        const uint64_t toa = radioTransmitTime(_messageLength);

        char buffer[1024];
        snprintf(buffer, 1024, "Recebendo #%u...", _messageIndex + 1);
        uiText(0, 15, buffer);

        uiFinish();

        // Receber mensagem e imprimir status da operação no Serial
        uint8_t message[10];
        uint8_t length = 10;
        
        const auto error = radioRecv(message, &length, toa + (4 * TX_DELAY / 2));

        logPrintf(
            "%llu,%u,%u,SF%hhu,CR%hhu,%f kHz,%hi dBm,%f dB,%u\n",
            timerTime() - _begin,
            _currentTest,
            _messageIndex,
            _parameters.sf,
            _parameters.cr,
            _parameters.bandwidth,
            radioRSSI(),
            radioSNR(),
            error
        );

        _messageIndex++;

        // Iniciar nova linha de testes após `MESSAGES_PER_TEST` mensagens
        if (_messageIndex == MESSAGES_PER_TEST) {
            _messageIndex = 0;
            _currentTest++;

            // Marcar o timer para resincronização e iniciar próximo teste
            _protoState = updateTestParameters() ? kFinished : kRunning;
            timerResync(radioTransmitTime(_messageLength) + BUDGET);

            // Terminar log
            if (_protoState == kFinished) {
                logClose();
            }
        }

        // Atualizar interface após receber
        uiClear();
        drawTestOverlay(NULL, true, true);
        uiAlign(kCenter);

        snprintf(buffer, 1024, "Recebido (e%u) #%u", error, _messageIndex);
        uiText(0, 15, buffer);

        uiFinish();
        break;
    }
    case kFinished: {
        uiText(0, 15, "Testes finalizados!");
        uiFinish();
        break;
    }
    }

    const uint64_t budgetEnd = timerTime();

    Serial.print(budgetEnd-budgetStart);
    Serial.print('/');
    Serial.print(totalBudget);
    Serial.println("ms used budget");
}

void loop() {
    halLoop();

    // Determinar qual tela desenhar dependendo do cargo
    switch (_role) {
    case kUnspecified: {
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
        break;
    }
    case kTx:
        txLoop();
        break;
    case kRx:
        rxLoop();
        break;
    }
}
