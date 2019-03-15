#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>

//Define for pipe
#define READ_END 0
#define WRITE_END 1
/* 
The code is written in the flow stated hereunder: 
Once process_arglist function is called, the parent process (the shell itself) ignore SIGINT and SIGCHLD, as set in prepare function
The process_arglist function checks the arglist in an "if, else if, else" statement for:
1. presence of &: BG process
2. presence of |: Pipe command
3. None of the above: Simple foreground process

In BG part: 
The parent process (shell) blocks SIGCHLD, forks
The child calls setpgid to move to a separate process group, then set's the default signal handlers, unblocks signals and execs
The parent does not wait for child, it simply unblock signals. Since the sigchld was set to ignore, no zombies will be created

In Pipe part:
The parent creates pipe, blocks signals, then forks. 
The first child set default signal handlers, unblocks signals, redirects STDOUT to the pipe and execs. 
The parent waits for the first child to finish, then forks again.
The second child sets default signal handlers, unblocks signals, redirects STDIN to the pipe and execs.
The parent unblocks signals and waits for the second child to finish.

In FG part:
The parent blocks signals then forks. 
The child sets default signal handlers, unblocks signals then execs. 
The parent unblocks signals and waits for child process to finish.
*/

// Sets IGNORE to SIGINT and SIGCHLD, default behaviour for shell
int prepare(void) {
	if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
		perror("signal");
		return 1;
	}
	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
		perror("signal");
		return 1;
	}
	return 0;
}

// Does nothing since no memory has been allocated
int finalize(void) {
	return 0;
}

/*  
Checks if command should be run as pipe.
Returns the index of | char if exists in arglist, else -1
*/
int isPipe(int count, char** arglist) {
	for (int i=0 ; i < count ; i++) {
		if (!strcmp(arglist[i],"|")) {
			return i;
		}
	}
	return -1;
}
/*
Checks if command should be run as background process
Returns 0 if & is found in arglist, else 0
*/
int isBackgroud(int count, char** arglist) {
	if (!strcmp(arglist[count-1],"&")) {
		return 1;
	}
	return 0;
}

