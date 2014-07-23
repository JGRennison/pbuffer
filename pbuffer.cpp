//  pbuffer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>

#include <deque>
#include <string>
#include <algorithm>
#include <cassert>

#ifndef VERSION_STRING
#define VERSION_STRING __DATE__ " " __TIME__
#endif
const char version_string[] = "pbuffer " VERSION_STRING;
const char authors[] = "Written by Jonathan G. Rennison <j.g.rennison@gmail.com>";

struct pollfd poll_array[2];

enum {
	POLLFD_INPUT = 0,
	POLLFD_OUTPUT = 1,
};

const size_t buffer_count_shrink_threshold = 16;
size_t max_queue = 0;
size_t read_size = 65536;

bool show_progress = false;
bool human_readable = false;
uint64_t total_read = 0;
uint64_t read_count = 0;
uint64_t write_count = 0;

struct buffer_info {
	void *buffer = nullptr;
	size_t length = 0;
	size_t offset = 0;

	buffer_info() { }
	buffer_info(buffer_info &&movefrom) noexcept {
		std::swap(buffer, movefrom.buffer);
		std::swap(length, movefrom.length);
		std::swap(offset, movefrom.offset);
	}
	~buffer_info() {
		free(buffer);
		buffer = nullptr;
	}
};

std::deque<buffer_info> buffers;
size_t total_buffered;

bool no_more_input = false;
bool force_exit = false;

void setnonblock(int fd, const char *name) {
	int flags = fcntl(fd, F_GETFL, 0);
	int res = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if(flags < 0 || res < 0) {
		fprintf(stderr, "Could not fcntl set O_NONBLOCK %s: %m\n", name);
		exit(1);
	}
}

void enable_input(bool enabled) {
	poll_array[POLLFD_INPUT].events = POLLERR | (enabled ? POLLIN : 0);
}

void enable_output(bool enabled) {
	poll_array[POLLFD_OUTPUT].events = POLLERR | (enabled ? POLLOUT : 0);
}

void check_input_block() {
	enable_input(!(no_more_input || total_buffered >= max_queue));
}

void check_output_block() {
	enable_output(!buffers.empty());
}

void read_input() {
	size_t to_read = std::min(read_size, max_queue - total_buffered);
	buffer_info buff;
	buff.buffer = malloc(to_read);
	if(!buff.buffer) exit(2);

	read:
	ssize_t bread = read(STDIN_FILENO, buff.buffer, to_read);
	if(bread < 0) {
		if(errno == EINTR) {
			if(force_exit) return;
			else goto read;
		}
		fprintf(stderr, "Failed to read from STDIN: %m\n");
		no_more_input = true;
	}
	else if(bread == 0) {
		fprintf(stderr, "no_more_input = true\n");
		no_more_input = true;
	}
	else if(bread > 0) {
		buff.length = bread;
		buffers.emplace_back(std::move(buff));
		total_buffered += bread;
		total_read += bread;
		read_count++;

		if(buffers.size() >= buffer_count_shrink_threshold) {
			//Starting to accumulate a lot of buffers
			//Shrink to fit the older ones to avoid storing large numbers of potentially mostly empty buffers
			buffer_info &buffer_to_shrink = buffers[buffers.size() - buffer_count_shrink_threshold];
			if(buffer_to_shrink.length <= read_size / 2) {
				buffer_to_shrink.buffer = realloc(buffer_to_shrink.buffer, buffer_to_shrink.length);
				if(!buffer_to_shrink.buffer) exit(2);
			}
		}
		enable_output(true);
	}
	check_input_block();
}

