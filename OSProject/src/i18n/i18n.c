/*
 * i18n/i18n.c — minimal internationalisation layer.
 *
 * Maintains the currently selected language and translates string keys
 * via tr(). If a key is missing from the active language table, the key
 * itself is returned so output never becomes blank. Default language is
 * Vietnamese. i18n_boot_menu() asks the user to pick 1 (VI) or 2 (EN).
 */
#include "i18n.h"
#include "types.h"
#include "console.h"
#include "keyboard.h"
#include "string.h"
#include "stdio.h"

/* Translation tables provided by lang_vi.c / lang_en.c */
extern const char* const lang_vi_keys[];
extern const char* const lang_vi_values[];
extern const int         lang_vi_count;
extern const char* const lang_en_keys[];
extern const char* const lang_en_values[];
extern const int         lang_en_count;

static lang_id_t g_current_lang = LANG_VI;

void i18n_set(lang_id_t l)
{
    if (l >= 0 && l < LANG_COUNT) {
        g_current_lang = l;
    }
}

lang_id_t i18n_get(void)
{
    return g_current_lang;
}

const char* tr(const char* key)
{
    const char* const* keys;
    const char* const* values;
    int count;
    int i;

    if (key == NULL) {
        return "";
    }

    if (g_current_lang == LANG_EN) {
        keys   = lang_en_keys;
        values = lang_en_values;
        count  = lang_en_count;
    } else {
        keys   = lang_vi_keys;
        values = lang_vi_values;
        count  = lang_vi_count;
    }

    for (i = 0; i < count; i++) {
        if (strcmp(keys[i], key) == 0) {
            return values[i];
        }
    }
    return key;
}

/*
 * Interactive language selection. Prints the menu in both languages,
 * blocks until the user presses '1' or '2', applies the choice, and
 * returns the selected lang_id_t (as int per the frozen prototype).
 */
int i18n_boot_menu(void)
{
    int c;

    console_clear();
    console_printf("========================================\n");
    console_printf("  %s\n", tr("boot_title"));
    console_printf("  %s\n", tr("press_1_vi"));
    console_printf("  %s\n", tr("press_2_en"));
    console_printf("========================================\n");
    console_printf("> ");

    for (;;) {
        c = console_getchar();
        if (c == '1') {
            i18n_set(LANG_VI);
            console_putc('\n');
            console_printf("%s\n", tr("lang_changed"));
            return (int)LANG_VI;
        }
        if (c == '2') {
            i18n_set(LANG_EN);
            console_putc('\n');
            console_printf("%s\n", tr("lang_changed"));
            return (int)LANG_EN;
        }
        /* Any other key is ignored until a valid choice arrives. */
    }
}
