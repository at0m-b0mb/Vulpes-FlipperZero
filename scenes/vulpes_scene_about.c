#include "../vulpes_i.h"

void vulpes_scene_about_on_enter(void* context) {
    VulpesApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    widget_add_text_scroll_element(
        widget,
        0,
        0,
        128,
        64,
        "\e#Vulpes " VULPES_VERSION "\e#\n"
        "Find the hidden transmitter.\n"
        "\n"
        "In radio direction finding the\n"
        "hidden transmitter is called the\n"
        "fox, and looking for it is a fox\n"
        "hunt. Vulpes is the fox hunt, on\n"
        "your Flipper.\n"
        "\n"
        "\e#1. Survey\e#\n"
        "Sweeps a Sub-GHz band in 64\n"
        "steps and keeps the loudest\n"
        "reading each frequency has ever\n"
        "given, so a beacon that speaks\n"
        "for 40 ms once a second is still\n"
        "caught. Anything standing 10 dB\n"
        "over the band's noise floor is\n"
        "listed. Up/Down to pick one,\n"
        "Left/Right for another band,\n"
        "hold OK to start the hold over.\n"
        "OK locks it and starts the hunt.\n"
        "\n"
        "\e#2. Hunt\e#\n"
        "COLD, COOL, WARM, HOT, BURNING,\n"
        "ON TOP - how far the signal\n"
        "stands above the noise. The\n"
        "arrow is a least-squares fit\n"
        "over the last 0.6 s, not a\n"
        "difference between two samples,\n"
        "and it stays blank rather than\n"
        "guess when the signal is too\n"
        "noisy to call.\n"
        "The clicks rise in pitch and\n"
        "rate as you close in, so you\n"
        "can search with your eyes up.\n"
        "OK marks a distance to measure\n"
        "progress from. Hold OK clears\n"
        "the peak and the statistics.\n"
        "Left/Right shows what kind of\n"
        "emitter it is.\n"
        "\n"
        "\e#The attenuator\e#\n"
        "Inside a couple of metres the\n"
        "receiver saturates and a plain\n"
        "RSSI meter goes blind. Up/Down\n"
        "tunes deliberately off\n"
        "frequency so the radio's own\n"
        "filter knocks the signal down\n"
        "and the scale works again -\n"
        "the trick fox-hunters do with a\n"
        "box of parts. AUTO does it for\n"
        "you. While it is engaged the\n"
        "dBm shown is not the true level.\n"
        "\n"
        "\e#3. Bearing\e#\n"
        "A whip antenna hears equally in\n"
        "every direction, so one reading\n"
        "has no direction in it. Hold\n"
        "the Flipper flat to your chest\n"
        "and turn a slow, even circle:\n"
        "your body shadows whatever is\n"
        "behind you, so the loudest\n"
        "bearing points at the fox.\n"
        "There is no compass in a\n"
        "Flipper, so the sectors come\n"
        "from elapsed time - an even\n"
        "pace IS the measurement.\n"
        "\n"
        "\e#What it found\e#\n"
        "CONTINUOUS - always on. Analog\n"
        "bug, video transmitter, jammer.\n"
        "PERIODIC - beacons on a clock.\n"
        "Tracker or telemetry.\n"
        "INTERMITTENT - irregular. A\n"
        "remote, sensor or doorbell.\n"
        "This is what keeps a neighbour's\n"
        "weather station from reading as\n"
        "a bug.\n"
        "\n"
        "\e#Honest limits\e#\n"
        "- RSSI gives closer and further,\n"
        "  never a distance. The 'x\n"
        "  closer' figure assumes free\n"
        "  space, 6 dB per halving.\n"
        "  Indoors it is optimistic.\n"
        "- Walls and metal reflect. A\n"
        "  strong peak can be an echo.\n"
        "  Confirm from two places.\n"
        "- TOO FLAT is a real answer,\n"
        "  and a common one indoors. It\n"
        "  is not a failure to report it.\n"
        "- 300-348, 387-464 and 779-928\n"
        "  MHz only: the CC1101's range.\n"
        "  No WiFi, no Bluetooth, no\n"
        "  GSM, no 2.4 GHz.\n"
        "- Finds transmitters. A camera\n"
        "  recording to an SD card emits\n"
        "  nothing to find.\n"
        "\n"
        "Listen-only. Vulpes never\n"
        "transmits.\n"
        "\n"
        "\e#Credits\e#\n"
        "by at0m-b0mb\n"
        "MIT licensed\n"
        "github.com/at0m-b0mb/\n"
        "Vulpes-FlipperZero\n");

    view_dispatcher_switch_to_view(app->view_dispatcher, VulpesViewAbout);
}

bool vulpes_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void vulpes_scene_about_on_exit(void* context) {
    VulpesApp* app = context;
    widget_reset(app->widget);
}
