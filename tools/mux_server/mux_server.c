/******************************************************************************
 * K42: (C) Copyright IBM Corp. 2000.
 * All Rights Reserved
 *
 * This file is distributed under the GNU LGPL. You should have
 * received a copy of the license along with K42; see the file LICENSE.html
 * in the top-level directory for more details.
 *
 * $Id: thinwire3.c,v 1.13 2006/04/04 23:55:14 mostrows Exp $
 *****************************************************************************/
/*****************************************************************************
 * Module Description: thinwire (de)multiplexor program
 * **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <signal.h>
#ifdef PLATFORM_AIX
#include <termios.h>
#else
#include <sys/termios.h>
#endif
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <time.h>
#ifndef TCP_NODELAY
#include <tiuser.h>		// needed on AIX to get TCP_NODELAY defined
#endif /* #ifndef TCP_NODELAY */
#include <sys/poll.h>
#include <signal.h>

#include <pthread.h>

/*
 * These prototypes should be in <termios.h>, but aren't.
 */
extern int tcgetattr(int, struct termios *);
extern int tcsetattr(int, int, const struct termios *);

char *program_name;
char *target_name = NULL;
int ext_program;
int hw_flowcontrol = 0;
int verbose = 0;
int debug = 0;
int nchannels;
void target_write(char *buf, int len);
int speed1 = B115200;

// Assume on AIX that 'highbaud' has been enabled, meaning
// the following apply:
#ifdef PLATFORM_AIX
#define B57600	B50
#define B115200 B110
#endif

struct speed {
	int val;
	const char *name;
};

#define _EVAL(a) a
#define SPEED(x) { _EVAL(B##x) , #x }
struct speed speeds[] = {
	SPEED(0),
	SPEED(50),
	SPEED(75),
	SPEED(110),
	SPEED(134),
	SPEED(150),
	SPEED(200),
	SPEED(300),
	SPEED(600),
	SPEED(1200),
	SPEED(1800),
	SPEED(2400),
	SPEED(4800),
	SPEED(9600),
	SPEED(19200),
	SPEED(38400),
	SPEED(57600),
	SPEED(115200),
#if defined(B230400)
	SPEED(230400),
#endif
	{0, NULL}
};

/*
 * These following defines need to be identical to their counterparts
 * in vhype/lib/thinwire.c and vtty.h
 */
#define CHAN_SET_SIZE 8
#define MAX_STREAMS 95
#define MAX_PACKET_LEN (4096*4)
#define STREAM_READY (1<<16)

#define MAX_FD 512
struct pollfd polled_fd[MAX_FD] = { {0,}, };

struct iochan {
	int fd;
	int fdout; /* alternate output descriptor */
	int poll_idx;
	int stream_id;
	int status;
	pthread_t net_thread;
	int port;
	long data;		/* private data */
	int (*write) (struct iochan * ic, char *buf, int len, int block);
	int (*read) (struct iochan * ic, char *buf, int len, int block);
	int (*state) (struct iochan * ic, int new_state);
	void (*detach) (struct iochan * ic);
	void (*setSpeed) (struct iochan * ic, char *buf, int len);
};

int last_channel = 0;
static struct iochan channels[MAX_FD];
static struct iochan *streams[MAX_STREAMS] = { NULL, };
static struct iochan *target = NULL;

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif /* #ifndef MIN */
#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif /* #ifndef MAX */

#define CH_SWITCH_ESCAPE 0x18 // Ctrl-X
#define CH_CHANNEL_BASE '0' // '0' for channel 0, '1' for channel 1, etc

// Tell remote end to resend its TX channel the next time it sends data
#define CH_CHANNEL_RESET 1

int getSpeed(const char *name)
{
	int i = 0;
	while (speeds[i].name) {
		if (strcmp(speeds[i].name, name) == 0) {
			return speeds[i].val;
		}
		++i;
	}
	return -1;
}

const char *getSpeedName(int val)
{
	int i = 0;
	while (speeds[i].name) {
		if (speeds[i].val == val) {
			return speeds[i].name;
		}
		++i;
	}
	return NULL;
}

void message(char *msg, ...)
{
	va_list ap;
	char buf[256];

	va_start(ap, msg);
	vsprintf(buf, msg, ap);
	va_end(ap);

	fprintf(stdout, "%s: %s.\n", program_name, buf);
	fflush(stdout);
}

