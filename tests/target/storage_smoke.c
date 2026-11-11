/* Disposable FAT16 image: exercise the module ABI through real IOCS. */
#include "ssos9/user.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    char name[] = {'S','M','O','K','E','.','B','I','N',0};
    char message[] = {'s','t','r','e','a','m','-', 'o','k','\n'};
    char actual[10];
    int32_t fd = ss9_open(name, SS9_OPEN_CREATE);
    if (fd < 0 || ss9_write((uint32_t)fd, message, 3) != 3 ||
        ss9_write((uint32_t)fd, message + 3, 7) != 7 || ss9_close((uint32_t)fd))
        return 1;
    fd = ss9_open_read(name);
    if (fd < 0 || ss9_read((uint32_t)fd, actual, 4) != 4 ||
        ss9_read((uint32_t)fd, actual + 4, 6) != 6 ||
        ss9_read((uint32_t)fd, actual, 1) != 0 || ss9_close((uint32_t)fd))
        return 2;
    for (unsigned i = 0; i < sizeof(actual); i++)
        if (actual[i] != message[i]) return 3;
    return ss9_write(1, message, sizeof(message)) == sizeof(message) ? 0 : 4;
}
