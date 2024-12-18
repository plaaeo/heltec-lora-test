/**
 * hal/timer.hh
 *
 * Controla o timer usado para sincronizar o receptor e
 * o transmissor no experimento.
 */

#include "esp_timer.h"
#include "freertos/task.h"

using timer_handler_fn = void (*)(void);

/// A task do FreeRTOS que lida com o timeout do timer.
TaskHandle_t _timerTask = NULL;

/// Função do usuário, definida em `timerStart` e `timerResync`
timer_handler_fn _timerUserFn = NULL;

volatile esp_timer_handle_t _timerHandle;

// Define o próximo período do timer, depois do próximo timeout.
volatile uint64_t _timerNextPeriod = 0;
timer_handler_fn _timerNextUserFn = NULL;

/// Chamado quando o tempo definido no timer passar.
void _timerCallback(void* _) {
    if (_timerNextPeriod > 0) {
        esp_timer_restart(_timerHandle, _timerNextPeriod);
        _timerNextPeriod = 0;
        
        // Muda o callback do usuário caso necessário.
        if (_timerNextUserFn) {
            _timerUserFn = _timerNextUserFn;
            _timerNextUserFn = NULL;
        }
    }

    // Notifica a task para executar o callback do usuario.
    vTaskNotifyGiveFromISR(_timerTask, NULL);
}

// Executa o callback do usuario apos ser notificado.
void _timerHandlerTask(void* _) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Executa a função de timeout do usuario.
        (_timerUserFn)();
        yield();
    }
}

/// Inicializa o timer.
void timerInit() {
    const esp_timer_create_args_t args = {
        .callback = &_timerCallback,
        .arg = NULL,
        .name = "sync",
    };

    // Criar o timer periódico
    ESP_ERROR_CHECK(
        esp_timer_create(&args, (esp_timer_handle_t*)&_timerHandle));
}

/// Inicia o timer periódico com um timeout definido em microsegundos.
/// Quando ocorrer um timeout, a função passada no parâmetro `fn` será
/// executada.
void timerStart(uint64_t micro, timer_handler_fn fn) {
    if (_timerTask != NULL)
        vTaskDelete(_timerTask);
    
    _timerUserFn = fn;

    // Cria uma task no mesmo nucleo que a funcao foi executada.
    // A task irá esperar o próximo timeout do timer e executará
    // a função do usuário.
    xTaskCreatePinnedToCore(_timerHandlerTask, "timer_handler", 8192, NULL,
                tskIDLE_PRIORITY + 10, &_timerTask, xPortGetCoreID());

    ESP_ERROR_CHECK(esp_timer_start_periodic(_timerHandle, micro));
}

/// Marca o timer para resincronização. No próximo tick do timer, o timeout irá
/// mudar para o valor especificado.
void timerResync(uint64_t micro, timer_handler_fn fn) {
    _timerNextPeriod = micro;
    _timerNextUserFn = fn;
}

/// Retorna o tempo atual no timer.
int64_t timerTime() {
    return esp_timer_get_time();
}

/// Retorna o período (ou timeout) do timer.
uint64_t timerPeriod() {
    uint64_t period = 0;

    ESP_ERROR_CHECK(esp_timer_get_period(_timerHandle, &period));

    return period;
}

/// Retorna o tempo absoluto, em microsegundos, do próximo tick do timer.
int64_t timerNextTick() {
    return esp_timer_get_next_alarm();
}

/// Finaliza o timer periódico.
void timerStop() {
    if (_timerTask != NULL)
        vTaskDelete(_timerTask);

    ESP_ERROR_CHECK(esp_timer_stop(_timerHandle));
}