/**
 * hal/log.hh
 *
 * Abstração para o datalogger utilizado no experimento.
 */

#include <SD.h>
#include <SPI.h>
#include <stdarg.h>
#include <stdio.h>

#define SD_SCK 2
#define SD_MISO 1
#define SD_MOSI 3
#define SD_CS 4

SPIClass _spiSd = SPIClass(FSPI);
File _file;

/// Inicializa o datalogger, preparando-o para gravar dados
/// em um arquivo, dado o nome.
bool logInit(const char* filename) {
    _spiSd.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    // Inicializar biblioteca do SD
    if (!SD.begin(SD_CS, _spiSd)) {
        Serial.println(
            "[hal/log.hh] Não foi possível inicializar o datalogger.");
        return false;
    }

    // Abrir arquivo especificado
    _file = SD.open(filename, FILE_APPEND);
    if (!_file) {
        Serial.println("[hal/log.hh] Não foi possível abrir o arquivo.");
        return false;
    }

    return true;
}

/// Imprime dados no datalogger, devendo ser executado
/// no estilo da função `printf`. Não deve inserir um '\n'
/// no final da linha.
int logPrintf(const char* format, ...) {
    va_list list;
    va_start(list, format);

    char buffer[1024];
    int res = vsnprintf(buffer, sizeof(buffer) / sizeof(char), format, list);

    // Imprime no Serial como fallback, caso a inicialização tenha falhado
    if (_file) {
        _file.print(buffer);
    } else if (Serial) {
        Serial.print("[hal/log.hh] ");
        Serial.print(buffer);
    }

    va_end(list);

    return res;
}

/// Finaliza o datalogger, salvando os dados do arquivo.
void logClose() {
    _file.close();
    SD.end();
    _spiSd.end();
}