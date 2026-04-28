#include "lurk.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

struct room rooms[MAX_ROOMS];

void init_rooms(void) {
  srand((unsigned int)time(NULL));

  for (int i=0;i<MAX_ROOMS;i++) {
    rooms[i].exit_count = 0;
    rooms[i].id = (uint16_t)(i + 1);
    rooms[i].name[0] = 0;
    rooms[i].desc[0] = 0;
    rooms[i].monster_count = 0;
  }

  // ----- Room setup -----
  strncpy(rooms[0].name, "Debugger Lockdown", 32);
  strncpy(rooms[0].desc,
      "You’ve been staring at the debugger for hours. Every time you step through the code, the bug disappears. Stop... run... bug... vanish... loop forever.",
      sizeof(rooms[0].desc)-1);
  rooms[0].exits[0] = 2; rooms[0].exits[1] = 4; rooms[0].exit_count = 2;

  strncpy(rooms[1].name, "Lost Wi-Fi", 32);
  strncpy(rooms[1].desc,
      "You're at home when the wifi shuts off unexplainably. It's 11:58pm. Project 1 is due in 2 minutes.",
      sizeof(rooms[1].desc)-1);
  rooms[1].exits[0] = 1; rooms[1].exits[1] = 5; rooms[1].exit_count = 2;

  strncpy(rooms[2].name, "Infinite Loop", 32);
  strncpy(rooms[2].desc,
      "You accidentally ran a while loop with no exit. The console floods with output. The fans are screaming. There is no escape.",
      sizeof(rooms[2].desc)-1);
  rooms[2].exits[0] = 4; rooms[2].exits[1] = 6; rooms[2].exit_count = 2;

  strncpy(rooms[3].name, "Syntax Abyss", 32);
  strncpy(rooms[3].desc,
      "The compiler says 'missing semicolon.' You check line 42. It isn’t missing. You check again. The semicolon is laughing at you.",
      sizeof(rooms[3].desc)-1);
  rooms[3].exits[0] = 1; rooms[3].exits[1] = 3; rooms[3].exits[2] = 7; rooms[3].exit_count = 3;

  strncpy(rooms[4].name, "Deadline at Midnight", 32);
  strncpy(rooms[4].desc,
      "It’s 11:59 PM. Canvas is timing out. Your internet is lagging. The submission button is greyed out. You swear you clicked submit...",
      sizeof(rooms[4].desc)-1);
  rooms[4].exits[0] = 2; rooms[4].exits[1] = 8; rooms[4].exit_count = 2;

  strncpy(rooms[5].name, "Segmentation Fault", 32);
  strncpy(rooms[5].desc,
      "You touch one pointer. The world collapses into 'core dumped.' No line number. Just void.",
      sizeof(rooms[5].desc)-1);
  rooms[5].exits[0] = 3; rooms[5].exits[1] = 9; rooms[5].exit_count = 2;

  strncpy(rooms[6].name, "Forgotten Password", 32);
  strncpy(rooms[6].desc,
      "You’re locked out of the lab computer. The password reset requires your student email. The email requires a password reset. Password reset requires backup email. Endless recursion.",
      sizeof(rooms[6].desc)-1);
  rooms[6].exits[0] = 4; rooms[6].exits[1] = 10; rooms[6].exit_count = 2;

  strncpy(rooms[7].name, "Pop Quiz", 32);
  strncpy(rooms[7].desc,
      "You sit for the exam. Question one: How many cycles does it take to access L1 cache. Everyone else starts writing. Your pencil breaks.",
      sizeof(rooms[7].desc)-1);
  rooms[7].exits[0] = 5; rooms[7].exits[1] = 9; rooms[7].exit_count = 2;

  strncpy(rooms[8].name, "Lost Code Realm", 32);
  strncpy(rooms[8].desc,
      "You find yourself where all lost code has ever gone. Laptop crashes, power outages, whatever the reason, it's all here.",
      sizeof(rooms[8].desc)-1);
  rooms[8].exits[0] = 6; rooms[8].exits[1] = 8; rooms[8].exits[2] = 10; rooms[8].exit_count = 3;

  strncpy(rooms[9].name, "Printer From Hell", 32);
  strncpy(rooms[9].desc,
      "You hit print. The queue says 37 jobs ahead of yours. The printer screams, spits out blank pages, then jams. You need these 10 pages of notes.",
      sizeof(rooms[9].desc)-1);
  rooms[9].exits[0] = 7; rooms[9].exits[1] = 9; rooms[9].exit_count = 2;

  // ----- Monsters -----
  const char *mnames[MAX_ROOMS] = {
    "Infinite Bug","Packet Gremlin","Loop Demon","Missing Semicolon",
    "CS Student","CS Student","CS Student","CS Student","CS Student","CS Student"
  };
  const char *mdesc[MAX_ROOMS] = {
    "A shapeless glitch that vanishes whenever you step through the code.",
    "Feeds on dropped packets and unfinished uploads.",
    "Repeats its last action forever. Forever. Forever.",
    "It hides at the end of every line and laughs at your compiler errors.",
    "Eyes glazed from debugging at 11:59 PM.",
    "Mumbling about pointers and core dumps.",
    "Forgot the password to their own mind.",
    "Cramming cache-latency charts into a spiral notebook.",
    "Ghost of a project that was never committed to Git.",
    "Tried to print their report. Still waiting."
  };

  for (int i = 0; i < MAX_ROOMS; ++i) {
    rooms[i].monster_count = 1;
    struct entity *mon = &rooms[i].monsters[0];
    memset(mon, 0, sizeof(*mon));

    mon->blob[0] = 10; // type
    memset(&mon->blob[1], 0, 32);
    strncpy((char*)&mon->blob[1], mnames[i], 31);
    unsigned char flags = FLAG_ALIVE | FLAG_MONSTER;
    mon->blob[33] = flags;

    unsigned short attack  = (unsigned short)(30 + rand() % 46);
    unsigned short defense = (unsigned short)(20 + rand() % 41);
    unsigned short regen   = 0;
    unsigned short health  = 100;
    unsigned short gold    = 0;

    memcpy(&mon->blob[34], &attack, 2);
    memcpy(&mon->blob[36], &defense, 2);
    memcpy(&mon->blob[38], &regen, 2);
    memcpy(&mon->blob[40], &health, 2);
    memcpy(&mon->blob[42], &gold, 2);
    memcpy(&mon->blob[44], &rooms[i].id, 2);

    uint16_t dlen = (uint16_t)strlen(mdesc[i]);
    memcpy(&mon->blob[46], &dlen, 2);
    memcpy(&mon->blob[48], mdesc[i], dlen);

    mon->len = 48 + dlen;
    mon->alive = 1;
  }
}

