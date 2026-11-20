#ifndef SHELL_H
#define SHELL_H

#include "types.h"

/*
 * Minimal interactive shell: reads a line from console, parses
 * builtins (help/ls/cd/cat/mkdir/rm/echo/clear/exit/run) and
 * launches apps. UTF-8 input/output.
 */
void shell_init(void);
void shell_run(void);
int  shell_exec(const char* line);

#endif /* SHELL_H */
