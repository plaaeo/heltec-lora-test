#include "hal/buttons.hh"
#include "hal/lib.hh"
#include "hal/log.hh"
#include "hal/ui.hh"

void setup() {
    halInit();
    uiSetup();
}

void loop() {
    halLoop();
    uiLoop();

    // Desenhar textos estáticos
    uiStatic();

    uiAlign(kLeft);
    uiText(0, 0, "Esquerda!");

    uiAlign(kCenter);
    uiText(0, 6, "Centro!");

    uiAlign(kRight);
    uiText(0, 12, "Direita!");

    // Desenhar indicador do texto pressionado
    static String pressionado = "(Um)";
    uiAlign(kRight);
    uiText(0, 12, pressionado);

    // Desenhar botões
    uiAlign(kCenter);

    if (uiButton(-64, 40, "Um"))
        pressionado = "(Um)";

    if (uiButton(64, 40, "Outro"))
        pressionado = "(Outro)";

    uiFinish();
}
