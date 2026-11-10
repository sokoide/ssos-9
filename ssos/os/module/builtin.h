#ifndef SS_BUILTIN_MODULE_H
#define SS_BUILTIN_MODULE_H

#include <stdint.h>
#include "module.h"

int16_t ss_builtin_modules_init(void);
int16_t ss_builtin_load(const char* name, SSModuleId* id);

#endif
