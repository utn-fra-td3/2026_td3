SET(SOURCES screens/ui_scrconfig.c
    screens/ui_scrsweep.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    fonts/ui_font_RobotoMono12.c
    fonts/ui_font_RobotoMono14.c
    fonts/ui_font_RobotoMono18.c)

add_library(ui ${SOURCES})
