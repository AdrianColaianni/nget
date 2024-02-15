#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 1024 * 1024 // 1 Kb

struct cli_args {
	in_addr_t ip;
	in_port_t port;
	int conn_type;
	bool verbose;
	bool listen;
} cli_args;

void parse_args(int, char*[]);
void listen_tcp(int sockfd, int epollfd, struct epoll_event* events);
void listen_udp(int sockfd, int epollfd, struct epoll_event* events);
void talk(int sockfd, int epollfd, struct epoll_event* events);

int main(int argc, char* argv[]) {
	parse_args(argc, argv);

	// Create socket
	int sockfd;
	if ((sockfd = socket(AF_INET, cli_args.conn_type, 0)) == -1) {
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

	if (cli_args.listen)
		if (cli_args.conn_type == SOCK_STREAM)
			listen_tcp(sockfd, epollfd, events);
		else
			listen_udp(sockfd, epollfd, events);
	else
		talk(sockfd, epollfd, events);
}

int send_string(int, char*);

void talk(int sockfd, int epollfd, struct epoll_event* events) {
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
				if (b == 0) // Connection has closed
					goto exit;
				buf[b] = 0;
				fprintf(stdout, "%s", buf);
				memset(buf, 0, BUF_SIZE);
				b = 0;
			} else {
				// Handle data in from stdin
				b = fread(buf, 1, BUF_SIZE, stdin);
				buf[b] = 0;
				send_string(sockfd, buf);
				if (feof(stdin)) // Stdin empty
					goto exit;
				memset(buf, 0, BUF_SIZE);
				b = 0;
			}
		}
	}

exit:
	close(epollfd);
	exit(EXIT_SUCCESS);
}

void listen_tcp(int sockfd, int epollfd, struct epoll_event* events) {
	// Create sockaddr_in from IP and port
	struct sockaddr_in lisen_addr;
	lisen_addr.sin_family = AF_INET;
	lisen_addr.sin_port = htons(cli_args.port);
	lisen_addr.sin_addr.s_addr = htonl(0);
	memset(&(lisen_addr.sin_zero), 0, 8);

	if (bind(sockfd, (struct sockaddr*)&lisen_addr, sizeof(struct sockaddr)) ==
		-1) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	if (listen(sockfd, 1) == -1) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	if (cli_args.verbose)
		fprintf(stdout, "Listening...\n");

	char buf[BUF_SIZE] = {};
	int b, nfds, connfd = 0;
	struct sockaddr_in remote_addr;
	socklen_t remote_addr_len;
	struct epoll_event ev;

	while (1) {
		nfds = epoll_wait(epollfd, events, 2, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}

		for (int n = 0; n < nfds; n++) {
			if (events[n].data.fd == sockfd) {
				// New connection
				if (connfd != 0)
					return;
				if ((connfd = accept(sockfd, (struct sockaddr*)&remote_addr,
									 &remote_addr_len)) == -1) {
					perror("accept");
					exit(EXIT_FAILURE);
				}
				if (cli_args.verbose)
					fprintf(stdout, "Connected\n");
				// Enroll new connection
				ev.events = EPOLLIN | EPOLLET;
				ev.data.fd = connfd;
				if (epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &ev) == -1) {
					perror("epoll_ctl: connfd");
					exit(EXIT_FAILURE);
				}
			} else if (connfd != 0 && events[n].data.fd == connfd) {
				// Connection present
				b = recv(connfd, buf, BUF_SIZE, 0);
				if (b == 0) // Connection has closed
					goto exit;
				buf[b] = 0;
				fprintf(stdout, "%s", buf);
				memset(buf, 0, BUF_SIZE);
				b = 0;
			} else {
				if (connfd == 0)
					continue; // Not ready to send data yet
				// Handle data in from stdin
				b = fread(buf, 1, BUF_SIZE, stdin);
				buf[b] = 0;
				send_string(connfd, buf);
				if (feof(stdin)) // Stdin empty
					goto exit;
				memset(buf, 0, BUF_SIZE);
				b = 0;
			}
		}
	}

exit:
	close(epollfd);
	exit(EXIT_SUCCESS);
}

void listen_udp(int sockfd, int epollfd, struct epoll_event* events) {
	// Create sockaddr_in from IP and port
	struct sockaddr_in lisen_addr;
	lisen_addr.sin_family = AF_INET;
	lisen_addr.sin_port = htons(cli_args.port);
	lisen_addr.sin_addr.s_addr = htonl(0);
	memset(&(lisen_addr.sin_zero), 0, 8);

	if (bind(sockfd, (struct sockaddr*)&lisen_addr, sizeof(struct sockaddr)) ==
		-1) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	if (cli_args.verbose)
		fprintf(stdout, "Listening...\n");

	char buf[BUF_SIZE] = {};
	int b, nfds;
	struct sockaddr_in remote_addr;
	socklen_t remote_addr_len = 0;

	while (1) {
		nfds = epoll_wait(epollfd, events, 2, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}

		for (int n = 0; n < nfds; n++) {
			if (events[n].data.fd == sockfd) {
				b = recvfrom(sockfd, buf, BUF_SIZE, 0, (struct sockaddr*)&remote_addr, &remote_addr_len);
				if (b == 0) // Connection has closed
					goto exit;
				buf[b] = 0;
				fprintf(stdout, "%s", buf);
				memset(buf, 0, BUF_SIZE);
				b = 0;
			} else {
				if (remote_addr.sin_addr.s_addr == 0)
					continue;
				// Handle data in from stdin
				b = fread(buf, 1, BUF_SIZE, stdin);
				buf[b] = 0;
				if (connect(sockfd, (struct sockaddr*)&remote_addr,
							remote_addr_len) == -1) {
					perror("connect: sockfd");
					exit(EXIT_FAILURE);
				}
				send_string(sockfd, buf);
				if (feof(stdin)) // Stdin empty
					goto exit;
				memset(buf, 0, BUF_SIZE);
				b = 0;
			}
		}
	}

exit:
	close(epollfd);
	exit(EXIT_SUCCESS);
}
void parse_args(int argc, char* argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <IP> <PORT>\n", argv[0]);
		exit(0);
	}
	// Defaults
	cli_args.conn_type = SOCK_STREAM;
	cli_args.listen = false;

	// Process flags
	for (int i = 1; i < argc; i++) {
		if (*argv[i] != '-')
			continue;
		switch (argv[i][1]) {
		case 'v':
			cli_args.verbose = true;
			break;
		case 'u':
			cli_args.conn_type = SOCK_DGRAM;
			break;
		case 'l':
			cli_args.listen = true;
			break;
		default:
			fprintf(stderr, "invalid flag %c\n", argv[i][1]);
			exit(EXIT_FAILURE);
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

	// No need for IP
	if (cli_args.listen)
		return;

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
