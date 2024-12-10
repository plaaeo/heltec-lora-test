#include "hal/buttons.hh"
#include "hal/lib.hh"
#include "hal/log.hh"
#include "hal/ui.hh"

void setup() {
    Serial.begin(115200);
    
    halInit();
    uiSetup();
}

void loop() {
    halLoop();
    uiLoop();

    // Desenhar textos alinhados
    uiAlign(kLeft);
    uiText(0, 0, "Esq.");

    uiAlign(kCenter);
    uiText(0, 0, "Centro!");

    uiAlign(kRight);
    uiText(0, 0, "Dir.");

    // Desenhar indicador do texto pressionado
    static String pressionado = "(Um)";
    uiAlign(kRight);
    uiText(0, 10, pressionado);

    // Desenhar botões
    uiAlign(kCenter);

    if (uiButton(-16, 25, "Um"))
        pressionado = "(Um)";

    if (uiButton(16, 25, "Outro"))
        pressionado = "(Outro)";

    static bool checado = false;
    uiAlign(kCenter);

    if (uiItem(0, 40, 30, 10))
        checado = !checado;

    uiCheckbox(-6, 41, checado);
    uiText(6, 39, "Oii");

    uiFinish();
}
