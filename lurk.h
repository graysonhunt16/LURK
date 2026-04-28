#ifndef LURK_H
#define LURK_H

#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define INITIAL_POINTS 100
#define STAT_LIMIT 100
#define MAX_BUFFER 8192
#define MAX_CLIENTS 128
#define MAX_ROOMS 10
#define MAX_MONSTERS_PER_ROOM 4

#define FLAG_ALIVE   (1<<7)
#define FLAG_MONSTER (1<<5)

// Shared structs
struct entity {
  unsigned char blob[MAX_BUFFER];
  ssize_t len;
  int alive;
};

struct room {
  uint16_t id;
  char name[33];
  char desc[512];
  uint16_t exits[8];
  int exit_count;
  struct entity monsters[MAX_MONSTERS_PER_ROOM];
  int monster_count;
};

struct client {
  int in_use;
  int fd;
  pthread_t tid;
  char ipbuf[INET_ADDRSTRLEN];
  unsigned char charbuf[MAX_BUFFER];
  ssize_t char_len;
  uint16_t room;
  int started;
  int alive;
};

// Globals (defined in client_thread.c / rooms.c)
extern struct room rooms[MAX_ROOMS];
extern struct client clients[MAX_CLIENTS];
extern pthread_mutex_t clients_lock;
extern pthread_mutex_t monster_lock;

// Utility functions
void setup_signal(void);
ssize_t read_exact(int fd, void *buf, size_t count);
ssize_t safe_send(int fd, const void *buf, size_t count);

// Rooms
void init_rooms(void);
int send_room(int fd, uint16_t roomnum);
void send_connections_for_room(int fd, uint16_t roomnum);

// Client thread
void *client_thread(void *arg);

#endif // LURK_H