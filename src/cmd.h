/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef _CMD_H
#define _CMD_H

#include "../util/parser/parser.h"
#include <string.h>

#define SHELL_EXIT -100
#define ERROR -1
#define COMMON_PERM S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
#define ARG_LEN 30
#define DEFAULT_BEHAVIOR 1

/**
 * Parse and execute a command.
 */
int parse_command(command_t *cmd, int level, command_t *father);

#endif /* _CMD_H */
