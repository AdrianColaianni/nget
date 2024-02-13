#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

	int sockfd;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "Faild to get socket\n");
		return -1;
	}

	struct sockaddr_in target_addr;
	target_addr.sin_family = AF_INET;
	target_addr.sin_port = htons(cli_args.port);
	target_addr.sin_addr.s_addr = htonl(cli_args.ip);
	memset(&(target_addr.sin_zero), 0, 8);

	if (cli_args.verbose)
		fprintf(stdout, "Connecting...\n");

	if (connect(sockfd, (struct sockaddr*)&target_addr,
				sizeof(struct sockaddr)) == -1) {
		fprintf(stderr, "Error connecting\n");
		return -1;
	}
	if (cli_args.verbose)
		fprintf(stdout, "Connected\n");

	char buf[100] = {}, input[100] = {};
	int bufi = 0, inputi = 0;
	while (1) {
		// Check if there's input to process
		if (fread(buf + bufi, 1, 1, stdin) == 0) {
			if (buf[bufi] == '\n') {
				send_string(sockfd, buf);
				memset(buf, 0, 100);
				bufi = 0;
			} else
				bufi++;
		}
		// Check if there's data from the stream
		while (recv(sockfd, input + inputi, 1, 0)) {
			if (input[inputi] == '\n') {
				fprintf(stdout, "%s", input);
				memset(input, 0, 100);
				inputi = 0;
			} else
				inputi++;
		}

		sleep(80);
		/* char* buf; */
		/* scanf("%m[^\n]", &buf); */
		/* getchar(); // Clears newline */
		/* strcat(buf, "\n"); */
		/* send_string(sockfd, buf); */
	}

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