void write_output() {
	if(!(poll_array[POLLFD_OUTPUT].revents & POLLOUT)) {
		fprintf(stderr, "Output poll() error.\n");
		exit(1);
	}

	while(!buffers.empty()) {
		buffer_info &buffer_to_write = buffers.front();

		ssize_t result = write(STDOUT_FILENO, buffer_to_write.buffer, buffer_to_write.length);
		if(result < 0) {
			if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;

			fprintf(stderr, "Write failed: %m.\n");
			exit(1);
		}
		else if(result < (ssize_t) buffer_to_write.length) {
			memmove(buffer_to_write.buffer, static_cast<const char *>(buffer_to_write.buffer) + result, buffer_to_write.length - result);
			buffer_to_write.length -= result;
		}
		else {
			buffers.pop_front();
		}
		total_buffered -= result;
		write_count++;
	}
	check_input_block();
	check_output_block();
}

void sighandler(int sig) {
	force_exit = true;
}

void show_usage(FILE *f) {
	fprintf(f,
			"Usage: pbuffer [options]\n"
			"\tCopy STDIN to STDOUT, storing up to a fixed number of bytes.\n"
			"\tIn the event of a read error or end of input, this will wait until\n"
			"\tall stored bytes have been output before exiting.\n"
			"\tNo attempt is made to line-buffer or coalesce the input.\n"
			"\n"
			"-m, --max-queue bytes\n"
			"\tMaximum amount of data to store.\n"
			"\tAccepts suffixes: k, M, G, T, for powers of 1024.\n"
			"\tThis option is required unless using -h or -V.\n"
			"-r, --read-size bytes\n"
			"\tMaximum amount of data to read in one go.\n"
			"\tAccepts suffixes: k, M, G, for multiples of 1024. Default: 64k.\n"
			"-p, --progress\n"
			"\tShow a progress line on STDERR.\n"
			"-s, --human-readable\n"
			"\tShow progress sizes in human-readable format (e.g. 1k, 23M).\n"
			"-h, --help\n"
			"\tShow this help\n"
			"-V, --version\n"
			"\tShow version information\n"
	);
}

// Returns true and sets output on success
bool parse_size(const char *input, size_t &output, const char *name) {
	char *end = 0;
	size_t value = strtoul(input, &end, 0);
	if(!end) { /* do nothing*/ }
	else if(!*end) { /* valid integer */ }
	else if(end == std::string("k")) value <<= 10;
	else if(end == std::string("M")) value <<= 20;
	else if(end == std::string("G")) value <<= 30;
	else if(end == std::string("T")) value <<= 40;
	else {
		fprintf(stderr, "Invalid %s: '%s'\n", name, input);
		return false;
	}
	output = value;
	return true;
}

static struct option options[] = {
	{ "max-queue",     required_argument,  nullptr, 'm' },
	{ "read-size",     required_argument,  nullptr, 'r' },
	{ "progress",      no_argument,        nullptr, 'p' },
	{ "human-readable",no_argument,        nullptr, 's' },
	{ "help",          no_argument,        nullptr, 'h' },
	{ "version",       no_argument,        nullptr, 'V' },
	{ nullptr, 0, nullptr, 0 },
};

char *humanise_size(double in, char *buffer, size_t length) {
	if(length < 7) return nullptr;

	const char *suffix = nullptr;
	do {
		if(in < 1024) {
			snprintf(buffer, 6, "%5d", (int) in);
			return buffer;
		}
		in /= 1024;
		if(in < 1024) { suffix = "k"; break; }
		in /= 1024;
		if(in < 1024) { suffix = "M"; break; }
		in /= 1024;
		if(in < 1024) { suffix = "G"; break; }
		in /= 1024;
		if(in < 1024) { suffix = "T"; break; }
		in /= 1024;
		if(in < 1024) { suffix = "P"; break; }
		in /= 1024;
		suffix = "E"; break;
	} while(false);
	snprintf(buffer, 6, " %#4.4g", in);
	if(buffer[4] == '.') buffer[4] = 0;
	else buffer++;
	strcat(buffer, suffix);
	return buffer;
}

