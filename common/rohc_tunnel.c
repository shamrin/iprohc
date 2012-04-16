/* rohc_tunnel.c -- Functions handling a tunnel, client or server-side

On a client connection :
 *

*/

#include <sys/socket.h> 
#include <sys/types.h> 
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

#include "rohc_tunnel.h"

/* ROHC includes */
#include <rohc.h>
#include <rohc_comp.h>
#include <rohc_decomp.h>

/* Initialize logger */
#include <syslog.h>
#define MAX_LOG LOG_INFO
#define trace(a, ...) if ((a) & MAX_LOG) syslog(LOG_MAKEPRI(LOG_DAEMON, a), __VA_ARGS__)



/*
 * Macros & definitions:
 */

/// Return the greater value from the two
#define max(x, y)  (((x) > (y)) ? (x) : (y))

/// The maximal size of data that can be received on the virtual interface
#define TUNTAP_BUFSIZE 1518

/// The maximal size of a ROHC packet
#define MAX_ROHC_SIZE   (5 * 1024)

int seq ;

void* new_tunnel(void* arg) {

    struct tunnel* tunnel = (struct tunnel*) arg ;
    int alive ;

    int failure = 0;
    int is_umode = 1 ; /* TODO : Handle other mode */

    fd_set readfds;
    struct timespec timeout;
    sigset_t sigmask;

    struct timeval last;
    struct timeval now;

    int tun;
    int ret;

    struct rohc_comp *comp;
    struct rohc_decomp *decomp;

    /* TODO : Check assumed present attributes
       (thread, local_address, dest_address, tun, fake_tun) */

    /*  Create raw socket */
    tunnel->raw_socket = create_socket() ;
    if (tunnel->raw_socket < 0) {
        perror("Unable to open raw socket") ;
        /* TODO : Handle error */
    }

    /* ROHC */
    /* create the compressor and activate profiles */
    comp = rohc_alloc_compressor(15, 0, 0, 0);
    if(comp == NULL)
    {
        fprintf(stderr, "cannot create the ROHC compressor\n");
        goto close_raw;
    }
    rohc_activate_profile(comp, ROHC_PROFILE_UNCOMPRESSED);
    rohc_activate_profile(comp, ROHC_PROFILE_UDP);
    rohc_activate_profile(comp, ROHC_PROFILE_IP);
    rohc_activate_profile(comp, ROHC_PROFILE_UDPLITE);
    rohc_activate_profile(comp, ROHC_PROFILE_RTP);

    /* create the decompressor (associate it with the compressor) */
    decomp = rohc_alloc_decompressor(is_umode ? NULL : comp);
    if(decomp == NULL)
    {
        fprintf(stderr, "cannot create the ROHC decompressor\n");
        goto destroy_comp;
    }

    /* init the tunnel sequence number */
    seq = 0;

    /* poll network interfaces each second */
    timeout.tv_sec = 1;
    timeout.tv_nsec = 0;

    /* mask signals during interface polling */
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGKILL);
    sigaddset(&sigmask, SIGTERM);
    sigaddset(&sigmask, SIGINT);

    /* initialize the last time we sent a packet */
    gettimeofday(&last, NULL);
	alive = 1 ;
    do
    {
        printf("pipo\n") ;
        /* poll the read sockets/file descriptors */
        FD_ZERO(&readfds);
        FD_SET(tunnel->tun, &readfds);
        FD_SET(tunnel->raw_socket, &readfds);

        ret = pselect(max(tunnel->tun, tunnel->raw_socket) + 1, &readfds, NULL, NULL, &timeout, &sigmask);
        if(ret < 0)
        {
            fprintf(stderr, "pselect failed: %s (%d)\n", strerror(errno), errno);
            failure = 1;
            alive = 0;
        }
        else if(ret > 0)
        {
            /* bridge from TUN to RAW */
            if(FD_ISSET(tunnel->tun, &readfds))
            {
                /* We read the fake TUN device if we are on a server */
                if (tunnel->fake_tun == NULL) {
                    /* No fake_tun, we use the real TUN */
                    tun = tunnel->tun ;
                } else {
                    tun = tunnel->fake_tun[0] ; 
                }
                failure = tun2raw(comp, tun, tunnel->raw_socket, tunnel->dest_address);
                gettimeofday(&last, NULL);
                if(failure) {
#if STOP_ON_FAILURE
                    alive = 0;
#endif          
                }
            }
  
            /* bridge from RAW to TUN */
            if(
#if STOP_ON_FAILURE
               !failure &&
#endif
               FD_ISSET(tunnel->raw_socket, &readfds))
            {
                failure = raw2tun(decomp, tunnel->raw_socket, tunnel->tun);
#if STOP_ON_FAILURE
                if(failure)
                    alive = 0;
#endif
            }
        }

        /* flush feedback data if nothing is sent in the tunnel for a moment */
        gettimeofday(&now, NULL);
        if(now.tv_sec > last.tv_sec + 1)
        {
/*            failure = flush_feedback(comp, tunnel->raw_socket, ;
            last = now;
#if STOP_ON_FAILURE
            if(failure)
                alive = 0;
#endif */
        }
    }
    while(alive);

    /*
     * Cleaning:
     */

    rohc_free_decompressor(decomp);
