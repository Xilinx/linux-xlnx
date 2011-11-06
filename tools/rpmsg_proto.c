#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "../include/net/rpmsg.h"

#define M3_CORE0	(0)

int main(void)
{
	int sock, sock2, err;
	struct sockaddr_rpmsg src_addr, dst_addr;
	socklen_t len;
	const char *msg = "Hello there!";
	char buf[512];

	/* create an RPMSG socket */
	sock = socket(AF_RPMSG, SOCK_SEQPACKET, 0);
	if (sock < 0) {
		printf("socket failed: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	/* connect to remote service */
	memset(&dst_addr, 0, sizeof(dst_addr));
	dst_addr.family = AF_RPMSG;
	dst_addr.vproc_id = M3_CORE0;
	dst_addr.addr = 51;

	printf("Connecting to address 0x%x on processor %d\n",
					dst_addr.addr, dst_addr.vproc_id);

	len = sizeof(struct sockaddr_rpmsg);
	err = connect(sock, (struct sockaddr *)&dst_addr, len);
	if (err < 0) {
		printf("connect failed: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	/* let's see what local address did we get */
	err = getsockname(sock, (struct sockaddr *)&src_addr, &len);
	if (err < 0) {
		printf("getpeername failed: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	printf("Our address: socket family: %d, proc id = %d, addr = %d\n",
			src_addr.family, src_addr.vproc_id, src_addr.addr);

	printf("Sending \"%s\"\n", msg);
	err = send(sock, msg, strlen(msg) + 1, 0);
	if (err < 0) {
		printf("sendto failed: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	memset(&src_addr, 0, sizeof(src_addr));

	len = sizeof(src_addr);

	err = recvfrom(sock, buf, sizeof(buf), 0,
					(struct sockaddr *)&src_addr, &len);
	if (err < 0) {
		printf("recvfrom failed: %s (%d)\n", strerror(errno), errno);
		return -1;
	}
	if (len != sizeof(src_addr)) {
		printf("recvfrom: got bad addr len (%d)\n", len);
		return -1;
	}

	printf("Received a msg from address 0x%x on processor %d\n",
					src_addr.addr, src_addr.vproc_id);
	printf("Message content: \"%s\".\n", buf);


	close(sock);

	/* create another RPMSG socket */
	sock2 = socket(AF_RPMSG, SOCK_SEQPACKET, 0);
	if (sock2 < 0) {
		printf("socket failed: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	/* bind a local addr */
	memset(&src_addr, 0, sizeof(src_addr));
	src_addr.family = AF_RPMSG;
	src_addr.vproc_id = M3_CORE0;
	src_addr.addr = 99;


	printf("Exposing address %d to processor %d\n",
					src_addr.addr, src_addr.vproc_id);

	len = sizeof(struct sockaddr_rpmsg);
	err = bind(sock2, (struct sockaddr *)&src_addr, len);
	if (err < 0) {
		printf("bind failed: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	/* let's see what local address did we bind */
	err = getsockname(sock2, (struct sockaddr *)&src_addr, &len);
	if (err < 0) {
		printf("getpeername failed: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	printf("Our address: socket family: %d, proc id = %d, addr = %d\n",
			src_addr.family, src_addr.vproc_id, src_addr.addr);

	return 0;
}
