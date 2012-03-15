#include <libgen.h>
#include "util.h"

int main(int ac, char **av)
{
    if (ac != 3)
        die("usage: %s enable|disable name", basename(av[0]));

    if (av[1][0] == 'e')
        mtrace_enable_set(1, av[2]);
    else
        mtrace_enable_set(0, av[2]);

    return 0;
}