void fatal(char *msg, ...)
{
	va_list ap;
	char buf[256];
	//int i;

	va_start(ap, msg);
	vsprintf(buf, msg, ap);
	va_end(ap);

	fprintf(stderr, "%s: %s.\n", program_name, buf);

	exit(-1);
}

void dump_char_hex(char *hdr, char *buf, int len)
{
	int i;
	int j = 0;
	char c;

	while (len) {
		int n = MIN(len, 16);
		if (j == 0) {
			fprintf(stderr, hdr);
		} else {
			fprintf(stderr, "          ");
		}
		for (i = 0; i < n; i++) {
			c = buf[j + i];
			if ((c < ' ') || (c > '~'))
				c = '.';
			putc(c, stderr);
		}
		fprintf(stderr, "%*s | ", 17 - n, " ");
		for (i = 0; i < n; i++) {
			fprintf(stderr, " %02x", ((int)buf[j + i]) & 0xff);
		}
		putc('\n', stderr);
		len -= n;
		j += n;
	}
}

void display(int chan, char direction, char *buf, int len)
{
	char hdr[32];
	hdr[0] = 0;
	sprintf(hdr, "%2.2d %c %4.4d ", chan, direction, len);
	dump_char_hex(hdr, buf, len);
}

int stream_find(int stream_id)
{
	int i;
	for (i = 0; i < MAX_STREAMS; i++) {
		if (streams[i])
			if (streams[i]->stream_id == stream_id)
				return i;
	}
	return -1;
}

static int default_state(struct iochan *c, int new_state)
{
	c->status = new_state | STREAM_READY;
	return 0;
}

static void default_setSpeed(struct iochan *ic, char *buf, int len)
{
	/* Return a speed of "0" indicating no change. */
	char reply[6] = { 'S', ' ', ' ', ' ' + 1, '0' };
	ic->write(ic, reply, 6, 1);
}

static void default_detach(struct iochan *ic)
{
	message("detach stream %d", ic->stream_id);
	close(polled_fd[ic->poll_idx].fd);
	polled_fd[ic->poll_idx].fd = 0;
	polled_fd[ic->poll_idx].events = 0;
	ic->fd = 0;
	ic->state = NULL;
	ic->status = 0;
	streams[ic->stream_id] = NULL;

	if (ic->fdout) {
		close(ic->fdout);
		ic->fdout = 0;
	}
}

static struct iochan *get_channel(int fd)
{
	int i = 0;
	for (; i < MAX_FD && i <= last_channel; ++i) {
		if (channels[i].state == NULL) {
			channels[i].state = default_state;
			channels[i].setSpeed = default_setSpeed;
			channels[i].detach = default_detach;
			channels[i].poll_idx = i;
			channels[i].fd = fd;
			channels[i].status = STREAM_READY;
			polled_fd[i].fd = fd;
			if (i == last_channel) {
				++last_channel;
			}
			return &channels[i];
		}
	}
	return NULL;
}

static int bitClear(int fd, int bit)
{
	int ret;
	ret = ioctl(fd, TIOCMBIC, &bit);
	if (ret < 0) {
		perror("ioctl(TIOCMBIC): ");
		return -1;
	}
	return ret;
}

static int bitSet(int fd, int bit)
{
	int ret;
	ret = ioctl(fd, TIOCMBIS, &bit);
	if (ret < 0) {
		perror("ioctl(TIOCMBIS): ");
		return -1;
	}
	return ret;
}

volatile int lastStatus;
static int checkBit(int fd, int bit)
{
	int ret;
	ret = ioctl(fd, TIOCMGET, &lastStatus);
	if (ret < 0) {
		perror("ioctl(TIOCMGET): ");
		return 0;
	}
	return lastStatus & bit;
}

/*
 * Definitions for a channel that functions over a serial port,
 * and may manage hw flow control lines.
 */

static int serial_read(struct iochan *ic, char *buf, int len, int block)
{
	int cnt;

	if (hw_flowcontrol)
		bitSet(ic->fd, TIOCM_RTS);

	buf[1] = 'G';

	cnt = read(ic->fd, buf, len);
	if (cnt < len) {
		ic->status &= ~POLLIN;
	}

	if (hw_flowcontrol)
		bitClear(ic->fd, TIOCM_RTS);
	return cnt;
}