void print_progress_line() {
	if(human_readable) {
		char total_read_s[16], total_buffered_s[16];
		fprintf(stderr, "\rRead: %s, Buffer: %s %3d%% (%u), Reads: %14llu, Writes: %14llu",
			humanise_size(total_read, total_read_s, sizeof(total_read_s)),
			humanise_size(total_buffered, total_buffered_s, sizeof(total_buffered_s)),
			(int) ((100 * total_buffered) / max_queue), (unsigned int) buffers.size(),
			(unsigned long long int) read_count, (unsigned long long int) write_count
		);
	}
	else {
		fprintf(stderr, "\rRead: %14llu, Buffer: %14zu %3d%% (%u), Reads: %14llu, Writes: %14llu",
			(unsigned long long int) total_read, (size_t) total_buffered,
			(int) ((100 * total_buffered) / max_queue), (unsigned int) buffers.size(),
			(unsigned long long int) read_count, (unsigned long long int) write_count
		);
	}
}

int main(int argc, char **argv) {
	int n = 0;
	while (n >= 0) {
		n = getopt_long(argc, argv, "m:r:pshV", options, NULL);
		if (n < 0) continue;
		switch (n) {
		case 'm': {
			bool ok = parse_size(optarg, max_queue, "max queue length");
			if(!ok) {
				show_usage(stderr);
				exit(1);
			}
			break;
		}
		case 'r': {
			bool ok = parse_size(optarg, read_size, "read size");
			if(!ok) {
				show_usage(stderr);
				exit(1);
			}
			break;
		}
		case 'p':
			show_progress = true;
			break;
		case 's':
			human_readable = true;
			break;
		case 'V':
			fprintf(stdout, "%s\n\n%s\n", version_string, authors);
			exit(0);
		case '?':
			show_usage(stderr);
			exit(1);
		case 'h':
			show_usage(stdout);
			exit(0);
		}
	}

	if(max_queue == 0) {
		show_usage(stderr);
		exit(1);
	}

	struct sigaction new_action;
	memset(&new_action, 0, sizeof(new_action));
	new_action.sa_handler = sighandler;
	sigaction(SIGINT, &new_action, 0);
	sigaction(SIGHUP, &new_action, 0);
	sigaction(SIGTERM, &new_action, 0);
	new_action.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &new_action, 0);

	setnonblock(STDIN_FILENO, "STDIN");
	setnonblock(STDOUT_FILENO, "STDOUT");
	poll_array[POLLFD_INPUT].fd = STDIN_FILENO;
	poll_array[POLLFD_OUTPUT].fd = STDOUT_FILENO;
	enable_input(true);
	enable_output(false);

	struct timeval now_time {0, 0};
	struct timeval prev_time {0, 0};
	int timeout = -1;
	if(show_progress) {
		gettimeofday(&now_time, nullptr);
		prev_time = now_time;
		timeout = 0;
	}

	while(!force_exit) {
		int n = poll(poll_array, sizeof(poll_array) / sizeof(poll_array[0]), timeout);
		if(n < 0) {
			if(errno == EINTR) continue;
			else break;
		}

		if(show_progress) {
			gettimeofday(&now_time, nullptr);
			int64_t now = (((int64_t) now_time.tv_sec) * 1000) + (((int64_t) now_time.tv_usec) / 1000);
			int64_t prev = (((int64_t) prev_time.tv_sec) * 1000) + (((int64_t) prev_time.tv_usec) / 1000);
			int64_t diff = now - prev;
			if(n == 0 || diff >= 1000) {
				prev_time = now_time;
				print_progress_line();
				diff -= 1000;
			}

			if(diff >= 1000) timeout = 0;
			else timeout = 1000 - diff;
		}

		if(poll_array[POLLFD_INPUT].revents) {
			read_input();
		}
		if(poll_array[POLLFD_OUTPUT].revents) {
			write_output();
		}

		if(no_more_input && buffers.empty()) { fprintf(stderr, "no_more_input && buffers.empty()\n"); break; }
	}
	if(show_progress) print_progress_line();
	return 0;
}
