/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef _UTILS_H
#define _UTILS_H

#include "../util/parser/parser.h"
#include <stdio.h>
#include <stdlib.h>

#define EXIT_FAILURE 1

/* Useful macro for handling error codes. */
#define DIE(assertion, call_description)			\
	do {							\
		if (assertion) {				\
			fprintf(stderr, "(%s, %s, %d): ",	\
				__FILE__, __func__, __LINE__);	\
			perror(call_description);		\
			exit(EXIT_FAILURE);			\
		}						\
	} while (0)

/**
 * Counts the number of parts in a word
*/
int word_count(word_t *s);

/**
 * Concatenate parts of the word to obtain the command.
 */
char *get_word(word_t *s);

/**
 * Concatenate command arguments in a NULL terminated list in order to pass
 * them directly to execv.
 */
char **get_argv(simple_command_t *command, int *size);

#endif /* _UTILS_H */