static int serial_write(struct iochan *ic, char *buf, int len, int block)
{
	int ret;

	if (hw_flowcontrol) {
		while (checkBit(ic->fd, TIOCM_CTS) == 0) ;
	}

	ret = write(ic->fd, buf, len);

	return ret;
}

static void serial_setSpeed(struct iochan *ic, char *buf, int len)
{
	char reply[32];
	struct termios t;
	int speed;

	memcpy(reply, buf, len);
	reply[len] = 0;

	speed = getSpeed(reply);
	if (debug) {
		message("Setting serial line speed: %*.*s %d",
			len, len, buf, speed);
	}

	if (speed < 0) {
		fatal("Invalid speed request: %*.*s %d", len, len, buf);
	}

	memcpy(reply + 5, buf, len);
	reply[0] = 'S';
	reply[1] = ' ' + ((len >> 12) & 63);
	reply[2] = ' ' + ((len >> 6) & 63);
	reply[3] = ' ' + ((len) & 63);
	reply[4] = ' ';

	/* Write response, and set new speed, after draining output */
	ic->write(ic, reply, 5 + len, 1);

	/* Pause to let the other side adjust */
	sleep(1);

	tcgetattr(ic->fd, &t);
	cfsetospeed(&t, speed);
	cfsetispeed(&t, speed);

	if (debug) {
		message("target_write: %d bytes to fd %d", 5 + len, ic->fd);
		dump_char_hex("raw write:", reply, 5 + len);
	}

	tcsetattr(ic->fd, TCSADRAIN, &t);

	/* Write the reply again, and now both side work at new speed */
	ic->write(ic, reply, 5 + len, 1);
	if (debug) {
		message("target_write: %d bytes to fd %d", 5 + len, ic->fd);
		dump_char_hex("raw write:", reply, 5 + len);
	}
}

static struct iochan *serial_channel(int fd)
{
	struct iochan *c = get_channel(fd);

	c->write = serial_write;
	c->read = serial_read;
	c->setSpeed = serial_setSpeed;
	polled_fd[c->poll_idx].events = POLLIN;

	return c;
}

/*
 * Definitions for a basic channel that just does read and write.
 * Read path may be optional.
 */
static int default_read(struct iochan *ic, char *buf, int len, int block)
{
	int ret = read(ic->fd, buf, len);

	if (ret < len) {
		ic->status &= ~POLLIN;
	}

	return ret;
}

static int default_write(struct iochan *ic, char *buf, int len, int block)
{
	return write(ic->fd, buf, len);
}

static int alt_write(struct iochan *ic, char *buf, int len, int block)
{
	return write(ic->fdout, buf, len);
}

static int no_read(struct iochan *ic, char *buf, int len, int block)
{
	return 0;
}

static struct iochan *basic_channel(int fd, int out_only)
{
	struct iochan *c = get_channel(fd);
	if (verbose)
		printf("basic_channel");
	c->write = default_write;
	if (out_only) {
		polled_fd[c->poll_idx].events = 0;
		c->read = no_read;
	} else {
		polled_fd[c->poll_idx].events = POLLIN | POLLERR | POLLHUP;
		c->read = default_read;
	}
	return c;
}

static struct iochan *separate_fd_channel(int fdin, int fdout)
{
	struct iochan *c = get_channel(fdin);
	if (verbose)
		printf("separate_fd_channel");

	c->fdout = fdout;
	c->write = alt_write;

	polled_fd[c->poll_idx].events = POLLIN | POLLERR | POLLHUP;
	c->read = default_read;

	return c;
}

static int stdin_read(struct iochan *ic, char *buf, int len, int block)
{
	int ret = read(0, buf, len);
	if (verbose)
		printf("stdin_read\n");
	if (ret < len) {
		ic->status &= ~POLLIN;
	}
	return ret;
}

static struct iochan *stdout_channel(int fd, int out_only)
{
	struct iochan *c = get_channel(fd);
	if (verbose)
		printf("stdout_channel");
	c->write = default_write;
	if (out_only) {
		polled_fd[c->poll_idx].events = 0;
		c->read = no_read;
	} else {
		polled_fd[c->poll_idx].events = POLLIN | POLLERR | POLLHUP;
		c->read = stdin_read;
	}
	return c;
}

static void set_socket_flag(int socket, int level, int flag)
{
	int tmp = 1;
	if (setsockopt(socket, level, flag, (char *)&tmp, sizeof(tmp)) != 0) {
		fatal("setsockopt(%d, %d, %d) failed", socket, level, flag);
	}
}

