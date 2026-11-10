#include "ssos9/user.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    char message[] = {'H', 'e', 'l', 'l', 'o', '\n'};
    return ss9_write(1, message, sizeof(message)) == sizeof(message) ? 0 : 1;
}
