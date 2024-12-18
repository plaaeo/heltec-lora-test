/**
 * hal/ui.hh
 *
 * Contém funções de utilidade para renderizar a interface gráfica,
 * sem precisar de um "display" específico.
 */

#pragma once
#include <OLEDDisplayUi.h>
#include <SSD1306Wire.h>

#include "buttons.hh"
#include "lib.hh"

/// Define três tipos de alinhamento diferentes.
enum alignment_t { kLeft, kCenter, kRight };

SSD1306Wire display(0x3c, SDA_OLED, SCL_OLED, GEOMETRY_128_64);

enum color_t { kWhite, kBlack, kInvert };

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

/// Define a cor do próximo elemento.
void uiSetColor(color_t color) {
    switch (color) {
    case kWhite:
        display.setColor(WHITE);
        break;
    case kBlack:
        display.setColor(BLACK);
        break;
    case kInvert:
        display.setColor(INVERSE);
        break;
    }
}

/// Determina o alinhamento dos próximos itens a serem desenhados na interface.
void uiAlign(alignment_t align) {
    _uiState.alignment = align;
}

/// Inicializa a interface com um estado padrão.
bool uiSetup() {
    pinMode(RST_OLED, OUTPUT);
    digitalWrite(RST_OLED, HIGH);
    delay(1);
    digitalWrite(RST_OLED, LOW);
    delay(20);
    digitalWrite(RST_OLED, HIGH);

    if (!display.init())
        return false;

    display.setContrast(255);
    display.flipScreenVertically();
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
    uiUpdateButton();

    if (uiSelectNext())
        _uiState.selection++;

    if (uiSelectPrev())
        _uiState.selection--;

    // Previne a seleção de itens inválidos, voltando ao primeiro item
    // caso o usuário tente selecionar um após o último.
    if (_uiState.items > 0)
        _uiState.selection %= _uiState.items;

    _uiState.wasPressed = uiPressed();
    _uiState.items = 0;
    _uiState.nextItem = 0;
    uiAlign(kLeft);
}

/// Limpa a tela da interface, preparando um novo frame.
void uiClear() {
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

/// Desenha uma caixa de item selecionável na interface de usuário.
/// Caso este item tenha sido pressionado no último frame, retorna `true`.
bool uiItem(int16_t x, int16_t y, uint16_t w, uint16_t h,
            color_t selectedColor = kInvert) {
    bool selected = _uiState.selection == _uiState.nextItem;

    if (selected) {
        // Desenha caixa em volta do item preenchido
        x = uiAlignX(x, w);
        uiSetColor(selectedColor);
        display.fillRect(x, y, w, h);
    }

    _uiState.items++;
    _uiState.nextItem++;

    return selected && _uiState.wasPressed;
}

/// Desenha um texto na interface de usuário.
void uiText(int16_t x, int16_t y, const String& text, color_t color = kInvert) {
    uiSetColor(color);

    // Alinhar horizontalmente
    int16_t w = display.getStringWidth(text);
    x = uiAlignX(x, w);

    // Não conflitar com o alinhamento da biblioteca
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(x, y, text);
};

/// Desenha um botão com o texto dado.
/// Caso este botão tenha sido pressionado no último frame, retorna `true`.
bool uiButton(int16_t x, int16_t y, const String& text,
              color_t selectedColor = kInvert) {
    int16_t w = display.getStringWidth(text);

    bool pressed = uiItem(x, y, w + 2, 10);

    // Centralizar texto no botão
    switch (_uiState.alignment) {
    case kLeft:
        x += 1;
        break;
    case kRight:
        x -= 1;
        break;
    }

    uiText(x, y - 2, text, selectedColor);

    return pressed;
}

enum rect_type_t {
    kFill,
    kStroke,
    kDither,
};

/// Desenha um retângulo na interface.
void uiRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
            rect_type_t type = kFill, color_t color = kInvert) {
    x = uiAlignX(x, w);
    uiSetColor(color);

    switch (type) {
    case kFill:
        display.fillRect(x, y, w, h);
        break;
    case kStroke:
        display.drawRect(x, y, w, h);
        break;
    case kDither:
        for (uint16_t yo = 0; yo < h; yo++) {
            for (uint16_t xo = 0; xo < w; xo++) {
                if (xo % 2 != 1 || yo % 2 != 1)
                    display.setPixel(x + xo, y + yo);
            }
        }
        break;
    }
}

/// Desenha uma checkbox na interface.
void uiCheckbox(int16_t x, int16_t y, bool filled, color_t color = kInvert) {
    x = uiAlignX(x, 8);
    uiSetColor(color);
    display.drawRect(x, y, 8, 8);

    if (filled)
        display.fillRect(x + 2, y + 2, 4, 4);
}

/// Finaliza o frame atual e desenha a interface no OLED.
void uiFinish() {
    display.display();
}