#include <tls.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#define INPUT_BUFSZ 1024 

int timeout = 3000;

int
connect_timeout(int s, struct sockaddr *sa, socklen_t sock_len)
{
	struct pollfd pfd = {0};
	int ret = 0;
	int err = 0;
	socklen_t err_len = sizeof(err);
	if (connect(s, sa, sock_len) == -1) {
		if (errno == EINPROGRESS) {
			pfd.fd = s;
			pfd.events = POLLOUT;
			ret = poll(&pfd, 1, timeout);
			if (ret == -1) {
				ret = errno;
				perror("poll() in connect_timeout()");
			} else if (ret == 0) {
				fprintf(stderr, "connect error: timeout\n");
				ret = ETIMEDOUT;
			} else {
				ret = getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &err_len);
				if (ret) {
					ret = errno;
					perror("getsockopt()");
				} else {
					ret = err;
					if (ret)
						fprintf(stderr, "connect error (%s)\n", strerror(ret));
				}
			}
		} else {
			ret = errno;
			fprintf(stderr, "connect error (%s %i)\n", strerror(ret), ret);
		}
	}
	return ret;
}

int
remote_connect(char *host)
{
	int s, gai, save_errno, conn_ret;
	struct addrinfo hints, *rp, *res;
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
		
	gai = getaddrinfo(host, "443", &hints, &res);
	if (gai) {
		fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(gai));
		return -1;
	}
	for (rp = res; rp; rp = rp->ai_next) {
		s = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, IPPROTO_TCP);
		if (s == -1)
			continue;
		conn_ret = connect_timeout(s, rp->ai_addr, rp->ai_addrlen);
		if (conn_ret)
			continue;
		else
			break;
		save_errno = errno;
		close(s);
		errno = save_errno;
	}
	if (rp == NULL)
		s = -1;
	freeaddrinfo(res);
	return s;
}

static int
timeout_tls(int s, struct tls *tls_ctx, int (*func)(struct tls *)) {
	struct pollfd pfd;
	int ret;

	while ((ret = (*func)(tls_ctx)) != 0) {
		if (ret == TLS_WANT_POLLIN)
			pfd.events = POLLIN;
		else if (ret == TLS_WANT_POLLOUT)
			pfd.events = POLLOUT;
		else
			break;
		pfd.fd = s;
		if ((ret = poll(&pfd, 1, timeout)) == 1) {
			continue;
		} else if (ret == 0) {
			errno = ETIMEDOUT;
			ret = -1;
			break;
		} else {
			perror("poll() int timeout_tls");
		}
	}
	return ret;
}

int
tls_setup_client(struct tls *tls_ctx, int s,char *host)
{
	const char *errstr;
	if (tls_connect_socket(tls_ctx, s, host) == -1) {
		fprintf(stderr, "tls connection failed (%s)\n", tls_error(tls_ctx));
		return -1;
	}
	if (timeout_tls(s, tls_ctx, tls_handshake) == -1) {
		if ((errstr = tls_error(tls_ctx)) == NULL)
			errstr = strerror(errno);
		fprintf(stderr, "tls handshake failed (%s)\n", errstr);
		return -1;
	}
	return 0;
}

void
readwrite(struct tls *tls_ctx, int s, char *host)
{
	int wb = 0;
	int rb = 0;
	int ret = -1;
	int reqlen = 0;
	int offset = 0;
	struct pollfd pfd = {0};
	char req[128] = {0};
	char input_buf[INPUT_BUFSZ] = {0};
	char *headers_sep = NULL;
	int hostlen = strlen(host);
	strncpy(req, "GET / HTTP/1.1\r\nHost:", 21);
	strncat(req, host, hostlen);
	strncat(req, "\r\n\r\n", 4);
	reqlen = strlen(req);
	
	pfd.fd = s;
	pfd.events = POLLOUT;
	for (;;) {
		ret = poll(&pfd, 1, timeout);
		if (ret == -1) {
			perror("poll() in readwrite()");
		} else if (ret == 0) {
			return;
		} else {
			if (pfd.revents & POLLOUT) {
				wb = tls_write(tls_ctx, req+offset, reqlen-offset);
				if (wb == -1)
					fprintf(stderr, "tls write failed (%s)\n", tls_error(tls_ctx));
				offset += wb;
				if (reqlen == offset)
					pfd.events = POLLIN;
			}
			offset = 0;
			if (pfd.revents & POLLIN) {
				rb = tls_read(tls_ctx, input_buf+offset, INPUT_BUFSZ-offset);
				if (rb == -1)
					fprintf(stderr, "tls read failed (%s)\n", tls_error(tls_ctx));
				offset += rb;
				if ((headers_sep = strstr(input_buf, "\n")) != NULL) {
					*headers_sep = 0;
					printf("%s\n", input_buf);
					break;
				}
				if (INPUT_BUFSZ == offset) {
					input_buf[offset-1] = 0;
					printf("%s\n", input_buf);
					printf("BUFFER IS FULL\n");
					break;
				}
			}
		}
	}
}

int https_request(char *host) {
	struct tls_config *tls_cfg = NULL;
	struct tls *tls_ctx = NULL;
	char *tls_protocols = NULL;
	char *tls_ciphers = NULL;
	int s;
	unsigned int protocols;

	if ((tls_cfg = tls_config_new()) == NULL) {
		fprintf(stderr, "Unable to allocate tls config\n");
		return -1;
	}
	if (tls_config_parse_protocols(&protocols, tls_protocols) == -1) {
		fprintf(stderr, "invalid TLS protocols `%s'\n", tls_protocols);
		return -1;
	}
	if (tls_config_set_protocols(tls_cfg, protocols) == -1) {
		fprintf(stderr, "%s\n", tls_config_error(tls_cfg));
		return -1;
	}
	if (tls_config_set_ciphers(tls_cfg, tls_ciphers) == -1) {
		fprintf(stderr, "%s\n", tls_config_error(tls_cfg));
		return -1;
	}
	 if ((tls_ctx = tls_client()) == NULL) {
		fprintf(stderr, "tls client creation failed\n");
		return -1;
	}
	if (tls_configure(tls_ctx, tls_cfg) == -1) {
		fprintf(stderr, "tls configuration failed (%s)\n", tls_error(tls_ctx));
		return -1;
	}
/* 	tls_config_insecure_noverifycert(tls_cfg); */
	s = remote_connect(host);
	if (s == -1)
		return -1;
	if (tls_setup_client(tls_ctx, s, host) == -1)
		return -1;
	readwrite(tls_ctx, s, host);
	timeout_tls(s, tls_ctx, tls_close);
	return 0;
}

int main(int argc, char **argv) {
	if (!argv[1]) {
		fprintf(stderr, "Usage: %s target_host\n", argv[0]);
		return -1;
	}
	return https_request(argv[1]);
}
