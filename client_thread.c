#include "lurk.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

// Globals
struct client clients[MAX_CLIENTS];
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t monster_lock = PTHREAD_MUTEX_INITIALIZER;

// Helper: send a CHARACTER blob
static void send_character_blob(int fd, unsigned char *blob, ssize_t blen) {
  if (blen <= 0) return;
  safe_send(fd, blob, (size_t)blen);
}

// Broadcast a CHARACTER to room WITHOUT holding clients_lock during send
static void broadcast_character_to_room(uint16_t roomnum, unsigned char *blob, ssize_t blen, int exclude_fd) {
  int fds[MAX_CLIENTS];
  int nfds = 0;

  pthread_mutex_lock(&clients_lock);
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i].in_use && clients[i].started && clients[i].room == roomnum && clients[i].fd != exclude_fd) {
      fds[nfds++] = clients[i].fd;
    }
  }
  pthread_mutex_unlock(&clients_lock);

  for (int i = 0; i < nfds; ++i) {
    safe_send(fds[i], blob, (size_t)blen);
  }
}

// Narration from Professor (recipient = Room <id>, sender = Professor (Narrator))
static void narrate_from_professor(uint16_t room_id, const char *text) {
  unsigned char out[MAX_BUFFER];
  size_t mlen = strlen(text);
  out[0] = 1;
  out[1] = (unsigned char)(mlen & 0xFF);
  out[2] = (unsigned char)((mlen >> 8) & 0xFF);

  // Recipient = Room N
  memset(&out[3], 0, 32);
  char rbuf[32];
  snprintf(rbuf, sizeof(rbuf), "Room %u", room_id);
  strncpy((char*)&out[3], rbuf, 31);

  // Sender = Professor
  memset(&out[35], 0, 30);
  strncpy((char*)&out[35], "Professor", 30);
  out[65] = 0;
  out[66] = 1; // narrator flag

  memcpy(&out[67], text, mlen);
  size_t outlen = 67 + mlen;

  int fds[MAX_CLIENTS];
  int nfds = 0;

  pthread_mutex_lock(&clients_lock);
  for (int c = 0; c < MAX_CLIENTS; c++) {
    if (clients[c].in_use && clients[c].started && clients[c].room == room_id) {
      fds[nfds++] = clients[c].fd;
    }
  }
  pthread_mutex_unlock(&clients_lock);

  for (int i = 0; i < nfds; ++i) safe_send(fds[i], out, outlen);
}

// Standard narration (from player name)
static void narrate_to_room(uint16_t room_id, const char *pname, const char *text) {
  unsigned char out[MAX_BUFFER];
  size_t mlen = strlen(text);
  out[0] = 1;
  out[1] = (unsigned char)(mlen & 0xFF);
  out[2] = (unsigned char)((mlen >> 8) & 0xFF);

  // Recipient empty
  memset(&out[3], 0, 32);

  // Sender = pname
  memset(&out[35], 0, 30);
  strncpy((char*)&out[35], pname, 30);
  out[65] = 0;
  out[66] = 0;

  memcpy(&out[67], text, mlen);
  size_t outlen = 67 + mlen;

  int fds[MAX_CLIENTS];
  int nfds = 0;

  pthread_mutex_lock(&clients_lock);
  for (int c = 0; c < MAX_CLIENTS; c++) {
    if (clients[c].in_use && clients[c].started && clients[c].room == room_id) {
      fds[nfds++] = clients[c].fd;
    }
  }
  pthread_mutex_unlock(&clients_lock);

  for (int i = 0; i < nfds; ++i) safe_send(fds[i], out, outlen);
}

