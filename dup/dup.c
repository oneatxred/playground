/*
 * Redirect stdout to file.
 * We cant just close(1), do fd = open(file...
 * and hope that fd = 1. open can return -1,
 * and fd can be later opened as something
 * not suitable for puts() and friends .
 */

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

int
main(void)
{
	int save1, fd;
	fd = open("test.txt", O_WRONLY|O_CREAT|O_APPEND, 0644);
	if (fd == -1) {
		perror("open");
		return -1;
	}
	puts("this line1 goes to stdout");
	save1 = dup(1);
	dup2(fd, 1);
	close(fd);
	printf("this line goes to test.txt\n");
	dup2(save1, 1);
	close(save1);
	puts("this line2 goes to stdout");
	return 0;
}
