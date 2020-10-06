#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "shell.h"
#include "fs.h"

std::string commands_str[] = {
    "format", "create", "cat", "ls",
    "cp", "mv", "rm", "append",
    "mkdir", "cd", "pwd",
    "chmod",
    "help", "quit"
};

Shell::Shell()
{
    std::cout << "Starting shell...\n";
}

Shell::~Shell()
{
    std::cout << "Exiting shell...\n";
}

void
Shell::run()
{
    bool running = true;
    std::string line;
    std::string str;
    char c;
    std::vector<std::string> cmd_line;
    std::string cmd, arg1, arg2;
    int ret_val = 0;
    while (running)
    {
        std::cout << "filesystem> ";
        std::getline(std::cin, line);
        std::stringstream linestream(line);
        cmd_line.clear();
        str.clear();
        while (linestream.get(c))
        {
            //std::cout << "parsing cmd line: " << c << "\n";
            if (c != ' ')
                str += c;
            else
            {
                // strip multiple blanks
                if (!str.empty())
                {
                    cmd_line.push_back(str);
                    str.clear();
                }
            }
        }
        if (!str.empty())
            cmd_line.push_back(str);
        if (line.empty())
            cmd = "";
        else
            cmd = cmd_line[0];

        if (DEBUG)
        {
            std::cout << "Line: " << line << std::endl;
            std::cout << "cmd: " << cmd << std::endl;
            for (unsigned i = 0; i < cmd_line.size(); ++i)
                std::cout << "cmd/arg: " << cmd_line[i] << "\n";
        }

        if (cmd == "format")
        {
            if (cmd_line.size() != 1)
            {
                std::cout << "Usage: format\n";
                continue;
            }
            // check return value so everything is ok
            ret_val = filesystem.format();
            if (ret_val)
                std::cout << "Error: format failed, error code " << ret_val << std::endl;
        }

        else if (cmd == "create")
        {
            if (cmd_line.size() != 2)
            {
                std::cout << "Usage: create <file>\n";
                continue;
            }
            arg1 = cmd_line[1];
            std::cout << "Enter data. Empty line to end.\n";
            // check return value so everything is ok
            ret_val = filesystem.create(arg1);
            if (ret_val)
            {
                std::cout << "Error: create " << arg1;
                std::cout << " failed, error code " << ret_val << std::endl;
            }
        }

        else if (cmd == "cat")
        {
            if (cmd_line.size() != 2)
            {
                std::cout << "Usage: cat <file>\n";
                continue;
            }
            arg1 = cmd_line[1];
            // check return value so everything is ok
            ret_val = filesystem.cat(arg1);
            if (ret_val)
            {
                std::cout << "Error: cat " << arg1;
                std::cout << " failed, error code " << ret_val << std::endl;
            }
        }

        else if (cmd == "ls")
        {
            if (cmd_line.size() != 1)
            {
                std::cout << "Usage: ls\n";
                continue;
            }
            // check return value so everything is ok
            ret_val = filesystem.ls();
            if (ret_val)
                std::cout << "Error: ls failed, error code " << ret_val << std::endl;
        }

        else if (cmd == "cp")
        {
            if (cmd_line.size() != 3)
            {
                std::cout << "Usage: <oldfile> <newfile>\n";
                continue;
            }
            arg1 = cmd_line[1];
            arg2 = cmd_line[2];
            // check return value so everything is ok
            ret_val = filesystem.cp(arg1, arg2);
            if (ret_val)
            {
                std::cout << "Error: cp " << arg1 << " " << arg2;
                std::cout << " failed, error code " << ret_val << std::endl;
            }
        }

        else if (cmd == "mv")
        {
            if (cmd_line.size() != 3)
            {
                std::cout << "Usage: mv <sourcepath> <destpath>\n";
                continue;
            }
            arg1 = cmd_line[1];
            arg2 = cmd_line[2];
            // check return value so everything is ok
            ret_val = filesystem.mv(arg1, arg2);
            if (ret_val)
            {
                std::cout << "Error: mv " << arg1 << " " << arg2;
                std::cout << " failed, error code " << ret_val << std::endl;
            }
        }

        else if (cmd == "rm")
        {
            if (cmd_line.size() != 2)
            {
                std::cout << "Usage: rm <file>\n";
                continue;
            }
            arg1 = cmd_line[1];
            // check return value so everything is ok
            ret_val = filesystem.rm(arg1);
            if (ret_val)
            {
                std::cout << "Error: rm " << arg1;
                std::cout << " failed, error code " << ret_val << std::endl;
            }
        }

        else if (cmd == "append")
        {
            if (cmd_line.size() != 3)
            {
                std::cout << "Usage: append <filepath1> <filepath2>\n";
                continue;
            }
            arg1 = cmd_line[1];
            arg2 = cmd_line[2];
            // check return value so everything is ok
            ret_val = filesystem.append(arg1, arg2);
            if (ret_val)
            {
                std::cout << "Error: append " << arg1 << " " << arg2;
                std::cout << " failed, error code " << ret_val << std::endl;
            }
        }

        else if (cmd == "mkdir")
        {
            if (cmd_line.size() != 2)
            {
                std::cout << "Usage: mkdir <dirpath>\n";
                continue;
            }
            arg1 = cmd_line[1];
            // check return value so everything is ok
            ret_val = filesystem.mkdir(arg1);
            if (ret_val)
            {
                std::cout << "Error: mkdir " << arg1;
                std::cout << " failed, error code " << ret_val << std::endl;
            }
        }

        else if (cmd == "cd")
        {
            if (cmd_line.size() != 2)
            {
                std::cout << "Usage: cd <dirpath>\n";
                continue;
            }
            arg1 = cmd_line[1];
            // check return value so everything is ok
            ret_val = filesystem.cd(arg1);
            if (ret_val)
            {
                std::cout << "Error: cd " << arg1;
                std::cout << " failed, error code " << ret_val << std::endl;
            }
        }

        else if (cmd == "pwd")
        {
            if (cmd_line.size() != 1)
            {
                std::cout << "Usage: pwd\n";
                continue;
            }
            // check return value so everything is ok
            ret_val = filesystem.pwd();
            if (ret_val)
                std::cout << "Error: pwd failed, error code " << ret_val << std::endl;
        }

        else if (cmd == "chmod")
        {
            if (cmd_line.size() != 3)
            {
                std::cout << "Usage: chmod <accessrights> <filepath>\n";
                continue;
            }
            arg1 = cmd_line[1];
            arg2 = cmd_line[2];
            // check return value so everything is ok
            ret_val = filesystem.chmod(arg1, arg2);
            if (ret_val)
            {
                std::cout << "Error: chmod " << arg1 << " " << arg2;
                std::cout << " failed, error code " << ret_val << std::endl;
            }
        }

        else if (cmd == "quit")
            running = false;

        else if (cmd == "help")
        {
            std::cout << "Available commands:\n";
            std::cout << "format, create, cat, ls, cp, mv, rm, append, mkdir, cd, pwd, chmod, help, quit\n";
        }

        else if (cmd == "")
        {
            ; // do nothing
        }

        else
        {
            std::cout << "Available commands:\n";
            std::cout << "format, create, cat, ls, cp, mv, rm, append, mkdir, cd, pwd, chmod, help, quit\n";
        }
    }
}
