#include "common.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

static const char *move_names[MOVE_COUNT] = {
	"Attack", "Defend", "Counter"
};

static const char *power_names[PW_COUNT] = {
	"Heal",
	"Double Damage",
	"Poison",
	"Double Defend",
	"Thorns",
	"Guard Break",
	"Full Counter"
};

static const char *power_moves[PW_COUNT] = {
	"any move",
	"Attack",
	"Attack",
	"Defend",
	"Defend",
	"Counter",
	"Counter"
};

static void die_with_error(const char *error_msg)
{
	printf("%s\n", error_msg);
	exit(EXIT_FAILURE);
}

static int send_line(int socket_fd, const char *line)
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

static int receive_line(int socket_fd, char *line, size_t size)
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

static int read_choice(const char *prompt, int minimum, int maximum,
					   int *choice)
{
	char input[100];
	char *end;
	long number;

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

static int power_matches_move(Power power, Move move)
{
	switch (power) {
	case PW_DOUBLE_DAMAGE:
	case PW_POISON:
		return move == MOVE_ATTACK;
	case PW_DOUBLE_DEFEND:
	case PW_THORNS:
		return move == MOVE_DEFEND;
	case PW_GUARD_BREAK:
	case PW_FULL_COUNTER:
		return move == MOVE_COUNTER;
	default:
		return 1; /* Heal works with any move. */
	}
}

static void print_power_options(void)
{
	Power power;

	for (power = PW_HEAL; power < PW_COUNT; power = (Power)(power + 1)) {
		printf("%d. %s\n", power + 1, power_names[power]);
	}
}

static void print_how_to_play(void)
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

static int choose_loadout(int socket_fd, char *line, size_t line_size)
{
	Power loadout[LOADOUT_SIZE];
	Power selected;
	int slot;
	int previous;
	int choice;
	int duplicate;

	while (1) {
		if (strcmp(line, "LOADOUT") != 0) {
			return 0;
		}

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

		snprintf(line, line_size, "LOADOUT %d %d %d\n",
				 loadout[0] + 1, loadout[1] + 1, loadout[2] + 1);
		if (!send_line(socket_fd, line)) {
			return 0;
		}

		printf("Waiting for Player 1 to choose a loadout ...\n");
		fflush(stdout);

		if (!receive_line(socket_fd, line, line_size)) {
			return 0;
		}

		if (strcmp(line, "OK") == 0) {
			return 1;
		}
		if (strcmp(line, "RETRY") != 0 ||
			!receive_line(socket_fd, line, line_size)) {
			return 0;
		}
	}
}

static int choose_round_power(int socket_fd, char *line, size_t line_size)
{
	int available[LOADOUT_SIZE];
	int choice;
	int slot;
	int found;

	while (1) {
		if (sscanf(line, "PICK %d %d %d", &available[0], &available[1], &available[2]) != 3) {
			return 0;
		}

		for (slot = 0; slot < LOADOUT_SIZE; ++slot) {
			if (available[slot] < 0 || available[slot] > PW_COUNT) {
				return 0;
			}
		}

		printf("\nChoose your power for this round:\n");
		for (slot = 0; slot < LOADOUT_SIZE; ++slot) {
			if (available[slot] > 0) {
				printf("%d. %s\n", available[slot], power_names[available[slot] - 1]);
			}
		}

		if (!read_choice("Choose this round's power: ", 1, PW_COUNT, &choice)) {
			return 0;
		}

		found = 0;
		for (slot = 0; slot < LOADOUT_SIZE; ++slot) {
			if (available[slot] == choice) {
				found = 1;
			}
		}
		if (!found) {
			printf("Choose one of the powers shown above.\n");
			continue;
		}

		snprintf(line, line_size, "PICK %d\n", choice);
		if (!send_line(socket_fd, line)) {
			return 0;
		}

		printf("Waiting for Player 1 to finish choosing a power ...\n");
		fflush(stdout);

		if (!receive_line(socket_fd, line, line_size)) {
			printf("Player 1 left the match. You win by forfeit!\n");
			return 0;
		}

		if (strcmp(line, "OK") == 0) {
			return 1;
		}
		if (strcmp(line, "RETRY") != 0 ||
			!receive_line(socket_fd, line, line_size)) {
			return 0;
		}
	}
}

static void print_hp_bar(const char *label, int hp)
{
	int i;

	printf("%-12s [", label);
	for (i = 0; i < MAX_HP / HP_PER_BAR_CHAR; ++i) {
		if (i * HP_PER_BAR_CHAR < hp) {
			printf("#");
		} else {
			printf("-");
		}
	}
	printf("] %d/%d\n", hp, MAX_HP);
}

static int choose_move(int socket_fd, const char *line)
{
	int hp[PLAYER_COUNT];
	int round_wins[PLAYER_COUNT];
	int power_code;
	int activated;
	int move;
	int activation = 0;
	int choice;
	char response[MAX_MESSAGE_LENGTH];

	if (sscanf(line, "TURN %d %d %d %d %d %d", &hp[0], &hp[1], &power_code, &activated,
			&round_wins[0], &round_wins[1]) != 6 ||
		power_code < 0 || power_code > PW_COUNT ||
		(activated != 0 && activated != 1)) {
		return 0;
	}
	printf("\nRound wins: Player 1: %d | You: %d\n", round_wins[0], round_wins[1]);
	print_hp_bar("Player 1 HP:", hp[0]);
	print_hp_bar("Your HP:", hp[1]);
	if (power_code > 0) {
		printf("Your round power: %s (use with %s, %s)\n",
			power_names[power_code - 1], power_moves[power_code - 1],
			activated ? "used" : "not used yet");
	}
	printf("1. Attack\n2. Defend\n3. Counter\n");
	if (!read_choice("Choose your move: ", 1, MOVE_COUNT, &choice)) {
		return 0;
	}
	move = choice;
	if (power_code > 0 && !activated &&
		power_matches_move((Power)(power_code - 1), (Move)(move - 1))) {
		if (!read_choice("Activate your power this turn? (1 yes, 2 no): ", 1, 2, &choice)) {
			return 0;
		}
		if (choice == 1) {
			activation = power_code;
		}
	}
	snprintf(response, sizeof(response), "MOVE %d %d\n", move, activation);
	if (!send_line(socket_fd, response)) {
		return 0;
	}
	printf("Waiting for Player 1 to choose a move ...\n");
	fflush(stdout);
	return 1;
}

static int show_result(const char *line)
{
	int moves[PLAYER_COUNT];
	int power_code[PLAYER_COUNT];
	int damage[PLAYER_COUNT];
	int hp[PLAYER_COUNT];
	int poison[PLAYER_COUNT];
	int bleed[PLAYER_COUNT];
	int player;
	int round_winner;

	if (sscanf(line, "RESULT %d %d %d %d %d %d %d %d %d %d %d %d %d",
			&moves[0], &moves[1], &power_code[0], &power_code[1],
			&damage[0], &damage[1], &hp[0], &hp[1], &round_winner,
			&poison[0], &poison[1], &bleed[0], &bleed[1]) != 13 ||
		moves[0] < 0 || moves[0] >= MOVE_COUNT ||
		moves[1] < 0 || moves[1] >= MOVE_COUNT ||
		power_code[0] < 0 || power_code[0] > PW_COUNT ||
		power_code[1] < 0 || power_code[1] > PW_COUNT) {
		return 0;
	}

	printf("\nPlayer 1 used %s!\n", move_names[moves[0]]);
	printf("You used %s!\n", move_names[moves[1]]);
	for (player = 0; player < PLAYER_COUNT; ++player) {
		if (damage[player] > 0) {
			printf("%s took %d damage.\n", player == 0 ? "Player 1" : "You",
				   damage[player]);
		}
		if (power_code[player] > 0) {
			printf("%s activated their power.\n", player == 0 ? "Player 1" : "You");
		}

		printf("%s HP: %d\n", player == 0 ? "Player 1" : "Your", hp[player]);

		if (poison[player] > 0) {
			printf("%s poisoned for %d more turn(s).\n",
				player == 0 ? "Player 1 is" : "You are", poison[player]);
		}
		if (bleed[player] > 0) {
			printf("%s bleeding for %d more turn(s).\n",
				player == 0 ? "Player 1 is" : "You are", bleed[player]);
		}
	}
	if (round_winner > 0) {
		printf("Player %d won the round!\n", round_winner);
	}
	if (hp[0] == 0 && hp[1] == 0) {
		printf("Double KO! No one wins this round. Replaying with full HP and powers restored.\n");
	}
	return 1;
}

int main(int argc, char *argv[])
{
	int client_sock;
	int port_no;
	int player_number;
	struct sockaddr_in server_addr;
	struct hostent *server;
	char line[MAX_MESSAGE_LENGTH];

	if (argc != 3) {
		fprintf(stderr, "Usage: %s hostname port_no\n", argv[0]);
		return EXIT_FAILURE;
	}
	printf("Client starting ...\n");
	client_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (client_sock < 0) {
		die_with_error("Error: socket() Failed.");
	}
	printf("Looking for host '%s'...\n", argv[1]);
	server = gethostbyname(argv[1]);
	if (server == NULL) {
		die_with_error("Error: No such host.");
	}
	printf("Host found!\n");
	port_no = atoi(argv[2]);
	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr_list[0],
		  (char *)&server_addr.sin_addr.s_addr, server->h_length);
	server_addr.sin_port = htons((unsigned short)port_no);
	printf("Connecting to server at port %d...\n", port_no);
	if (connect(client_sock, (struct sockaddr *)&server_addr,
				sizeof(server_addr)) < 0) {
		die_with_error("Error: connect() Failed.");
	}
	printf("Connection successful!\n");
	print_how_to_play();
	if (!receive_line(client_sock, line, sizeof(line)) || strcmp(line, "LOADOUT") != 0 ||
		!choose_loadout(client_sock, line, sizeof(line)) ||
		!receive_line(client_sock, line, sizeof(line)) ||
		sscanf(line, "READY %d", &player_number) != 1) {
		printf("Server did not complete loadout selection.\n");
		close(client_sock);
		return EXIT_FAILURE;
	}
	printf("Connected as Player %d.\n", player_number);

	while (1) {
		if (!receive_line(client_sock, line, sizeof(line))) {
			printf("Player 1 left the match. You win by forfeit!\n");
			break;
		}
		if (strncmp(line, "ROUND ", 6) == 0) {
			int round_number;

			if (sscanf(line, "ROUND %d", &round_number) == 1) {
				printf("\nRound %d begins!\n", round_number);
			}
		} else if (strncmp(line, "PICK ", 5) == 0) {
			if (!choose_round_power(client_sock, line, sizeof(line))) {
				break;
			}
		} else if (strncmp(line, "TURN ", 5) == 0) {
			if (!choose_move(client_sock, line)) {
				break;
			}
			if (!receive_line(client_sock, line, sizeof(line))) {
				printf("Player 1 left the match. You win by forfeit!\n");
				break;
			}
			if (strcmp(line, "RETRY") == 0) {
				printf("Invalid choice. Choose again.\n");
				continue;
			}
			if (strncmp(line, "RESULT ", 7) != 0 || !show_result(line) ||
				!receive_line(client_sock, line, sizeof(line))) {
				break;
			}
			if (strcmp(line, "CONTINUE") == 0) {
				continue;
			}
			if (strncmp(line, "END ", 4) == 0) {
				int winner;
				if (sscanf(line, "END %d", &winner) == 1) {
					printf("Player %d wins the match!\n", winner);
				}
				break;
			}
			break;
		}
	}
	printf("Closing client connection ...\n");
	close(client_sock);
	return EXIT_SUCCESS;
}
