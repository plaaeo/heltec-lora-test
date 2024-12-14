/**
 * hal/radio.hh
 *
 * Abstração para a biblioteca do radiotransmissor LoRa utilizado
 * no experimento.
 */

#pragma once
#include <SPI.h>
#include <SX126x.h>
#include <stdint.h>

SPIClass _radioSPI = SPIClass(HSPI);
SX126x _radio;

enum radio_error_t {
    kNone,

    /// A mensagem recebida chegou corrompida.
    kCrc,

    /// A mensagem recebida veio com o header inválido.
    kHeader,

    /// Houve um timeout na operação desejada.
    kTimeout,

    /// Houve um erro de tipo inesperado.
    kUnknown,
};

/// Converte os erros da LoRa-RF em `radio_error_t`
radio_error_t _radioConvertError(uint8_t e) {
    switch (e) {
    case LORA_STATUS_DEFAULT:
    case LORA_STATUS_TX_DONE:
    case LORA_STATUS_RX_DONE:
    case LORA_STATUS_CAD_DONE:
        return kNone;
    case LORA_STATUS_CRC_ERR:
    case LORA_STATUS_HEADER_ERR:
        return kCrc;
    case LORA_STATUS_TX_TIMEOUT:
    case LORA_STATUS_RX_TIMEOUT:
        return kTimeout;
    default:
        // Sinalizar erro desconhecido no Serial
        if (Serial) {
            Serial.println("[hal/radio.hh] Erro desconhecido (" + String(e) +
                           ")");
        }

        return kUnknown;
    }
}

/// Define todos os parâmetros modificáveis do radiotransmissor.
struct radio_parameters_t {
    /// A potência de transmissão, de -9 a 22dBm.
    int8_t power;

    /// A frequência da transmissão, em MHz.
    float frequency;
    uint16_t preambleLength;

    /// A largura de banda, em kHz.
    float bandwidth;

    /// O fator de espalhamento, ou `spreading factor`, de 7 a 12
    uint8_t sf;

    /// O denominador da taxa de codificação, ou `coding rate`, de 4 a 8.
    uint8_t cr;

    /// Liga ou desliga o código de detecção de erro incluido em cada
    /// mensagem.
    bool crc;
    bool invertIq;

    /// Determina a sensibilidade extra do receptor, `false` para o modo
    /// de economia de energia.
    bool boostedRxGain;

    /// Comprimento padrão do pacote. Caso maior que 0, o modo de header
    /// implícito é ligado.
    uint32_t packetLength;

    /// O byte usado como "endereço" de cada pacote.
    uint8_t syncWord;
};

/// Inicializa o radio LoRa.
/// Retorna `true` caso o radio tenha inicializado com sucesso.
bool radioInit() {
    _radioSPI.begin(SCK, MISO, MOSI, SS);
    _radioSPI.setFrequency(SX126X_SPI_FREQUENCY);

    _radio.setSPI(_radioSPI, SX126X_SPI_FREQUENCY);
    if (!_radio.begin(SS, RST_LoRa, BUSY_LoRa, DIO0, -1, -1))
        return false;

    _radio.setDio3TcxoCtrl(SX126X_DIO3_OUTPUT_1_8, SX126X_TCXO_DELAY_10);
    _radio.setRegulator(SX126X_REGULATOR_LDO);
    _radio.setFrequency(915000000);
    _radio.standby(SX126X_STANDBY_XOSC);
    _radio.setFallbackMode(SX126X_FALLBACK_STDBY_XOSC);
    return true;
}

/// Calcula o timeout para o SX1262, dado o timeout em microsegundos.
uint64_t radioCalculateTimeout(uint64_t timeout) {
    return timeout / 15.625;
}

/// Envia um pacote e aguarda ele terminar de ser enviado,
/// retornando o resultado da transmissão.
radio_error_t radioSend(const uint8_t* message, uint8_t size,
                        uint64_t timeout = 0) {
    _radio.beginPacket();

    _radio.write((uint8_t*)message, size);

    // Falha apenas se for chamado enquanto outra mensagem ainda estiver sendo
    // enviada, logo, não deve acontecer.
    timeout = radioCalculateTimeout(timeout);
    if (!_radio.endPacket(timeout >> 6))
        return kUnknown;

    // Aguarda o pacote ser enviado completamente, ou demore demais e cause um
    // timeout.
    if (!_radio.wait())
        return kTimeout;

    uint8_t status = _radio.status();
    return _radioConvertError(status);
}

