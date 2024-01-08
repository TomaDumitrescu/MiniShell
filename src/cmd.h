/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef _CMD_H
#define _CMD_H

#include <string.h>

#include "../util/parser/parser.h"

#define SHELL_EXIT -100

// error code for a command
#define ERROR -1

// success code for a command
#define SUCCESS 0

// permissions given to the file by a redirect open syscall in linux shell
#define COMMON_PERM S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH

// for set_env, creating a new variable or reassign it flag
#define DEFAULT_BEHAVIOR 1

/**
 * Parse and execute a command.
 */
int parse_command(command_t *cmd, int level, command_t *father);

#endif /* _CMD_H */
