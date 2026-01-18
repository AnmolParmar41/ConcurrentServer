#include <asm-generic/errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <uv.h>
#include <uv/unix.h>
#include <stdbool.h>

#include "utils.h"

#define N_BACKLOG 10000
#define BUFFER_SIZE 1024

typedef enum
{
	INITIAL_ACK,
	WAIT_FOR_MSG,
	IN_MSG
} processing_state;

typedef struct
{

	processing_state peer_status;
	char send_buffer[BUFFER_SIZE];
	int sendbuff_end;
	uv_tcp_t *client;

} peer_status_t;

typedef struct
{

	uv_stream_t *client;
	ssize_t nread;
	char *buf;
	bool is_done = false;

} read_cb_args;

uv_buf_t global_uv_buff[100];
int counter = 0;
void on_client_closed(uv_handle_t *handle)
{
	uv_tcp_t *client = (uv_tcp_t *)handle;
	// The client handle owns the peer state storing its address in the data
	// field, so we free it here.
	if (client->data)
	{
		free(client->data);
	}
	free(client);
	client == NULL;	
}

void on_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buff)
{
	buff->base = (char *)calloc(suggested_size, sizeof(char));
	buff->len = suggested_size;
}

void on_peer_write(uv_write_t *req, int status)
{
	if (status)
	{
		printf("Write error: %s\n", uv_strerror(status));
	}

	peer_status_t *peer_state = (peer_status_t *)req->data;

	peer_state->sendbuff_end = 0;
	free(req);
}

void after_work_write(uv_work_t *work_req, int status)
{
	if (status)
	{
		printf("uv_work_cb status: %s", uv_strerror(status));
		exit(EXIT_FAILURE);
	}
	
	peer_status_t *peer_state = (peer_status_t *)work_req->data;
	uv_tcp_t *client = peer_state->client;

	if(!client) {
		return;
	}

	if (peer_state->sendbuff_end > 0)
	{
		uv_buf_t write_buf = uv_buf_init(peer_state->send_buffer, peer_state->sendbuff_end);
		uv_write_t *writereq = (uv_write_t *)malloc(sizeof(uv_write_t));
		writereq->data = peer_state;

		int rc;
		if ((rc = uv_write(writereq, (uv_stream_t *)client, &write_buf, 1, on_peer_write)) < 0)
		{
			printf("uv_write failed: %s", uv_strerror(rc));
			exit(EXIT_FAILURE);
		}
	}
}

void on_peer_recv(uv_work_t *work_req)
{

	read_cb_args *args = work_req->data;
	uv_stream_t *client = args->client;
	ssize_t nread = args->nread;
	const char *buff = args->buf;

	if (nread < 0)
	{
		if (nread != UV_EOF)
		{
			printf("Read error: %s", uv_strerror(nread));
		}
		uv_close((uv_handle_t *)client, on_client_closed);
	}
	else if (nread == 0)
	{
		return;
	}
	else
	{
		peer_status_t *peer_state = (peer_status_t *)client->data;

		if (peer_state->peer_status == INITIAL_ACK)
		{
			free(buff);
			return;
		}

		for (int i = 0; i < nread; i++)
		{
			switch (peer_state->peer_status)
			{
			case INITIAL_ACK:
				break;
			case WAIT_FOR_MSG:
				if (buff[i] == '^')
				{
					peer_state->peer_status = IN_MSG;
				}
				break;
			case IN_MSG:
				if (buff[i] == '$')
				{
					peer_state->peer_status = WAIT_FOR_MSG;
				}
				else
				{
					peer_state->send_buffer[peer_state->sendbuff_end++] = buff[i] + 1;
				}
				break;
			}
		}
		work_req->data = peer_state;
	}

	free(buff);
	free(args);
}

// this function is wrapper for on_peer_recv so that it could be done in a seprate thread;

void work__read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf)
{
	read_cb_args *args = (read_cb_args *)malloc(sizeof(*args));
	args->client = client;
	args->nread = nread;
	args->buf = buf->base;

	uv_work_t *work_req = (uv_work_t *)malloc(sizeof(*work_req));
	work_req->data = args;
	uv_queue_work(uv_default_loop(), work_req, on_peer_recv, after_work_write);
}

void on_write_ack(uv_write_t *req, int status)
{
	if (status < 0)
	{
		printf("Initial ack write failure: %s", uv_strerror(status));
	}

	peer_status_t *peer_state = (peer_status_t *)req->data;
	peer_state->peer_status = WAIT_FOR_MSG;
	peer_state->sendbuff_end = 0;

	// Callback work__read() will be called when the available data is read.
	
	uv_read_start((uv_stream_t *)peer_state->client, on_alloc_buffer, work__read);
}

void on_peer_connected(uv_stream_t *server_stream, int status)
{
	if (status < 0)
	{
		printf("%s", uv_strerror(status));
		return;
	}

	uv_tcp_t *client = (uv_tcp_t *)malloc(sizeof(*client));

	if (uv_tcp_init(uv_default_loop(), client) < 0)
	{
		perror_die("uv_tcp_init:");
		return;
	}
	client->data = NULL;
	if (uv_accept(server_stream, (uv_stream_t *)client) == 0)
	{
		struct sockaddr_storage peername;
		int namelen = sizeof(peername);
		int rc = 0;
		if ((rc = uv_tcp_getpeername(client, (struct sockaddr *)&peername, &namelen)) < 0)
		{
			printf("uv_tcp_getpeername failed: %s", uv_strerror(rc));
		}

		report_peer_connected((const struct sockaddr_in *)&peername, namelen);

		peer_status_t *peer_state = (peer_status_t *)malloc(sizeof(peer_status_t));
		peer_state->peer_status = INITIAL_ACK;
		peer_state->send_buffer[0] = '*';
		peer_state->sendbuff_end = 1;
		peer_state->client = client;
		client->data = peer_state;

		uv_buf_t write_buff = uv_buf_init(peer_state->send_buffer, peer_state->sendbuff_end);
		uv_write_t *req = (uv_write_t *)malloc(sizeof(*req));

		req->data = peer_state;

		if ((rc = uv_write(req, (uv_stream_t *)client, &write_buff, 1, on_write_ack)) < 0)
		{
			printf("uv_read_start failed: %s", uv_strerror(rc));
			exit(EXIT_FAILURE);
		}
		//printf("%s", uv_strerror(uv_translate_sys_error(errno)));
	}
	else
	{
		uv_close((uv_handle_t *)client, on_client_closed);
	}
}

int main()
{
	setvbuf(stdout, NULL, _IONBF, 0);
	uv_tcp_t server_stream;
	uv_thread_t thread1;

	int rc = uv_tcp_init(uv_default_loop(), &server_stream);
	if (rc < 0)
	{
		perror_die("uv_tcp_init: rc < 0\n");
	}

	struct sockaddr_in server_addr;
	if ((rc = uv_ip4_addr("127.0.0.1", 9090, &server_addr)) < 0)
	{
		perror_die("uv_ip4_addr: ");
	}

	rc = uv_tcp_bind(&server_stream, (struct sockaddr *)&server_addr, 0);
	if (rc < 0)
	{
		perror_die("uv_tcp_bind: bind < 0\n");
	}

	printf("Listening on port 9090\n");
	int listen_var = uv_listen((uv_stream_t *)&server_stream, N_BACKLOG, on_peer_connected);
	if (listen_var < 0)
	{
		perror_die("uv_listen: listen_var");
	}

	uv_run(uv_default_loop(), UV_RUN_DEFAULT);

	return uv_loop_close(uv_default_loop());
}
