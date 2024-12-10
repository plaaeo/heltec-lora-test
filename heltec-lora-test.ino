#include "hal/buttons.hh"
#include "hal/lib.hh"
#include "hal/log.hh"
#include "hal/ui.hh"
#include "hal/radio.hh"

// Determina o índice do teste atual. Usado para calcular o 
// SF, CR, largura de banda e potência de transmissão do teste.
uint32_t _currentTest = 0;

// Define os parâmetros atuais da transmissão.
radio_parameters_t _parameters = radio_parameters_t {
    .power = 5,
    .frequency = 915.0,
    .preambleLength = 8,
    .bandwidth = 125.0,
    .sf = 7,
    .cr = 5,
    .crc = true,
    .invertIq = false,
    .boostedRxGain = false,
    .packetLength = 0,
};

// Define o cargo atual deste aparelho.
enum role_t {
    kUnspecified,
    kTx,
    kRx,
} _role = kUnspecified;

void setup() {
    Serial.begin(115200);
    
    halInit();
    radioInit();
    uiSetup();
}

// Atualiza os parâmetros atuais do teste de acordo com o índice do teste atual.
// Retorna `true` se o índice do teste era inválido.
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
    radioSetParameters(_parameters);
    return wasInvalidTest;
}

// Desenha o overlay com os parâmetros e resultados da transmissão atual.
void drawTestOverlay(const char* title, bool drawQuality) {    
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

    uiAlign(kRight);
    
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

void txLoop() {
    static uint16_t _name = esp_random() & 0xFFFF;
    
    
}

void rxLoop() {
    static uint16_t _name = esp_random() & 0xFFFF;
    
    // Determina a tela atual
    static enum {
        kBroadcasting,
    } _screen = kBroadcasting;

    switch (_screen) {
    case kBroadcasting:
        drawTestOverlay("Aguardando inicio", false);
        break;
    }
}

void loop() {
    const uint32_t rate = 1000 / 20;
    uint32_t start = millis();

    halLoop();
    uiLoop();

    updateTestParameters();
    
    // Determinar qual tela desenhar dependendo do cargo
    switch (_role) {
    case kUnspecified:
        drawTestOverlay("Selecione o cargo", false);
        uiAlign(kCenter);
        
        if (uiButton(-30, 30, "Transmissor"))
            _role = kTx;

        if (uiButton(30, 30, "Receptor"))
            _role = kRx;

        break;
    case kTx:
        txLoop();
        break;
    case kRx:
        rxLoop();
        break;
    }

    uiFinish();

    // Esperar o suficiente para uma taxa de 20FPS
    uint32_t time = millis() - start;
    delay(time >= rate ? 0 : rate - time);
}
