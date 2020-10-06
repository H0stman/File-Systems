#include <iostream>
#include "fs.h"

#ifndef __SHELL_H__
#define __SHELL_H__

class Shell {
private:
    FS filesystem;
public:
    Shell();
    ~Shell();
    void run();
};

#endif // __SHELL_H__