static int tcp_listen_state(struct iochan *orig, int state)
{
	int fd;
	struct protoent *protoent;
	int id = orig->stream_id;
	if (!(state & POLLIN)) {
		return 0;
	}

	fd = accept(orig->data, 0, 0);
	if (fd < 0) {
		fatal("accept() failed for stream %d", id);
	}

	protoent = getprotobyname("tcp");
	if (protoent == NULL) {
		fatal("getprotobyname(\"tcp\") failed");
	}
	set_socket_flag(fd, protoent->p_proto, TCP_NODELAY);
	message("accepted connection on stream %d", id);

	polled_fd[orig->poll_idx].events = POLLIN | POLLERR | POLLHUP;
	polled_fd[orig->poll_idx].fd = fd;
	orig->fd = fd;

	orig->state = default_state;
	orig->status = STREAM_READY;
	orig->read = default_read;
	orig->write = default_write;

	return 1;
}

/* Restore the listening stream socket */
static void tcp_detach_listen(struct iochan *ic)
{
	message("detach tcp stream %d", ic->stream_id);
	ic->state = tcp_listen_state;
	ic->fd = (int)ic->data;
	ic->status = 0;
	polled_fd[ic->poll_idx].fd = ic->fd;
	polled_fd[ic->poll_idx].events = POLLIN;
}

//static char str_escape[] = {0x42, 0x58"BX";
static char str_escape[] = { CH_SWITCH_ESCAPE, 0x58 };
static char str_char[] = { CH_SWITCH_ESCAPE, CH_SWITCH_ESCAPE };

#define TX_SEND_ESCAPE      0x12
#define TX_SEND_DATA        0x13

int tx_flag_state;

pthread_mutex_t input_mutex = PTHREAD_MUTEX_INITIALIZER;

#ifdef STATS
int target_rx_ch_switch_cnt;
int target_rx_ch_escapes;
int target_rx_char_cnt;
int target_tx_ch_switch_cnt;
int target_tx_ch_escapes;
int target_tx_char_cnt;
int stream_rx_char_cnt;
int stream_tx_char_cnt;
time_t start_time;
time_t end_time;
#endif

void handle_stream_input(struct iochan *c, char *str, int length)
{
	int i;
	int next_tx = 0;
	int clear_counter = 0;
	static struct iochan *current_c = NULL;
	if (verbose)
		printf("handle_stream_input %d\n", length);

	if (current_c != c) {
		tx_flag_state = TX_SEND_ESCAPE;
		current_c = c;
	}

	if (tx_flag_state == TX_SEND_ESCAPE) {
		str_escape[1] = c->stream_id + CH_CHANNEL_BASE;
		target_write((char *)str_escape, 2);
		tx_flag_state = TX_SEND_DATA;
#ifdef STATS
		target_tx_ch_switch_cnt++;
#endif
	}

	for (i = 0; i < length; i++) {
		clear_counter++;
		if (str[i] == CH_SWITCH_ESCAPE) {
#ifdef STATS
			target_tx_ch_escapes++;
#endif
			if (clear_counter) {
				target_write(&str[next_tx], clear_counter);
				clear_counter = 0;
			}
			next_tx = i + 1;
			target_write((char *)str_char, 1);

		}
	}
	target_write(&str[next_tx], clear_counter);
}

void *channel_thread(void *p)
{
	struct iochan *c = (struct iochan *)p;
	char buf[1000];
	int num;
	int rc;

	c->fd = accept(c->data, NULL, 0);
	if (verbose)
		printf("channel_thread: accept() %d\n", c->fd);

	if (c->fd < 0)
		pthread_exit(NULL);

	c->status = c->status | STREAM_READY;
	while (1) {
		num = recv(c->fd, buf, 1000, 0);
		if (num <= 0) {
			if (verbose)
				printf("socket exit %d\n", c->fd);
			close(c->fd);
			rc = pthread_create(&c->net_thread, NULL,
					    channel_thread, c);
			if (verbose)
				printf("thread create %d\n", rc);
			pthread_exit(NULL);
		}
#ifdef STATS
		stream_rx_char_cnt += num;
#endif

		pthread_mutex_lock(&input_mutex);
		handle_stream_input(c, buf, num);
		pthread_mutex_unlock(&input_mutex);
	}
}

#define MAX_RETRY_BIND_PORT 20

