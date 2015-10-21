/*  vi:ts=4:sw=4:noet
The MIT License (MIT)

Copyright (c) 2015 Olivier Tilmans, olivier.tilmans@uclouvain.be

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdlib.h> /* malloc, free, EXIT_X, ...*/
#include <stdio.h> /* printf, fprintf, recvfrom */
#include <unistd.h> /* getopt */
#include <netinet/in.h> /* sockaddr_in6 */
#include <sys/types.h> /* in6_addr */
#include <sys/socket.h> /* socket, bind, connect */
#include <sys/select.h> /* fd_set, select */
#include <time.h> /* time */
#include <string.h> /* memcpy, memcmp */
#include <errno.h> /* errno, EAGAIN, ... */
#include <fcntl.h> /* fcntl */
#include <arpa/inet.h> /* inet_ntop */
#include <limits.h> /* INT_MAX, SHRT_MAX */

#include "min_queue.h" /* minq_x */

/* Max packet length in the protocol */
#define MAX_PKT_LEN 520
/* Random number between 0 and 100 */
#define RAND_PERCENT ((unsigned int)(rand() % 101))

int forward_port = 12345;
int port = 2141;
unsigned int delay = 0;
unsigned int jitter = 0;
unsigned int err_rate = 0;
unsigned int cut_rate = 0;
unsigned int loss_rate = 0;
int sfd = -1; /* socket file des. */
minqueue_t *pkt_queue = NULL; /* Queue for delayed packet */
struct timeval last_clock; /* Cache current timestamp */
struct sockaddr_in6 dest_addr, src_addr; /* The addresses of the 2 parties */
int has_source_addr = 0; /* Have we seen the other party yet */

struct pkt_slot { /* One entry in the packet queue */
	struct timeval ts; /* Expiration date */
	int size; /* How many bytes are used in buf */
	char buf[MAX_PKT_LEN]; /* The packet data */
};

/* Get the human-readable representation of an IPv6 */
static inline const char *sockaddr6_to_human(const struct in6_addr *a)
{
	static char b[INET6_ADDRSTRLEN];
	/* Can safely ignore return value as we control all parameters */
	inet_ntop(AF_INET6, a, b, sizeof(*a));
	return b;
}

/* @return: left > right */
static inline int timeval_cmp(const struct timeval *left,
							const struct timeval *right)
{
	return left->tv_sec == right->tv_sec ?
		left->tv_usec > right->tv_usec :
		left->tv_sec > right->tv_sec;
}

/* @return: c = a - b */
static void timeval_diff(const struct timeval *a,
					const struct timeval *b,
					struct timeval *c)
{
	c->tv_sec = a->tv_sec - b->tv_sec;
	c->tv_usec = a->tv_usec - b->tv_usec;
	/* Underflow in usec, compensate through secs */
	if (c->tv_usec < 0) {
		if (--c->tv_sec)
			c->tv_usec += 1000000;
	}
}

/* Log an action on a processed packet */
#define LOG_PKT_FMT(buf, fmt, ...) \
	printf("[SEQ %3u] " fmt, (unsigned char)buf[1], ##__VA_ARGS__)
#define LOG_PKT(buf, msg) LOG_PKT_FMT(buf, msg "\n")

/* Send a packet to the host we're proxying */
static int write_out(const char *buf, int len)
{
	LOG_PKT(buf, "Sent packet");
	return sendto(sfd, buf, len, 0,
				(struct sockaddr*)&dest_addr, sizeof(dest_addr)) == len ?
		EXIT_SUCCESS : EXIT_FAILURE;
}

/* Deliver all queued packets whose timestamps have expired */
static int deliver_delayed_pkt()
{
	struct pkt_slot *p = (struct pkt_slot*)minq_peek(pkt_queue);
	/* We have a packet and its timestamp is < current time */
	while (p && timeval_cmp(&last_clock, &p->ts)) {
		/* Send it */
		if (write_out(p->buf, p->size)) {
			/* We can try again later for these errors
			 * (send bunf is full, or ...) */
			if (errno == EWOULDBLOCK || errno == EINTR || errno == EAGAIN)
				return EXIT_SUCCESS;
			/* Otherwise propagate error */
			perror("Failed to write all delayed bytes");
			return EXIT_FAILURE;
		}
		minq_pop(pkt_queue);
		free(p);
		p = (struct pkt_slot*)minq_peek(pkt_queue);
	}
	return EXIT_SUCCESS;
}

/* @return: 1 iff a != b, else 0 */
static inline int sockaddr_cmp(const struct sockaddr_in6 *a,
						const struct sockaddr_in6 *b)
{
	return memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) ||
		a->sin6_port != b->sin6_port;
}