/// Aguarda até que um pacote seja recebido, ou ocorra timeout (passado em
/// microsegundos), retornando o resultado da operação. Recebe em `*length` o
/// tamanho do buffer de destino e, após a operação, armazezna o tamanho do
/// pacote lido.
radio_error_t radioRecv(uint8_t* dest, uint8_t* length, uint64_t timeout = 0) {
    timeout = radioCalculateTimeout(timeout);
    _radio.request(timeout >> 6);
    _radio.wait();

    uint8_t status = _radio.status();
    radio_error_t error = _radioConvertError(status);

    if (error != kNone)
        return error;

    uint8_t recvLength = _radio.available();

    if (recvLength < *length)
        *length = recvLength;

    // Ler o máximo possível que caiba em `dest`
    _radio.read(dest, *length);

    // Limpar o resto da mensagem que não foi lida
    _radio.purge(recvLength - *length);

    status = _radio.status();

    _radio.standby(SX126X_STANDBY_XOSC);
    return _radioConvertError(status);
}

/// Determina se será ligado o Low Data Rate Optimization (LDRO)
bool radioHasLDRO(radio_parameters_t& param) {
    double symbolTime = ((double)(1u << (uint32_t)param.sf)) / param.bandwidth;
    return symbolTime > 16.0;
}

/// Retorna o tempo esperado de transmissão, em millisegundos, dados
/// os parâmetros definidos e o comprimento do pacote a ser medido.
uint64_t radioTransmitTime(radio_parameters_t& param, uint32_t packetLength) {
    // Fórmula do Time on Air do datasheet do SX1268, seção (6.1.4 LoRa®
    // Time-on-Air)
    bool isSf56 = (param.sf == 5 || param.sf == 6);
    bool ldro = radioHasLDRO(param);

    double nSymbolHeader = param.packetLength == 0 ? 20 : 0;
    double nBitCrc = param.crc ? 16 : 0;

    // Numerador da fração interna do N_symbol
    double numerator = 8.0 * packetLength + nBitCrc - (4.0 * param.sf) +
                       (isSf56 ? 0 : 8) + nSymbolHeader;

    // Denominador da fração interna do N_symbol
    double denominator = 4 * (param.sf - (ldro ? 2 : 0));

    // Numero de símbolos na transmissão (N_symbol)
    double nSymbol =
        param.preambleLength + 4.25 + (isSf56 ? 2.0 : 0.0) + 8 +
        (ceil(max(numerator, 0.0) / denominator) * (double)(param.cr));

    // ToA = N_symbol * (2^SF) / BW
    double toa =
        1000 * nSymbol * (double)(1u << (uint32_t)param.sf) / param.bandwidth;

    return (uint64_t)toa;
}

/// Retorna o RSSI da última mensagem recebida.
int16_t radioRSSI() {
    return _radio.packetRssi();
}

/// Retorna o SNR da última mensagem recebida.
float radioSNR() {
    return _radio.snr();
}

/// Atualiza os parâmetros do radiotransmissor.
void radioSetParameters(radio_parameters_t& param) {
    _radio.setFrequency(param.frequency * 1000000);
    _radio.setTxPower(param.power, SX126X_TX_POWER_SX1262);
    _radio.setSyncWord(param.syncWord);
    _radio.setLoRaPacket(param.packetLength == 0 ? SX126X_HEADER_EXPLICIT
                                                 : SX126X_HEADER_IMPLICIT,
                         param.preambleLength, param.packetLength, param.crc,
                         param.invertIq);

    _radio.setRxGain(param.boostedRxGain ? SX126X_RX_GAIN_BOOSTED
                                         : SX126X_RX_GAIN_POWER_SAVING);

    _radio.setLoRaModulation(param.sf, param.bandwidth * 1000, param.cr,
                             radioHasLDRO(param));

    _radio.standby(SX126X_STANDBY_XOSC);
}