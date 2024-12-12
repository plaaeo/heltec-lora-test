/**
 * hal/timer.hh
 *
 * Controla o timer usado para sincronizar o receptor e
 * o transmissor no experimento.
 */

#include "esp_timer.h"

volatile esp_timer_handle_t _timerHandle;
volatile bool _timerFlag = false;
volatile bool _timerShouldResync = false;
volatile uint64_t _timerNextPeriod = 0;

/// Chamado quando o tempo definido no timer passar.
static void _timerCallback(void* _arg) {
    if (_timerShouldResync) {
        esp_timer_restart(_timerHandle, _timerNextPeriod);
        _timerShouldResync = false;
    } else {
        _timerFlag = true;
    }

}

/// Inicializa o timer.
void timerInit() {
    const esp_timer_create_args_t args = {
        .callback = &_timerCallback,
        .arg = (void*) &_timerHandle,
        .name = "sync",
    };

    // Criar o timer periódico
    ESP_ERROR_CHECK(esp_timer_create(&args, (esp_timer_handle_t*)&_timerHandle));
}

/// Inicia o timer periódico com um timeout definido em microsegundos.
void timerStart(uint64_t micro) {
    ESP_ERROR_CHECK(esp_timer_start_periodic(_timerHandle, micro));
}

/// Marca o timer para resincronização. No próximo tick do timer, o timeout irá
/// mudar para o valor especificado.
void timerResync(uint64_t micro) {
    _timerShouldResync = true;
    _timerNextPeriod = micro;
}

/// Retorna o tempo atual no timer. 
uint64_t timerTime() {
    return esp_timer_get_time();
}

/// Retorna o período (ou timeout) do timer.
uint64_t timerPeriod() {
    uint64_t period = 0;
    
    ESP_ERROR_CHECK(esp_timer_get_period(_timerHandle, &period));
    
    return period;
}

/// Retorna o tempo, em microsegundos, do próximo tick do timer.
int64_t timerNextTicks() {
    return esp_timer_get_next_alarm();
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