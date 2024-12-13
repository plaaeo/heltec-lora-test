/**
 * hal/lib.hh
 *
 * É possível controlar o funcionamento da biblioteca `heltec_unofficial`
 * através de macros usando #define. Este arquivo serve como a "configuração"
 * da biblioteca caso seja necessário.
 */

#pragma once
/// Inicializa a biblioteca `heltec_unofficial`.
///
/// Deve ser executado antes de qualquer função de inicialização
/// dos módulos no diretório `hal`.
void halInit() {};

/// Executa quaisquer funções sejam necessárias no loop.
void halLoop() {};
