#ifndef NAC_CONFIG_H
#define NAC_CONFIG_H

#include <stddef.h>

/* Loads .env values only when the process environment doesn't already define
 * the key. Returns 0 when the file is absent, 1 when loaded, and -1 on error. */
int nac_load_dotenv(const char *path, char *error, size_t error_cap);

#endif
