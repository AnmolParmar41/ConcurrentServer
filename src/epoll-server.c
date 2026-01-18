#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <assert.h>
#include <bits/types/struct_iovec.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/poll.h>

#include "utils.h"

#define SENDBUF_SIZE 1024
typedef enum   { INITIAL_ACK, WAIT_FOR_MSG, IN_MSG } ProcessingState;
typedef struct {
	ProcessingState state;

	// sendbuf contains data the server has to send back to the client. The
	// on_peer_ready_recv handler populates this buffer, and on_peer_ready_send
	// drains it. sendbuf_end points to the last valid byte in the buffer, and
	// sendptr at the next byte to send.
	uint8_t sendbuf[SENDBUF_SIZE];
	int sendbuf_end;
	int sendptr;

} peer_state_t;

peer_state_t global_state[2048];

bool on_peer_connected(int sockfd, const struct sockaddr_in* peer_addr, socklen_t peer_addr_len) {
	assert(sockfd < 2048);
	report_peer_connected(peer_addr, peer_addr_len);
	peer_state_t* peerstate = &global_state[sockfd];

	// Initialize state to send back a '*' to the peer immediately.
	peerstate->state = INITIAL_ACK;
	peerstate->sendbuf[0] = '*';
	peerstate->sendptr = 0;
	peerstate->sendbuf_end = 1;

	return true;
}

void on_peer_ready_recv(int sockfd){
	peer_state_t* peerstate = &global_state[sockfd];
	
	if (peerstate->state == INITIAL_ACK ||
		peerstate->sendptr < peerstate->sendbuf_end) {
		// Until the initial ACK has been sent to the peer, there's nothing we
		// want to receive. Also, wait until all data waiting for sending is sent to
		// receive more data.
		return;
	}
	uint8_t buf[1024];
	int nbytes = recv(sockfd, buf, sizeof buf, 0);
	if (nbytes == 0) {
		close(sockfd);
		// The peer disconnected.
	} 
	else if (nbytes < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
		// The socket is not *really* ready for recv; wait until it is.
			return;
		}
		else {
			perror_die("recv");
		}
	}

	for (int i = 0; i < nbytes; ++i) {
		switch (peerstate->state) {
			case INITIAL_ACK:
				assert(0 && "can't reach here");
				break;
			case WAIT_FOR_MSG:
				if (buf[i] == '^') {
					peerstate->state = IN_MSG;
				}
				break;
			case IN_MSG:
				if (buf[i] == '$') {
					peerstate->state = WAIT_FOR_MSG;
				} 
				else {
					assert(peerstate->sendbuf_end < SENDBUF_SIZE);
					peerstate->sendbuf[peerstate->sendbuf_end++] = buf[i] + 1;
				}
				break;
		}
	}
}

void on_peer_ready_send(int sockfd) {
	assert(sockfd < 2048);
	peer_state_t* peerstate = &global_state[sockfd];

	if (peerstate->sendptr >= peerstate->sendbuf_end) {
		// Nothing to send.
		return;
	}
	int sendlen = peerstate->sendbuf_end - peerstate->sendptr;
	int nsent = send(sockfd, &peerstate->sendbuf[peerstate->sendptr], sendlen, 0);
	if (nsent == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return;
		} 
		else {
			perror_die("send");
		}
	}
	
	if (nsent < sendlen) {
		peerstate->sendptr += nsent;
		return ;
	} 
	else {
		// Everything was sent successfully; reset the send queue.
		peerstate->sendptr = 0;
		peerstate->sendbuf_end = 0;

		// Special-case state transition in if we were in INITIAL_ACK until now.
		if (peerstate->state == INITIAL_ACK) {
			peerstate->state = WAIT_FOR_MSG;
		}

		return;
	}
}
int main(){ 
	setvbuf(stdout, NULL, _IONBF, 0);
	struct epoll_event ev, event_buffer[1000];
	int ep_fd = epoll_create1(0);
	
	printf("Listening to port 9090\n");
	int listner_fd = listen_inet_socket(9090); 
	make_socket_non_blocking(listner_fd);
	ev.data.fd = listner_fd; 

	if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, listner_fd, &ev) < 0) {
		perror_die("epoll_ctl");
	}

	while (true) {
		int ready_events = epoll_wait(ep_fd, event_buffer, 1000, -1);
		printf("Ready Events in the buffer: %d\n", ready_events);

		if (ready_events < 0) {
			perror_die("ready_events < 0");
		}

		for(int i = 0; i < ready_events; i++) {
			if (event_buffer[i].events & EPOLLIN) {
				if (event_buffer[i].data.fd == listner_fd) {
					struct sockaddr_in peer_addr;
					socklen_t peer_addr_len = sizeof(peer_addr);

					int peer_fd = accept(listner_fd, (struct sockaddr*) &peer_addr, &peer_addr_len);
					if (peer_fd < 0) {
						perror_die("accept");
					}
					make_socket_non_blocking(peer_fd);
					on_peer_connected(peer_fd, &peer_addr, peer_addr_len);
					ev.data.fd = peer_fd;
					ev.events = EPOLLIN | EPOLLOUT;
					if(epoll_ctl(ep_fd, EPOLL_CTL_ADD, peer_fd, &ev)) {
						perror_die("epoll_ctl : peer_fd");
					}
				}
				else {
					on_peer_ready_recv(event_buffer[i].data.fd);
				}

			}	
			if (event_buffer[i].events & EPOLLOUT) {
				on_peer_ready_send(event_buffer[i].data.fd);
			}
		}
	}

}






















