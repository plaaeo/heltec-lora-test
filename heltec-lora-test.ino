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
    uiText(0, 10, "Centro!");

    uiAlign(kRight);
    uiText(0, 20, "Direita!");

    // Desenhar indicador do texto pressionado
    static String pressionado = "(Um)";
    uiAlign(kRight);
    uiText(0, 30, pressionado);

    // Desenhar botões
    uiAlign(kCenter);

    if (uiButton(-64, 45, "Um"))
        pressionado = "(Um)";

    if (uiButton(64, 45, "Outro"))
        pressionado = "(Outro)";

    static bool checado = false;
    uiAlign(kCenter);

    if (uiItem(-40, 55, 80, 10))
        checado = !checado;

    uiCheckbox(-41, 56, checado);
    uiText(-41 + 15, 56, "Oii");

    uiFinish();
}
