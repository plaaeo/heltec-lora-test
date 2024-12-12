/**
 * hal/radio.hh
 *
 * Abstração para a biblioteca do radiotransmissor LoRa utilizado
 * no experimento.
 */

#pragma once
#include <stdint.h>

#include "lib.hh"

/// Recebe `true` quando um interrupt for gerado pelo radiotransmissor.
volatile bool __radioDidIRQ = false;

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void __radioIRQ(void) {
    __radioDidIRQ = true;
}

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

/// Converte os erros da RadioLib em `radio_error_t`
radio_error_t _radioConvertError(int16_t e) {
    switch (e) {
    case RADIOLIB_ERR_NONE:
        return kNone;
    case RADIOLIB_ERR_CRC_MISMATCH:
        return kCrc;
    case RADIOLIB_ERR_TX_TIMEOUT:
    case RADIOLIB_ERR_RX_TIMEOUT:
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
    radio.begin();
    radio.setDio1Action(__radioIRQ);
    return true;
}

/// Envia um pacote e aguarda ele terminar de ser enviado,
/// retornando o resultado da transmissão.
radio_error_t radioSend(const uint8_t* message, uint8_t size) {
    int16_t status = radio.startTransmit((uint8_t*)message, size);
    radio_error_t error = _radioConvertError(status);

    // Não aguarda o fim da transmissão caso ocorra um erro.
    if (error != kNone)
        return error;

    // Aguarda o pacote ser enviado completamente.
    while (!__radioDidIRQ);
    __radioDidIRQ = false;

    status = radio.finishTransmit();
    radio.standby();
    return _radioConvertError(status);
}

/// Aguarda até que um pacote seja recebido, ou ocorra timeout (passado em
/// microsegundos), retornando o resultado da operação. Recebe em `*length` o
/// tamanho do buffer de destino e, após a operação, armazezna o tamanho do
/// pacote lido.
radio_error_t radioRecv(uint8_t* dest, uint8_t* length, uint64_t timeout = 0) {
    auto timeoutReal = radio.calculateRxTimeout(timeout);
    int16_t status = radio.startReceive(timeoutReal);
    radio_error_t error = _radioConvertError(status);

    // Não aguarda até o fim da operação caso ocorra um erro.
    if (error != kNone)
        return error;

    // Aguarda o pacote ser recebido, ou o timeout.
    while (!__radioDidIRQ);
    __radioDidIRQ = false;

    size_t msgLength = radio.getPacketLength();

    // Evitar buffer overflow
    if (*length < msgLength)
        *length = msgLength;

    status = radio.readData(dest, *length);
    radio.standby();
    return _radioConvertError(status);
}

/// Retorna o tempo esperado de transmissão, em millisegundos, dados
/// os parâmetros definidos e o comprimento do pacote a ser medido.
uint64_t radioTransmitTime(uint32_t packetLength) {
    return radio.getTimeOnAir(packetLength);
}

/// Retorna o RSSI da última mensagem recebida.
int16_t radioRSSI() {
    return radio.getRSSI();
}

/// Retorna o SNR da última mensagem recebida.
float radioSNR() {
    return radio.getSNR();
}

/// Atualiza os parâmetros do radiotransmissor.
void radioSetParameters(radio_parameters_t& param) {
    radio.autoLDRO();
    radio.setFrequency(param.frequency);
    radio.setBandwidth(param.bandwidth);
    radio.setSpreadingFactor(param.sf);
    radio.setCodingRate(param.cr);
    radio.setCRC(param.crc);
    radio.setPreambleLength(param.preambleLength);
    radio.setRxBoostedGainMode(param.boostedRxGain);

    if (param.packetLength > 0)
        radio.implicitHeader(param.packetLength);
    else
        radio.explicitHeader();

    radio.invertIQ(param.invertIq);
    radio.setSyncWord(param.syncWord);
}