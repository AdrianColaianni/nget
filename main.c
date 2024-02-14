#include <arpa/inet.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 1024*1024 // 1 Kb

struct cli_args {
	in_addr_t ip;
	in_port_t port;
	bool verbose;
	bool udp;
} cli_args;

void parse_args(int, char*[]);
int send_string(int, char*);
int recv_string(int, char*, int);

int main(int argc, char* argv[]) {
	parse_args(argc, argv);

	// Create socket
	int sockfd;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	// Set stdin to non-blocking
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

	// Create epoll instance to watch network & stdin
	struct epoll_event ev, events[2];
	int epollfd = epoll_create(2);
	if (epollfd == -1) {
		perror("epoll_create");
	}
	// Register socket
	ev.events = EPOLLIN;
	ev.data.fd = sockfd;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
		perror("epoll_ctl: sockfd");
		exit(EXIT_FAILURE);
	}
	// Register stdin
	ev.events = EPOLLIN;
	ev.data.fd = 0;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, 0, &ev) == -1) {
		perror("epoll_ctl: stdin");
		exit(EXIT_FAILURE);
	}

	// Create sockaddr_in from IP and port
	struct sockaddr_in target_addr;
	target_addr.sin_family = AF_INET;
	target_addr.sin_port = htons(cli_args.port);
	target_addr.sin_addr.s_addr = htonl(cli_args.ip);
	memset(&(target_addr.sin_zero), 0, 8);

	if (cli_args.verbose)
		fprintf(stdout, "Connecting...\n");

	if (connect(sockfd, (struct sockaddr*)&target_addr,
				sizeof(struct sockaddr)) == -1) {
		perror("connect: sockfd");
		exit(EXIT_FAILURE);
	}

	if (cli_args.verbose)
		fprintf(stdout, "Connected\n");

	char buf[BUF_SIZE] = {};
	int b, nfds;
	while (1) {
		nfds = epoll_wait(epollfd, events, 2, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}

		for (int n = 0; n < nfds; n++) {
			if (events[n].data.fd == sockfd) {
				// Handle data in from network
				b = recv(sockfd, buf, BUF_SIZE, 0);
				buf[b] = 0;
				fprintf(stdout, "%s", buf);
				memset(buf, 0, BUF_SIZE);
				b = 0;
			} else {
				// Handle data in from stdin
				b = fread(buf, 1, BUF_SIZE, stdin);
				buf[b] = 0;
				send_string(sockfd, buf);
				if (feof(stdin)) {
					goto exit;
				}
				memset(buf, 0, BUF_SIZE);
				b = 0;
			}
		}
	}

exit:
	close(epollfd);
	return 0;
}

void parse_args(int argc, char* argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <IP> <PORT>\n", argv[0]);
		exit(0);
	}

	// Process flags
	for (int i = 1; i < argc - 2; i++) {
		if (*argv[i] != '-')
			continue;
		switch (argv[i][1]) {
		case 'v':
			cli_args.verbose = true;
			break;
		case 'u':
			cli_args.udp = true;
			break;
		}
	}

	// Convert port
	int b = 0;
	if (sscanf(argv[argc - 1], "%d", &b) != 1) {
		fprintf(stderr, "Invalid port %s\n", argv[argc - 1]);
		exit(-1);
	}
	cli_args.port = b;
	if (cli_args.port != b || cli_args.port == 0) {
		fprintf(stderr, "Invalid port %s\n", argv[argc - 1]);
		exit(-1);
	}

	// Convert IP
	if ((cli_args.ip = inet_network(argv[argc - 2])) == -1) {
		fprintf(stderr, "Invalid IP %s\n", argv[argc - 2]);
		exit(-1);
	}
}

int send_string(int sockfd, char* buf) {
	int send_bytes, bytes_to_send;
	bytes_to_send = strlen(buf);
	while (bytes_to_send > 0) {
		send_bytes = send(sockfd, buf, bytes_to_send, 0);
		if (send_bytes == -1)
			return 0;
		bytes_to_send -= send_bytes;
		buf += send_bytes;
	}
	return 1;
}

int recv_string(int sockfd, char* buf, int buf_size) {
#define EOL "\r\n"
#define EOL_SIZE 2
	char* ptr;
	int eol_matched = 0, bytes_read = 0;

	ptr = buf;
	while (recv(sockfd, ptr, 1, 0) == 1 && bytes_read + 1 < buf_size) {
		bytes_read++;
		if (*ptr == EOL[eol_matched]) {
			eol_matched++;
			if (eol_matched == EOL_SIZE) {
				*(ptr + 1 - EOL_SIZE) = '\0';
				return strlen(buf);
			}
		} else {
			eol_matched = 0;
		}
		ptr++;
	}
	buf[bytes_read] = '\0';
	return bytes_read;
}