// Remove client safely and notify others (snapshot recipients, don't hold lock during I/O)
static void remove_client_and_notify(int slot) {
  if (slot < 0 || slot >= MAX_CLIENTS) return;

  int fd = -1;
  uint16_t prev_room = 0;
  ssize_t blen = 0;
  unsigned char tmp[MAX_BUFFER];
  int fds[MAX_CLIENTS];
  int nfds = 0;

  pthread_mutex_lock(&clients_lock);
  if (!clients[slot].in_use) {
    pthread_mutex_unlock(&clients_lock);
    return;
  }

  fd = clients[slot].fd;
  prev_room = clients[slot].room;
  blen = clients[slot].char_len;
  if (blen > 0) {
    memcpy(tmp, clients[slot].charbuf, (size_t)blen);
    tmp[44] = 0xFF;
    tmp[45] = 0xFF;
  }

  // collect fds to notify
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i].in_use && clients[i].started && clients[i].room == prev_room && clients[i].fd != fd) {
      fds[nfds++] = clients[i].fd;
    }
  }

  // mark client removed
  clients[slot].in_use = 0;
  clients[slot].fd = -1;
  clients[slot].char_len = 0;
  clients[slot].started = 0;
  clients[slot].alive = 0;
  pthread_mutex_unlock(&clients_lock);

  if (blen > 0) {
    for (int i = 0; i < nfds; ++i) {
      safe_send(fds[i], tmp, (size_t)blen);
    }
  }

  if (fd >= 0) close(fd);
}

// Read a full CHARACTER after type byte (used at login)
static ssize_t read_full_character_after_type(int fd, unsigned char *buffer) {
  if (read_exact(fd, buffer + 1, 47) != 47) return -1;
  unsigned short desc_len = (unsigned short)(buffer[46] | (buffer[47] << 8));
  if ((size_t)desc_len + 48 > MAX_BUFFER) return -1;
  if (desc_len > 0) {
    if (read_exact(fd, buffer + 48, desc_len) != desc_len) return -1;
  }
  return (ssize_t)(48 + desc_len);
}

static int find_free_client_slot(void) {
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (!clients[i].in_use) return i;
  }
  return -1;
}

static void send_error(int fd, unsigned char code, const char *msg) {
  unsigned short mlen = (unsigned short)strlen(msg);
  unsigned char error_msg[4 + 1024];
  error_msg[0] = 7;        // ERROR type
  error_msg[1] = code;
  error_msg[2] = (unsigned char)(mlen & 0xFF);
  error_msg[3] = (unsigned char)((mlen >> 8) & 0xFF);
  memcpy(&error_msg[4], msg, mlen);
  safe_send(fd, error_msg, 4 + mlen);
}

