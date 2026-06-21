// --- Includes ---
#include "ui_scrconfig_kb.h"
#include "ui.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Defines privados ---
#define KB_BUF_LEN 8 // max "100000\0"

// --- Tipos privados ---
typedef enum {
    PARAM_FREC_INICIO = 0,
    PARAM_FREC_FINAL,
    PARAM_PUNTOS,
    PARAM_TIEMPO
} param_id_e;

// --- Variables privadas ---
static const char *kb_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t kb_ctrl[] = {
    1, 1, 1,
    1, 1, 1,
    1, 1, 1,
    1, 1, LV_BUTTONMATRIX_CTRL_CHECKED | 1
};

static param_id_e param_activo;
static char       buf_entrada[KB_BUF_LEN];

static uint32_t val_frec_inicio = 10;
static uint32_t val_frec_final  = 100000;
static uint32_t val_puntos      = 200;
static uint32_t val_tiempo      = 5;

static const uint32_t MIN[]  = {10,     10,     2,   1};
static const uint32_t MAX[]  = {100000, 100000, 200, 60};
static const char *   UNIT[] = {"Hz", "Hz", "pts", "s"};

static lv_obj_t **val_labels[] = {
    &ui_lblvalue1, &ui_lblvalue2,
    &ui_lblvalue3, &ui_lblvalue4
};

// --- Prototipos privados ---
static void update_label(param_id_e param, uint32_t val);
static void update_label_raw(param_id_e param, const char *buf);
static void kb_show(param_id_e param);
static void kb_hide(void);
static void row_event_cb(lv_event_t *e);
static void kb_event_cb(lv_event_t *e);

// --- Funciones ---

void ui_scrconfig_kb_init(void)
{
    lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);

    lv_keyboard_set_map(ui_Keyboard1, LV_KEYBOARD_MODE_USER_1, kb_map, kb_ctrl);

    lv_obj_add_event_cb(ui_Keyboard1, kb_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_add_event_cb(ui_row1, row_event_cb, LV_EVENT_CLICKED, (void *)PARAM_FREC_INICIO);
    lv_obj_add_event_cb(ui_row2, row_event_cb, LV_EVENT_CLICKED, (void *)PARAM_FREC_FINAL);
    lv_obj_add_event_cb(ui_row3, row_event_cb, LV_EVENT_CLICKED, (void *)PARAM_PUNTOS);
    lv_obj_add_event_cb(ui_row4, row_event_cb, LV_EVENT_CLICKED, (void *)PARAM_TIEMPO);
}

uint32_t ui_config_get_frec_inicio(void) { return val_frec_inicio; }
uint32_t ui_config_get_frec_final(void)  { return val_frec_final; }
uint32_t ui_config_get_puntos(void)      { return val_puntos; }
uint32_t ui_config_get_tiempo(void)      { return val_tiempo; }

static void update_label(param_id_e param, uint32_t val)
{
    char tmp[16];
    bool es_frecuencia = (param == PARAM_FREC_INICIO || param == PARAM_FREC_FINAL);
    if (es_frecuencia && val >= 1000)
        snprintf(tmp, sizeof(tmp), "%lu kHz", val / 1000);
    else
        snprintf(tmp, sizeof(tmp), "%lu %s", val, UNIT[param]);
    lv_label_set_text(*val_labels[param], tmp);
}

static void update_label_raw(param_id_e param, const char *buf)
{
    char tmp[16];
    if (buf[0] != '\0')
        snprintf(tmp, sizeof(tmp), "%s %s", buf, UNIT[param]);
    else
        snprintf(tmp, sizeof(tmp), "_ %s", UNIT[param]);
    lv_label_set_text(ui_uikbdisplay, tmp);
}

static void kb_show(param_id_e param)
{
    param_activo = param;
    buf_entrada[0] = '\0';

    lv_obj_set_pos(ui_uikbdisplay, 0, 0);
    lv_obj_remove_flag(ui_uikbdisplay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui_uikbdisplay);
    lv_obj_move_foreground(ui_Keyboard1);

    update_label_raw(param, buf_entrada);
}

static void kb_hide(void)
{
    lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_uikbdisplay, LV_OBJ_FLAG_HIDDEN);
}

static void row_event_cb(lv_event_t *e)
{
    param_id_e param = (param_id_e)(uintptr_t)lv_event_get_user_data(e);
    kb_show(param);
}

static void kb_event_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    uint32_t btn_id = lv_buttonmatrix_get_selected_button(kb);
    if (btn_id == LV_BUTTONMATRIX_BUTTON_NONE)
        return;
    const char *txt = lv_buttonmatrix_get_button_text(kb, btn_id);
    if (!txt)
        return;

    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
    {
        size_t len = strlen(buf_entrada);
        if (len > 0)
            buf_entrada[len - 1] = '\0';
        update_label_raw(param_activo, buf_entrada);
    }
    else if (strcmp(txt, LV_SYMBOL_OK) == 0)
    {
        if (strlen(buf_entrada) > 0)
        {
            uint32_t val = (uint32_t)atoi(buf_entrada);
            if (val < MIN[param_activo])
                val = MIN[param_activo];
            if (val > MAX[param_activo])
                val = MAX[param_activo];

            switch (param_activo)
            {
            case PARAM_FREC_INICIO: val_frec_inicio = val; break;
            case PARAM_FREC_FINAL:  val_frec_final = val;  break;
            case PARAM_PUNTOS:      val_puntos = val;       break;
            case PARAM_TIEMPO:      val_tiempo = val;       break;
            }
            update_label(param_activo, val);
        }
        kb_hide();
    }
    else
    {
        if (strlen(buf_entrada) < KB_BUF_LEN - 2)
        {
            if (!(strlen(buf_entrada) == 0 && strcmp(txt, "0") == 0))
                strcat(buf_entrada, txt);
        }
        update_label_raw(param_activo, buf_entrada);
    }
}
