#include "shared.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

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

static int choose_loadout(int socket_fd, char *line, size_t line_size)
{
	Power loadout[LOADOUT_SIZE];

	while (1) {
		if (strcmp(line, "LOADOUT") != 0) {
			return 0;
		}

		if (!pick_loadout(loadout)) {
			return 0;
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

static int choose_move(int socket_fd, const char *line, int hp[PLAYER_COUNT])
{
	int round_number;
	int turn_number;
	int power_code;
	int activated;
	int move;
	int activation = 0;
	int choice;
	int menu_move;
	char response[MAX_MESSAGE_LENGTH];

	if (sscanf(line, "TURN %d %d %d %d %d %d", &hp[0], &hp[1], &power_code, &activated,
			&round_number, &turn_number) != 6 ||
		power_code < 0 || power_code > PW_COUNT ||
		(activated != 0 && activated != 1)) {
		return 0;
	}
	printf("\n----- Round %d | Turn %d -----\n", round_number, turn_number);
	if (turn_number == 1) {
		print_hp_bar("Player 1 HP:", hp[0], 0, 0, 0);
		print_hp_bar("Your HP:", hp[1], 0, 0, 0);
	}
	if (power_code > 0) {
		printf("Your round power: %s (use with %s, %s)\n",
			power_names[power_code - 1], power_moves[power_code - 1],
			activated ? "used" : "not used yet");
	}
	for (menu_move = 0; menu_move < MOVE_COUNT; ++menu_move) {
		printf("%d. %s", menu_move + 1, move_names[menu_move]);
		if (power_code > 0 && !activated &&
			power_matches_move((Power)(power_code - 1), (Move)menu_move)) {
			printf("  (" COLOR_YELLOW "%s" COLOR_RESET " available)",
				power_names[power_code - 1]);
		}
		printf("\n");
	}
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

static void print_choice(const char *who, int move, int power_code)
{
	printf("%s: %s", who, move_names[move]);
	if (power_code > 0) {
		printf(" + " COLOR_YELLOW "%s" COLOR_RESET, power_names[power_code - 1]);
	}
}

static int show_result(const char *line, const int start_hp[PLAYER_COUNT])
{
	int moves[PLAYER_COUNT];
	int power_code[PLAYER_COUNT];
	int damage[PLAYER_COUNT];
	int hp[PLAYER_COUNT];
	int poison[PLAYER_COUNT];
	int bleed[PLAYER_COUNT];
	int player;
	int round_winner;
	int stall_penalty;

	if (sscanf(line, "RESULT %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
			&moves[0], &moves[1], &power_code[0], &power_code[1],
			&damage[0], &damage[1], &hp[0], &hp[1], &round_winner,
			&poison[0], &poison[1], &bleed[0], &bleed[1], &stall_penalty) != 14 ||
		moves[0] < 0 || moves[0] >= MOVE_COUNT ||
		moves[1] < 0 || moves[1] >= MOVE_COUNT ||
		power_code[0] < 0 || power_code[0] > PW_COUNT ||
		power_code[1] < 0 || power_code[1] > PW_COUNT) {
		return 0;
	}

	printf("\n");
	print_choice("Player 1", moves[0], power_code[0]);
	printf("   vs   ");
	print_choice("You", moves[1], power_code[1]);
	printf("\n");
	if (stall_penalty > 0) {
		printf(COLOR_RED "Too much stalling! Both players lose %d HP." COLOR_RESET "\n",
			stall_penalty);
	}
	for (player = 0; player < PLAYER_COUNT; ++player) {
		if (damage[player] > 0) {
			printf(COLOR_RED "%s took %d damage!" COLOR_RESET "\n",
				player == 0 ? "Player 1" : "You", damage[player]);
		}
		if (power_code[player] == PW_HEAL + 1) {
			printf(COLOR_GREEN "%s healed %d HP." COLOR_RESET "\n",
				player == 0 ? "Player 1" : "You", HEAL_AMT);
		}
	}
	print_hp_bar("Player 1 HP:", hp[0], hp[0] - start_hp[0], poison[0], bleed[0]);
	print_hp_bar("Your HP:", hp[1], hp[1] - start_hp[1], poison[1], bleed[1]);
	if (round_winner > 0) {
		printf(COLOR_GREEN "%s won the round!" COLOR_RESET "\n",
			round_winner == 2 ? "You" : "Player 1");
	}
	if (hp[0] == 0 && hp[1] == 0) {
		printf(COLOR_YELLOW "%s" COLOR_RESET "\n",
			"Double KO! No one wins this round. Replaying with full HP and powers restored.");
	}
	return 1;
}

int main(int argc, char *argv[])
{
	int client_sock;
	int port_no;
	int player_number;
	int turn_hp[PLAYER_COUNT];
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
			int score[PLAYER_COUNT];

			if (sscanf(line, "ROUND %d %d %d", &round_number, &score[0], &score[1]) == 3) {
				printf("\n==================== ROUND %d ====================\n", round_number);
				printf("Score: You %d - %d Player 1\n", score[1], score[0]);
			}
		} else if (strncmp(line, "PICK ", 5) == 0) {
			if (!choose_round_power(client_sock, line, sizeof(line))) {
				break;
			}
		} else if (strncmp(line, "TURN ", 5) == 0) {
			if (!choose_move(client_sock, line, turn_hp)) {
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
			if (strncmp(line, "RESULT ", 7) != 0 || !show_result(line, turn_hp) ||
				!receive_line(client_sock, line, sizeof(line))) {
				break;
			}
			if (strcmp(line, "CONTINUE") == 0) {
				continue;
			}
			if (strncmp(line, "END ", 4) == 0) {
				int winner;
				int score[PLAYER_COUNT];

				if (sscanf(line, "END %d %d %d", &winner, &score[0], &score[1]) == 3) {
					printf("\n=================================================\n");
					printf("%s the match! Final score: You %d - %d Player 1\n",
						winner == 2 ? "You win" : "Player 1 wins", score[1], score[0]);
					printf("=================================================\n");
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