static struct iochan *tcp_listen_channel(int port, int stream_id)
{
	struct sockaddr_in sockaddr;
	struct iochan *c;
	int num_bind_retries = 0;
	int rc;

	if (verbose)
		printf("tcp_listen_channel %d %d\n", port, stream_id);

	int fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		fatal("socket() failed for stream %d", stream_id);
	}

	/* Allow rapid reuse of this port. */
	set_socket_flag(fd, SOL_SOCKET, SO_REUSEADDR);
#ifndef __linux__
#ifndef __CYGWIN__
	set_socket_flag(fd, SOL_SOCKET, SO_REUSEPORT);
#endif /* #ifndef __CYGWIN__ */
#endif /* #ifndef __linux__ */

	sockaddr.sin_family = PF_INET;
	sockaddr.sin_addr.s_addr = INADDR_ANY;
	sockaddr.sin_port = htons(port);
      retry:
	if (bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) != 0) {
		if (errno == EADDRINUSE) {
			if (num_bind_retries > MAX_RETRY_BIND_PORT) {
				fatal
				    ("Too many retries for bind on port %d for stream %d\n",
				     port, stream_id);
			}
			message("Retrying bind on port %d", port);
			num_bind_retries++;
			sleep(1);
			goto retry;
		} else {
			fatal("Bind failed");
		}
	}

	listen(fd, 4);

	c = get_channel(fd);

	polled_fd[c->poll_idx].events = POLLIN;

	c->state = tcp_listen_state;
	c->detach = tcp_detach_listen;
	c->stream_id = stream_id;
	c->data = fd;
	c->status = 0;
	c->write = default_write;

	message("IO Channel %d listening on port %d for stream %d",
		c->poll_idx, port, stream_id);

	rc = pthread_create(&c->net_thread, NULL, channel_thread, c);
	return c;
}

static int check_channel(struct iochan *c, int timeout)
{
	int idx = c->poll_idx;
	int ret = poll(&polled_fd[idx], 1, timeout);
	if (ret) {
		c->state(c, polled_fd[idx].revents & polled_fd[idx].events);
	}
	return ret;
}

void usage(void)
{
	fprintf(stderr, "Mux server version 1.01\n");
	fprintf(stderr, "usage: mux_server [-s <baud-rate>] [-exec] [-verbose] [-debug] \n");
	fprintf(stderr, "                  <target> <channel_port> ...\n");
	fprintf(stderr, "\n"
		"           <baud-rate> is the baud rate (e.g. 115200)\n\n"
		"           <target> is in the form <target>:<port> or <serial_device>,\n"
		"           or an external command to execute if -exec is specified.\n\n"
		"           <channel_port> is a list of network ports corresponding\n"
		"           mux channel numbers.\n");
	exit(-1);
}

void parse_command_line(int argc, char **argv)
{
	program_name = argv[0];

	if (argc < 2) {
		usage();
	}

	++argv;
	--argc;
	while (argc > 1) {
		if (strcmp(argv[0], "-verbose") == 0) {
			verbose = 1;
		} else if (strcmp(argv[0], "-debug") == 0) {
			debug = 1;
		} else if (strcmp(argv[0], "-hw") == 0) {
			hw_flowcontrol = 1;
		} else if (strcmp(argv[0], "-s") == 0) {
			if (argc < 2) {
				fatal("bad speed specification");
			}
			speed1 = getSpeed(argv[1]);
			if (speed1 == -1) {
				fatal("bad speed specification: %s", argv[1]);
			}
			argc -= 1;
			argv += 1;
		} else if (strcmp(argv[0], "-exec") == 0) {
			ext_program = 1;
		} else {
			break;
		}
		argv++;
		argc--;
	}

	if (argc < 1)
		usage();

	target_name = argv[0];
	nchannels = 0;
	argv++;
	argc--;

	for (nchannels = 0; nchannels < MAX_STREAMS; ++nchannels) {
		streams[nchannels] = NULL;
	}
	nchannels = 0;
	while (argc && (nchannels < MAX_STREAMS)) {
		int port = strtol(argv[0], NULL, 0);
		if (argv[0][0] == ':') {
			do {
				++nchannels;
			} while (nchannels % CHAN_SET_SIZE);
			argv++;
			argc--;
			continue;
		}

		if (strcmp(argv[0], "stdout") == 0) {
			streams[nchannels] = stdout_channel(1, 1);
		} else if (strcmp(argv[0], "stderr") == 0) {
			streams[nchannels] = stdout_channel(2, 1);
		} else if (strcmp(argv[0], "stdin") == 0) {
			streams[nchannels] = stdout_channel(1, 0);
		} else if (port > 0) {
			streams[nchannels] =
			    tcp_listen_channel(port, nchannels);
		} else {
			fatal("Unrecognizable stream spec: '%s'", argv[0]);
		}

		if (streams[nchannels]) {
			streams[nchannels]->stream_id = nchannels;
		}

		++nchannels;
		++argv;
		--argc;
	}
	if (argc) {
		fatal("%d stream maximum, %d specified", MAX_STREAMS,
		      nchannels + argc);
	}

	if (nchannels == 0)
		usage();
}

