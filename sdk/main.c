#include "vsdos.h"

int main(void)
{
    static const char message[] = "Hello World!\r\n";

    vsdos_print(message, sizeof(message) - 1);
    return 0;
}
