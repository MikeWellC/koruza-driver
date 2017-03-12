/*
 * koruza-driver - KORUZA driver
 *
 * Copyright (C) 2017 Jernej Kos <jernej@kos.mx>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "network.h"
#include "message.h"
#include "configuration.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libubox/uloop.h>
#include <libubox/avl-cmp.h>

// Multicast group used for KORUZA devices.
#define KORUZA_MULTICAST_GROUP "ff02::1:1042"
// Announce interval.
#define KORUZA_ANNOUNCE_INTERVAL 1000

// AVL tree containing all discovered koruza units.
static struct avl_tree discovered_units;
// Multicast socket listening for autodiscovery messages.
static struct uloop_fd ad_socket;
// Multicast group address.
static struct in6_addr multicast_group;
// Announce timer.
static struct uloop_timeout timer_announce;
// Currently selected pair.
static struct network_device *neighbour;

void network_message_received(struct uloop_fd *sock, unsigned int events);
void network_announce_ourselves(struct uloop_timeout *timer);
int network_send_message(message_t *message);

int network_init(struct uci_context *uci)
{
  neighbour = NULL;

  // Initialize the discovered units AVL tree.
  avl_init(&discovered_units, avl_strcmp, false, NULL);

  // Initialize multicast group address.
  inet_pton(AF_INET6, KORUZA_MULTICAST_GROUP, &multicast_group);

  // Prepare multicast socket, listen for updates.
  char *interface = uci_get_string(uci, "koruza.@network[0].interface");
  if (!interface) {
    syslog(LOG_WARNING, "Network interface not configured. Skipping network initialization.");
    return 0;
  }

  ad_socket.fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (ad_socket.fd < 0) {
    syslog(LOG_ERR, "Failed to setup autodiscovery socket.");
    free(interface);
    return -1;
  }

  if (setsockopt(ad_socket.fd, SOL_SOCKET, SO_BINDTODEVICE, interface, strlen(interface)) != 0) {
    syslog(LOG_ERR, "Failed to bind autodiscovery socket to configured interface.");
    close(ad_socket.fd);
    free(interface);
    return -1;
  }
  free(interface);

  struct sockaddr_in6 address;
  memset(&address, 0, sizeof(address));
  address.sin6_family = AF_INET6;
  address.sin6_port = htons(10424);
  if (bind(ad_socket.fd, (struct sockaddr*) &address, sizeof(address)) != 0) {
    syslog(LOG_ERR, "Failed to bind autodiscovery socket.");
    close(ad_socket.fd);
    return -1;
  }

  // Subscribe to multicast messages.
  struct ipv6_mreq mreq;
  memcpy(&mreq.ipv6mr_multiaddr, &multicast_group, sizeof(multicast_group));
  mreq.ipv6mr_interface = 0;

  if (setsockopt(ad_socket.fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
    syslog(LOG_ERR, "Failed to join multicast group.");
    close(ad_socket.fd);
    return -1;
  }

  // Set hop limit for sent multicast messages.
  int hops = 1;
  if (setsockopt(ad_socket.fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops)) != 0) {
    syslog(LOG_ERR, "Failed to configure hop limit.");
    close(ad_socket.fd);
    return -1;
  }

  // Add socket to uloop.
  ad_socket.cb = network_message_received;
  uloop_fd_add(&ad_socket, ULOOP_READ);

  // Setup announce timer.
  timer_announce.cb = network_announce_ourselves;
  uloop_timeout_set(&timer_announce, KORUZA_ANNOUNCE_INTERVAL);

  syslog(LOG_INFO, "Initialized network.");

  return 0;
}

void network_message_received(struct uloop_fd *sock, unsigned int events)
{
  (void) sock;
  (void) events;

  // message_result_t message_parse(message_t *message, const uint8_t *data, size_t length);
  // TODO.
}

int network_send_message(message_t *message)
{
  message_tlv_add_checksum(message);
  // TODO: Add cryptographic signature.

  // Serialize message.
  size_t buffer_size = message_serialized_size(message);
  uint8_t *buffer = (uint8_t*) malloc(buffer_size);
  if (!buffer) {
    return -1;
  }

  ssize_t result = message_serialize(buffer, buffer_size, message);
  if (result != buffer_size) {
    free(buffer);
    return -1;
  }

  sendto(ad_socket.fd, buffer, buffer_size, 0, (struct sockaddr*) &multicast_group, sizeof(multicast_group));
  free(buffer);
  return 0;
}

void network_announce_ourselves(struct uloop_timeout *timer)
{
  // TODO: Generate announce message.
  message_t msg;
  message_init(&msg);
  network_send_message(&msg);
  message_free(&msg);

  // Reschedule timer if no neighbour selected.
  if (neighbour == NULL) {
    uloop_timeout_set(timer, KORUZA_ANNOUNCE_INTERVAL);
  }
}
