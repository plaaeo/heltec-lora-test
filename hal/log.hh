/**
 * hal/log.hh
 *
 * Abstração para o datalogger utilizado no experimento.
 */

#include <stdarg.h>
#include <stdio.h>

/// Inicializa o datalogger, preparando-o para gravar dados
/// em um arquivo, dado o nome.
void logInit(const char* filename) {
    // TODO
}

/// Imprime dados no datalogger, devendo ser executado
/// no estilo da função `printf`. Não deve inserir um '\n'
/// no final da linha.
int logPrintf(const char* format, ...) {
    va_list list;
    va_start(list, format);

    char buffer[1024];
    int res = vsnprintf(buffer, sizeof(buffer) / sizeof(char), format, list);

    // TODO: Imprimir `buffer` no arquivo
    if (Serial) {
        Serial.print("[hal/log.hh] ");
        Serial.println(buffer);
    }

    va_end(list);

    return res;
}

/// Finaliza o datalogger, salvando os dados do arquivo.
void logClose() {
    // TODO
}