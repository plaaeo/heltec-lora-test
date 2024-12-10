/**
 * hal/ui.hh
 *
 * Contém funções de utilidade para renderizar a interface gráfica,
 * sem precisar de um "display" específico.
 */

#pragma once
#include "buttons.hh"
#include "lib.hh"

/// Define três tipos de alinhamento diferentes.
enum alignment_t { kLeft, kCenter, kRight };

static struct {
    /// Determina o índice do item selecionado atualmente.
    uint32_t selection;

    /// Determina se o item selecionado foi pressionado neste tick.
    bool wasPressed;

    /// Determina a quantidade de items selecionáveis na interface.
    uint32_t items;

    /// Determina o índice do próximo item a ser desenhado.
    uint32_t nextItem;

    /// Determina o alinhamento utilizado para os itens da interface atualmente.
    alignment_t alignment;
} _uiState;

/// Determina o alinhamento dos próximos itens a serem desenhados na interface.
void uiAlign(alignment_t align) {
    _uiState.alignment = align;

    // Converter para enum da biblioteca
    auto textAlign = TEXT_ALIGN_LEFT;
    switch (align) {
    case kLeft:
        textAlign = TEXT_ALIGN_LEFT;
        break;
    case kCenter:
        textAlign = TEXT_ALIGN_CENTER;
        break;
    case kRight:
        textAlign = TEXT_ALIGN_RIGHT;
        break;
    }

    display.setTextAlignment(textAlign);
}

/// Inicializa a interface com um estado padrão.
void uiSetup() {
    display.setFont(ArialMT_Plain_10);

    _uiState = {};
    _uiState.selection = 0;
    _uiState.wasPressed = false;
    _uiState.items = 0;
    _uiState.nextItem = 0;
    uiAlign(kLeft);
}

/// Atualiza o estado da interface dependendo da entrada do usuário.
void uiLoop() {
    if (uiSelectNext())
        _uiState.selection++;

    if (uiSelectPrev())
        _uiState.selection--;

    // Previne a seleção de itens inválidos, voltando ao primeiro item
    // caso o usuário tente selecionar um após o último.
    _uiState.selection %= _uiState.items;

    _uiState.wasPressed = uiPressed();
    _uiState.items = 0;
    _uiState.nextItem = 0;
    uiAlign(kLeft);

    display.clear();
}

/// Alinha a coordenada X dada de acordo com o valor de alinhamento atual.
int16_t uiAlignX(int16_t x, int16_t w = 0) {
    switch (_uiState.alignment) {
    case kCenter:
        x += (display.getWidth() - w) / 2;
        break;
    case kRight:
        x = display.getWidth() - x - w;
        break;
    }

    return x;
}

/// Prepara para desenhar um item estático na interface.
void uiStatic() {
    display.setColor(WHITE);
}

/// Desenha uma caixa de item selecionável na interface de usuário.
/// Caso este item tenha sido pressionado no último frame, retorna `true`.
bool uiItem(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    bool selected = _uiState.selection == _uiState.nextItem;

    if (selected) {
        // Desenha caixa em volta do item preenchido
        x = uiAlignX(x, w);
        display.setColor(WHITE);
        display.fillRect(x, y, w, h);

        // Inverte a cor dos próximos elementos
        display.setColor(BLACK);
    } else {
        display.setColor(WHITE);
    }

    _uiState.items++;
    _uiState.nextItem++;

    return selected && _uiState.wasPressed;
}

/// Desenha um texto na interface de usuário.
void uiText(int16_t x, int16_t y, const String& text) {
    display.drawString(x, y, text);
};

/// Desenha um botão com o texto dado.
/// Caso este botão tenha sido pressionado no último frame, retorna `true`.
bool uiButton(int16_t x, int16_t y, const String& text) {
    int16_t w = display.getStringWidth(text);

    bool pressed = uiItem(x - 1, y - 1, w + 2, 12);
    uiText(x, y, text);
    return pressed;
}

/// Desenha uma checkbox na interface.
void uiCheckbox(int16_t x, int16_t y, bool filled) {
    x = uiAlignX(x, 6);
    display.drawRect(x, y, 6, 6);

    if (filled)
        display.fillRect(x + 2, y + 2, 2, 2);
}

/// Finaliza o frame atual e desenha a interface no OLED.
void uiFinish() {
    display.display();
}