#ifndef _SHELL_COMMANDS_H
#define _SHELL_COMMANDS_H

#include "header/shell/shell.h"

void cmd_cd(int argc, char **argv, struct ShellIO *io);
void cmd_ls(int argc, char **argv, struct ShellIO *io);
void cmd_mkdir(int argc, char **argv, struct ShellIO *io);
void cmd_grep(int argc, char **argv, struct ShellIO *io);
void cmd_cp(int argc, char **argv, struct ShellIO *io);
void cmd_rm(int argc, char **argv, struct ShellIO *io);

#endif
