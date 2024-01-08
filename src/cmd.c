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
	return !(dir && dir->string && chdir(dir->string) == 0);
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
			DIE(dup2(fd, file_dest) == ERROR, "dup2");
			DIE(dup2(fd, file_dest2) == ERROR, "dup2");
		}
		DIE(close(fd) != SUCCESS, "close");

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
		DIE(dup2(fd, file_dest) == ERROR, "dup2");
	DIE(close(fd) != SUCCESS, "close");
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
			DIE(close(fd) != SUCCESS, "close");
		}

		// negation due to the fact that returns true when a directory is changed
		return shell_cd(s->params);
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
			value = getenv(last_part + SKIP_DOLLAR);
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
	case ERROR:
		DIE(true, "fork");
		break;
	case CHILD:
		;	// label error
		int stop = 0;

		do_redirect(true, s->in, STDIN_FILENO, false, true, NULL, JUNK_VALUE, false, &stop);
		do_redirect(true, s->out, STDOUT_FILENO, s->io_flags & IO_OUT_APPEND, false, s->err,
					STDERR_FILENO, s->io_flags & IO_ERR_APPEND, &stop);
		do_redirect(true, s->err, STDERR_FILENO, s->io_flags & IO_ERR_APPEND, false, NULL,
					JUNK_VALUE, false, &stop);

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
		DIE(waitpid(pid, &status, DEFAULT_OPTIONS) == ERROR, "waitpid");

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
	/*
	 * Running the commands in parallel will result in the following process tree:
	 * initial_process - cmd1_process -> exit
	 *				   - initial process - cmd2_process -> exit
	 *									 - initial process -> wait for cmd1, cmd 2 and return
	 */

	pid_t cmd1_pid = fork();
	int cmd1_status, cmd2_status;

	switch (cmd1_pid) {
	case ERROR:
		DIE(true, "fork");
		break;
	case CHILD:
		// the exit code of the process is the result code of parse_command
		exit(parse_command(cmd1, level, father));
		break;
	}

	// This code is accessed only by initial_process, the parent of cmd1_process
	pid_t cmd2_pid = fork();

	switch (cmd2_pid) {
	case ERROR:
		DIE(true, "fork");
		break;
	case CHILD:
		exit(parse_command(cmd2, level, father));
		break;
	default:
		// the parent of cmd2_process will wait for both processes
		DIE(waitpid(cmd1_pid, &cmd1_status, DEFAULT_OPTIONS) == ERROR, "waitpid");
		DIE(waitpid(cmd2_pid, &cmd2_status, DEFAULT_OPTIONS) == ERROR, "waitpid");
	}

	// both cmd1 and cmd2 result code counts to the final result code
	return !(__WIFEXITED(cmd1_status) && __WEXITSTATUS(cmd1_status) == SUCCESS
		   && __WIFEXITED(cmd2_status) && __WEXITSTATUS(cmd2_status) == SUCCESS);
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	// creating a pipe
	int pipe_channel[2];

	if (pipe(pipe_channel) != SUCCESS)
		return true;

	pid_t cmd1_pid = fork();
	int cmd1_status;

	// exit code of this process does not affect the pipe result code
	switch (cmd1_pid) {
	case ERROR:
		DIE(close(pipe_channel[READ]) != SUCCESS, "close");
		DIE(close(pipe_channel[WRITE]) != SUCCESS, "close");
		DIE(true, "fork");
	case CHILD:
		DIE(close(pipe_channel[READ]) != SUCCESS, "close");
		dup2(pipe_channel[WRITE], STDOUT_FILENO);
		DIE(close(pipe_channel[WRITE]) != SUCCESS, "close");

		// the exit code of the process is obtained from actually running command 1
		exit(parse_command(cmd1, level, father));
	}

	pid_t cmd2_pid = fork();
	int cmd2_status;

	switch (cmd2_pid) {
	case ERROR:
		DIE(close(pipe_channel[READ]) != SUCCESS, "close");
		DIE(close(pipe_channel[WRITE]) != SUCCESS, "close");
		DIE(true, "fork");

		// execution of the final process failed, returning failure code 1
		return true;
	case CHILD:
		// cmd1 output redirection mechanism to cmd2 input
		DIE(close(pipe_channel[WRITE]) != SUCCESS, "close");
		dup2(pipe_channel[READ], STDIN_FILENO);
		DIE(close(pipe_channel[READ]) != SUCCESS, "close");

		exit(parse_command(cmd2, level, father));
	}

	DIE(close(pipe_channel[READ]) != SUCCESS, "close");
	DIE(close(pipe_channel[WRITE]) != SUCCESS, "close");

	// firstly, wait for the execution of cmd1, then for the cmd2 with special input
	DIE(waitpid(cmd1_pid, &cmd1_status, DEFAULT_OPTIONS) == ERROR, "waitpit");
	DIE(waitpid(cmd2_pid, &cmd2_status, DEFAULT_OPTIONS) == ERROR, "waitpid");

	/** only the cmd2 exit code matters, taking the negated value of the result code,
	 * because run_on_pipe succeeds when returning false (success code)
	 */
	return !(__WIFEXITED(cmd2_status) && __WEXITSTATUS(cmd2_status) == SUCCESS);
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
		cmd_exit = !run_in_parallel(c->cmd1, c->cmd2, level + 1, c);
		break;

	case OP_CONDITIONAL_NZERO:
		cmd_exit = parse_command(c->cmd1, level + 1, c);
		if (cmd_exit != SUCCESS)
			cmd_exit = parse_command(c->cmd2, level + 1, c);
		break;

	case OP_CONDITIONAL_ZERO:
		cmd_exit = parse_command(c->cmd1, level + 1, c);
		if (cmd_exit == SUCCESS)
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
