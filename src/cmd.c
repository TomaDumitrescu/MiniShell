// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	// logical short-circuiting
	return dir && dir->string && chdir(dir->string) == 0;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	return SHELL_EXIT;
}

/**
 * Redirect for in, out, err
 */
static void do_redirect(bool act_redirect, word_t *file, int file_dest, bool append, bool in,
						word_t *file2, int file_dest2, bool append2, int *stop)
{
	if (!file || !file->string || *stop == 1)
		return;

	int fd;
	char *file_name = get_word(file), *file_name2 = get_word(file);

	// &> redirection type
	if (file2 && strcmp(file->string, file2->string) == 0) {
		if (append || append2)
			fd = open(file_name, O_CREAT | O_WRONLY | O_APPEND, COMMON_PERM);
		else
			fd = open(file_name2, O_CREAT | O_WRONLY | O_TRUNC, COMMON_PERM);

		DIE(fd < 0, "open");
		if (act_redirect) {
			DIE(dup2(fd, file_dest) < 0, "dup2");
			DIE(dup2(fd, file_dest2) < 0, "dup2");
		}
		DIE(close(fd) != 0, "close");

		// block the following do_redirect for stderr
		*stop = 1;

		free(file_name);
		free(file_name2);

		return;
	}

	if (!append && !in)
		fd = open(file_name, O_CREAT | O_WRONLY | O_TRUNC, COMMON_PERM);
	else if (!in)
		fd = open(file_name, O_CREAT | O_WRONLY | O_APPEND, COMMON_PERM);
	else
		fd = open(file_name, O_RDONLY, COMMON_PERM);

	free(file_name);
	free(file_name2);

	DIE(fd < 0, "open");
	// redirect method
	if (act_redirect)
		DIE(dup2(fd, file_dest) < 0, "dup2");
	DIE(close(fd) != 0, "close");
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	if (!s || !s->verb)
		return ERROR;

	word_t *verb = s->verb;

	if (verb->string && strncmp("cd", verb->string, strlen("cd")) == 0) {
		int stop = 0;

		// minishell cd and redirect to files produce only junk files
		do_redirect(false, s->out, STDOUT_FILENO, s->io_flags & IO_OUT_APPEND, false,
					s->err, STDERR_FILENO, s->io_flags & IO_ERR_APPEND, &stop);
		do_redirect(false, s->err, STDERR_FILENO, s->io_flags & IO_ERR_APPEND, false,
					s->out, STDOUT_FILENO, s->io_flags & IO_OUT_APPEND, &stop);

		if (s->err) {
			int fd = open(s->err->string, O_CREAT | O_WRONLY | O_TRUNC, COMMON_PERM);

			DIE(fd < 0, "open");
			DIE(close(fd) != 0, "close");
		}

		// negation due to the fact that returns true when a directory is changed
		return !shell_cd(s->params);
	}

	if (verb->string && (!strncmp("quit", verb->string, strlen("quit"))
		|| !strncmp("exit", verb->string, strlen("exit"))))
		return shell_exit();

	word_t *assignment = s->verb;

	if (assignment && assignment->next_part && assignment->next_part->next_part
		&& strcmp(assignment->next_part->string, "=") == 0) {
		char *last_part = get_word(assignment->next_part->next_part);
		char *value = strdup(last_part);
		bool valueChanged = false;

		// searching for the second environment variable and save its value
		if (last_part[0] == '$') {
			free(value);
			value = getenv(last_part + 1);
			valueChanged = true;
		}

		setenv(s->verb->string, value, DEFAULT_BEHAVIOR);

		// last part is the result of get_word that allocates the concatenated string
		free(last_part);
		if (!valueChanged)
			free(value);

		return SUCCESS;
	}

	// Initialize non-existent environment variables with '\0'
	pid_t pid;
	int status, argc;

	// forking the process
	pid = fork();
	switch (pid) {
	case -1:
		DIE(true, "fork");
		break;
	case 0:
		;	// label error
		int stop = 0;

		do_redirect(true, s->in, STDIN_FILENO, false, true, NULL, -1, false, &stop);
		do_redirect(true, s->out, STDOUT_FILENO, s->io_flags & IO_OUT_APPEND, false, s->err,
					STDERR_FILENO, s->io_flags & IO_ERR_APPEND, &stop);
		do_redirect(true, s->err, STDERR_FILENO, s->io_flags & IO_ERR_APPEND, false, NULL,
					-1, false, &stop);

		// setting the arguments
		char **args = get_argv(s, &argc);

		// execute the string command
		int result = execvp(args[0], (char *const *)args);

		if (result == ERROR) {
			fprintf(stderr, "Execution failed for '%s'\n", s->verb->string);
			exit(ERROR);
		}

		return SUCCESS;
	default:
		// parent process waiting for the child
		DIE(waitpid(pid, &status, 0) < 0, "waitpid");

		// command exit code is equal to process exit code
		if (__WIFEXITED(status))
			return __WEXITSTATUS(status);
	}

	return SUCCESS;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Execute cmd1 and cmd2 simultaneously. */

	return true; /* TODO: Replace with actual exit status. */
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Redirect the output of cmd1 to the input of cmd2. */

	return true; /* TODO: Replace with actual exit status. */
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	int cmd_exit = ERROR;

	if (!c || level < 0)
		return ERROR;

	switch (c->op) {
	case OP_NONE:
		cmd_exit = parse_simple(c->scmd, level, father);
		break;
	case OP_SEQUENTIAL:
		cmd_exit = parse_command(c->cmd1, level + 1, c);
		cmd_exit = parse_command(c->cmd2, level + 1, c);
		break;

	case OP_PARALLEL:
		cmd_exit = run_in_parallel(c->cmd1, c->cmd2, level + 1, c);
		break;

	case OP_CONDITIONAL_NZERO:
		cmd_exit = parse_command(c->cmd1, level + 1, c);
		if (cmd_exit != SUCCESS)
			cmd_exit = parse_command(c->cmd2, level + 1, c);
		break;

	case OP_CONDITIONAL_ZERO:
		cmd_exit = parse_command(c->cmd1, level + 1, c);
		if (cmd_exit == 0)
			cmd_exit = parse_command(c->cmd2, level + 1, c);
		break;

	case OP_PIPE:
		cmd_exit = run_on_pipe(c->cmd1, c->cmd2, level + 1, c);
		break;

	case OP_DUMMY:
		cmd_exit = ERROR;
		break;

	default:
		cmd_exit = SHELL_EXIT;
	}

	return cmd_exit;
}
