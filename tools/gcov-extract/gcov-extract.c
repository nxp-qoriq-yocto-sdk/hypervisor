/******************************************************************************
 *
 * Copyright (C) 2007-2011 Freescale Semiconductor, Inc.
 *
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
 * gcov-extract: extract gcov data from the HV target.
 * Conforms to gcov architecture/design (v8)
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <stdbool.h>

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
#include <sys/termios.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/poll.h>
#include <signal.h>
#include <assert.h>

#include "gcov-extract.h"

int main(int argc, char *argv[]);

static void show_usage(char *prog_name);
static int connect_to_target(char *host, int port);
static void set_socket_flag(int socket, int level, int flag);
static int32_t byte_reverse(int32_t n);
static int64_t byte_reverse64(int64_t n);
#ifdef GET_STR_SIZE_OPTIMIZATION
static unsigned int fetch_str_size(int tgt_fd, unsigned int tgt_addr);
#endif
static void write_buf(int tgt_fd, const void *buf, size_t length);
static void read_buf(int tgt_fd, char *buf, unsigned int length);
static void fetch_tgt_data(int tgt_fd, unsigned int tgt_addr, unsigned int len_hst, char *gd_rsp);
static void fetch_tgt_gi_field(int tgt_fd, unsigned int tgt_addr, unsigned int len_hst, void *field);
static void fetch_tgt_gi_field64(int tgt_fd, unsigned int tgt_addr, unsigned int len_hst, void *field);
static void gcov_extract(int tgt_fd);

bool flag_help = false;
bool flag_verbose = false;
bool flag_target = false;
bool flag_libgcov_ver = false;
unsigned int host_libgcov_ver;

#ifdef DEBUG
#define DLOG(fmt, ...) printf(fmt, ## __VA_ARGS__);
#else
#define DLOG(fmt, ...)
#endif

#define VLOG(fmt, ...)               \
if (flag_verbose == true)            \
	printf(fmt, ## __VA_ARGS__);

int main(int argc, char *argv[])
{
	int index;
	int port;
	int tgt_fd;
	char *prog_name = argv[0];
	char *target_name, *p;
	char *host;

	if (argc == 1) {

		show_usage(prog_name);
		exit(0);
	}
	for (index = 1; index < argc; index++) {

		if (flag_help == false && strcmp(argv[index], "-h") == 0) {

			flag_help = true;
			show_usage(prog_name);

		} else if (flag_verbose == false && strcmp(argv[index], "-v") == 0) {

			flag_verbose = true;
			DLOG("Option -v specified, going verbose.\n");

		} else if (flag_libgcov_ver == false &&
		           strncmp(argv[index], "--host-libgcov-version=",
		           strlen("--host-libgcov-version=")) == 0) {

			flag_libgcov_ver = true;
			p = argv[index] + strlen("--host-libgcov-version=");
			host_libgcov_ver |= p[0] << 24;
			host_libgcov_ver |= p[1] << 16;
			host_libgcov_ver |= p[2] << 8;
			host_libgcov_ver |= p[3];
			DLOG("Option --host-libgcov-version specified, setting host libgcov version to: 0x%x\n",
			      host_libgcov_ver);

		} else if (flag_target == false) {

			flag_target = true;
			target_name = argv[index];
			p = strrchr(target_name, ':');

			if (p != NULL) {

				*p = '\0';
				host = target_name;
				port = atoi(p + 1);
				DLOG("Connecting to target (host: %s, port: %d)\n", host, port);
				tgt_fd = connect_to_target(host, port);
				gcov_extract(tgt_fd);

			} else {

				fprintf(stderr, "Incorrect port specification: %s\n", argv[index]);
				fprintf(stderr, "Exiting...\n");
				show_usage(prog_name);
				exit(1);
			}
		} else {

			fprintf(stderr, "Unknown or repeated option: %s\n", argv[index]);
			show_usage(prog_name);
			fprintf(stderr, "Exiting...\n");
			exit(1);
		}
	}
}

void show_usage(char *prog_name)
{
	printf("SYNTAX\n\n");
	printf("    %s -h\n", prog_name);
	printf("    %s [-v] [--host-libgcov-version=<version>] host:port\n\n", prog_name);
	printf("OPTIONS\n");
	printf("             -h\tprovide syntax/help message\n\n");
	printf("             -v\tVerbose.  Provides verbose messages during processing.\n\n");
	printf("             --host-libgcov-version=<version>\n\n");
	printf("             \tCreate .gcda files with given version value. The default behaviour is\n");
	printf("             \tto use version information from target side gcov_info; - that may not \n");
	printf("             \tcorrespond to the libgcov version on the host system).\n\n");
	printf("             host:port\tThe network port corresponding to the gcov byte-channel.\n\n");
	printf("SYNOPSIS\n");
	printf("             Extract remote gcov data from target and then create .gcda files that\n");
	printf("             gcov(1) can use.\n\n");
}

static void set_socket_flag(int socket, int level, int flag)
{
	int tmp = 1;
	if (setsockopt(socket, level, flag, (char *)&tmp, sizeof(tmp)) != 0) {

		fprintf(stderr, "setsockopt(%d, %d, %d) failed", socket, level, flag);
		fprintf(stderr, "Exiting...\n");
		exit(1);
	}
}

/* Adapted from mux_server/mux_server.c/target_connect() */
static int connect_to_target(char *host, int port)
{
	int tgt_fd;
	struct sockaddr_in sockaddr;
	struct hostent *hostent;
	int try_count = 0;

	while (1) {

		DLOG("Trying to connect to target. (Attempt %d)\n", try_count);
		hostent = gethostbyname(host);

		if (hostent == NULL) {

			/* Fatal error. */
			fprintf(stderr, "Unknown host: %s\n", host);
			fprintf(stderr, "Exiting...\n");
			exit(1);

		}
		tgt_fd = socket(PF_INET, SOCK_STREAM, 0);
		DLOG("Target file descriptor = %d\n", tgt_fd);

		if (tgt_fd < 0) {

			fprintf(stderr, "socket() failed for target.");
			fprintf(stderr, "Exiting...\n");
			exit(1);

		}
		/* Allow rapid reuse of this port. */
		set_socket_flag(tgt_fd, SOL_SOCKET, SO_REUSEADDR);
#ifndef __linux__
#ifndef __CYGWIN__
		set_socket_flag(tgt_fd, SOL_SOCKET, SO_REUSEPORT);
#endif /* #ifndef PLATFORM_CYGWIN */
#endif /* #ifndef __linux__ */
		/* Enable TCP keep alive process. */
		set_socket_flag(tgt_fd, SOL_SOCKET, SO_KEEPALIVE);
		set_socket_flag(tgt_fd, IPPROTO_TCP, TCP_NODELAY);

		sockaddr.sin_family = PF_INET;
		sockaddr.sin_port = htons(port);
		memcpy(&sockaddr.sin_addr.s_addr, hostent->h_addr, sizeof(struct in_addr));

		if (connect(tgt_fd, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) != 0) {

			if (errno == ECONNREFUSED) {

				/* Close and retry. */
				close(tgt_fd);
				sleep(1);

			} else {

				/* Fatal error. */
				fprintf(stderr, "Connecting to target failed");
				fprintf(stderr, "Exiting...\n");
				exit(1);
			}
		} else {
			/* Connected. */
			break;
		}
		try_count++;
	}
	DLOG("Connected on file descriptor: %d\n", tgt_fd);
	return tgt_fd;
}