destroy_comp:
    rohc_free_compressor(comp);

close_raw:
    close(tunnel->raw_socket) ;
    return NULL ;
}


int create_socket() {
    int sock ;
    int ret  ;

    /* create socket */
    sock = socket(AF_INET, SOCK_RAW, IPPROTO_IPIP) ;
    if (sock < 0) {
        perror("Can't open RAW socket\n") ;
        goto quit ;
    }

    /* enable IP header creation and try to reuse socket */
    char on = 1; 
    ret  = setsockopt(sock,IPPROTO_IP,IP_HDRINCL,&on,sizeof(on)); 
    if (ret < 0) {
        perror("Can't set setsocket option hdr incl\n") ;
    }

	return sock ;

quit:
    return -1 ;
}

/**
 * @brief Forward IP packets received on the TUN interface to the RAW socket
 *
 * The function compresses the IP packets thanks to the ROHC library before
 * sending them on the RAW socket.
 *
 * @param comp   The ROHC compressor
 * @param from   The TUN file descriptor to read from
 * @param to     The RAW socket descriptor to write to
 * @param raddr  The remote address of the tunnel
 * @return       0 in case of success, a non-null value otherwise
 */
int tun2raw(struct rohc_comp *comp,
            int from, int to,
            struct in_addr raddr)
{
	static unsigned char buffer[TUNTAP_BUFSIZE];
	static unsigned char rohc_packet[2 + MAX_ROHC_SIZE];
	unsigned int buffer_len = TUNTAP_BUFSIZE;
	unsigned char *packet;
	unsigned int packet_len;
	int rohc_size;
	int ret;

	/* read the IP packet from the virtual interface */
	ret = read_from_tun(from, buffer, &buffer_len);
	if(ret != 0)
	{
		fprintf(stderr, "read_from_tun failed\n");
		goto error;
	}

	if(buffer_len == 0)
		goto quit;

	packet = &buffer[4];
	packet_len = buffer_len - 4;

	/* increment the tunnel sequence number */
	seq++;

	/* compress the IP packet */
#if DEBUG
	fprintf(stderr, "compress packet #%u (%u bytes)\n", seq, packet_len);
#endif
	rohc_size = rohc_compress(comp, packet, packet_len,
	                          rohc_packet + 2, MAX_ROHC_SIZE);
	if(rohc_size <= 0)
	{
		fprintf(stderr, "compression of packet #%u failed\n", seq);
		goto error;
	}

	/* write the ROHC packet in the UDP tunnel if not dropped */
    ret = write_to_raw(to, raddr, rohc_packet, 2 + rohc_size);
    if(ret != 0)
    {
        fprintf(stderr, "write_to_raw failed\n");
        goto error;
    }

quit:
	return 0;
error:
	return 1;
}


/**
 * @brief Forward ROHC packets received on the UDP socket to the TUN interface
 *
 * The function decompresses the ROHC packets thanks to the ROHC library before
 * sending them on the TUN interface.
 *
 * @param decomp  The ROHC decompressor
 * @param from    The RAW socket descriptor to read from
 * @param to      The TUN file descriptor to write to
 * @return        0 in case of success, a non-null value otherwise
 */
