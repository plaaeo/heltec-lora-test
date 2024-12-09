/**
 * hal/buttons.hh
 *
 * Abstração para a interface utilizada para interagir com o
 * experimento. No caso do experimento base, será utilizado
 * apenas um botão integrado ao V3.
 */

#pragma once
#include "lib.hh"

/// Retorna `true` caso o elemento da interface selecionado
/// deve foi pressionado.
bool uiPressed() {
    return button.isSingleClick();
}

/// Retorna `true` caso o usuário tenha pedido para selecionar
/// o próximo elemento.
bool uiSelectNext() {
    return button.pressedFor(500);
}

/// Retorna `true` caso o usuário tenha pedido para selecionar
/// o elemento anterior.
bool uiSelectPrev() {
    // Atualmente, não é possível voltar utilizando apenas um botão.
    return false;
}
