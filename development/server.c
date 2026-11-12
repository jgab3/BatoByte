#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

static const char *move_names[MOVE_COUNT] = {
	"Attack", "Defend", "Counter"
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

static int choose_move(const int hp[PLAYER_COUNT], const int round_wins[PLAYER_COUNT], int heal_available, Move *move, int *heal)
{
	int choice;
	int heal_choice;

	printf("\nRound wins: You: %d | Player 2: %d\n", round_wins[0], round_wins[1]);
	printf("Your HP: %d | Player 2 HP: %d\n", hp[0], hp[1]);
	printf("1. Attack\n2. Defend\n3. Counter\n");
	if (!read_choice("Choose your move: ", 1, MOVE_COUNT, &choice)) {
		return 0;
	}
	*move = (Move)(choice - 1);
	*heal = 0;
	if (heal_available) {
		if (!read_choice("Use Heal this turn? (1 yes, 2 no): ", 1, 2,
					 &heal_choice)) {
			return 0;
		}
		*heal = heal_choice == 1;
	}
	return 1;
}

static int get_move(const char *line, Move *move, int *heal)
{
	int move_choice;
	int heal_choice;
	char extra;

	if (sscanf(line, "MOVE %d %d %c", &move_choice, &heal_choice, &extra) != 2 ||
		move_choice < 1 || move_choice > MOVE_COUNT ||
		(heal_choice != 0 && heal_choice != 1)) {
		return 0;
	}
	*move = (Move)(move_choice - 1);
	*heal = heal_choice;
	return 1;
}

static void print_result(const Move moves[PLAYER_COUNT],
						 const int heal[PLAYER_COUNT],
						 const int damage[PLAYER_COUNT],
						 const int hp[PLAYER_COUNT])
{
	int player;

	printf("\nYou used %s!\n", move_names[moves[0]]);
	printf("Player 2 used %s!\n", move_names[moves[1]]);
	for (player = 0; player < PLAYER_COUNT; ++player) {
		if (damage[player] > 0) {
			printf("%s took %d damage.\n", player == 0 ? "You" : "Player 2", damage[player]);
		}
		if (heal[player]) {
			printf("%s used Heal.\n", player == 0 ? "You" : "Player 2");
		}
		printf("%s HP: %d\n", player == 0 ? "Your" : "Player 2", hp[player]);
	}
}

int main(int argc, char *argv[])
{
	int server_sock;
	int client_sock;
	int port_no;
	int option = 1;
	int length;
	int hp[PLAYER_COUNT];
	int heal[PLAYER_COUNT];
	int damage[PLAYER_COUNT];
	int round_wins[PLAYER_COUNT];
	int match_winner = -1;
	int player;
	socklen_t client_size;
	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
	Move moves[PLAYER_COUNT];
	Power use[PLAYER_COUNT];
	Game game;
	TurnResult result;
	GameError game_error;
	char line[MAX_MESSAGE_LENGTH];

	if (argc != 2) {
		printf("Usage: %s port_no\n", argv[0]);
		return EXIT_FAILURE;
	}
	if (game_init_basic(&game) != GAME_OK) {
		die_with_error("Error: game initialization failed.");
	}
	for (player = 0; player < PLAYER_COUNT; ++player) {
		if (game_pick_round_power(&game, player, PW_HEAL) != GAME_OK) {
			die_with_error("Error: could not select Heal.");
		}
	}

	printf("Server starting ...\n");
	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		die_with_error("Error: socket() Failed.");
	}
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &option,
			   sizeof(option)) < 0) {
		die_with_error("Error: setsockopt() Failed.");
	}
	bzero((char *)&server_addr, sizeof(server_addr));
	port_no = atoi(argv[1]);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons((unsigned short)port_no);
	if (bind(server_sock, (struct sockaddr *)&server_addr,
			 sizeof(server_addr)) < 0) {
		die_with_error("Error: bind() Failed.");
	}
	if (listen(server_sock, 1) < 0) {
		die_with_error("Error: listen() Failed.");
	}
	printf("Server listening to port %d ...\n", port_no);
	printf("Waiting for Player 2 to connect ...\n");

	client_size = sizeof(client_addr);
	client_sock = accept(server_sock, (struct sockaddr *)&client_addr,
					 &client_size);
	if (client_sock < 0) {
		die_with_error("Error: accept() Failed.");
	}
	printf("Player 2 successfully connected ...\n");
	close(server_sock);
	if (!send_line(client_sock, "READY 2\n")) {
		die_with_error("Error: send() Failed.");
	}

	while (match_winner < 0) {
		while (1) {
			length = snprintf(line, sizeof(line), "TURN %d %d %d %d %d\n", game.players[0].hp, game.players[1].hp, !game.players[1].power_activated, game.players[0].round_wins, game.players[1].round_wins);
			
			hp[0] = game.players[0].hp;
			hp[1] = game.players[1].hp;

			round_wins[0] = game.players[0].round_wins;
			round_wins[1] = game.players[1].round_wins;

			if (length < 0 || (size_t)length >= sizeof(line) ||
				!send_line(client_sock, line) ||
				!choose_move(hp, round_wins, !game.players[0].power_activated, &moves[0], &heal[0])) {
				printf("Input ended or Player 2 disconnected.\n");
				close(client_sock);
				return EXIT_FAILURE;
			}
			printf("Waiting for Player 2 to choose a move ...\n");
			fflush(stdout);
			if (!receive_line(client_sock, line, sizeof(line))) {
				printf("Player 2 disconnected.\n");
				close(client_sock);
				return EXIT_FAILURE;
			}
			if (!get_move(line, &moves[1], &heal[1]) ||
				(heal[1] && game.players[1].power_activated)) {
				if (!send_line(client_sock, "RETRY\n")) {
					die_with_error("Error: send() Failed.");
				}
				continue;
			}
			break;
		}

		for (player = 0; player < PLAYER_COUNT; ++player) {
			use[player] = heal[player] ? PW_HEAL : PW_NONE;
		}
		game_error = resolve_turn(&game, moves, use, &result);
		if (game_error != GAME_OK) {
			if (!send_line(client_sock, "RETRY\n")) {
				die_with_error("Error: send() Failed.");
			}
			continue;
		}
		for (player = 0; player < PLAYER_COUNT; ++player) {
			damage[player] = result.tick_damage[player] +
						 result.move_damage[player] +
						 result.stall_penalty[player];
			hp[player] = result.final_hp[player];
		}

		if (result.outcome == TURN_MATCH_WON) {
			match_winner = result.round_winner + 1;
		} else if (result.outcome == TURN_ROUND_WON) {
			for (player = 0; player < PLAYER_COUNT; ++player) {
				game.players[player].remaining_powers[0] = PW_HEAL;
			}
		}

		if (result.outcome == TURN_ROUND_WON ||
			result.outcome == TURN_MUTUAL_KO) {
			for (player = 0; player < PLAYER_COUNT; ++player) {
				if (game_pick_round_power(&game, player, PW_HEAL) != GAME_OK) {
					die_with_error("Error: could not select Heal.");
				}
			}
		}

		print_result(moves, heal, damage, hp);
		if (result.round_winner >= 0) {
			printf("Player %d won the round!\n", result.round_winner + 1);
		}
		length = snprintf(line, sizeof(line), "RESULT %d %d %d %d %d %d %d %d %d\n", moves[0], moves[1], heal[0], heal[1], damage[0], damage[1], hp[0], hp[1], result.round_winner + 1);
		if (length < 0 || (size_t)length >= sizeof(line) ||
			!send_line(client_sock, line)) {
			die_with_error("Error: send() Failed.");
		}
		if (match_winner >= 0) {
			length = snprintf(line, sizeof(line), "END %d\n", match_winner);
			if (length < 0 || (size_t)length >= sizeof(line) ||
				!send_line(client_sock, line)) {
				die_with_error("Error: send() Failed.");
			}
			break;
		}
		if (!send_line(client_sock, "CONTINUE\n")) {
			die_with_error("Error: send() Failed.");
		}
	}

	if (match_winner == 0) {
		printf("The match ends in a draw.\n");
	} else {
		printf("Player %d wins the match!\n", match_winner);
	}
	printf("Closing connection ...\n");
	close(client_sock);
	return EXIT_SUCCESS;
}