static void exec_ext_program(const char *target_name)
{
	int ret;
	
	/* in/out relative to the mux server, not the child */
	int outpipe[2], inpipe[2];
	
	ret = pipe(outpipe);
	if (ret < 0) {
		perror("exec_ext_program: pipe");
		exit(-1);
	}

	ret = pipe(inpipe);
	if (ret < 0) {
		perror("exec_ext_program: pipe");
		exit(-1);
	}

	ret = fork();
	if (ret < 0) {
		perror("exec_ext_program: fork");
		exit(-1);
	}

	if (ret > 0) {
		target = separate_fd_channel(inpipe[0], outpipe[1]);
		close(outpipe[0]);
		close(inpipe[1]);
		return;
	}

	/* Assume we already have a stdin/stdout/stderr. */
	dup2(outpipe[0], 0);
	dup2(inpipe[1], 1);

	close(outpipe[0]);
	close(outpipe[1]);
	close(inpipe[0]);
	close(inpipe[1]);

	ret = execlp("sh", "sh", "-c", target_name, NULL);
	perror("exec_ext_program: exec");
}

void target_connect(char *target_name, int speed)
{
	char *p, *host;
	int port, status;
	struct hostent *hostent;
	struct sockaddr_in sockaddr;
	struct sockaddr unixname;
	struct protoent *protoent;
	struct termios serialstate;
	int target_fd;
	int rc;

	if (ext_program) {
		exec_ext_program(target_name);
		return;
	}

	/*
	 * Anything with a ':' in it is interpreted as
	 * host:port over a TCP/IP socket.
	 */
	p = strrchr(target_name, ':');
	if (p != NULL) {
		*p = '\0';
		host = target_name;
		port = atoi(p + 1);

		hostent = gethostbyname(host);

		if (hostent == NULL) {
			fatal("unknown target host: %s", host);
		}

		message("connecting to target (host \"%s\", port %d)", host,
			port);
		while (1) {
			target_fd = socket(PF_INET, SOCK_STREAM, 0);
			if (debug)
				message("target is fd %d", target_fd);

			if (target_fd < 0) {
				fatal("socket() failed for target");
			}
			/* Allow rapid reuse of this port. */
			set_socket_flag(target_fd, SOL_SOCKET, SO_REUSEADDR);
#ifndef __linux__
#ifndef __CYGWIN__
			set_socket_flag(target_fd, SOL_SOCKET, SO_REUSEPORT);
#endif /* #ifndef PLATFORM_CYGWIN */
#endif /* #ifndef __linux__ */
			/* Enable TCP keep alive process. */
			set_socket_flag(target_fd, SOL_SOCKET, SO_KEEPALIVE);

			sockaddr.sin_family = PF_INET;
			sockaddr.sin_port = htons(port);
			memcpy(&sockaddr.sin_addr.s_addr, hostent->h_addr,
			       sizeof(struct in_addr));

			if (connect(target_fd, (struct sockaddr *)&sockaddr,
				    sizeof(sockaddr)) != 0) {
				if (errno == ECONNREFUSED) {
					/* close and retry */
					close(target_fd);
					sleep(1);
				} else {
					/* fatal error */
					fatal("connecting to target failed");
				}
			} else {
				// connected
				break;
			}
		}
		message("connected on fd %d", target_fd);

		protoent = getprotobyname("tcp");
		if (protoent == NULL) {
			fatal("getprotobyname(\"tcp\") failed");
		}

		set_socket_flag(target_fd, protoent->p_proto, TCP_NODELAY);

		target = basic_channel(target_fd, 0);

		return;
	}

	/*
	 * Perhaps it is a Unix domain socket
	 */
	target_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	unixname.sa_family = AF_UNIX;
	strcpy(unixname.sa_data, target_name);

	if (connect(target_fd, &unixname, strlen(unixname.sa_data) +
		    sizeof(unixname.sa_family)) >= 0) {
		message("connected on fd %d", target_fd);

		target = basic_channel(target_fd, 0);
		return;
	}
	close(target_fd);

	/*
	 * Perhaps it is a serial port
	 */
	target_fd = open(target_name, O_RDWR | O_NOCTTY);
	if (target_fd < 0) {
		fatal("open() failed for target (device \"%s\"): %d",
		      target_name, errno);
	}

	rc = tcgetattr(target_fd, &serialstate);
	if (rc < 0) {
		perror("error: ");
		fatal("tcgetattr() failed for target %d", rc);
	}

	/*
	 * Baud rate.
	 */
	cfsetospeed(&serialstate, speed);
	cfsetispeed(&serialstate, speed);

	/*
	 * Raw mode.
	 */
#if 1
	serialstate.c_iflag = 0;
	serialstate.c_oflag = 0;
	serialstate.c_lflag = 0;
	serialstate.c_cflag &= ~(CSIZE | PARENB);
	serialstate.c_cflag |= CLOCAL | CS8;
	serialstate.c_cc[VMIN] = 1;
	serialstate.c_cc[VTIME] = 0;

#else
	cfmakeraw(&serialstate);
	serialstate.c_cflag |= CLOCAL;
#endif

	if (tcsetattr(target_fd, TCSANOW, &serialstate) < 0) {
		fprintf(stderr, "tcsetattr() failed\n");
	}

	/* Pseudo tty's typically do not support modem signals */
	if (ioctl(target_fd, TIOCMGET, &status) < 0) {
		target = basic_channel(target_fd, 0);
	} else {
		target = serial_channel(target_fd);

		/* Allow other side to write to us, those making it readable */
		bitSet(target_fd, TIOCM_RTS);
	}
}