static int32_t byte_reverse(int32_t n)
{
	char *a, tmp;
	int index;

	assert(sizeof(int) == 4);
	a = (char *) &n;
	for (index = 0; index < 2; index++) {
		tmp = a[index];
		a[index] = a[3 - index];
		a[3 - index] = tmp;
	}
	return n;
}

static int64_t byte_reverse64(int64_t n)
{
	char *a, tmp;
	int index;

	assert(sizeof(uint64_t) == 8);
	a = (char *) &n;
	for (index = 0; index < 4; index++) {
		tmp = a[index];
		a[index] = a[7 - index];
		a[7 - index] = tmp;
	}
	return n;
}

#ifdef GET_STR_SIZE_OPTIMIZATION
static unsigned int fetch_str_size(int tgt_fd, unsigned int tgt_addr)
{
	char gsl_rsp[tgt_info.word_size];
	unsigned int tgt_addr_tgt, str_size_hst, str_size_tgt;
	char get_str_size_cmd[sizeof("get-str-size") + tgt_info.word_size], *cmd;

	DLOG("Sending: get-str-size 0x%x\n", tgt_addr);
	cmd = get_str_size_cmd;
	/* sizeof() gets the trailing '\0' in too after the command's name,
	 * as the gcov-extract protocol expects.
	 */
	memcpy(cmd, "get-str-size", sizeof("get-str-size"));
	cmd += sizeof("get-str-size");
	tgt_addr_tgt = byte_reverse(tgt_addr);
	memcpy(cmd, &tgt_addr_tgt, sizeof(tgt_addr_tgt));
	cmd += sizeof(tgt_addr_tgt);
	write_buf(tgt_fd, get_str_size_cmd, sizeof(get_str_size_cmd));
	read_buf(tgt_fd, gsl_rsp, tgt_info.word_size);
	memcpy(&str_size_tgt, &gsl_rsp[0], tgt_info.word_size);
	DLOG("str_size_tgt = %d\n", str_size_tgt);
	str_size_hst = byte_reverse(str_size_tgt);
	DLOG("str_size_hst = %d\n", str_size_hst);
	return str_size_hst;
}
#endif

