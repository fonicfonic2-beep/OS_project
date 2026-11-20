#ifndef APPS_H
#define APPS_H

#include "types.h"

/* Built-in user-space / kernel-space application entry points */
void app_editor(const char* file);
void app_calculator(void);
void app_filemanager(void);
void app_taskmanager(void);
void app_clock(void);
void app_help(void);

#endif /* APPS_H */