/* Simulate the effect of a lossy link on a received packet */
static inline int simulate_link(char *buf, int len)
{
	/* Do we drop it? */
	if (loss_rate && RAND_PERCENT < loss_rate) {
		LOG_PKT(buf, "Dropping packet");
		return EXIT_SUCCESS;
	}
	/* Do we cut it after the header? */
	if (cut_rate && RAND_PERCENT < cut_rate) {
		LOG_PKT(buf, "Cutting packet");
		len = 4;
	/* or do we corrupt it? */
	} else if (err_rate && RAND_PERCENT < err_rate) {
		LOG_PKT(buf, "Corrupting packet");
		buf[len - 1] = ~buf[len - 1]; /* invert last byte of the CRC */
	}
	/* Do we want to simulate delay? */
	if (delay) {
		/* Random delay to add is capped to 10s */
		unsigned int applied_delay = RAND_PERCENT > 49 ?
			delay + rand() % jitter :
			delay - rand() % jitter;
		applied_delay %= 10000;
		LOG_PKT_FMT(buf, "Delayed packet by %u ms\n", applied_delay);
		/* Create a slot for the packet queue */
		struct pkt_slot *slot;
		if (!(slot = malloc(sizeof(*slot)))) {
			fprintf(stderr,
					"Failed to allocate memory for a delayed packet!\n");
			return EXIT_FAILURE;
		}
		/* Copy the packet in the slot */
		memcpy(slot->buf, buf, len);
		slot->size = len;
		/* Register expiration date: current date + delay */
		slot->ts.tv_sec = last_clock.tv_sec + applied_delay / 1000;
		/* delay is in ms not us! */
		slot->ts.tv_usec = last_clock.tv_usec + (applied_delay % 1000) * 1000;
		/* Enqueue the new slot */
		if (minq_push(pkt_queue, slot)) {
			perror("Failed to enqueue a packet!");
			return EXIT_FAILURE;
		}
	} else {
		/* Forward it to the host we're proxying */
		if (write_out(buf, len)) {
			perror("Failed to write all bytes");
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

/* sfd has been marked for reading, handle the read and process the packet */
static int process_incoming_pkt()
{
	struct sockaddr_in6 from; /* Whois the one sending us data? */
	socklen_t len_from = sizeof(from);
	char buf[MAX_PKT_LEN]; /* Max allowed packet size for the protocol */
	int len; /* Actual received packet size */
	if ((len = recvfrom(sfd, buf, MAX_PKT_LEN, 0,
					(struct sockaddr *)&from, &len_from)) <= 0) {
		/* Ignore if we have been interrupted by a signal,
		 * or if select marked sfd as ready for reading
		 * without any no data available. */
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
			return EXIT_SUCCESS;
		/* Real error, abort mission */
		perror("recv failed");
		return EXIT_FAILURE;
	}
	/* Check packet consistency */
	if (len < 4) {
		printf("Received malformed data, shutting down!\n");
		return EXIT_FAILURE;
	}
	/* We need to track who is sending us data, so that we can send him the
	 * reverse traffic coming from the host we're proxying
	 */
	if (!has_source_addr) {
		memcpy(&src_addr, &from, sizeof(src_addr));
		fprintf(stderr, "@@ Remote host is %s [%d]\n",
				sockaddr6_to_human(&from.sin6_addr), ntohs(from.sin6_port));
		has_source_addr = 1; /* We're logically connected to that guy */
	}
	/* Simply relay packets from the host we're proxying */
	if (!sockaddr_cmp(&from, &dest_addr)) {
		/* Forward reverse-traffic */
		if (sendto(sfd, buf, len, 0, (struct sockaddr*)&src_addr,
					sizeof(src_addr)) != len) {
			perror("Failed to relay a message back to the source");
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	} else if (sockaddr_cmp(&from, &src_addr)) {
		/* We do not know the guy that sent us this data, ignore him */
		fprintf(stderr, "@@ Received %d bytes from %s [%d], "
			"which is an alien to the connection. Dropping it!\n",
			len, sockaddr6_to_human(&from.sin6_addr), ntohs(from.sin6_port));
		return EXIT_SUCCESS;
	}
	/* We have valid data, simulate the behavior of a lossy link
	 * before delivery
	 */
	return simulate_link(buf, len);
}

/* Update last_lock to the current time */
static int update_time()
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
		perror("Cannot internal clock");
		return EXIT_FAILURE;
	}
	last_clock.tv_sec = ts.tv_sec;
	last_clock.tv_usec = ts.tv_nsec/1000;
	return EXIT_SUCCESS;
}

/* If a packet is queue, return how long until it should be delivered,
 * otherwise return NULL
 */
static struct timeval* get_queue_timeout()
{
	static struct timeval timeout;
	/* No queued packet */
	if (minq_empty(pkt_queue))
		return NULL;
	/* Get closest expiration date for the queued packet */
	struct timeval *ts = &((struct pkt_slot*)minq_peek(pkt_queue))->ts;
	/* timeout = expiration_date - current date */
	timeval_diff(ts, &last_clock, &timeout);
	/* If we queued the packet for too long, set a 1ms timeout. We cannot set
	 * 0 as packet queued for too long can be due to the send buffer
	 * being full, thus packet not being dequeued.
	 */
	if (timeout.tv_sec < 0 || (!timeout.tv_sec && timeout.tv_usec <= 0)) {
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000; /* 1ms */
	}
	return &timeout;
}

/* Loop forever, waiting on packet to process */
static int proxy_loop()
{
	fd_set rfds;
	FD_ZERO(&rfds);
	if (update_time()) return EXIT_FAILURE;
	while (1) {
		/* Reset sfd in fdset, as timeout expiration would have removed it. */
		FD_SET(sfd, &rfds);
		/* Wait for incoming data, or end of a delay on a previously received
		 * packet */
		if (select(sfd+1, &rfds, NULL, NULL, get_queue_timeout()) < 0) {
			/* Ignore if interruption is due to a signal */
			if (errno == EINTR) continue;
			else {
				/* Bad things do happen ... */
				perror("Select failed");
				break;
			}
		}
		if (update_time() || /* Update time cache */
			deliver_delayed_pkt() || /* Deliver delayed packets */
			/* Process incoming packets, applying drop rates etc */
			(FD_ISSET(sfd, &rfds) && process_incoming_pkt()))
			break;
	}
	/* Reached only on error */
	return EXIT_FAILURE;
}

/* Get a socket,
 * bind to all interfaces on specified port,
 * connect to localhost on forward_port,
 * set as non-blocking,
 * @return: -1 on error or a valid file descriptor.
 */
static int get_socket()
{

	const char *err_str;
	/* Socket creation (IPv6, UDP) */
	if ((sfd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		err_str = "Cannot create socket";
		goto fail;
	}
	/* Force the socket to use IPv6,
	 * enable address sharing: multiple processes can consume data for this
	 * IP/port port combination*/
	int enable = 1;
	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable))) {
		err_str = "Couldn't enable the re-use of the address ...";
		goto fail_socket;
	}
	if (setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &enable, sizeof(enable))) {
		err_str = "Cannot force the socket to IPv6";
		goto fail_socket;
	}
	/* Bind the socket to listen on all interfaces (::), on port */
	struct sockaddr_in6 addr;
	/* Implicitly set address to ::,
	 * as well as flowinfo-scope  as they are unwanted here */
	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(port);
	if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		err_str = "Cannot bind socket";
		goto fail_socket;
	}
	/* Initialize the dest_addr struct (loopback, forward_port).
	 * Cannot connect as we will receive/send data from/to multiple hosts */
	memset(&dest_addr, 0, sizeof(dest_addr));
	dest_addr.sin6_family = AF_INET6;
	dest_addr.sin6_port = htons(forward_port);
	memcpy(&dest_addr.sin6_addr, &in6addr_loopback,
			sizeof(dest_addr.sin6_addr));
	/* Set the socket to non-blocking,
	 * as select() indicates that a socket is ready to be read, but not that it
	 * will not block. */
	if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		err_str = "Cannot set the socket to non-blocking mode";
		goto fail_socket;
	}
	return sfd;

