#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_SAMPLES  1000
#define ICMP_PAYLOAD 56   /* bytes of payload after icmphdr, mimics ping */

static uint16_t inet_cksum(void *data, size_t len) {
    uint16_t *p = (uint16_t *)data;
    unsigned long sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len)         sum += *(uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static double timespec_ms(const struct timespec *a, const struct timespec *b) {
    return (b->tv_sec  - a->tv_sec)  * 1000.0 +
           (b->tv_nsec - a->tv_nsec) / 1e6;
}

static int cmp_dbl(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <host> [count] [interval_ms]\n", prog);
    fprintf(stderr, "  Requires CAP_NET_RAW or root for raw ICMP sockets.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(argv[0]); return EXIT_FAILURE; }

    const char *host     = argv[1];
    int         count    = (argc > 2) ? atoi(argv[2]) : 10;
    int         intv_ms  = (argc > 3) ? atoi(argv[3]) : 1000;
    if (count < 1 || count > MAX_SAMPLES) count = 10;
    if (intv_ms < 10) intv_ms = 10;

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_ICMP;
    int gai = getaddrinfo(host, NULL, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "Cannot resolve '%s': %s\n", host, gai_strerror(gai));
        return EXIT_FAILURE;
    }
    struct sockaddr_in dest;
    memcpy(&dest, res->ai_addr, sizeof(dest));
    freeaddrinfo(res);

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        perror("socket (need root or CAP_NET_RAW)");
        return EXIT_FAILURE;
    }

    struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint16_t ident = (uint16_t)getpid();
    double   rtts[MAX_SAMPLES];
    int      received = 0;

    char host_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dest.sin_addr, host_ip, sizeof(host_ip));
    printf("Pinging %s (%s): %d packets, interval %d ms\n",
           host, host_ip, count, intv_ms);

    for (int seq = 0; seq < count; seq++) {
        /* Build ICMP echo request */
        uint8_t pkt[sizeof(struct icmphdr) + ICMP_PAYLOAD];
        memset(pkt, 0, sizeof(pkt));
        struct icmphdr *icmp = (struct icmphdr *)pkt;
        icmp->type             = ICMP_ECHO;
        icmp->code             = 0;
        icmp->un.echo.id       = htons(ident);
        icmp->un.echo.sequence = htons((uint16_t)seq);
        memset(pkt + sizeof(struct icmphdr), 0x42, ICMP_PAYLOAD);
        icmp->checksum = inet_cksum(pkt, sizeof(pkt));

        struct timespec t_send, t_recv;
        clock_gettime(CLOCK_MONOTONIC, &t_send);

        if (sendto(sock, pkt, sizeof(pkt), 0,
                   (struct sockaddr *)&dest, sizeof(dest)) < 0) {
            perror("sendto");
            continue;
        }

        /* Wait for matching echo reply */
        int got = 0;
        while (!got) {
            uint8_t rbuf[256];
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            ssize_t n = recvfrom(sock, rbuf, sizeof(rbuf), 0,
                                 (struct sockaddr *)&from, &fromlen);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    printf("seq=%-4d timeout\n", seq);
                else
                    perror("recvfrom");
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &t_recv);

            /* Skip IP header */
            struct iphdr *ip = (struct iphdr *)rbuf;
            int ip_len = ip->ihl * 4;
            if (n < (ssize_t)(ip_len + sizeof(struct icmphdr))) continue;

            struct icmphdr *reply = (struct icmphdr *)(rbuf + ip_len);
            if (reply->type != ICMP_ECHOREPLY)            continue;
            if (ntohs(reply->un.echo.id) != ident)        continue;
            if (ntohs(reply->un.echo.sequence) != (uint16_t)seq) continue;

            double rtt = timespec_ms(&t_send, &t_recv);
            rtts[received++] = rtt;
            printf("seq=%-4d  rtt=%.3f ms  from=%s\n",
                   seq, rtt, host_ip);
            got = 1;
        }

        if (seq < count - 1)
            usleep((useconds_t)(intv_ms * 1000));
    }

    close(sock);

    if (received == 0) {
        printf("No replies received.\n");
        return EXIT_FAILURE;
    }

    qsort(rtts, received, sizeof(double), cmp_dbl);

    double sum = 0.0;
    for (int i = 0; i < received; i++) sum += rtts[i];
    double avg  = sum / received;
    double mn   = rtts[0];
    double mx   = rtts[received - 1];
    int    p99i = (received - 1) * 99 / 100;
    double p99  = rtts[p99i];

    printf("\n--- %s statistics ---\n", host);
    printf("%d packets sent, %d received, %d%% loss\n",
           count, received, (count - received) * 100 / count);
    printf("rtt min/avg/max/p99 = %.3f/%.3f/%.3f/%.3f ms\n",
           mn, avg, mx, p99);

    return EXIT_SUCCESS;
}
