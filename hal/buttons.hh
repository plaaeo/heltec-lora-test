/**
 * hal/buttons.hh
 *
 * Abstração para a interface utilizada para interagir com o
 * experimento. No caso do experimento base, será utilizado
 * apenas um botão integrado ao V3.
 */

#pragma once

#define BUTTON GPIO_NUM_0

uint32_t _btnTime = 0;
bool _btnState = false;
bool _btnReleased = false;
bool _btnMoved = false;

/// Retorna o estado atual do botão.
void uiUpdateButton() {
    pinMode(BUTTON, INPUT_PULLUP);
    
    bool state = digitalRead(BUTTON) == LOW;
    _btnReleased = _btnState && !state;

    // Salva o tempo no instante que o botão foi apertado
    if (!_btnState && state) {
        _btnMoved = false;
        _btnTime = millis();
    }

    _btnState = state;
}

/// Retorna `true` caso o elemento da interface selecionado
/// deve foi pressionado.
bool uiPressed() {
    // Marca como pressionado caso o botão tenha sido solto,
    // porém antes de ser marcado como "segurado".
    return _btnReleased && !_btnMoved;
}

/// Retorna `true` caso o usuário tenha pedido para selecionar
/// o próximo elemento.
bool uiSelectNext() {
    bool moved = _btnState && (millis() - _btnTime) >= 500;
    
    if (moved) {
        _btnMoved = true;
        _btnTime = millis();
    }

    return moved;
}

/// Retorna `true` caso o usuário tenha pedido para selecionar
/// o elemento anterior.
bool uiSelectPrev() {
    // Atualmente, não é possível voltar utilizando apenas um botão.
    return false;
}
