#include <stdlib.h>

int unsetenv(const char *name) {
  return putenv((char*)name);
}

