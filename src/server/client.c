/*
This file is part of iprohc.

iprohc is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
any later version.

iprohc is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with iprohc.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <signal.h>

#include "log.h"

#include "tun_helpers.h"
#include "rohc_tunnel.h"
#include "client.h"
#include "tls.h"


int new_client(int socket,
               int tun,
               const size_t tun_itf_mtu,
               const size_t basedev_mtu,
               struct client**clients,
               int max_clients,
               struct server_opts server_opts)
{
	int conn;
	struct   sockaddr_in src_addr;
	socklen_t src_addr_len = sizeof(src_addr);
	struct in_addr local;
	int client_id;
	int raw;
	int ret;
	unsigned int verify_status;
	int status = -3;

	/* New client */

	/* Initialize TLS */
	gnutls_session_t session;
	gnutls_init(&session, GNUTLS_SERVER);
	gnutls_priority_set(session, server_opts.priority_cache);
	gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, server_opts.xcred);
	gnutls_certificate_server_set_request(session, GNUTLS_CERT_REQUEST);

	/* accept connection */
	conn = accept(socket, (struct sockaddr*)&src_addr, &src_addr_len);
	if(conn < 0)
	{
		trace(LOG_ERR, "failed to accept new connection: %s (%d)",
				strerror(errno), errno);
		status = -3;
		goto error;
	}
	trace(LOG_INFO, "new connection from %s:%d\n", inet_ntoa(src_addr.sin_addr),
			ntohs(src_addr.sin_port));

	/* TLS */
	/* Get rid of warning, it's a "bug" of GnuTLS
	 * (see http://lists.gnu.org/archive/html/help-gnutls/2006-03/msg00020.html) */
	gnutls_transport_set_ptr_nowarn(session, conn);
	do
	{
		ret = gnutls_handshake (session);
	}
	while(ret < 0 && gnutls_error_is_fatal (ret) == 0);

	if(ret < 0)
	{
		trace(LOG_ERR, "TLS handshake failed: %s (%d)", gnutls_strerror(ret), ret);
		status = -3;
		goto tls_deinit;
	}
	trace(LOG_INFO, "TLS handshake succeeded");

	ret = gnutls_certificate_verify_peers2(session, &verify_status);
	if(ret < 0)
	{
		trace(LOG_ERR, "TLS verify failed: %s (%d)", gnutls_strerror(ret), ret);
		status = -3;
		goto tls_deinit;
	}

	if((verify_status & GNUTLS_CERT_INVALID) &&
	   (verify_status != (GNUTLS_CERT_INSECURE_ALGORITHM | GNUTLS_CERT_INVALID)))
	{
		trace(LOG_ERR, "certificate cannot be verified (status %u)",
		      verify_status);
		if(verify_status & GNUTLS_CERT_REVOKED)
		{
			trace(LOG_ERR, " - Revoked certificate");
		}
		if(verify_status & GNUTLS_CERT_SIGNER_NOT_FOUND)
		{
			trace(LOG_ERR, " - Unable to trust certificate issuer");
		}
		if(verify_status & GNUTLS_CERT_SIGNER_NOT_CA)
		{
			trace(LOG_ERR, " - Certificate issuer is not a CA");
		}
		if(verify_status & GNUTLS_CERT_NOT_ACTIVATED)
		{
			trace(LOG_ERR, " - The certificate is not activated");
		}
		if(verify_status & GNUTLS_CERT_EXPIRED)
		{
			trace(LOG_ERR, " - The certificate has expired");
		}
		status = -3;
		goto tls_deinit;
	}

	/* client creation parameters */
	trace(LOG_DEBUG, "Creation of client");

	for(client_id = 0; client_id < max_clients &&
		                clients[client_id] != NULL; client_id++)
	{
	}
	if(client_id == max_clients)
	{
		trace(LOG_ERR, "no more clients accepted, maximum %d reached",
				max_clients);
		status = -2;
		goto tls_deinit;
	}

	clients[client_id] = malloc(sizeof(struct client));
	if(clients[client_id] == NULL)
	{
		trace(LOG_ERR, "failed to allocate memory for new client");
		status = -2;
		goto tls_deinit;
	}
	trace(LOG_DEBUG, "Allocating %p", clients[client_id]);
	memset(&clients[client_id]->tunnel.stats, 0, sizeof(struct statitics));
	clients[client_id]->tcp_socket = conn;
	clients[client_id]->tls_session = session;

	/* dest_addr */
	clients[client_id]->tunnel.src_address.s_addr = INADDR_ANY;
	clients[client_id]->tunnel.dest_address  = src_addr.sin_addr;
	/* local_addr */
	local.s_addr = htonl(ntohl(server_opts.local_address) + client_id + 10);
	clients[client_id]->local_address = local;

	/* set tun */
	clients[client_id]->tunnel.tun = tun;  /* real tun device */
	clients[client_id]->tunnel.tun_itf_mtu = tun_itf_mtu;
	if(socketpair(AF_UNIX, SOCK_RAW, 0, clients[client_id]->tunnel.fake_tun) < 0)
	{
		trace(LOG_ERR, "failed to create a socket pair for TUN: %s (%d)",
				strerror(errno), errno);
		/* TODO  : Flush */
		status = -1;
		goto reset_tun;
	}

	/* set raw */
	raw = create_raw();
	if(raw < 0)
	{
		trace(LOG_ERR, "Unable to create raw socket");
		status = -1;
		goto close_tun_pair;
	}
	clients[client_id]->tunnel.raw_socket = raw;
	clients[client_id]->tunnel.basedev_mtu = basedev_mtu;
	if(socketpair(AF_UNIX, SOCK_RAW, 0, clients[client_id]->tunnel.fake_raw) < 0)
	{
		trace(LOG_ERR, "failed to create a socket pair for the raw socket: "
				"%s (%d)", strerror(errno), errno);
		/* TODO  : Flush */
		status = -1;
		goto close_raw;
	}

	memcpy(&(clients[client_id]->tunnel.params),  &(server_opts.params),
			 sizeof(struct tunnel_params));
	clients[client_id]->tunnel.params.local_address = local.s_addr;
	clients[client_id]->tunnel.status = IPROHC_TUNNEL_CONNECTING;
	clients[client_id]->last_keepalive.tv_sec = -1;

	trace(LOG_DEBUG, "Created");

	return client_id;