int raw2tun(struct rohc_decomp *decomp, int from, int to)
{
	static unsigned char packet[2 + MAX_ROHC_SIZE];
	static unsigned char decomp_packet[MAX_ROHC_SIZE + 4];
	unsigned int packet_len = TUNTAP_BUFSIZE;
	int decomp_size;
	int ret;
	static unsigned int max_seq = 0;
	unsigned int new_seq;
	static unsigned long lost_packets = 0;

#if DEBUG
	fprintf(stderr, "\n");
#endif

	/* read the sequence number + ROHC packet from the UDP tunnel */
	ret = read_from_raw(from, packet, &packet_len);
	if(ret != 0)
	{
		fprintf(stderr, "read_from_raw failed\n");
		goto error;
	}

	if(packet_len <= 2)
		goto quit;

	/* find out if some ROHC packets were lost between compressor and
	 * decompressor (use the tunnel sequence number) */
	new_seq = ntohs((packet[0] << 8) + packet[1]);

	if(new_seq < max_seq)
	{
		/* some packets were reordered, the packet was wrongly
		 * considered as lost */
		fprintf(stderr, "ROHC packet with seq = %u received after seq = %u\n",
		        new_seq, max_seq);
		lost_packets--;
	}
	else if(new_seq > max_seq + 1)
	{
		/* there is a gap between sequence numbers, some packets were lost */
		fprintf(stderr, "ROHC packet(s) probably lost between "
		        "seq = %u and seq = %u\n", max_seq, new_seq);
		lost_packets += new_seq - (max_seq + 1);
	}
	else if(new_seq == max_seq)
	{
		/* should not happen */
		fprintf(stderr, "ROHC packet #%u duplicated\n", new_seq);
	}
	
	if(new_seq > max_seq)
	{
		/* update max sequence numbers */
		max_seq = new_seq;
	}

	/* decompress the ROHC packet */
#if DEBUG
	fprintf(stderr, "decompress ROHC packet #%u (%u bytes)\n",
	        new_seq, packet_len - 2);
#endif
	decomp_size = rohc_decompress(decomp, packet + 2, packet_len - 2,
	                              &decomp_packet[4], MAX_ROHC_SIZE);
	if(decomp_size <= 0)
	{
		if(decomp_size == ROHC_FEEDBACK_ONLY)
		{
			/* no stats for feedback-only packets */
			goto quit;
		}
		else
		{
			fprintf(stderr, "decompression of packet #%u failed\n", new_seq);
			goto drop;
		}
	}

	/* build the TUN header */
	decomp_packet[0] = 0;
	decomp_packet[1] = 0;
	switch((decomp_packet[4] >> 4) & 0x0f)
	{
		case 4: /* IPv4 */
			decomp_packet[2] = 0x08;
			decomp_packet[3] = 0x00;
			break;
		case 6: /* IPv6 */
			decomp_packet[2] = 0x86;
			decomp_packet[3] = 0xdd;
			break;
		default:
			fprintf(stderr, "bad IP version (%d)\n",
			        (decomp_packet[4] >> 4) & 0x0f);
			goto drop;
	}
	
	/* write the IP packet on the virtual interface */
	ret = write_to_tun(to, decomp_packet, decomp_size + 4);
	if(ret != 0)
	{
		fprintf(stderr, "write_to_tun failed\n");
		goto drop;
	}

//	/* print packet statistics */
//	ret = print_decomp_stats(decomp, new_seq, lost_packets);
//	if(ret != 0)
//	{
//		fprintf(stderr, "cannot display stats (print_decomp_stats failed)\n");
//		goto drop;
//	}

quit:
	return 0;

drop:
//	/* print packet statistics */
//	ret = print_decomp_stats(decomp, new_seq, lost_packets);
//	if(ret != 0)
//		fprintf(stderr, "cannot display stats (print_decomp_stats failed)\n");
//
error:
	return 1;
}


int read_from_tun(int fd, unsigned char *packet, unsigned int *length)
{
    int ret;

    ret = read(fd, packet, *length);

    if(ret < 0 || ret > *length)
    {
        fprintf(stderr, "read failed: %s (%d)\n", strerror(errno), errno);
        goto error;
    }

    *length = ret;

#if DEBUG
    fprintf(stderr, "read %u bytes on fd %d\n", ret, fd);
#endif

    return 0;

error:
    *length = 0;
    return 1;
}


int write_to_tun(int fd, unsigned char *packet, unsigned int length)
{
    int ret;

    ret = write(fd, packet, length);
    if(ret < 0)
    {
        fprintf(stderr, "write failed: %s (%d)\n", strerror(errno), errno);
        goto error;
    }

#if DEBUG
    fprintf(stderr, "%u bytes written on fd %d\n", length, fd);
#endif

    return 0;

error:
    return 1;
}


int read_from_raw(int sock, unsigned char *buffer, unsigned int *length)
{
    struct sockaddr_in addr;
    socklen_t addr_len;
    int ret;

    addr_len = sizeof(struct sockaddr_in);
    bzero(&addr, addr_len);

    /* read data from the RAW socket */
    ret = recvfrom(sock, buffer, *length, 0, (struct sockaddr *) &addr,
                   &addr_len);

    if(ret < 0 || ret > *length)
    {
        fprintf(stderr, "recvfrom failed: %s (%d)\n", strerror(errno), errno);
        goto error;
    }

    if(ret == 0)
        goto quit;

    *length = ret;

#if DEBUG
    fprintf(stderr, "read one %u-byte ROHC packet on UDP sock %d\n",
            *length - 2, sock);
#endif

quit:
    return 0;

error:
    *length = 0;
    return 1;
}

int write_to_raw(int sock, struct in_addr raddr, unsigned char *packet, unsigned int length)
{
    struct sockaddr_in addr;
    int ret;

    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = raddr.s_addr;

    /* write the tunnel sequence number at the beginning of packet */
    packet[0] = (htons(seq) >> 8) & 0xff;
    packet[1] = htons(seq) & 0xff;

    /* send the data on the RAW socket */
    ret = sendto(sock, packet, length, 0, (struct sockaddr *) &addr,
                 sizeof(struct sockaddr_in));
    if(ret < 0)
    {
        fprintf(stderr, "sendto failed: %s (%d)\n", strerror(errno), errno);
        goto error;
    }

#if DEBUG
    fprintf(stderr, "%u bytes written on socket %d\n", length, sock);
#endif

    return 0;

error:
    return 1;
}
