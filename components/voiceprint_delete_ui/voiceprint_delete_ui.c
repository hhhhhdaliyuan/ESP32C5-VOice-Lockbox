#include "voiceprint_delete_ui.h"

#include <stdio.h>
#include <string.h>

#include "gc9a01.h"

#define UI_BACKGROUND GC9A01_COLOR_DARKBLUE
#define UI_TEXT GC9A01_COLOR_WHITE
#define UI_MUTED GC9A01_COLOR_LGRAY
#define UI_ACCENT GC9A01_COLOR_CYAN
#define UI_WARNING GC9A01_COLOR_YELLOW
#define UI_DANGER GC9A01_COLOR_RED
#define UI_LIST_ENTRIES_PER_PAGE 2U

typedef enum {
    UI_GLYPH_SHI = 0,
    UI_GLYPH_FOU,
    UI_GLYPH_QUE,
    UI_GLYPH_REN,
    UI_GLYPH_SHAN,
    UI_GLYPH_CHU,
    UI_GLYPH_MA,
    UI_GLYPH_QU,
    UI_GLYPH_XIAO,
} voiceprint_delete_ui_glyph_t;

static const uint8_t s_glyphs[][32] = {
    { 0x00, 0x00, 0x00, 0x00, 0x07, 0xFE, 0x04, 0x02, 0x07, 0xFE, 0x04, 0x02, 0x07, 0xFE, 0x04, 0x02, 0x00, 0x00, 0x3F, 0xFF, 0x02, 0x20, 0x06, 0x3F, 0x06, 0x20, 0x0B, 0x20, 0x18, 0xFF, 0x20, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x00, 0x40, 0x00, 0xD0, 0x01, 0x4C, 0x06, 0x46, 0x08, 0x43, 0x30, 0x00, 0x0F, 0xFE, 0x08, 0x02, 0x08, 0x02, 0x08, 0x02, 0x08, 0x02, 0x0F, 0xFE, 0x08, 0x02 },
    { 0x00, 0x00, 0x00, 0x10, 0x1F, 0x3E, 0x04, 0x46, 0x04, 0x84, 0x09, 0x7F, 0x0F, 0x48, 0x19, 0x48, 0x19, 0x7F, 0x29, 0x48, 0x09, 0x48, 0x09, 0x7F, 0x0F, 0xC8, 0x08, 0x88, 0x01, 0x0B, 0x00, 0x00 },
    { 0x00, 0x00, 0x08, 0x08, 0x04, 0x08, 0x06, 0x08, 0x02, 0x08, 0x00, 0x08, 0x1C, 0x08, 0x04, 0x08, 0x04, 0x0C, 0x04, 0x14, 0x04, 0x94, 0x05, 0xB2, 0x07, 0x22, 0x06, 0x41, 0x04, 0xC1, 0x01, 0x80 },
    { 0x00, 0x00, 0x00, 0x00, 0x1E, 0xF0, 0x12, 0x94, 0x12, 0x94, 0x12, 0x94, 0x12, 0x94, 0x3F, 0xFC, 0x12, 0x94, 0x12, 0x94, 0x12, 0x94, 0x12, 0x94, 0x12, 0x90, 0x23, 0x10, 0x2F, 0x77, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x18, 0x1E, 0x18, 0x12, 0x34, 0x14, 0x62, 0x14, 0xC1, 0x15, 0xFF, 0x14, 0x08, 0x12, 0x08, 0x12, 0xFF, 0x12, 0x08, 0x1E, 0x4B, 0x10, 0x89, 0x11, 0x08, 0x10, 0x78, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x1E, 0x01, 0x12, 0x41, 0x12, 0x42, 0x12, 0x42, 0x12, 0x42, 0x12, 0x7F, 0x12, 0x00, 0x12, 0x00, 0x1E, 0xFE, 0x12, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x08, 0x90, 0x08, 0x90, 0x0F, 0x90, 0x08, 0x89, 0x08, 0x89, 0x0F, 0x89, 0x08, 0x8E, 0x08, 0x86, 0x0B, 0xE6, 0x1C, 0x9B, 0x00, 0xB1, 0x00, 0xE0, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x11, 0x08, 0x08, 0x89, 0x04, 0xCA, 0x00, 0x48, 0x10, 0x08, 0x09, 0xFF, 0x01, 0x00, 0x09, 0xFF, 0x09, 0x00, 0x09, 0xFF, 0x11, 0x00, 0x11, 0x00, 0x11, 0x0F, 0x00, 0x00 },
};

