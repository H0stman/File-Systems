#include "shell.h"
#include "fs.h"
#include "disk.h"

int
main(int argc, char **argv)
{
    Shell shell;
    shell.run();
    return 0;
}
