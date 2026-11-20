#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"

#define COM1_PORT  0x3F8
#define COM2_PORT  0x2F8

void serial_init(void);
void serial_write(char c);
void serial_write_str(const char* s);
void serial_write_hex(uint64_t v);
int  serial_received(void);
char serial_read(void);

#endif /* SERIAL_H */
