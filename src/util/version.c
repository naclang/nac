#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/interpreter.h"

char latest[64] = {0};

void get_latest(void) {
#ifdef _WIN32
    system("curl.exe -s -L -o latest.json https://api.github.com/repos/naclang/nac/releases/latest 2>nul");
#else
    system("curl -s -L -o latest.json https://api.github.com/repos/naclang/nac/releases/latest 2>/dev/null");
#endif

    FILE *f = fopen("latest.json", "r");
    if (!f) {
        strcpy(latest, "UNKNOWN");
        return;
    }

    char buf[8192];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    buf[len] = '\0';
    fclose(f);

    char *tag = strstr(buf, "\"tag_name\"");
    if (!tag) {
        strcpy(latest, "UNKNOWN");
        remove("latest.json");
        return;
    }

    char version[64] = {0};
    if (sscanf(tag, "\"tag_name\": \"%63[^\"]\"", version) == 1) {
        strcpy(latest, version);
    } else {
        strcpy(latest, "UNKNOWN");
    }

    remove("latest.json");
}

int compare_versions(const char *v1, const char *v2) {
    const char *ver1 = v1;
    const char *ver2 = v2;

    if (strncmp(ver1, "NaC", 3) == 0) ver1 += 3;
    if (strncmp(ver2, "NaC", 3) == 0) ver2 += 3;

    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;

    sscanf(ver1, "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(ver2, "%d.%d.%d", &major2, &minor2, &patch2);

    if (major1 != major2) return (major1 > major2) ? 1 : -1;
    if (minor1 != minor2) return (minor1 > minor2) ? 1 : -1;
    if (patch1 != patch2) return (patch1 > patch2) ? 1 : -1;

    return 0;
}
