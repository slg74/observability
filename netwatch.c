#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_IFACES  32
#define BUF_SIZE    4096
#define IFACE_LEN   16
#define HDR_EVERY   20   /* reprint header every N samples */

typedef struct {
    char             name[IFACE_LEN];
    unsigned long long rx_bytes, tx_bytes;
    unsigned long long rx_pkts,  tx_pkts;
    unsigned long long rx_errs,  tx_errs;
} Iface;

static Iface prev[MAX_IFACES];
static int   prev_n = 0;

static int read_net_dev(Iface *ifaces, int max) {
    char buf[BUF_SIZE];
    int fd = open("/proc/net/dev", O_RDONLY);
    if (fd < 0) { perror("/proc/net/dev"); return -1; }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';

    int count = 0;
    char *line = strtok(buf, "\n");
    if (line) line = strtok(NULL, "\n"); /* skip header 1 */
    if (line) line = strtok(NULL, "\n"); /* skip header 2 */

    while (line && count < max) {
        char *colon = strchr(line, ':');
        if (!colon) { line = strtok(NULL, "\n"); continue; }

        char *p = line;
        while (*p == ' ') p++;
        int nl = (int)(colon - p);
        if (nl >= IFACE_LEN) nl = IFACE_LEN - 1;
        strncpy(ifaces[count].name, p, nl);
        ifaces[count].name[nl] = '\0';

        unsigned long long rb, rp, re, rd, rfi, rfr, rc, rm;
        unsigned long long tb, tp, te, td, tfi, tc, tca, tco;
        if (sscanf(colon + 1,
            "%llu %llu %llu %llu %llu %llu %llu %llu"
            " %llu %llu %llu %llu %llu %llu %llu %llu",
            &rb, &rp, &re, &rd, &rfi, &rfr, &rc, &rm,
            &tb, &tp, &te, &td, &tfi, &tc, &tca, &tco) == 16) {
            ifaces[count].rx_bytes = rb; ifaces[count].tx_bytes = tb;
            ifaces[count].rx_pkts  = rp; ifaces[count].tx_pkts  = tp;
            ifaces[count].rx_errs  = re + rd;
            ifaces[count].tx_errs  = te + td;
            count++;
        }
        line = strtok(NULL, "\n");
    }
    return count;
}

static long long prev_retrans = -1;

static long long read_retransmits(void) {
    char buf[BUF_SIZE];
    int fd = open("/proc/net/snmp", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';

    /* The file has two "Tcp:" lines: header then values */
    char *t1 = strstr(buf, "Tcp:");
    if (!t1) return -1;
    char *t2 = strstr(t1 + 1, "Tcp:");
    if (!t2) return -1;

    unsigned long long v[14];
    int got = sscanf(t2 + 4,
        "%llu %llu %llu %llu %llu %llu %llu %llu"
        " %llu %llu %llu %llu %llu %llu",
        &v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],
        &v[7],&v[8],&v[9],&v[10],&v[11],&v[12],&v[13]);
    if (got >= 12) return (long long)v[11]; /* RetransSegs */
    return -1;
}

static int read_tcp_conns(void) {
    char buf[512];
    int fd = open("/proc/net/sockstat", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    char *p = strstr(buf, "TCP: inuse ");
    if (!p) return -1;
    int inuse = 0;
    sscanf(p, "TCP: inuse %d", &inuse);
    return inuse;
}

static void print_header(void) {
    printf("\n%-12s %10s %10s %10s %10s %10s %10s %10s\n",
           "Interface", "RX MB/s", "TX MB/s",
           "RX kpps", "TX kpps",
           "RX err/s", "TX err/s", "TCP conns");
    printf("%-12s %10s %10s %10s %10s %10s %10s %10s\n",
           "------------","----------","----------",
           "----------","----------","----------","----------","----------");
}

int main(void) {
    Iface curr[MAX_IFACES];
    int sample = 0;

    prev_n = read_net_dev(prev, MAX_IFACES);
    read_retransmits(); /* discard first reading to prime delta */
    sleep(1);

    print_header();

    while (1) {
        int curr_n = read_net_dev(curr, MAX_IFACES);
        long long retrans_now = read_retransmits();
        int conns             = read_tcp_conns();

        long long retrans_delta = 0;
        if (prev_retrans >= 0 && retrans_now >= 0)
            retrans_delta = retrans_now - prev_retrans;
        prev_retrans = retrans_now;

        int printed = 0;
        for (int i = 0; i < curr_n; i++) {
            if (strcmp(curr[i].name, "lo") == 0) continue;

            Iface *p = NULL;
            for (int j = 0; j < prev_n; j++) {
                if (strcmp(curr[i].name, prev[j].name) == 0) {
                    p = &prev[j]; break;
                }
            }
            if (!p) continue;

            double rx_mb = (double)(curr[i].rx_bytes - p->rx_bytes) / 1048576.0;
            double tx_mb = (double)(curr[i].tx_bytes - p->tx_bytes) / 1048576.0;
            double rx_kp = (double)(curr[i].rx_pkts  - p->rx_pkts)  / 1000.0;
            double tx_kp = (double)(curr[i].tx_pkts  - p->tx_pkts)  / 1000.0;
            long long rxe = (long long)(curr[i].rx_errs - p->rx_errs);
            long long txe = (long long)(curr[i].tx_errs - p->tx_errs);

            char conns_str[16] = "  -";
            if (printed == 0 && conns >= 0)
                snprintf(conns_str, sizeof(conns_str), "%d", conns);

            printf("%-12s %10.3f %10.3f %10.3f %10.3f %10lld %10lld %10s\n",
                   curr[i].name, rx_mb, tx_mb, rx_kp, tx_kp,
                   rxe < 0 ? 0 : rxe, txe < 0 ? 0 : txe, conns_str);
            printed++;
        }

        if (retrans_now >= 0)
            printf("  TCP retransmits/s: %lld\n", retrans_delta);

        memcpy(prev, curr, sizeof(Iface) * curr_n);
        prev_n = curr_n;
        fflush(stdout);

        if (++sample % HDR_EVERY == 0) print_header();
        sleep(1);
    }
    return EXIT_SUCCESS;
}