// arglist - a list of char* arguments (words) provided by the user
// it contains count+1 items, where the last item (arglist[count]) and *only* the last is NULL
// RETURNS - 1 if should cotinue, 0 otherwise
int process_arglist(int count, char** arglist) {
	//Set up for blocking SIGINT
	sigset_t int_mask;
	sigset_t int_origMask;
	if (sigemptyset(&int_mask) == -1) {
		perror("sigemptyset");
		return 0;
	}
	if (sigaddset(&int_mask, SIGINT) == -1) {
		perror("sigaddset");
		return 0;
	}

	//Set up for blocking SIGCHLD
	sigset_t chld_mask;
	sigset_t chld_origMask;
	if (sigemptyset(&chld_mask) == -1) {
		perror("sigemptyset");
		return 0;
	}
	if (sigaddset(&chld_mask, SIGCHLD) == -1) {
		perror("sigaddset");
		return 0;
	}
	if (isBackgroud(count, arglist)) { 
		arglist[count-1] = NULL; //Discard & from arglist by replacing with NULL
		//Block SIGCHLD to allow children to set default handler
		if (sigprocmask(SIG_BLOCK, &chld_mask, &chld_origMask)) { 
			perror("sigprocmask");
			return 0;
		}		
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			return 0;
		}
		if (pid == 0) { //Child
			if (setpgid(pid, 0) == -1) { //Put in it's own process group -> BG process group
				perror("Setpgid"); 
				exit(1);
			} 
			if (signal(SIGINT, SIG_DFL) == SIG_ERR) { //Set default handling for sigchld
				perror("signal");
				exit(1);
			}
			if (sigprocmask(SIG_UNBLOCK, &chld_mask, &chld_origMask)) { //Unblock sigchld
				perror("sigprocmask");
				exit(1);
			}
			if (execvp(arglist[0], arglist) == -1) {
				perror("Execvp, Invalid command");
				exit(1);
			}
		}
		else { //Parent
			if (sigprocmask(SIG_UNBLOCK, &chld_mask, &chld_origMask)) { //Unblock sigchld
				perror("sigprocmask");
				return 0;
			}
		}
	} //End of BG
	else if (isPipe(count, arglist) != -1) { 
		int fd[2];
		int status;
		int pipeCharIndex = isPipe(count, arglist);
		arglist[pipeCharIndex] = NULL; //Replace | char in arglist with NULL
		if (pipe(fd) < 0) {
			perror("Pipe");
			return 0;
		}
		//Block sig_int to allow child to set default handler
		if (sigprocmask(SIG_BLOCK, &int_mask, &int_origMask) < 0) {
			perror("sigprocmask");
			return 0;
		}
		pid_t firstProg = fork();
		if (firstProg < 0) {
			perror("Fork");
			return 0;
		}
		if (firstProg == 0) { //First child
			if (signal(SIGINT, SIG_DFL) == SIG_ERR) { //Set default SIGINT handler
				perror("signal");
				exit(1);
			}
			if (sigprocmask(SIG_UNBLOCK, &int_mask, &int_origMask)) { //Unblock SIGINT
				perror("sigprocmask");
				exit(1);
			}
			close(fd[READ_END]);
			if (dup2(fd[WRITE_END], 1) < 0) { //Redirect stdout to pipe
				perror("dup2");
				exit(1);
			} 
			close(fd[WRITE_END]); //Close after duplication
			if (execvp(arglist[0], arglist)== -1) { //Run first prog
				perror("Execvp, invalid command");
				exit(1);
			}
		}
		else { //Parent		
			pid_t secondProg = fork();
			if (secondProg < 0) {
				perror("Fork");
				return 0;
			}
			if (secondProg == 0) {//Second child
				if (signal(SIGINT, SIG_DFL) == SIG_ERR) { //Set default SIGINT handler
					perror("signal");
					exit(1);
				}
				if (sigprocmask(SIG_UNBLOCK, &int_mask, &int_origMask)) { //Unblock SIGINT
					perror("sigprocmask");
					exit(1);
				}
				close(fd[WRITE_END]);
				if (dup2(fd[READ_END], 0) < 0) { //Redirect stdin to pipe
					perror("dup2");
					exit(1);
				} 
				close(fd[READ_END]);
				if (execvp(arglist[pipeCharIndex+1], arglist+pipeCharIndex+1) == -1) { 
					perror("Execvp, Invalid command");
					exit(1);
				}
			}
			else { //Parent again
				if (sigprocmask(SIG_UNBLOCK, &int_mask, &int_origMask)) { //Unblock SIGINT
					perror("sigprocmask");
					return 0;
				}
				if(!((waitpid(firstProg, &status, 0) == -1) && (errno == ECHILD))) { //Wait for first program
					perror("waitpid");
					return 0;
				} 
				if(!((waitpid(secondProg, &status, 0) == -1) && (errno == ECHILD))) { //Wait for second program
					perror("waitpid");
					return 0;
				} 
				//Close pipe
				close(fd[READ_END]);
				close(fd[WRITE_END]);
			}
		}
	}
	else { //Simple Foreground task, no pipe
		if (sigprocmask(SIG_BLOCK, &int_mask, &int_origMask) < 0) { //Block SIGINT to allow child to set handler
			perror("sigprocmask");
			return 0;
		}
		int pid = fork();
		if (pid == 0) { //Child
			if (signal(SIGINT, SIG_DFL) == SIG_ERR) { //Set default handler for child process
				perror("signal");
				exit(1);
			}
			if (sigprocmask(SIG_UNBLOCK, &int_mask, &int_origMask)) { //Unblock SIGINT
				perror("sigprocmask");
				exit(1);
			}
			if (execvp(arglist[0], arglist) < 0) {
				perror("Execvp, Invalid command");
				exit(1);
			}
		}
		else { //Parent 
			if (sigprocmask(SIG_UNBLOCK, &int_mask, &int_origMask)) { //Unblock SIGINT
				perror("sigprocmask");
				return 0;
			}
			int status;
			if(!((waitpid(pid, &status, 0) == -1) && (errno == ECHILD))) {
				perror("waitpid");
				return 0;
			}
		}
	}
	return 1;
}