// send_room and send_connections_for_room adapted from original work.c
int send_room(int fd, uint16_t roomnum) {
  struct room *r = NULL;
  for (int i=0;i<MAX_ROOMS;i++) if (rooms[i].id == roomnum) { r = &rooms[i]; break; }
  if (!r) return -1;
  size_t rname_len = strnlen(r->name, 32);
  size_t rdesc_len = strnlen(r->desc, sizeof(r->desc));
  unsigned char room_msg[3 + 32 + 2 + 2048];
  room_msg[0] = 9;
  room_msg[1] = (unsigned char)(roomnum & 0xFF);
  room_msg[2] = (unsigned char)((roomnum >> 8) & 0xFF);
  memset(&room_msg[3], 0, 32);
  if (rname_len >= 32) memcpy(&room_msg[3], r->name, 32);
  else {
    memcpy(&room_msg[3], r->name, rname_len);
    room_msg[3 + rname_len] = 0;
  }
  room_msg[35] = (unsigned char)(rdesc_len & 0xFF);
  room_msg[36] = (unsigned char)((rdesc_len >> 8) & 0xFF);
  memcpy(&room_msg[37], r->desc, rdesc_len);
  if (safe_send(fd, room_msg, 37 + rdesc_len) <= 0) return -1;
  return 0;
}

void send_connections_for_room(int fd, uint16_t roomnum) {
  struct room *r = NULL;
  for (int i=0;i<MAX_ROOMS;i++) if (rooms[i].id == roomnum) { r = &rooms[i]; break; }
  if (!r) return;
  for (int i=0;i<r->exit_count;i++) {
    uint16_t rn = r->exits[i];
    struct room *rt = NULL;
    for (int j=0;j<MAX_ROOMS;j++) if (rooms[j].id == rn) { rt = &rooms[j]; break; }
    if (!rt) continue;
    size_t rname_len = strnlen(rt->name, 32);
    size_t rdesc_len = strnlen(rt->desc, sizeof(rt->desc));
    unsigned char conn[3 + 32 + 2 + 2048];
    conn[0] = 13;
    conn[1] = (unsigned char)(rn & 0xFF);
    conn[2] = (unsigned char)((rn >> 8) & 0xFF);
    memset(&conn[3], 0, 32);
    if (rname_len >= 32) memcpy(&conn[3], rt->name, 32);
    else {
      memcpy(&conn[3], rt->name, rname_len);
      conn[3 + rname_len] = 0;
    }
    conn[35] = (unsigned char)(rdesc_len & 0xFF);
    conn[36] = (unsigned char)((rdesc_len >> 8) & 0xFF);
    memcpy(&conn[37], rt->desc, rdesc_len);
    safe_send(fd, conn, 37 + rdesc_len);
  }
}