int target_read(char *buf, int len)
{
	int cnt = target->read(target, buf, len, 1);

	if (debug) {
		message("target_read: %d/%d bytes from fd %d", cnt, len,
			target->fd);
		dump_char_hex("raw read: ", buf, cnt);
	}

	if (cnt < 0) {
		fatal("read() failed for target");
	}

	if (cnt == 0) {
		fatal("EOF on read from target");
	}

	return cnt;
}

void target_write(char *buf, int len)
{
	int ret = 0;

	if (!target)
		return;		/* if we're not connected just ignore */

	if (debug) {
		message("target_write: %d bytes to fd %d", len, target->fd);
		dump_char_hex("raw write:", buf, len);
	}
#ifdef STATS
	target_tx_char_cnt += len;
#endif

	while (len > 0) {
		ret = target->write(target, buf, len, 1);
		if (ret < 0) {
			/* Shouldn't happen with blocking I/O, but just in case... */
			if (errno == EAGAIN)
				continue;

			break;
		}

		len -= ret;
		buf += ret;
	}

	if (ret < 0)
		fatal("write() failed for target socket: %s", strerror(errno));
}

int stream_write(int id, char *buf, int len, int block_for_connect)
{
	int n;
	int total = 0;
	int orig = len;

	if (!streams[id])
		return -1;

      restart:
	if (!(streams[id]->status & STREAM_READY)) {
		/* Perhaps somebody will connect */
		if (block_for_connect == 0) {
			check_channel(streams[id], 0);
		} else {
			check_channel(streams[id], -1);
			goto restart;
		}
	}

	if (!(streams[id]->status & STREAM_READY)) {
		/* Here we know block_for_connect == 0 ,
		 *  so non-blocking, thus quietly abort */
		return -1;
	}

	if (verbose) {
		display(id, '>', buf, len);
	}
	while (len) {
		n = streams[id]->write(streams[id], buf, len, 1);
		if (n < 0) {
			/* Should we go to restart if block_for_connect? */
			streams[id]->detach(streams[id]);
			if (block_for_connect)
				goto restart;
			message("Aborted write\n");
			if (total == 0) {
				return -1;
			}
			return total;
		} else {
			buf += n;
			len -= n;
		}
		total += n;

	}
	if (debug) {
		message("stream_write: %d/%d bytes to stream %d fd %d",
			total, orig, id, streams[id]->fd);
	}
	return total;
}

