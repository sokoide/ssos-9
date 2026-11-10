#ifndef SS_QEMU_IOCS_H
#define SS_QEMU_IOCS_H

void _iocs_b_print(const char* text);
int _iocs_b_putc(int ch);
int _iocs_b_read(int mode, int position, int size, void* out);
void _iocs_b_clr_st(void);
void _iocs_b_locate(int x, int y);
int _iocs_b_keyinp(void);
int _iocs_b_keysns(void);

#endif
