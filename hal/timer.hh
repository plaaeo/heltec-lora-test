/**
 * hal/timer.hh
 *
 * Controla o timer usado para sincronizar o receptor e
 * o transmissor no experimento.
 */

#include "esp_timer.h"

esp_timer_handle_t _timerHandle;
volatile bool _timerFlag = false;

/// Chamado quando o tempo definido no timer passar.
static void _timerCallback(void* _arg) {
    _timerFlag = true;
}

/// Inicializa o timer.
void timerInit() {
    const esp_timer_create_args_t args = {
        .callback = &_timerCallback,
        .name = "sync",
    };

    // Criar o timer periódico
    ESP_ERROR_CHECK(esp_timer_create(&args, &_timerHandle));
}

/// Inicia o timer periódico com um timeout definido em microsegundos.
void timerStart(uint64_t micro) {
    if (esp_timer_is_active(_timerHandle))
        ESP_ERROR_CHECK(esp_timer_stop(_timerHandle));

    ESP_ERROR_CHECK(esp_timer_start_periodic(_timerHandle, micro));
}

/// Retorna `true` se o timer acabou de executar.
bool timerFlag() {
    bool flag = _timerFlag;
    _timerFlag = false;
    return flag;
}

/// Finaliza o timer periódico.
void timerStop() {
    ESP_ERROR_CHECK(esp_timer_stop(_timerHandle));
}