static esp_err_t ui_draw_line(uint8_t line, const char *text, uint16_t color)
{
    return gc9a01_draw_string(line, 2, text, color, UI_BACKGROUND);
}

static void ui_ascii_copy(char *output, size_t output_size, const char *input,
                          size_t max_chars)
{
    size_t used = 0;

    if (!output || output_size == 0) {
        return;
    }
    if (!input) {
        output[0] = '\0';
        return;
    }
    for (; *input && used < max_chars && used + 1 < output_size; input++) {
        output[used++] = (*input >= 0x20 && *input <= 0x7e) ? *input : '?';
    }
    output[used] = '\0';
}

static esp_err_t ui_draw_title(const char *title, uint16_t accent)
{
    esp_err_t ret = gc9a01_fill(UI_BACKGROUND);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_fill_rect(16, 12, 223, 39, accent);
    if (ret != ESP_OK) {
        return ret;
    }
    return gc9a01_draw_string(2, 5, title, UI_TEXT, accent);
}

static esp_err_t ui_draw_chinese(uint16_t x, uint16_t y,
                                 const voiceprint_delete_ui_glyph_t *glyphs,
                                 size_t count, uint16_t color)
{
    for (size_t i = 0; i < count; i++) {
        esp_err_t ret = gc9a01_draw_glyph16(x + (uint16_t)(i * 16U), y,
                                             s_glyphs[glyphs[i]], color,
                                             UI_BACKGROUND);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t voiceprint_delete_ui_show_loading(void)
{
    esp_err_t ret = ui_draw_title("VOICEPRINT DELETE", UI_ACCENT);
    if (ret != ESP_OK) {
        return ret;
    }
    return ui_draw_line(8, "LOADING REGISTERED LIST", UI_TEXT);
}

esp_err_t voiceprint_delete_ui_show_list(const voiceprint_speaker_t *speakers,
                                         size_t count, size_t selected)
{
    esp_err_t ret = ui_draw_title("VOICEPRINT DELETE", UI_ACCENT);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!speakers || selected > count) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *header = "REGISTERED LIST";
    ret = ui_draw_line(4, header, UI_MUTED);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t total_entries = count + 1U;
    size_t first = (selected / UI_LIST_ENTRIES_PER_PAGE)
                   * UI_LIST_ENTRIES_PER_PAGE;
    for (size_t index = first; index < total_entries
         && index < first + UI_LIST_ENTRIES_PER_PAGE; index++) {
        uint8_t line = (uint8_t)(6U + (index - first) * 4U);
        char field[30];

        if (index == count) {
            snprintf(field, sizeof(field), "%c EXIT MENU",
                     index == selected ? '>' : ' ');
            ret = ui_draw_line(line, field,
                               index == selected ? UI_WARNING : UI_TEXT);
            if (ret != ESP_OK) {
                return ret;
            }
            ret = ui_draw_line((uint8_t)(line + 1U),
                               "  LONG PRESS TO EXIT", UI_MUTED);
            if (ret != ESP_OK) {
                return ret;
            }
            continue;
        }

        char name[20];
        char id[26];
        char created[20];
        ui_ascii_copy(name, sizeof(name), speakers[index].display_name, 18U);
        if (name[0] == '\0') {
            ui_ascii_copy(name, sizeof(name), speakers[index].speaker_id, 18U);
        }
        ui_ascii_copy(id, sizeof(id), speakers[index].speaker_id, 24U);
        ui_ascii_copy(created, sizeof(created), speakers[index].created_at, 16U);
        if (strlen(created) > 10U && created[10] == 'T') {
            created[10] = ' ';
        }

        snprintf(field, sizeof(field), "%c NAME:%.18s",
                 index == selected ? '>' : ' ', name);
        ret = ui_draw_line(line, field,
                           index == selected ? UI_WARNING : UI_TEXT);
        if (ret != ESP_OK) {
            return ret;
        }
        snprintf(field, sizeof(field), "  ID:%.24s", id);
        ret = ui_draw_line((uint8_t)(line + 1U), field, UI_MUTED);
        if (ret != ESP_OK) {
            return ret;
        }
        snprintf(field, sizeof(field), "  REG:%.16s", created);
        ret = ui_draw_line((uint8_t)(line + 2U), field, UI_MUTED);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t voiceprint_delete_ui_show_confirmation(
    const voiceprint_speaker_t *speaker, bool confirm_selected)
{
    static const voiceprint_delete_ui_glyph_t prompt[] = {
        UI_GLYPH_SHI, UI_GLYPH_FOU, UI_GLYPH_QUE, UI_GLYPH_REN,
        UI_GLYPH_SHAN, UI_GLYPH_CHU, UI_GLYPH_MA,
    };
    static const voiceprint_delete_ui_glyph_t confirm[] = {
        UI_GLYPH_QUE, UI_GLYPH_REN,
    };
    static const voiceprint_delete_ui_glyph_t cancel[] = {
        UI_GLYPH_QU, UI_GLYPH_XIAO,
    };
    char text[28];
    char value[26];
    esp_err_t ret = ui_draw_title("VOICEPRINT DELETE", UI_DANGER);
    if (ret != ESP_OK || !speaker) {
        return ret != ESP_OK ? ret : ESP_ERR_INVALID_ARG;
    }

    ui_ascii_copy(value, sizeof(value), speaker->display_name, 20U);
    snprintf(text, sizeof(text), "NAME:%.20s", value);
    ret = ui_draw_line(4, text, UI_TEXT);
    if (ret != ESP_OK) {
        return ret;
    }
    ui_ascii_copy(value, sizeof(value), speaker->speaker_id, 24U);
    snprintf(text, sizeof(text), "ID:%.24s", value);
    ret = ui_draw_line(6, text, UI_MUTED);
    if (ret != ESP_OK) {
        return ret;
    }
    ui_ascii_copy(value, sizeof(value), speaker->created_at, 16U);
    if (strlen(value) > 10U && value[10] == 'T') {
        value[10] = ' ';
    }
    snprintf(text, sizeof(text), "REG:%.16s", value);
    ret = ui_draw_line(8, text, UI_MUTED);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = ui_draw_chinese(56U, 120U, prompt,
                          sizeof(prompt) / sizeof(prompt[0]), UI_TEXT);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_draw_char(9, 20, '?', UI_TEXT, UI_BACKGROUND);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_draw_char(11, 8, confirm_selected ? '>' : ' ',
                            UI_WARNING, UI_BACKGROUND);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = ui_draw_chinese(72U, 160U, confirm,
                          sizeof(confirm) / sizeof(confirm[0]), UI_TEXT);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_draw_char(11, 17, confirm_selected ? ' ' : '>',
                            UI_WARNING, UI_BACKGROUND);
    if (ret != ESP_OK) {
        return ret;
    }
    return ui_draw_chinese(144U, 160U, cancel,
                           sizeof(cancel) / sizeof(cancel[0]), UI_TEXT);
}

esp_err_t voiceprint_delete_ui_show_result(bool success)
{
    esp_err_t ret = ui_draw_title("VOICEPRINT DELETE",
                                  success ? UI_ACCENT : UI_DANGER);
    if (ret != ESP_OK) {
        return ret;
    }
    return ui_draw_line(8, success ? "DELETE COMPLETE" : "DELETE FAILED",
                        success ? UI_TEXT : UI_WARNING);
}

esp_err_t voiceprint_delete_ui_show_error(const char *message)
{
    esp_err_t ret = ui_draw_title("VOICEPRINT DELETE", UI_DANGER);
    if (ret != ESP_OK) {
        return ret;
    }
    return ui_draw_line(8, message ? message : "REQUEST FAILED", UI_WARNING);
}

esp_err_t voiceprint_delete_ui_show_deleting(void)
{
    esp_err_t ret = ui_draw_title("VOICEPRINT DELETE", UI_DANGER);
    if (ret != ESP_OK) {
        return ret;
    }
    return ui_draw_line(8, "DELETING SELECTED VOICE", UI_TEXT);
}
