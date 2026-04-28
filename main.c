#include "lurk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

extern struct client clients[MAX_CLIENTS];
extern pthread_mutex_t clients_lock;

int main(void) {
  setup_signal();
  init_rooms();

  int skt = socket(AF_INET, SOCK_STREAM, 0);
  if (skt < 0) { perror("socket"); return 1; }

  int opt = 1;
  if (setsockopt(skt, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt SO_REUSEADDR"); close(skt); return 1;
  }

  struct sockaddr_in sad;
  memset(&sad, 0, sizeof(sad));
  sad.sin_family = AF_INET;
  sad.sin_port = htons(5035);
  sad.sin_addr.s_addr = INADDR_ANY;

  if (bind(skt, (struct sockaddr*)&sad, sizeof(sad)) < 0) { perror("bind"); close(skt); return 1; }
  if (listen(skt, 128) < 0) { perror("listen"); close(skt); return 1; }

  printf("Lurk server listening on port 5035...\n");

  for (int i=0;i<MAX_CLIENTS;i++) {
    clients[i].in_use = 0;
    clients[i].fd = -1;
    clients[i].char_len = 0;
    clients[i].started = 0;
    clients[i].alive = 0;
  }

  for (;;) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(skt, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) { perror("accept"); continue; }

    char ipbuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ipbuf, sizeof(ipbuf));
    printf("Connection from %s\n", ipbuf);

    pthread_mutex_lock(&clients_lock);
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
      if (!clients[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
      pthread_mutex_unlock(&clients_lock);
      fprintf(stderr, "No free client slots; rejecting connection\n");
      close(client_fd);
      continue;
    }
    clients[slot].in_use = 1;
    clients[slot].fd = client_fd;
    strncpy(clients[slot].ipbuf, ipbuf, sizeof(clients[slot].ipbuf)-1);
    clients[slot].char_len = 0;
    clients[slot].started = 0;
    clients[slot].alive = 1;
    pthread_mutex_unlock(&clients_lock);

    if (pthread_create(&clients[slot].tid, NULL, client_thread, (void*)(intptr_t)slot) != 0) {
      perror("pthread_create");
      // cleanup
      pthread_mutex_lock(&clients_lock);
      clients[slot].in_use = 0;
      clients[slot].fd = -1;
      pthread_mutex_unlock(&clients_lock);
      close(client_fd);
      continue;
    }
  }

  close(skt);
  return 0;
}