close_raw:
	clients[client_id]->tunnel.raw_socket = -1;
	close(raw);
close_tun_pair:
	close(clients[client_id]->tunnel.fake_tun[0]);
	clients[client_id]->tunnel.fake_tun[0] = -1;
	close(clients[client_id]->tunnel.fake_tun[1]);
	clients[client_id]->tunnel.fake_tun[1] = -1;
reset_tun:
	clients[client_id]->tunnel.tun = -1;
	free(clients[client_id]);
	clients[client_id] = NULL;
tls_deinit:
	close(conn);
	gnutls_deinit(session);
error:
	return status;
}


void del_client(struct client *const client)
{
	assert(client != NULL);

	free(client->tunnel.stats.stats_packing);

	/* close RAW socket pair */
	close(client->tunnel.fake_raw[0]);
	client->tunnel.fake_raw[0] = -1;
	close(client->tunnel.fake_raw[1]);
	client->tunnel.fake_raw[1] = -1;

	/* close RAW socket (nothing to do if close_tunnel() was already called) */
	close(client->tunnel.raw_socket);
	client->tunnel.raw_socket = -1;

	/* close TUN socket pair */
	close(client->tunnel.fake_tun[0]);
	client->tunnel.fake_tun[0] = -1;
	close(client->tunnel.fake_tun[1]);
	client->tunnel.fake_tun[1] = -1;

	/* reset TUN fd (do not close it, it is shared with other clients) */
	client->tunnel.tun = -1;

	/* close TCP socket */
	close(client->tcp_socket);
	client->tcp_socket = -1;

	/* free TLS resources */
	gnutls_deinit(client->tls_session);
	
	/* free client context */
	free(client);
}


int start_client_tunnel(struct client*client)
{
	int ret;

	/* Go threads, go ! */
	ret = pthread_create(&(client->thread_tunnel), NULL, new_tunnel,
	                     (void*)(&(client->tunnel)));
	if(ret != 0)
	{
		trace(LOG_ERR, "failed to create the client tunnel thread: %s (%d)",
		      strerror(ret), ret);
		return -1;
	}

	return 0;
}

void stop_client_tunnel(struct client *const client)
{
	assert(client != NULL);
	assert(client->tunnel.raw_socket != -1);

	client->tunnel.status = IPROHC_TUNNEL_PENDING_DELETE;  /* Mark to be deleted */

	trace(LOG_INFO, "wait for client thread to stop");
	pthread_kill(client->thread_tunnel, SIGHUP);
	pthread_join(client->thread_tunnel, NULL);
}
