#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef enum { WAIT_FOR_MSG, IN_MSG } ProcessingState;

void serve_connection(int sockfd) {
	if (send(sockfd, "*", 1, 0) < 1) {
		perror("send");
	}

	ProcessingState state = WAIT_FOR_MSG;

	while (1) {
		uint8_t buf[1024];
		int len = recv(sockfd, buf, sizeof buf, 0);
	    	if (len < 0) {
			perror("recv");
		} else if (len == 0) {
			break;
	    	}

		for (int i = 0; i < len; ++i) {
			switch (state) {
	      		case WAIT_FOR_MSG:
				if (buf[i] == '^') {
					state = IN_MSG;
				}
				break;

	      		case IN_MSG:
				if (buf[i] == '$') {
					state = WAIT_FOR_MSG;
				} else {
		  			buf[i] += 1;
		  			if (send(sockfd, &buf[i], 1, 0) < 1) {
						perror("send error");
						close(sockfd);
						return;
		  			}
				}
				break;
	      		}
		}
	}

	close(sockfd);
}

void* start_routine(void* arg) {
	int* sock_fd_p = (int *)arg;
	int  sock_fd   = *sock_fd_p;
	free(arg);

	unsigned long thread_id = (unsigned long) pthread_self();
	printf("Thread %lu created to server the FD %d\n", thread_id, sock_fd);
	 
	serve_connection((sock_fd));

	printf("Thread %lu done\n", thread_id);
	return 0;
}

int main () {
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(server_fd < 0) {
		exit(1);
	}

	int port = 8080;
	int opt = 1;

	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
		printf("Error in set sock opt!!\n");
		exit(4);
	}

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family 	    = 	     AF_INET;
	server_addr.sin_addr.s_addr =  	  INADDR_ANY;
	server_addr.sin_port 	    = 	 htons(port);

	if (bind(server_fd, (struct sockaddr*) &server_addr, sizeof(server_addr))) {
		printf("Error in Binding Socket!!\n");
	}

	if (listen(server_fd, 10)) {
		printf("Error in listening Socket!!\n");
	}
	
	printf("Listening Port on 8080!!\n");

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		int client_fd = accept(server_fd, (struct sockaddr*) &client_addr, &client_addr_len);
		
		int* sock_fd = (int*) malloc(sizeof(client_fd));
		if  (sock_fd == NULL) {
			printf("malloc\n");
			continue;
		}

		*sock_fd =  client_fd;
		pthread_t this_thread;
		
		pthread_create(&this_thread, NULL, start_routine, sock_fd);
		pthread_detach(this_thread);

	}
	return 0;
}
