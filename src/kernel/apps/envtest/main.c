#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syscalls.h>


int main(int argc, char **argv)
{
	int i, rc;
	team_id pid;
	char temp[16];
	char *var;
	
	printf("Setting some env vars...\n");
	
	if (setenv("TEST", "This is a test!", 0)) {
		printf("setenv() error\n");
		return -1;
	}
	if (setenv("HOME", "/boot", 1)) {
		printf("setenv() error\n");
		return -1;
	}
	
	printf("Getting VAR_1: ");
	var = getenv("VAR_1");
	if (var)
		printf("found set to: \"%s\"\n", var);
	else
		printf("not found\n");
	
	printf("List of env variables set:\n");
	for (i=0; environ[i]; i++)
		printf("%s\n", environ[i]);
	
	if (argc > 1) {
		char buffer[16];
		char *_argv[] = { argv[0], buffer, NULL };
		int val = atoi(argv[1]) - 1;
		
		sprintf(temp, "VAR_%d", val);
		if (setenv(temp, "dummy", 0)) {
			printf("setenv() error\n");
			return -1;
		}
		
		if (val > 0) {
			printf("Spawning test (%d left)\n", val);
			sprintf(buffer, "%d", val);
			pid = sys_create_team(_argv[0], _argv[0], _argv, 2, NULL, 0, 5);
			sys_wait_on_team(pid, &rc);
		}
	}
	return 0;
}


