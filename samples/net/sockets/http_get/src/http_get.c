/*
 * Copyright (c) 2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>

#if !defined(__ZEPHYR__) || defined(CONFIG_POSIX_API)

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#else

#include <zephyr/net/socket.h>
#include <zephyr/kernel.h>

#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
#include <zephyr/net/tls_credentials.h>
#include "ca_certificate.h"
#endif

#endif

#include <zephyr/net/net_mgmt.h>
#include <zephyr/drivers/modem/hl7800.h>

/* HTTP server to connect to */
#define HTTP_HOST "google.com"
/* Port to connect to, as string */
#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
#define HTTP_PORT "443"
#else
#define HTTP_PORT "80"
#endif
/* HTTP path to request */
#define HTTP_PATH "/"


#define SSTRLEN(s) (sizeof(s) - 1)
#define CHECK(r) { if (r == -1) { printf("Error: " #r "\n"); exit(1); } }

#define REQUEST "GET " HTTP_PATH " HTTP/1.0\r\nHost: www." HTTP_HOST "\r\n\r\n"

static char response[1024];

static bool is_network_ready = false;

static void iface_dns_added_evt_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
                    struct net_if *iface)
{
    if (mgmt_event != NET_EVENT_DNS_SERVER_ADD)
    {
        return;
    }
    printk("DNS ready\n");
    is_network_ready = true;
}

static void iface_up_evt_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
                    struct net_if *iface)
{
    if (mgmt_event != NET_EVENT_IF_UP)
    {
        return;
    }
    printk("interface is up\n");
}

static void iface_down_evt_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
                    struct net_if *iface)
{
    if (mgmt_event != NET_EVENT_IF_DOWN)
    {
        return;
    }
    printk("Interface is down\n");
    is_network_ready = false;
}

static struct mgmt_events {
    uint32_t event;
    net_mgmt_event_handler_t handler;
    struct net_mgmt_event_callback cb;
} iface_events[] = {
    {.event = NET_EVENT_DNS_SERVER_ADD, .handler = iface_dns_added_evt_handler},
    {.event = NET_EVENT_IF_UP,          .handler = iface_up_evt_handler},
    {.event = NET_EVENT_IF_DOWN,        .handler = iface_down_evt_handler},
    {0} /* setup_iface_events requires this extra location. */
};

static void setup_iface_events(void)
{
    int i;
    for (i = 0; iface_events[i].event; i++)
    {
        net_mgmt_init_event_callback(&iface_events[i].cb, iface_events[i].handler,
                            iface_events[i].event);

        net_mgmt_add_event_callback(&iface_events[i].cb);
    }
}

void dump_addrinfo(const struct addrinfo *ai)
{
	printf("addrinfo @%p: ai_family=%d, ai_socktype=%d, ai_protocol=%d, "
	       "sa_family=%d, sin_port=%x\n",
	       ai, ai->ai_family, ai->ai_socktype, ai->ai_protocol,
	       ai->ai_addr->sa_family,
	       ((struct sockaddr_in *)ai->ai_addr)->sin_port);
}

void main(void)
{
	static struct addrinfo hints;
	struct addrinfo *res;
	int st, sock;

	setup_iface_events();

    	st = mdm_hl7800_reset();
    	if (st != 0)
    	{
        	printk("modem init err: %d\n", st);
	        return;
    	}

	    while (!is_network_ready)
    	{
        	printk("Waiting for network to be ready...\n");
        	k_sleep(K_SECONDS(5));
    	}
	
#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
	tls_credential_add(CA_CERTIFICATE_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
			   ca_certificate, sizeof(ca_certificate));
#endif

	printf("Preparing HTTP GET request for http://" HTTP_HOST
	       ":" HTTP_PORT HTTP_PATH "\n");

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	st = getaddrinfo(HTTP_HOST, HTTP_PORT, &hints, &res);
	printf("getaddrinfo status: %d\n", st);

	if (st != 0) {
		printf("Unable to resolve address, quitting\n");
		return;
	}

#if 0
	for (; res; res = res->ai_next) {
		dump_addrinfo(res);
	}
#endif

	dump_addrinfo(res);

#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
	sock = socket(res->ai_family, res->ai_socktype, IPPROTO_TLS_1_2);
#else
	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
#endif
	CHECK(sock);
	printf("sock = %d\n", sock);

#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
	sec_tag_t sec_tag_opt[] = {
		CA_CERTIFICATE_TAG,
	};
	CHECK(setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
			 sec_tag_opt, sizeof(sec_tag_opt)));

	CHECK(setsockopt(sock, SOL_TLS, TLS_HOSTNAME,
			 HTTP_HOST, sizeof(HTTP_HOST)))
#endif

	CHECK(connect(sock, res->ai_addr, res->ai_addrlen));
	CHECK(send(sock, REQUEST, SSTRLEN(REQUEST), 0));

	printf("Response:\n\n");

	while (1) {
		int len = recv(sock, response, sizeof(response) - 1, 0);

		if (len < 0) {
			printf("Error reading response\n");
			return;
		}

		if (len == 0) {
			break;
		}

		response[len] = 0;
		printf("%s", response);
	}

	printf("\n");

	(void)close(sock);
}