void *client_thread(void *arg) {
  int slot = (int)(intptr_t)arg;
  pthread_detach(pthread_self());

  pthread_mutex_lock(&clients_lock);
  int client_fd = clients[slot].fd;
  char ipbuf[INET_ADDRSTRLEN];
  strncpy(ipbuf, clients[slot].ipbuf, sizeof(ipbuf)-1);
  pthread_mutex_unlock(&clients_lock);

  unsigned char buffer[MAX_BUFFER];

  unsigned char version[5] = {14, 2, 3, 0, 0};
  if (safe_send(client_fd, version, sizeof(version)) <= 0) { remove_client_and_notify(slot); return NULL; }

  const char *game_desc =
    "\\n"
    ".-..-. _       .-.   .-.                            \\n"
    ": `: ::_;      : :  .' `.                           \\n"
    ": .` :.-. .--. : `-.`. .',-.,-.,-. .--.  .--.  .--. \\n"
    ": :. :: :' .; :: .. :: : : ,. ,. :' .; ; : ..'' '_.'\\n"
    ":_;:_;:_;`._. ;:_;:_;:_; :_;:_;:_;`.__,_;:_;  `.__.'\\n"
    "          .-. :                                     \\n"
    "          `._.'                                     \\n"
    "Because your assignments weren't stressful enough."
    ;
  size_t gdesc_len = strlen(game_desc);
  unsigned char game[7 + 1024];
  game[0] = 11;
  game[1] = (unsigned char)(INITIAL_POINTS & 0xFF);
  game[2] = (unsigned char)((INITIAL_POINTS >> 8) & 0xFF);
  game[3] = (unsigned char)(STAT_LIMIT & 0xFF);
  game[4] = (unsigned char)((STAT_LIMIT >> 8) & 0xFF);
  game[5] = (unsigned char)(gdesc_len & 0xFF);
  game[6] = (unsigned char)((gdesc_len >> 8) & 0xFF);
  memcpy(&game[7], game_desc, gdesc_len);
  if (safe_send(client_fd, game, 7 + gdesc_len) <= 0) { remove_client_and_notify(slot); return NULL; }

  // Accept loop: expect CHARACTER (type 10)
  ssize_t char_len = 0;
  int accepted = 0;
  while (!accepted) {
    unsigned char t;
    if (read_exact(client_fd, &t, 1) != 1) { remove_client_and_notify(slot); return NULL; }

    if (t != 10) {
      send_error(client_fd, 4, "Expected CHARACTER (type 10) when creating player");
      continue;
    }
    buffer[0] = 10;
    char_len = read_full_character_after_type(client_fd, buffer);
    if (char_len <= 0) { remove_client_and_notify(slot); return NULL; }

    unsigned short attack  = (unsigned short)(buffer[34] | (buffer[35] << 8));
    unsigned short defense = (unsigned short)(buffer[36] | (buffer[37] << 8));
    unsigned short regen   = (unsigned short)(buffer[38] | (buffer[39] << 8));
    unsigned int sum_stats = (unsigned int)attack + (unsigned int)defense + (unsigned int)regen;

    if (sum_stats > INITIAL_POINTS) {
      send_error(client_fd, 4, "Stat error: sum of stats exceeds initial points");
      continue;
    }
    // --- Validate player name ---
    char pname[31];
    memset(pname, 0, sizeof(pname));
    memcpy(pname, &buffer[1], 30);

    // Trim spaces
    for (int i = 29; i >= 0; i--) {
      if (pname[i] == ' ' || pname[i] == '\0')
        pname[i] = '\0';
      else
        break;
    }

    // Reject empty name
    if (strlen(pname) == 0) {
      send_error(client_fd, 4, "You must enter a name before starting the game.");
      continue;
    }

    // Force health = 100
    buffer[40] = (unsigned char)(100 & 0xFF);
    buffer[41] = (unsigned char)((100 >> 8) & 0xFF);

    pthread_mutex_lock(&clients_lock);
    clients[slot].char_len = char_len;
    memcpy(clients[slot].charbuf, buffer, (size_t)char_len);
    clients[slot].room = 0; // until START
    clients[slot].started = 0;
    clients[slot].alive = 1;
    pthread_mutex_unlock(&clients_lock);

    unsigned char accept_msg[2] = {8, 6};
    if (safe_send(client_fd, accept_msg, 2) <= 0) { remove_client_and_notify(slot); return NULL; }

    accepted = 1;
  }

  // Wait for START (type 6)
  for (;;) {
    unsigned char t;
    if (read_exact(client_fd, &t, 1) != 1) { remove_client_and_notify(slot); return NULL; }
    if (t == 6) break;
    if (t == 10) {
      buffer[0] = 10;
      ssize_t alt_len = read_full_character_after_type(client_fd, buffer);
      if (alt_len <= 0) { remove_client_and_notify(slot); return NULL; }
      unsigned short attack  = (unsigned short)(buffer[34] | (buffer[35] << 8));
      unsigned short defense = (unsigned short)(buffer[36] | (buffer[37] << 8));
      unsigned short regen   = (unsigned short)(buffer[38] | (buffer[39] << 8));
      unsigned int sum_stats = (unsigned int)attack + (unsigned int)defense + (unsigned int)regen;
      if (sum_stats <= INITIAL_POINTS) {
        buffer[40] = (unsigned char)(100 & 0xFF);
        buffer[41] = (unsigned char)((100 >> 8) & 0xFF);
        pthread_mutex_lock(&clients_lock);
        clients[slot].char_len = alt_len;
        memcpy(clients[slot].charbuf, buffer, (size_t)alt_len);
        pthread_mutex_unlock(&clients_lock);
        unsigned char accept_msg[2] = {8, 6};
        if (safe_send(client_fd, accept_msg, 2) <= 0) { remove_client_and_notify(slot); return NULL; }
      } else {
        send_error(client_fd, 4, "Stat error: sum exceeds initial points");
      }
    } else {
      // ignore other bytes before start
    }
  }

  // Place into room 1
  pthread_mutex_lock(&clients_lock);
  clients[slot].started = 1;
  clients[slot].room = 1;
  clients[slot].charbuf[33] |= (1 << 3); // Ready
  clients[slot].charbuf[33] |= (1 << 4); // Started
  clients[slot].charbuf[44] = (unsigned char)(1 & 0xFF);
  clients[slot].charbuf[45] = (unsigned char)((1 >> 8) & 0xFF);
  ssize_t saved_char_len = clients[slot].char_len;
  unsigned char saved_char[MAX_BUFFER];
  memcpy(saved_char, clients[slot].charbuf, (size_t)saved_char_len);
  pthread_mutex_unlock(&clients_lock);

  // Send updated CHARACTER (and room, connections, others)
  send_character_blob(client_fd, saved_char, saved_char_len);
  if (send_room(client_fd, 1) < 0) { remove_client_and_notify(slot); return NULL; }

  // Send monsters in this room
  {
    struct room *r = NULL;
    for (int i = 0; i < MAX_ROOMS; i++) {
      if (rooms[i].id == 1) { r = &rooms[i]; break; }
    }
    if (r) {
      for (int m = 0; m < r->monster_count; m++) {
        if (r->monsters[m].alive) {
          send_character_blob(client_fd, r->monsters[m].blob, r->monsters[m].len);
        }
      }
    }
  }

  // ensure order ROOM then CHARACTER — send CHARACTER again after ROOM
  send_character_blob(client_fd, saved_char, saved_char_len);

  // snapshot other players in room 1 and send outside lock
  unsigned char *snap_bufs[MAX_CLIENTS];
  ssize_t snap_lens[MAX_CLIENTS];
  int snap_count = 0;
  for (int i = 0; i < MAX_CLIENTS; ++i) { snap_bufs[i] = NULL; snap_lens[i] = 0; }

  pthread_mutex_lock(&clients_lock);
  for (int i=0;i<MAX_CLIENTS;i++) {
    if (clients[i].in_use && i != slot && clients[i].started && clients[i].room == 1) {
      ssize_t len = clients[i].char_len;
      if (len > 0) {
        snap_bufs[snap_count] = malloc(len);
        if (snap_bufs[snap_count]) {
          memcpy(snap_bufs[snap_count], clients[i].charbuf, (size_t)len);
          snap_lens[snap_count] = len;
          snap_count++;
        }
      }
    }
  }
  pthread_mutex_unlock(&clients_lock);

  for (int i = 0; i < snap_count; ++i) {
    if (snap_bufs[i]) {
      safe_send(client_fd, snap_bufs[i], (size_t)snap_lens[i]);
      free(snap_bufs[i]);
    }
  }

  // connections & broadcast
  send_connections_for_room(client_fd, 1);
  broadcast_character_to_room(1, saved_char, saved_char_len, client_fd);

  // Main loop
  for (;;) {
    unsigned char t;
    if (read_exact(client_fd, &t, 1) != 1) { remove_client_and_notify(slot); return NULL; }

    // Refresh alive state every iteration
    int is_alive = 0;
    uint16_t cur_room = 0;
    char pname[31];
    memset(pname, 0, sizeof(pname));

    pthread_mutex_lock(&clients_lock);
    is_alive = clients[slot].alive;
    cur_room = clients[slot].room;
    if (clients[slot].char_len >= 31)
      memcpy(pname, &clients[slot].charbuf[1], 30);
    pthread_mutex_unlock(&clients_lock);

    if (!is_alive && t != 2 && t != 12) {
      send_error(client_fd, 9, "You are dead and can only move between rooms.");
      narrate_to_room(cur_room, pname, "A ghostly whisper echoes: only movement remains...");
      continue;
    }

    if (t == 2) {
      unsigned char rb[2];
      if (read_exact(client_fd, rb, 2) != 2) { remove_client_and_notify(slot); return NULL; }
      uint16_t new_room = (uint16_t)(rb[0] | (rb[1] << 8));

      pthread_mutex_lock(&clients_lock);
      if (!clients[slot].started) { pthread_mutex_unlock(&clients_lock); send_error(client_fd, 5, "Not Ready: START not sent"); continue; }
      uint16_t old_room = clients[slot].room;
      int new_exists = 0;
      for (int i=0;i<MAX_ROOMS;i++) if (rooms[i].id == new_room) { new_exists = 1; break; }
      if (!new_exists) { pthread_mutex_unlock(&clients_lock); send_error(client_fd, 1, "Bad room"); continue; }
      int connected = 0;
      for (int i=0;i<MAX_ROOMS;i++) if (rooms[i].id == old_room) {
        for (int j=0;j<rooms[i].exit_count;j++) if (rooms[i].exits[j] == new_room) { connected = 1; break; }
        break;
      }
      if (!connected) { pthread_mutex_unlock(&clients_lock); send_error(client_fd, 1, "Bad room: not connected to current room"); continue; }

      clients[slot].room = new_room;
      clients[slot].charbuf[44] = (unsigned char)(new_room & 0xFF);
      clients[slot].charbuf[45] = (unsigned char)((new_room >> 8) & 0xFF);
      ssize_t my_char_len = clients[slot].char_len;
      unsigned char my_char[MAX_BUFFER];
      memcpy(my_char, clients[slot].charbuf, (size_t)my_char_len);
      pthread_mutex_unlock(&clients_lock);

      if (send_room(client_fd, new_room) < 0) { send_error(client_fd, 1, "Bad room"); continue; }
      // Send monsters in this room
      {
        struct room *r = NULL;
        for (int i = 0; i < MAX_ROOMS; i++) {
          if (rooms[i].id == new_room) { r = &rooms[i]; break; }
        }
        if (r) {
          for (int m = 0; m < r->monster_count; m++) {
            if (r->monsters[m].alive) {
              send_character_blob(client_fd, r->monsters[m].blob, r->monsters[m].len);
            }
          }
        }
      }

      send_character_blob(client_fd, my_char, my_char_len);

      // snapshot other clients in the new room to send to this client
      unsigned char *snap_bufs2[MAX_CLIENTS];
      ssize_t snap_lens2[MAX_CLIENTS];
      int snap_count2 = 0;
      for (int i = 0; i < MAX_CLIENTS; ++i) { snap_bufs2[i] = NULL; snap_lens2[i] = 0; }

      pthread_mutex_lock(&clients_lock);
      for (int i=0;i<MAX_CLIENTS;i++) {
        if (clients[i].in_use && clients[i].fd != client_fd && clients[i].started && clients[i].room == new_room) {
          ssize_t len = clients[i].char_len;
          if (len > 0) {
            snap_bufs2[snap_count2] = malloc(len);
            if (snap_bufs2[snap_count2]) {
              memcpy(snap_bufs2[snap_count2], clients[i].charbuf, (size_t)len);
              snap_lens2[snap_count2] = len;
              snap_count2++;
            }
          }
        }
      }
      pthread_mutex_unlock(&clients_lock);

      for (int i = 0; i < snap_count2; ++i) {
        if (snap_bufs2[i]) {
          safe_send(client_fd, snap_bufs2[i], (size_t)snap_lens2[i]);
          free(snap_bufs2[i]);
        }
      }

      send_connections_for_room(client_fd, new_room);

      // notify old room and new room inhabitants
      broadcast_character_to_room(old_room, my_char, my_char_len, client_fd);
      broadcast_character_to_room(new_room, my_char, my_char_len, client_fd);

    } else if (t == 12) {
      remove_client_and_notify(slot);
      break;

    } else if (t == 1) {
      unsigned char lenb[2];
      if (read_exact(client_fd, lenb, 2) != 2) { remove_client_and_notify(slot); return NULL; }
      unsigned short mlen = (unsigned short)(lenb[0] | (lenb[1] << 8));
      size_t toread = 32 + 30 + 2 + mlen;
      if (toread + 2 > MAX_BUFFER) {
        send_error(client_fd, 0, "Message too large");
        unsigned char tmp[1024];
        size_t skip = toread;
        while (skip > 0) {
          size_t chunk = skip > sizeof(tmp) ? sizeof(tmp) : skip;
          if (read_exact(client_fd, tmp, chunk) != (ssize_t)chunk) { remove_client_and_notify(slot); return NULL; }
          skip -= chunk;
        }
        continue;
      }
      if (read_exact(client_fd, buffer + 2, toread) != (ssize_t)toread) { remove_client_and_notify(slot); return NULL; }

      unsigned char out[MAX_BUFFER];
      out[0] = 1;
      out[1] = (unsigned char)(mlen & 0xFF);
      out[2] = (unsigned char)((mlen >> 8) & 0xFF);
      memset(&out[3], 0, 32);
      pthread_mutex_lock(&clients_lock);
      if (clients[slot].char_len >= 34) {
        memcpy(&out[35], &clients[slot].charbuf[1], 30);
      } else {
        memset(&out[35], 0, 30);
      }
      out[65] = 0; out[66] = 0;
      pthread_mutex_unlock(&clients_lock);

      size_t msg_offset_in_read = 2 + 32 + 30 + 2;
      memcpy(&out[67], buffer + msg_offset_in_read, mlen);
      size_t out_len = 67 + mlen;

      uint16_t sroom;
      int fds[MAX_CLIENTS];
      int nfds = 0;

      pthread_mutex_lock(&clients_lock);
      sroom = clients[slot].room;
      for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].in_use && clients[i].started && clients[i].room == sroom) {
          fds[nfds++] = clients[i].fd;
        }
      }
      pthread_mutex_unlock(&clients_lock);

      for (int i = 0; i < nfds; ++i) {
        safe_send(fds[i], out, out_len);
      }

    } else {
      // Not implemented: FIGHT (3), PVPFIGHT (4), LOOT (5) -> return errors or skip payload
      if (t == 3) {
        // simplified fight logic: check monsters, compute damages, update under locks and broadcast
        uint16_t room_id;
        pthread_mutex_lock(&clients_lock);
        room_id = clients[slot].room;
        pthread_mutex_unlock(&clients_lock);

        struct room *r = NULL;
        for (int i = 0; i < MAX_ROOMS; i++) if (rooms[i].id == room_id) { r = &rooms[i]; break; }
        if (!r) { send_error(client_fd, 1, "Invalid room"); continue; }

        // Check for alive monsters under monster_lock
        int any_alive = 0;
        pthread_mutex_lock(&monster_lock);
        for (int i = 0; i < r->monster_count; i++) {
          unsigned char flags = r->monsters[i].blob[33];
          if ((flags & FLAG_MONSTER) && (flags & FLAG_ALIVE) && r->monsters[i].alive) { any_alive = 1; break; }
        }
        pthread_mutex_unlock(&monster_lock);

        if (!any_alive) {
          send_error(client_fd, 7, "A fight cannot be initiated, no monster in room.");
          continue;
        }

        unsigned short pA, pD, pH;
        char pname[31]; memset(pname,0,sizeof(pname));
        pthread_mutex_lock(&clients_lock);
        pA = (unsigned short)(clients[slot].charbuf[34] | (clients[slot].charbuf[35] << 8));
        pD = (unsigned short)(clients[slot].charbuf[36] | (clients[slot].charbuf[37] << 8));
        pH = (unsigned short)(clients[slot].charbuf[40] | (clients[slot].charbuf[41] << 8));
        memcpy(pname, &clients[slot].charbuf[1], 30);
        pthread_mutex_unlock(&clients_lock);

        unsigned int mAtkSum = 0, mDefSum = 0; int mCount = 0;
        unsigned char monster_snap[MAX_MONSTERS_PER_ROOM][MAX_BUFFER];
        ssize_t monster_snap_len[MAX_MONSTERS_PER_ROOM];
        memset(monster_snap_len, 0, sizeof(monster_snap_len));

        pthread_mutex_lock(&monster_lock);
        for (int i = 0; i < r->monster_count; i++) {
          unsigned char flags = r->monsters[i].blob[33];
          if ((flags & FLAG_MONSTER) && (flags & FLAG_ALIVE) && r->monsters[i].alive) {
            unsigned short ma = (unsigned short)(r->monsters[i].blob[34] | (r->monsters[i].blob[35] << 8));
            unsigned short md = (unsigned short)(r->monsters[i].blob[36] | (r->monsters[i].blob[37] << 8));
            mAtkSum += ma;
            mDefSum += md;
            mCount++;
          }
        }

        unsigned short mDefAvg = mCount ? (unsigned short)(mDefSum / mCount) : 0;
        int dmg_to_mon = (int)pA - (int)(mDefAvg / 2); if (dmg_to_mon < 1) dmg_to_mon = 1;
        int dmg_to_player = (int)mAtkSum - (int)(pD / 2); if (dmg_to_player < 1) dmg_to_player = 1;

        char tmpmsg[256];
        snprintf(tmpmsg, sizeof(tmpmsg), "%s initiated a fight!", pname);
        char narr2[256]; snprintf(narr2, sizeof(narr2), "Players are attacking with a power of %u!", pA);
        char narr3[256]; snprintf(narr3, sizeof(narr3), "Monsters are attacking with a power of %u!", mAtkSum);

        for (int i = 0; i < r->monster_count; i++) {
          unsigned char flags = r->monsters[i].blob[33];
          if ((flags & FLAG_MONSTER) && (flags & FLAG_ALIVE) && r->monsters[i].alive) {
            unsigned short mH = (unsigned short)(r->monsters[i].blob[40] | (r->monsters[i].blob[41] << 8));
            int newH = (int)mH - dmg_to_mon;
            int died = 0;
            if (newH <= 0) {
              newH = 0;
              r->monsters[i].alive = 0;
              r->monsters[i].blob[33] &= (unsigned char)(~FLAG_ALIVE);
              died = 1;
            }
            r->monsters[i].blob[40] = (unsigned char)(newH & 0xFF);
            r->monsters[i].blob[41] = (unsigned char)((newH >> 8) & 0xFF);

            ssize_t blen = r->monsters[i].len;
            if (blen > MAX_BUFFER) blen = MAX_BUFFER;
            memcpy(monster_snap[i], r->monsters[i].blob, (size_t)blen);
            monster_snap_len[i] = blen;
          } else {
            monster_snap_len[i] = 0;
          }
        }
        pthread_mutex_unlock(&monster_lock);

        unsigned int total_kills = 0;
        for (int i = 0; i < r->monster_count; i++) {
          if (monster_snap_len[i] > 0) {
            unsigned char snap_flags = monster_snap[i][33];
            if (!(snap_flags & FLAG_ALIVE)) total_kills++;
          }
        }

        unsigned char pblob[MAX_BUFFER];
        ssize_t pclen = 0;

        if (total_kills > 0) {
          pthread_mutex_lock(&clients_lock);
          unsigned short pGold = (unsigned short)(clients[slot].charbuf[42] | (clients[slot].charbuf[43] << 8));
          unsigned int newGold = (unsigned int)pGold + (5 * total_kills);
          if (newGold > 65535) newGold = 65535;
          clients[slot].charbuf[42] = (unsigned char)(newGold & 0xFF);
          clients[slot].charbuf[43] = (unsigned char)((newGold >> 8) & 0xFF);
          ssize_t pclen2 = clients[slot].char_len;
          if (pclen2 > MAX_BUFFER) pclen2 = MAX_BUFFER;
          memcpy(pblob, clients[slot].charbuf, (size_t)pclen2);
          pclen = pclen2;
          pthread_mutex_unlock(&clients_lock);

          char msgbuf[128];
          snprintf(msgbuf, sizeof(msgbuf), "Good job, %s! You were awarded %u gold.", pname, 5 * total_kills);
          narrate_from_professor(room_id, msgbuf);
        } else {
          pthread_mutex_lock(&clients_lock);
          ssize_t pclen2 = clients[slot].char_len;
          if (pclen2 > MAX_BUFFER) pclen2 = MAX_BUFFER;
          memcpy(pblob, clients[slot].charbuf, (size_t)pclen2);
          pclen = pclen2;
          pthread_mutex_unlock(&clients_lock);
        }

        int newpH;
        pthread_mutex_lock(&clients_lock);
        {
          unsigned short curHp = (unsigned short)(clients[slot].charbuf[40] | (clients[slot].charbuf[41] << 8));
          int calc = (int)curHp - dmg_to_player;
          if (calc <= 0) {
            newpH = 0;
            clients[slot].alive = 0;
            clients[slot].charbuf[33] &= (unsigned char)(~FLAG_ALIVE);
          } else newpH = calc;
          clients[slot].charbuf[40] = (unsigned char)(newpH & 0xFF);
          clients[slot].charbuf[41] = (unsigned char)((newpH >> 8) & 0xFF);

          ssize_t pclen2 = clients[slot].char_len;
          unsigned char pblob2[MAX_BUFFER];
          if (pclen2 > MAX_BUFFER) pclen2 = MAX_BUFFER;
          memcpy(pblob2, clients[slot].charbuf, (size_t)pclen2);
          memcpy(pblob, pblob2, (size_t)pclen2);
          pclen = pclen2;
        }
        pthread_mutex_unlock(&clients_lock);

        narrate_to_room(room_id, pname, tmpmsg);
        narrate_to_room(room_id, pname, narr2);
        narrate_to_room(room_id, pname, narr3);

        for (int i = 0; i < r->monster_count; i++) {
          if (monster_snap_len[i] > 0) broadcast_character_to_room(room_id, monster_snap[i], monster_snap_len[i], -1);
        }

        broadcast_character_to_room(room_id, pblob, pclen, -1);

        if (newpH <= 0) {
          char deadmsg[256];
          snprintf(deadmsg, sizeof(deadmsg),
              "This is the end of the line for %s, they couldn't escape the wrath of CompSci.", pname);
          narrate_to_room(room_id, pname, deadmsg);
        }
      } else if (t == 4) {
        unsigned char name[32];
        if (read_exact(client_fd, name, 32) != 32) { remove_client_and_notify(slot); return NULL; }
        send_error(client_fd, 0, "Action not implemented");
      } else if (t == 5) {
        send_error(client_fd, 0, "Loot is given upon slaying a monster");
      } else {
        // Unknown; ignore
      }
    }
  }

  remove_client_and_notify(slot);
  return NULL;
}