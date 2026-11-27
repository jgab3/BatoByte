#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

const char *move_names[MOVE_COUNT] = {
	"Attack", "Defend", "Counter"
};

const char *power_names[PW_COUNT] = {
	"Heal",
	"Double Damage",
	"Poison",
	"Double Defend",
	"Thorns",
	"Guard Break",
	"Full Counter"
};

const char *power_moves[PW_COUNT] = {
	"any move",
	"Attack",
	"Attack",
	"Defend",
	"Defend",
	"Counter",
	"Counter"
};

void die_with_error(const char *error_msg)
{
	printf("%s\n", error_msg);
	exit(EXIT_FAILURE);
}

int send_line(int socket_fd, const char *line)
{
	size_t sent = 0;
	size_t length = strlen(line);

	while (sent < length) {
		ssize_t n = send(socket_fd, line + sent, length - sent, MSG_NOSIGNAL);
		if (n <= 0) {
			return 0;
		}
		sent += (size_t)n;
	}
	return 1;
}

int receive_line(int socket_fd, char *line, size_t size)
{
	size_t used = 0;
	char character;
	ssize_t n;

	while (used < size - 1) {
		n = recv(socket_fd, &character, 1, 0);
		if (n <= 0) {
			return 0;
		}
		if (character == '\n') {
			line[used] = '\0';
			return 1;
		}
		line[used++] = character;
	}
	return 0;
}

int read_choice(const char *prompt, int minimum, int maximum,
					   int *choice)
{
	char input[100];
	char *end;
	long number;
	tcflush(STDIN_FILENO, TCIFLUSH);

	while (1) {
		printf("%s", prompt);
		fflush(stdout);
		if (fgets(input, sizeof(input), stdin) == NULL) {
			return 0;
		}
		number = strtol(input, &end, 10);
		while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
			++end;
		}
		if (end != input && *end == '\0' && number >= minimum &&
			number <= maximum) {
			*choice = (int)number;
			return 1;
		}
		printf("Enter a number from %d to %d.\n", minimum, maximum);
	}
}

void print_power_options(void)
{
	Power power;

	for (power = PW_HEAL; power < PW_COUNT; power = (Power)(power + 1)) {
		printf("%d. %s\n", power + 1, power_names[power]);
	}
}

void print_how_to_play(void)
{
	printf("\n=============== BatoByte: Power Battle ===============\n");
	printf("Each round starts at %d HP. First to win %d rounds wins.\n",
		START_HP, ROUNDS_TO_WIN);
	printf("Each turn, both players secretly pick a move:\n");
	printf("  Attack beats Defend, Defend beats Counter, Counter reflects Attack.\n");
	printf("Before the match, pick %d different powers.\n", LOADOUT_SIZE);
	printf("Each round, pick 1 of them. You can use it once that round,\n");
	printf("but ONLY with its move. Used or not, it is gone after the round.\n");
	printf("If no one is hit for too many turns, both players lose HP.\n");
	printf("\nPowers (move needed):\n");
	printf("  Heal          (any move) - restore some HP\n");
	printf("  Double Damage (Attack)   - hit twice as hard, even through Defend\n");
	printf("  Poison        (Attack)   - a landed hit poisons for a few turns\n");
	printf("  Double Defend (Defend)   - block all attack damage\n");
	printf("  Thorns        (Defend)   - an attacker who hits you bleeds\n");
	printf("  Guard Break   (Counter)  - your Counter hurts a plain Defend\n");
	printf("  Full Counter  (Counter)  - reflect double damage\n");
	printf("======================================================\n\n");
}

int pick_loadout(Power loadout[LOADOUT_SIZE])
{
	int slot;
	int previous;
	int choice;
	int duplicate;
	Power selected;

	for (slot = 0; slot < LOADOUT_SIZE; ++slot) {
		while (1) {
			printf("\nChoose power %d of %d:\n",
				   slot + 1, LOADOUT_SIZE);
			print_power_options();

			if (!read_choice("Choose a power: ",
							 1, PW_COUNT, &choice)) {
				return 0;
			}

			selected = (Power)(choice - 1);

			duplicate = 0;
			for (previous = 0; previous < slot; ++previous) {
				if (loadout[previous] == selected) {
					duplicate = 1;
				}
			}
			if (duplicate) {
				printf("Choose each power only once.\n");
				continue;
			}

			loadout[slot] = selected;
			break;
		}
	}
	return 1;
}

void print_hp_bar(const char *label, int hp, int change, int poison, int bleed)
{
	int i;
	const char *color = COLOR_GREEN;

	if (hp <= HP_RED_LEVEL) {
		color = COLOR_RED;
	} else if (hp <= HP_YELLOW_LEVEL) {
		color = COLOR_YELLOW;
	}
	printf("%-12s [", label);
	for (i = 0; i < MAX_HP / HP_PER_BAR_CHAR; ++i) {
		if (i * HP_PER_BAR_CHAR < hp) {
			printf("%s#" COLOR_RESET, color);
		} else {
			printf("-");
		}
	}
	printf("] %d/%d", hp, MAX_HP);
	if (change < 0) {
		printf(" " COLOR_RED "(%+d)" COLOR_RESET, change);
	} else if (change > 0) {
		printf(" " COLOR_GREEN "(%+d)" COLOR_RESET, change);
	}
	if (poison > 0) {
		printf("  " COLOR_YELLOW "POISONED (%d)" COLOR_RESET, poison);
	}
	if (bleed > 0) {
		printf("  " COLOR_YELLOW "BLEEDING (%d)" COLOR_RESET, bleed);
	}
	printf("\n");
}