fail_socket:
	close(sfd);
fail:
	perror(err_str);
	return -1;
}

 /* ? a > b */
static int pkt_slot_cmp(const void *a, const void *b)
{
	struct timeval *left = &((struct pkt_slot*)a)->ts;
	struct timeval *right = &((struct pkt_slot*)b)->ts;
	/* We compare the slots based on their (future) expiration date */
	return timeval_cmp(left, right);
}

static int proxy_traffic()
{
#define _DIE(label, msg, ...) do { \
	fprintf(stderr, msg, ##__VA_ARGS__); \
	rval = EXIT_FAILURE; \
	goto label; \
} while (0)

	int rval = EXIT_SUCCESS;

	if (get_socket() < 0)
		_DIE(exit, "Socket initialization failure!\n");

	if (!(pkt_queue = minq_new(pkt_slot_cmp)))
		_DIE(sfd, "Cannot create priority queue!\n");

	/* Process incoming traffic until error (or forever) */
	if ((rval = proxy_loop()))
		_DIE(queue, "The proxy loop crashed, "
			"had %zu element(s) left in pkt_queue\n", minq_size(pkt_queue));

queue:
	minq_del(pkt_queue);
sfd:
	close(sfd);
exit:
	return rval;

#undef _DIE
}

static void usage(const char *prog_name)
{
	fprintf(stderr,
"Link sim: A simple lossy link simulator.\n"
"This program will relay all incoming UDP traffic on port `port` to\n"
"the loopback address [::1], on port `forward_port`, simulating \n"
"random losses, transmission errors, ...\n"
"\n"
"Usage: %s [-p port] [-P forward_port] [-d delay] [-j jitter]\n"
"       %*s [-e err_rate] [-c cut_rate] [-l loss_rate] [-s seed] [-h]\n"
"-p port          The UDP port on which the link simulator operates.\n"
"                 Defaults to: 2141\n"
"-P forward_port  The UDP port on localhost on which the incoming traffic\n"
"                 should be forwarded.\n"
"                 Defaults to: 12345\n"
"-d delay         The delay (in ms) that should be applied to the traffic.\n"
"                 Defaults to: 0\n"
"-j jitter        The jitter (in ms) that should be applied to the traffic.\n"
"                 The total delay applied to one packet will be:\n"
"                 delay + rand[-jitter, jitter].\n"
"                 Defaults to: 0\n"
"                 Unused if delay == 0.\n"
"-e err_rate      The rate of packet corruption occurrence (in packet/100).\n"
"                 Defaults to: 0\n"
"                 A packet that has been corrupted will NOT be cut.\n"
"-c cut_rate      The rate of packet being cut after the header to simulate\n"
"                 congestion (in packet/100).\n"
"                 Defaults to: 0\n"
"                 A packet that has been cut will NOT be corrupted.\n"
"-l loss_rate     The rate of packets loss (in packet/100).\n"
"                 Defaults to 0\n"
"-s seed          The seed for the random generator, to replay a previous\n"
"                 session.\n"
"                 Defaults to: time() casted to int\n"
"-h               Prints this message and exit.\n",
			prog_name,
			(int)strlen(prog_name),
			"");
}