static void write_buf(int tgt_fd, const void *buf, size_t length)
{
	int i, count;
	char tmpbuf[length * 2 + 1];
	uint8_t ch;

	for (i = 0; i < length; i++) {
		ch = ((uint8_t *)buf)[i];
		snprintf(&tmpbuf[i * 2], 3, "%02x", ch);
	}

	count = write(tgt_fd, tmpbuf, length * 2);
	if (count == -1) {
		fprintf(stderr, "I/O error.");
		fprintf(stderr, "Exiting...\n");
		exit(-1);
	}
}

static int hex_digit(uint8_t c)
{
        if (c >= '0' && c <= '9')
                return c - '0';
        else if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;

	fprintf(stderr, "%s: invalid hex digit: 0x%02x\n", __func__, c);
        assert(0);

        return -1;
}

static void read_buf(int tgt_fd, char *buf, unsigned int length)
{
	unsigned int received, count;
	uint8_t ch;
	int d1, d2;

	for(received = 0; received < length;) {
		count = read(tgt_fd, &ch, 1);
		if (count == -1) {
			fprintf(stderr, "I/O error.");
			fprintf(stderr, "Exiting...\n");
			exit(-1);
		}
		d1 = hex_digit(ch);

		count = read(tgt_fd, &ch, 1);
		if (count == -1) {
			fprintf(stderr, "I/O error.");
			fprintf(stderr, "Exiting...\n");
			exit(-1);
		}
		d2 = hex_digit(ch);

		buf[received++] = (d1 << 4) | d2;
	}
}

static void fetch_tgt_data(int tgt_fd, unsigned int tgt_addr, unsigned int len_hst, char *gd_rsp)
{
	unsigned int tgt_addr_tgt, len_tgt;
	char get_data_cmd[sizeof("get-data") + 2 * tgt_info.word_size], *cmd;

	DLOG("Sending: get-data 0x%x %d\n", tgt_addr, len_hst);
	cmd = get_data_cmd;
	/* sizeof() gets the trailing '\0' in too after the command's name,
	 * as the gcov-extract protocol expects.
	 */
	memcpy(cmd, "get-data", sizeof("get-data"));
	cmd += sizeof("get-data");
	tgt_addr_tgt = byte_reverse(tgt_addr);
	memcpy(cmd, &tgt_addr_tgt, sizeof(tgt_addr_tgt));
	cmd += sizeof(tgt_addr_tgt);
	len_tgt = byte_reverse(len_hst);
	memcpy(cmd, &len_tgt, sizeof(len_tgt));
	write_buf(tgt_fd, get_data_cmd, sizeof(get_data_cmd));
	read_buf(tgt_fd, gd_rsp, len_hst);
}

