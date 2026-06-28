// --- Includes ---
#include "ui_scrconfig_kb.h"
#include "app_common.h"
#include "ui.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// --- Defines privados ---
#define KB_BUF_LEN 8 // max "100000\0"

// --- Variables privadas ---
static const char *TAG = "ui_scrconfig_kb";

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

static sweep_param_e param_activo;
static char          buf_entrada[KB_BUF_LEN];

static const char *UNIT[] = {"Hz", "Hz", "pts", "ms"};

// --- Prototipos privados ---
static void update_label_raw(sweep_param_e param, const char *buf);
static void kb_show(sweep_param_e param);
static void kb_hide(void);
static void row_event_cb(lv_event_t *e);
static void kb_event_cb(lv_event_t *e);

// --- Funciones ---

void ui_scrconfig_kb_init(void)
{
    lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);

    lv_keyboard_set_map(ui_Keyboard1, LV_KEYBOARD_MODE_USER_1, kb_map, kb_ctrl);

    lv_obj_add_event_cb(ui_Keyboard1, kb_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_add_event_cb(ui_row1, row_event_cb, LV_EVENT_CLICKED, (void *)SWEEP_PARAM_FREC_INICIO);
    lv_obj_add_event_cb(ui_row2, row_event_cb, LV_EVENT_CLICKED, (void *)SWEEP_PARAM_FREC_FINAL);
    lv_obj_add_event_cb(ui_row3, row_event_cb, LV_EVENT_CLICKED, (void *)SWEEP_PARAM_PUNTOS);
    lv_obj_add_event_cb(ui_row4, row_event_cb, LV_EVENT_CLICKED, (void *)SWEEP_PARAM_TIEMPO);
}

static void update_label_raw(sweep_param_e param, const char *buf)
{
    char tmp[16];
    if (buf[0] != '\0')
    {
        snprintf(tmp, sizeof(tmp), "%s %s", buf, UNIT[param]);
    }
    else
    {
        snprintf(tmp, sizeof(tmp), "_ %s", UNIT[param]);
    }
    lv_label_set_text(ui_uikbdisplay, tmp);
}

static void kb_show(sweep_param_e param)
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
    sweep_param_e param = (sweep_param_e)(uintptr_t)lv_event_get_user_data(e);
    kb_show(param);
}

static void kb_event_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    uint32_t btn_id = lv_buttonmatrix_get_selected_button(kb);
    if (btn_id == LV_BUTTONMATRIX_BUTTON_NONE)
    {
        return;
    }
    const char *txt = lv_buttonmatrix_get_button_text(kb, btn_id);
    if (!txt)
    {
        return;
    }

    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
    {
        size_t len = strlen(buf_entrada);
        if (len > 0)
        {
            buf_entrada[len - 1] = '\0';
        }
        update_label_raw(param_activo, buf_entrada);
    }
    else if (strcmp(txt, LV_SYMBOL_OK) == 0)
    {
        if (strlen(buf_entrada) > 0)
        {
            menu_event_msg_t ev = {
                .type  = MENU_EVT_CONFIG_SET,
                .param = param_activo,
                .value = (uint32_t)strtoul(buf_entrada, NULL, 10),
            };
            if (xQueueSend(queue_menu_events, &ev, 0) != pdTRUE)
            {
                ESP_LOGW(TAG, "queue_menu_events llena, valor descartado");
            }
        }
        kb_hide();
    }
    else
    {
        if (strlen(buf_entrada) < KB_BUF_LEN - 2)
        {
            if (!(strlen(buf_entrada) == 0 && strcmp(txt, "0") == 0))
            {
                strcat(buf_entrada, txt);
            }
        }
        update_label_raw(param_activo, buf_entrada);
    }
}
