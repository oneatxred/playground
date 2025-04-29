For some reason if we comment first puts(),
printf() will be printed to stdout.  
To fix this we need to set stdout line buffered
or no buffered with setvbuf() before 
```
save1 = dup(1);
```
line

```
int
main(void)
{
	int save1, fd;
	fd = open("test.txt", O_WRONLY|O_CREAT|O_APPEND, 0644);
	if (fd == -1) {
		perror("open");
		return -1;
	}
/* 	puts("this line1 goes to stdout"); */
	setvbuf(stdout, NULL, _IOLBF, 0);
	save1 = dup(1);
	dup2(fd, 1);
	close(fd);
	printf("this line goes to test.txt\n");
	dup2(save1, 1);
	close(save1);
	puts("this line2 goes to stdout");
	return 0;
}
```
