#ifndef BATOBYTE_SHARED_H
#define BATOBYTE_SHARED_H

#include "common.h"

#include <stddef.h>

extern const char *move_names[MOVE_COUNT];
extern const char *power_names[PW_COUNT];
extern const char *power_moves[PW_COUNT];

void die_with_error(const char *error_msg);
int send_line(int socket_fd, const char *line);
int receive_line(int socket_fd, char *line, size_t size);
int read_choice(const char *prompt, int minimum, int maximum, int *choice);
void print_power_options(void);
void print_how_to_play(void);
int pick_loadout(Power loadout[LOADOUT_SIZE]);
void print_hp_bar(const char *label, int hp, int change, int poison, int bleed);

#endif
