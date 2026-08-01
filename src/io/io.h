#ifndef NAC_IO_H
#define NAC_IO_H

char *read_file(const char *filename);
void console_print(const char *text);
int console_read_line(char *buffer, int size);

#endif