static void fetch_tgt_gi_field(int tgt_fd, unsigned int tgt_addr, unsigned int len_hst, void *field)
{
	char gd_rsp[len_hst];

	/* gcov_info.n_functions */
	fetch_tgt_data(tgt_fd, tgt_addr, len_hst, gd_rsp);
	memcpy(field, gd_rsp, len_hst);
	/* What got memcpy()'ied is in target endianness,
	 * so convert it to host endianness */
	*(int *)field = byte_reverse(*(int *)field);
}

static void fetch_tgt_gi_field64(int tgt_fd, unsigned int tgt_addr, unsigned int len_hst, void *field)
{
	char gd_rsp[len_hst];

	/* gcov_info.n_functions */
	fetch_tgt_data(tgt_fd, tgt_addr, len_hst, gd_rsp);
	memcpy(field, gd_rsp, len_hst);
	/* What got memcpy()'ied is in target endianness,
	 * so convert it to host endianness */
	for (int i = 0; i < len_hst / sizeof (uint64_t); i++)
		((uint64_t *)field)[i] = byte_reverse64(((uint64_t *)field)[i]);
}

static void gcov_extract(int tgt_fd)
{
	/* Note: In the variable names below:
	 *
         * _hst suffix => To use/interprete on hst
         * _tgt suffix => To use/interprete on tgt
	 *
         * Also,
         *
         * hst_ prefix => Host entity
         * tgt_ prefix => Target entity
         *
         * e.g.
         *
         * tgt_gcov_info_addr_tgt refers to a gcov_info structure address as obtained
         * from the target. To perform (on the host) any pointer arithmetic on the
         * address, we'd do:
         * tgt_gcov_info_addr_hst = byte_reverse (tgt_gcov_info_addr_tgt);
         * do the target style pointer arithmetic on tgt_gcov_info_addr_hst (using
         * tgt_gcov_info_addr_hst + tgt_word_size_hst to construct the address of the
         * next word on the target, for example) and then reconvert back:
         * tgt_gcov_info_addr_tgt = byte_reverse (tgt_gcov_info_addr_hst);
         * for use on the target.
         *
	 * Note: This nomenclature only applies when we need to go back and forth from
	 *       target to host (and vice-versa) when dealing with a value.
	 *       e.g. hst_gcov_info_list and hst_gcov_info will only be used on the
	 *       host side, and hence do not have a _hst suffix
	 */

	/* The first four bytes in the tgt's response to get-data-info */
	const int tgt_word_size_len = 4;
	/* The next four bytes in the tgt's response to get-data-info */
	const int tgt_data_size_len = 4;
	/* The header occupies the first tgt_data_info_len bytes in the tgt's
	 * response to get-data-info
	 */
	const int tgt_data_info_len = tgt_word_size_len + tgt_data_size_len;
	char gdi_rsp_hdr[tgt_data_info_len];
	unsigned int tgt_word_size_hst, tgt_data_size_hst;
	unsigned int tgt_word_size_tgt, tgt_data_size_tgt;
	unsigned int index;
	char *gdi_rsp_data;
	unsigned int tgt_gcov_info_addr_tgt;
	unsigned int tgt_gcov_info_addr_hst;
	unsigned int tgt_gcov_info_size_tgt;
	unsigned int tgt_gcov_info_size_hst;
	struct gcov_info *hst_gcov_info_list = NULL, *hst_gcov_info, *hst_gcov_info_next;

	DLOG("Sending: get-data-info\n");
	/* sizeof() gets the trailing '\0' in too after the command's name,
	 * as the gcov-extract protocol expects.
	 */
	write_buf(tgt_fd, "get-data-info", sizeof("get-data-info"));
	read_buf(tgt_fd, gdi_rsp_hdr, tgt_data_info_len);

	memcpy(&tgt_word_size_tgt, &gdi_rsp_hdr[0], tgt_word_size_len);
	DLOG("tgt_word_size_tgt = %d\n", tgt_word_size_tgt);
	/* Convert from big-endian to little-endian. */
	tgt_word_size_hst = byte_reverse(tgt_word_size_tgt);
	DLOG("tgt_word_size_hst = %d\n", tgt_word_size_hst);

	DLOG("tgt_info.word_size = %d\n", tgt_word_size_hst);
	tgt_info.word_size = tgt_word_size_hst;

	memcpy(&tgt_data_size_tgt, &gdi_rsp_hdr[tgt_word_size_len], tgt_data_size_len);
	DLOG("tgt_data_size_tgt = %d\n", tgt_data_size_tgt);
	/* Convert from big-endian to little-endian. */
	tgt_data_size_hst = byte_reverse(tgt_data_size_tgt);
	DLOG("tgt_data_size_hst = %d\n", tgt_data_size_hst);

	assert(tgt_data_size_hst % (2 * tgt_word_size_hst) == 0);
        /* Two-times, 'cause the format is: (addr, size)+ */

	/* Now, get the target data. */
	gdi_rsp_data = (char *) malloc(tgt_data_size_hst * sizeof(char));
	if (!gdi_rsp_data) {
		fprintf(stderr, "Out of memory.\n");
		fprintf(stderr, "Exiting...\n");
		exit(1);
	}
	read_buf(tgt_fd, gdi_rsp_data, tgt_data_size_hst);

	/* Now, iterate though the linked list of gcov_info structures on the target,
	 * creating your own host-side gcov_info's
	 */
	memcpy(&tgt_gcov_info_addr_tgt, &gdi_rsp_data[0], tgt_word_size_hst);
	DLOG("tgt_gcov_info_addr_tgt = 0x%x\n", tgt_gcov_info_addr_tgt);
	tgt_gcov_info_addr_hst = byte_reverse(tgt_gcov_info_addr_tgt);
	DLOG("tgt_gcov_info_addr_hst = 0x%x\n", tgt_gcov_info_addr_hst);

	memcpy(&tgt_gcov_info_size_tgt, &gdi_rsp_data[tgt_word_size_hst],
	        tgt_word_size_hst);
	DLOG("tgt_gcov_info_size_tgt = 0x%x\n", tgt_gcov_info_size_tgt);
	tgt_gcov_info_size_hst = byte_reverse(tgt_gcov_info_size_tgt);
	DLOG("tgt_gcov_info_size_hst = 0x%x\n", tgt_gcov_info_size_hst);

	VLOG("Extracting remote gcov data.\n");
	while (tgt_gcov_info_addr_hst != 0x0) {

		unsigned int tgt_gi_filename_addr_hst;
		unsigned int tgt_gi_functions_addr_hst;
		unsigned int tgt_gci_values_addr_hst;
		/* +Hard coding alert!+ */
		const int TGT_FILENAME_MAX_LEN = 256;
		char tgt_gi_filename[TGT_FILENAME_MAX_LEN];
		/* -Hard coding alert!- */
		unsigned int tgt_gi_filename_len;

		/* Build gcov_info's on host. */
		DLOG("Creating host gcov_info structure.\n");
		hst_gcov_info = (struct gcov_info *) calloc(1, sizeof(struct gcov_info) +
		                                               sizeof(struct gcov_ctr_info));
		assert(hst_gcov_info != NULL);
		hst_gcov_info->next = hst_gcov_info_list;
		hst_gcov_info_list = hst_gcov_info;

		/* gcov_info.version */
		fetch_tgt_gi_field(tgt_fd, tgt_gcov_info_addr_hst + tgt_info.gi_version_offst,
		                   tgt_info.unsigned_size, &hst_gcov_info->version);
		/* Unless the target and the host use the same GCC, just match it to whatever
		 * version is expected on the host-side. The user would specify this via
		 * the argument to the command-line switch: --host-libgcov-version
		 */
		if (flag_libgcov_ver == true) {

			DLOG("Target was compiled assuming libgcov version: 0x%x; "
			     "over-riding to libgcov version: 0x%x\n",
			      hst_gcov_info->version, host_libgcov_ver);
			hst_gcov_info->version = host_libgcov_ver;
		}

		/* gcov_info.stamp */
		fetch_tgt_gi_field(tgt_fd, tgt_gcov_info_addr_hst + tgt_info.gi_stamp_offst,
		                   tgt_info.unsigned_size, &hst_gcov_info->stamp);

		/* gcov_info.filename */
		fetch_tgt_gi_field(tgt_fd, tgt_gcov_info_addr_hst + tgt_info.gi_filename_offst,
		                   tgt_word_size_hst, &tgt_gi_filename_addr_hst);

/* GET_STR_SIZE_OPTIMIZATION ought to be defined by default.
 * The #else is just in case we want to revert to the classic
 * protocol anytime.
 */
#ifdef GET_STR_SIZE_OPTIMIZATION
		/* get-str-size extension/optimization to classic protocol */
		tgt_gi_filename_len = fetch_str_size(tgt_fd,
		                                     tgt_gi_filename_addr_hst);
		assert(tgt_gi_filename_len < TGT_FILENAME_MAX_LEN);
		fetch_tgt_data(tgt_fd, tgt_gi_filename_addr_hst,
		               tgt_gi_filename_len, tgt_gi_filename);
#else
		/* Classic protocol. */
		tgt_gi_filename_len = 0;
		do {

			assert(tgt_gi_filename_len < TGT_FILENAME_MAX_LEN);
			fetch_tgt_data(tgt_fd, tgt_gi_filename_addr_hst + tgt_gi_filename_len,
			               1, tgt_gi_filename + tgt_gi_filename_len);
			DLOG("tgt_gi_filename[%d] = %c\n", tgt_gi_filename_len,
			      tgt_gi_filename[tgt_gi_filename_len]);
		} while(tgt_gi_filename[tgt_gi_filename_len++] != '\0');
#endif
		DLOG("tgt_gi_filename_len = %d\n", tgt_gi_filename_len);
		DLOG("tgt_gi_filename = %s\n", tgt_gi_filename);
		hst_gcov_info->filename =(char *) calloc(tgt_gi_filename_len, sizeof(char));
		assert(hst_gcov_info->filename != NULL);
		strcpy(hst_gcov_info->filename, tgt_gi_filename);
		VLOG("Extracting remote gcov data for: %s\n", hst_gcov_info->filename);

		/* gcov_info.n_functions */
		fetch_tgt_gi_field(tgt_fd, tgt_gcov_info_addr_hst + tgt_info.gi_n_functions_offst,
		                   tgt_info.unsigned_size, &hst_gcov_info->n_functions);

		/* gcov_info.functions */
		hst_gcov_info->functions = (struct gcov_fn_info *) calloc(hst_gcov_info->n_functions,
                                                                          sizeof(struct gcov_fn_info) +
		                                                          sizeof(unsigned));
		assert(hst_gcov_info->functions != NULL);

		fetch_tgt_gi_field(tgt_fd, tgt_gcov_info_addr_hst + tgt_info.gi_functions_offst,
		                   tgt_word_size_hst, &tgt_gi_functions_addr_hst);

		struct gcov_fn_info *fi;
		char fn_info_tmp[tgt_info.gcov_fn_info_size * 8];
		int left = hst_gcov_info->n_functions;
		int amount;

		index = 0;
		while (left > 0) {
			amount = left < 8 ? left : 8;

			fetch_tgt_data(tgt_fd,
				       tgt_gi_functions_addr_hst + index * tgt_info.gcov_fn_info_size,
				       amount * tgt_info.gcov_fn_info_size,
				       fn_info_tmp);

			for (int i = 0; i < amount; i++) {
				fi = (struct gcov_fn_info *) ((char *) hst_gcov_info->functions +
                                                                    (index + i) * tgt_info.gcov_fn_info_size);

				fi->ident = byte_reverse(*(int *)(fn_info_tmp + i * tgt_info.gcov_fn_info_size + tgt_info.gfi_ident_offst));
				fi->checksum = byte_reverse(*(int *)(fn_info_tmp + i * tgt_info.gcov_fn_info_size + tgt_info.gfi_checksum_offst));
				fi->n_ctrs[0] =  byte_reverse(*(int *)(fn_info_tmp + i * tgt_info.gcov_fn_info_size + tgt_info.gfi_n_ctrs_offst));

				DLOG("fi->ident = %d fi->checksum = %x fi->n_ctrs = %x\n",
					fi->ident, fi->checksum, fi->n_ctrs[0]);
			}

			left -= amount;
			index += amount;

			VLOG("\tExtracting functions... %d/%d\r", index, hst_gcov_info->n_functions);
			fflush(stdout);
		}
		VLOG("\n");

		/* gcov_info.ctr_mask */
		fetch_tgt_gi_field(tgt_fd, tgt_gcov_info_addr_hst + tgt_info.gi_ctr_mask_offst,
		                   tgt_info.unsigned_size, &hst_gcov_info->ctr_mask);

		/* gcov_info.counts[0].num */
		fetch_tgt_gi_field(tgt_fd, tgt_gcov_info_addr_hst + tgt_info.gi_counts_offst + tgt_info.gci_num_offst,
		                   tgt_info.unsigned_size, &hst_gcov_info->counts[0].num);

		/* gcov_info.counts[0].values */
		hst_gcov_info->counts[0].values = (uint64_t *) calloc(hst_gcov_info->counts[0].num, sizeof(uint64_t));
		assert(hst_gcov_info->counts[0].values != NULL);

		fetch_tgt_gi_field(tgt_fd, tgt_gcov_info_addr_hst + tgt_info.gi_counts_offst + tgt_info.gci_values_offst,
		                   tgt_word_size_hst, &tgt_gci_values_addr_hst);

		int n = (sizeof (uint64_t) / sizeof (tgt_info.long_size)) * sizeof (tgt_info.long_size);
		left =  hst_gcov_info->counts[0].num;
		index = 0;
		while (left > 0) {
			amount = left < 16 ? left : 16;
			fetch_tgt_gi_field64(tgt_fd, tgt_gci_values_addr_hst + index * n, amount * n,
					     &hst_gcov_info->counts[0].values[index]);
			left -= amount;
			index += amount;

			VLOG("\tExtracting counters... %d/%d\r", index, hst_gcov_info->counts[0].num);
			fflush(stdout);
		}
		VLOG("\n");

		/* gcov_info.counts[0].merge */
		hst_gcov_info->counts[0].merge = (gcov_merge_fn) &__gcov_merge_add;

		/* Calculate the address of the next gcov_info in the linked-list on the target. */
		fetch_tgt_gi_field(tgt_fd, tgt_gcov_info_addr_hst + tgt_info.gi_next_offst,
		                   tgt_word_size_hst, &tgt_gcov_info_addr_hst);
	}
	VLOG("Done extracting remote gcov data.\n");

	VLOG("Creating .gcda files.\n");
	for (hst_gcov_info = hst_gcov_info_list; hst_gcov_info != 0x0; ) {
		hst_gcov_info_next = hst_gcov_info->next;
		/* Save hst_gcov_info->next cause __gcov_init() will end up
		 * setting it to 0x0 on the zeroeth call. */
		VLOG("Creating: %s\n", hst_gcov_info->filename);
		__gcov_init(hst_gcov_info);
		hst_gcov_info = hst_gcov_info_next;
	}
	VLOG("Done creating .gcda files.\n");

	exit(0);
}
