#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <getopt.h>
#include <poll.h>
#include <time.h>
#include "utils.h"
#include "rpc.h"

#define progname "wk-rpc-client"

#define DEFAULT_ASYNC 0
#define DEFAULT_DELAY 10
#define DEFAULT_PORT 9876

struct opts {
	int async;
	int delay;
	int cmd;
	unsigned long count;
	char *server;
	int port;
	int poll;
	int repeat;
	int verbose;
};

struct cx {
	int sockfd;
	struct sockaddr_in serv_addr;
	struct hostent *server;
};


__attribute__((noreturn))
static void usage(void) {
    fprintf(stderr, "Usage: %s [OPTIONS] [COMMAND]\n", progname);
    fprintf(stderr, "\nOptions:\n\n");
    fprintf(stderr, "--server         server address\n");
    fprintf(stderr, "--port           port (default 9876)\n");
    fprintf(stderr, "--delay          server side operation delay (ms)\n");
    fprintf(stderr, "--async          amount of asynchronous processing (ms)\n");
    fprintf(stderr, "--command        command to execute on the server [ hog, sleep ]\n");
    fprintf(stderr, "--verbose        be more verbose\n");
    fprintf(stderr, "--help           print this message and exit\n");
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

static void parse_opts(int argc, char **argv, struct opts *opts) {
	int opt;
	int idx;

	struct option options[] = {
		{ "help",       0, 0, 'h' },
		{ "server",     1, 0, 's' },
		{ "port",       1, 0, 'p' },
		{ "async",      1, 0, 'a' },
		{ "delay",      1, 0, 'd' },
		{ "command",    1, 0, 'c' },
		{ "poll",       1, 0, 'x' },
		{ "repeat",     1, 0, 'r' },
		{ "verbose",    0, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	opts->cmd = RPC_HOG;
	opts->delay = DEFAULT_DELAY;
	opts->async = DEFAULT_ASYNC;
	opts->port = DEFAULT_PORT;
	opts->poll = 0;
	opts->verbose = 0;
	opts->repeat = 1;
	opts->server = NULL;

	while ((opt = getopt_long(argc, argv, "hva:d:c:s:p:x:", options, &idx)) != -1) {
		switch (opt) {
		case 'c':
			if (strcmp(optarg, "hog") == 0) {
				opts->cmd = RPC_HOG;
			} else if (strcmp(optarg, "sleep") == 0) {
				opts->cmd = RPC_SLEEP;
			} else if (strcmp(optarg, "ping") == 0) {
				opts->cmd = RPC_PING;
			}
			break;
		case 'a':
			opts->async = atoi(optarg);
			break;
		case 'd':
			opts->delay = atoi(optarg);
			break;
		case 's':
			opts->server = strdup(optarg);
			break;
		case 'p':
			opts->port = atoi(optarg);
			break;
		case 'x':
			opts->poll = atoi(optarg);
			break;
		case 'r':
			opts->repeat = atoi(optarg);
			break;
		case 'h':
			usage();
			break;
		case 'v':
			opts->verbose = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if (opts->server == NULL) {
		printf("error: server address must be specified\n");
		usage();
	}
}

int rpc_connect(struct opts *opts, struct cx *cx) {
	int ret;

	cx->sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (cx->sockfd < 0) {
		printf("socket() failed\n");
		return -1;
	}
	cx->server = gethostbyname(opts->server);
	if (cx->server == NULL) {
		printf("gethostbyname() failed\n");
		return -1;
	}
	memset(&cx->serv_addr, 0, sizeof(cx->serv_addr));
	cx->serv_addr.sin_family = AF_INET;
	memcpy(&cx->serv_addr.sin_addr.s_addr, cx->server->h_addr,
			sizeof(cx->server->h_length));
	cx->serv_addr.sin_port = htons(opts->port);

	ret = connect(cx->sockfd, (struct sockaddr *) &cx->serv_addr,
			sizeof(cx->serv_addr));
	if (ret < 0) {
		printf("connect() failed\n");
		return -1;
	}

	return 0;
}

int rpc_command(struct opts *opts, struct cx *cx, struct message *msg, struct message *ans) {
	int ret;
	struct pollfd fds;
	ret = write(cx->sockfd, msg, sizeof(struct message));
	if (ret < 0) {
		printf("write() failed\n");
		return ret;
	}
	if (opts->async) {
		do_hog(opts->async * opts->count);
	}
	if (opts->poll) {
		fds.fd = cx->sockfd;
		fds.events = POLLIN;
		do {
			ret = poll(&fds, 1, opts->poll);
		} while(ret == 0);
	}
	ret = read(cx->sockfd, ans, sizeof(struct message));
	if (ret < 0) {
		printf("read() failed\n");
		return ret;
	}
	if (opts->verbose) {
		printf("status: %d\n", ans->ret);
	}
	return ret;
}

int rpc_terminate(struct cx *cx) {
	close(cx->sockfd);
	return 0;
}

int rpc_calibrate() {
	int ret;
	struct stat info;
	char *path;
	unsigned long count = 0;

	asprintf(&path, "%s/%s", getenv("HOME"), ".wk-calibrate");
	printf("path=%s\n", path);
	stat(path, &info);
	if (S_ISREG(info.st_mode)) {
		FILE *f = fopen(path, "r");
		ret = fread(&count, sizeof(count), 1, f);
		fclose(f);
	} else {
		count = calibrate(10000) / 10;
		FILE *f = fopen(path, "w");
		ret = fwrite(&count, sizeof(count), 1, f);
		fclose(f);
	}
	return count;
}

int rpc_experiment(struct opts *opts, struct cx *cx) {
	struct message msg, ans;
	struct timespec *data;
	int i;

	/* allocate memory ahead and writes to it to avoid page fault */
	data = malloc(opts->repeat * sizeof(struct timespec));
	memset(data, 0, opts->repeat * sizeof(struct timespec));

	for (i = 0; i < opts->repeat; i++) {
		clock_gettime(CLOCK_MONOTONIC, &data[i]);
		msg.cmd = opts->cmd;
		msg.arg = opts->delay;
		msg.cnt = opts->repeat - i;
		msg.ret = 0;
		memset(&ans, 0, sizeof(struct message));
		rpc_command(opts, cx, &msg, &ans);
	}

	if (opts->repeat > 1) {
		FILE *out = fopen("rpc-stats.out", "w+");
		for (i = 0; i < opts->repeat - 1; i++) {
			struct timespec ts = time_sub(&data[i + 1], &data[i]);
			fprintf(out, "%d,%ld.%09ld\n", i, ts.tv_sec, ts.tv_nsec);
		}
		fclose(out);
	}
	return 0;
}

int main(int argc, char *argv[]) {
	struct opts opts;
	struct cx cx;
	int ret;

	parse_opts(argc, argv, &opts);
	opts.count = rpc_calibrate();
	printf("count=%ld\n", opts.count);
	ret = rpc_connect(&opts, &cx);
	if (ret < 0)
		return -1;
	rpc_experiment(&opts, &cx);
	rpc_terminate(&cx);
	return 0;
}