int current_rx_stream;
int rx_flag_state;
int rx_problem;
int process_mux_stream(char *buf, int length)
{
	int i;
	int stream = -1;

	for (i = 0; i < length; i++) {
		if (rx_flag_state == 1) {
			rx_flag_state = 0;

			if (buf[i] != CH_SWITCH_ESCAPE) {
#ifdef STATS
				target_rx_ch_switch_cnt++;
#endif

				if (buf[i] == CH_CHANNEL_RESET) {
					tx_flag_state = TX_SEND_ESCAPE;
					continue;
				}

				if (buf[i] < CH_CHANNEL_BASE) {
					message("%s: bad command %d after CH_SWITCH_ESCAPE\n",
					        __func__, buf[i]);
					rx_problem++;
					continue;
				}

				stream = stream_find(buf[i] - CH_CHANNEL_BASE);
				if (debug)
					message("Channel switch arrived %d %d",
						buf[i], stream);

				/* Can not find this one */
				/* for now skip it       */
				if (stream == -1) {
					message("%s: bad RX channel %d\n",
					        __func__, buf[i] - CH_CHANNEL_BASE);
					rx_problem++;
					continue;
				}
				current_rx_stream = stream;
				continue;
			} else {
#ifdef STATS
				target_rx_ch_escapes++;
#endif
			}

		} else if (buf[i] == CH_SWITCH_ESCAPE) {
			if (debug) {
				message("channel switch escape");
			}
			rx_flag_state++;
			continue;
		}

		if (current_rx_stream != -1) {
			if (debug)
				message("char %c sent to %d", buf[i], stream);
#ifdef STATS
			stream_tx_char_cnt++;
#endif
			stream_write(current_rx_stream, &buf[i], 1, 0);
		}

	}
	return length - i;
}

// FIXME: close open handles
void sighandler(int sig)
{
	message("Cleaning up");

#ifdef STATS
	fprintf(stderr, "\nserial received %d\n", target_rx_char_cnt);
	fprintf(stderr, "network received %d\n", stream_rx_char_cnt);
	fprintf(stderr, "serial received escapes %d\n",
		target_rx_ch_switch_cnt);
	fprintf(stderr, "serial received double %d\n", target_rx_ch_escapes);

	fprintf(stderr, "serial transmitted escapes %d\n",
		target_tx_ch_switch_cnt);
	fprintf(stderr, "serial transmitted double %d\n", target_tx_ch_escapes);
	fprintf(stderr, "serial transmitted chars %d\n", target_tx_char_cnt);
#endif

	exit(0);
}

int main(int argc, char **argv)
{
	int len;
	char buf[5 + (2 * MAX_PACKET_LEN)];
	char txreset[] = { CH_SWITCH_ESCAPE, CH_CHANNEL_RESET };
	const char *speedstr;
#ifdef STATS
	int first_time = 1;
#endif

#ifdef _POSIX_THREADS
	if (verbose)
		printf("sysconf(_SC_THREADS): %ld\n", sysconf(_SC_THREADS));
#else
	printf("_POSIX_THREADS not defined\n");
#endif

	fflush(stdout);

	parse_command_line(argc, argv);

	if (signal(SIGINT, sighandler) == SIG_ERR) {
		perror("signal registration failed: ");
		exit(1);
	}

	target_connect(target_name, speed1);

	pthread_mutex_lock(&input_mutex);
	target_write(txreset, 2);
	pthread_mutex_unlock(&input_mutex);

	speedstr = getSpeedName(speed1);
	if (speedstr)
		message("using speed: %s", speedstr);

	check_channel(target, 0);
	if (!(streams[0]->status & STREAM_READY)) {
		check_channel(streams[0], 0);
	}

	for (;;) {
		/* read the muxed channels from the target */
		len = target_read(buf, sizeof(buf));

		/* write to appropriate stream */
		process_mux_stream(buf, len);

#ifdef STATS
		if (stream_rx_char_cnt)
			if (first_time) {
				first_time = 0;
				time(&start_time);
			}

		target_rx_char_cnt += len;

		if (stream_rx_char_cnt > 0x200000) {
			double time_diff;
			time(&end_time);
			time_diff = difftime(end_time, start_time);
			fprintf(stderr, "Throughput %f bits per second\n",
				target_rx_char_cnt * 8 / (float)time_diff);
		}
#endif
	}

}
