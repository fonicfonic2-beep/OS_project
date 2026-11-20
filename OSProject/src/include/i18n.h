#ifndef I18N_H
#define I18N_H

#include "types.h"

typedef enum lang_id {
    LANG_VI = 0,
    LANG_EN = 1,
    LANG_COUNT
} lang_id_t;

/* Translation keys (string identifiers used with tr()) */
#define L_KEY_HELP         "help"
#define L_KEY_LS           "ls"
#define L_KEY_CD           "cd"
#define L_KEY_PWD          "pwd"
#define L_KEY_MKDIR        "mkdir"
#define L_KEY_RM           "rm"
#define L_KEY_CAT          "cat"
#define L_KEY_ECHO         "echo"
#define L_KEY_CLEAR        "clear"
#define L_KEY_EXIT         "exit"
#define L_KEY_WELCOME      "welcome"
#define L_KEY_PROMPT       "prompt"
#define L_KEY_ERROR        "error"
#define L_KEY_UNKNOWN_CMD  "unknown_cmd"
#define L_KEY_LANGUAGE     "language"
#define L_KEY_SELECT_LANG  "select_lang"
#define L_KEY_FILE_NOT_FOUND "file_not_found"
#define L_KEY_NOT_A_DIR    "not_a_dir"
#define L_KEY_PERM_DENIED  "perm_denied"
#define L_KEY_APPS         "apps"
#define L_KEY_EDITOR       "editor"
#define L_KEY_CALCULATOR   "calculator"
#define L_KEY_FILEMANAGER  "filemanager"
#define L_KEY_TASKMANAGER  "taskmanager"
#define L_KEY_CLOCK        "clock"

void        i18n_set(lang_id_t l);
lang_id_t   i18n_get(void);
const char* tr(const char* key);   /* returns key itself if no translation */
int         i18n_boot_menu(void);  /* interactive selection, returns lang_id_t */

#endif /* I18N_H */
