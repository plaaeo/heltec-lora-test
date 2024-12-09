/**
 * Abstração para a biblioteca do radiotransmissor LoRa utilizado
 * no experimento.
 */

#pragma once
#include "lib.hh"

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

radio_error_t convertError(uint8_t e) {
}

/**
 * Define todos os parâmetros modificáveis do radiotransmissor.
 */
struct radio_parameters_t {
    // A frequência da transmissão, em MHz.
    float frequency;
    uint16_t preambleLength;

    // A largura de banda, em bps.
    uint32_t bandwidth;

    // O fator de espalhamento, ou `spreading factor`, de 7 a 12
    uint8_t sf;

    // O denominador da taxa de codificação, ou `coding rate`, de 4 a 8.
    uint8_t cr;

    // Liga ou desliga o código de detecção de erro incluido em cada mensagem.
    bool crc;
    bool invertIq;

    // Determina a sensibilidade extra do receptor, `false` para o modo
    // de economia de energia.
    bool boostedRxGain;

    // Comprimento padrão do pacote. Caso maior que 0, o modo de header
    // implícito é ligado.
    uint32_t packetLength;
};

/**
 * Inicializa o radio LoRa.
 * Retorna `true` caso o radio tenha inicializado com sucesso.
 */
bool radioInit() {
    radio.setDio1Action(__radioIRQ);
    return true;
}

/**
 * Envia um pacote e aguarda ele terminar de ser enviado,
 * retornando o resultado da transmissão.
 */
radio_error_t radioSend(uint8_t *message, uint8_t size) {
    int16_t status = radio.startTransmit(message, size);
    radio_error_t error = convertError(status);

    // Não aguarda o fim da transmissão caso ocorra um erro.
    if (error != kNone)
        return error;

    // Aguarda o pacote ser enviado completamente.
    while (!__radioDidIRQ);

    status = radio.finishTransmit();
    return convertError(status);
}

/**
 * Aguarda até que um pacote seja recebido, ou ocorra timeout,
 * retornando o resultado da operação.
 */
radio_error_t radioRecv(uint8_t *dest, uint8_t *length, uint32_t timeout = 0) {
    // TODO: Recv
}

/**
 * Retorna o tempo esperado de transmissão, em millisegundos, dados
 * os parâmetros definidos e o comprimento do pacote a ser medido.
 */
uint32_t radioTransmitTime(uint32_t packetLength) {
    return radio.getTimeOnAir(packetLength);
}

/**
 * Retorna o RSSI da última mensagem recebida.
 */
int16_t radioRSSI() {
    return radio.getRSSI();
}

/**
 * Retorna o SNR da última mensagem recebida.
 */
float radioSNR() {
    return radio.getSNR();
}

/**
 * Atualiza os parâmetros do radiotransmissor.
 */
void radioSetParameters(radio_parameters_t &param) {
    // TODO
}