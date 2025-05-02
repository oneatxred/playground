#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define INIT_SZ 8

struct input {
	char *buf;
	size_t bufsz;
	size_t offset;
};

int
init_input(struct input *i)
{
	i->buf = malloc(INIT_SZ * sizeof(char));
	if (i->buf == NULL) {
		perror("buf init malloc");
		return 0;
	}
	memset(i->buf, 0, INIT_SZ);
	i->bufsz = INIT_SZ;
	i->offset = 0;
	return 1;
}

int
resize_buf(struct input *i)
{
	size_t newsize = i->bufsz + INIT_SZ;
	char *t = malloc(newsize);
	if (!t) {
		perror("realloc buffer");
		return 0;
	}
	memset(t, 0, newsize);
	memcpy(t, i->buf, i->bufsz);
	free(i->buf);
	i->buf = t;
	i->bufsz = newsize;
	printf("{!}");
	return 1;
}

int
clear(struct input *i)
{
	free(i->buf);
	return init_input(i);
}

int
handle_input(struct input *i)
{
	char *nl = NULL;
	ssize_t rb = read(1, i->buf + i->offset, i->bufsz - i->offset);
	if (rb == -1) {
		int err = errno;
		if (err == EAGAIN || err == EWOULDBLOCK)
			return 0;
		else {
			perror("read()");
			return -1;
		}
	} else if (rb == 0) {
		printf("stream closed\n");
		return -1;
	} else {
		nl = strchr(i->buf, '\n');
		if (i->offset + rb == i->bufsz) {
			if (nl) {
				*nl = 0;
				return 1;
			} else {
				if (!resize_buf(i)) {
					*(i->buf + i->bufsz) = 0;
					return 1;
				} else {
					i->offset += rb;
				}
			}
		} else {
			if (nl) {
				*nl = 0;
				return 1;
			} else {
				i->offset += rb;
			}
		}
	}
	return 0;
}

int
main(void)
{
	int ret = -1;
	int revents = 0;
	struct input input = { NULL, 0, 0 };
	struct pollfd pfd = {0};
	if (!init_input(&input))
		return -1;
	if (fcntl(1, F_SETFL, fcntl(1, F_GETFL) | O_NONBLOCK) == -1) {
		perror("fcntl");
		return -1;
	}
	pfd.fd = 1;
	pfd.events = POLLIN;
	for (;;) {
		ret = poll(&pfd, 1, -1);
		if (ret == -1) {
			int err = errno;
			if (err == EINTR)
				continue;
			perror("poll");
			return -1;
		} else if (ret == 0) {
			/* timeout should not happen */
		} else {
			revents = pfd.revents;
			if (revents & POLLNVAL)
				fprintf(stderr, "poll() POLLNVAL revents 0x%04Xd\n", revents);
			if (revents & ~(POLLIN|POLLOUT|POLLERR|POLLHUP|POLLNVAL))
				fprintf(stderr, "poll() bogus revents 0x%04Xd\n", revents);
			if (revents & (POLLERR|POLLHUP|POLLNVAL))
				revents |= POLLIN;
			if (revents & POLLIN) {
				int ret = handle_input(&input);
				if (ret == -1) {
					break;
				} else if (ret == 0) {
					continue;
				} else {
					printf("[%s]\n", input.buf);
					if (!clear(&input))
						return -1;
				}
			}
		}
	}
	return 0;
}