int main(int argc, char **argv)
{
	int opt;
	long seed = -1L;
	/* parse option values */
	while ((opt = getopt(argc, argv, "p:P:d:j:e:c:s:l:h")) != -1) {
		switch (opt) {
#define _READINT_CAP(x, y, lim) case x: y = atoi(optarg) % lim; break;
#define _READINT(x, y) _READINT_CAP(x, y, INT_MAX)
			_READINT_CAP('p', port, (1 << 16))
			_READINT_CAP('P', forward_port, (1 << 16))
			_READINT('d', delay)
			_READINT('j', jitter)
			_READINT_CAP('e', err_rate, 101)
			_READINT_CAP('c', cut_rate, 101)
			_READINT_CAP('l', loss_rate, 101)
			_READINT('s', seed)
#undef _READINT
#undef _READINT_CAP
			case 'h':
				/* Fall-through */
			default:
				usage(argv[0]);
				return EXIT_FAILURE;
		}
	}
	/* Setup RNG */
	if (seed == -1L) {
		seed = (int)time(NULL);
		fprintf(stderr, "@@ Using random seed: %d\n", (int)seed);
	}
	srand((int)seed);
	fprintf(stderr, "@@ Using parameters:\n"
					".. port: %u\n"
					".. forward_port: %u\n"
					".. delay: %u\n"
					".. jitter: %u\n"
					".. err_rate: %u\n"
					".. cut_rate: %u\n"
					".. loss_rate: %u\n"
					".. seed: %d\n",
					port, forward_port, delay, jitter, err_rate,
					cut_rate, loss_rate, (int)seed);
	/* Start proxying UDP traffic according to the specified options */
	return proxy_traffic();
}
