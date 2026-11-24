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

static void send_or_die(int socket_fd, const char *line)
{
	if (!send_line(socket_fd, line)) {
		die_with_error("Error: send() Failed.");
	}
}

static void leave_match(int socket_fd, const char *message)
{
	printf("%s\n", message);
	close(socket_fd);
	exit(EXIT_FAILURE);
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

static int choose_loadout(Power loadout[LOADOUT_SIZE])
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

static int get_loadout(const char *line, Power loadout[LOADOUT_SIZE])
{
	int codes[LOADOUT_SIZE];
	int slot;
	int previous;
	char extra;
	Power selected;

	if (sscanf(line, "LOADOUT %d %d %d %c",
			   &codes[0], &codes[1], &codes[2], &extra) != 3) {
		return 0;
	}

	for (slot = 0; slot < LOADOUT_SIZE; ++slot) {
		if (codes[slot] < 1 || codes[slot] > PW_COUNT) {
			return 0;
		}

		selected = (Power)(codes[slot] - 1);

		for (previous = 0; previous < slot; ++previous) {
			if (loadout[previous] == selected) {
				return 0;
			}
		}
		loadout[slot] = selected;
	}

	return 1;
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

static int choose_move(const Game *game, Move *move, Power *use)
{
	const Player *me = &game->players[0];
	const Player *other = &game->players[1];
	int choice;
	int activate = 2;

	printf("\nRound wins: You: %d | Player 2: %d\n", me->round_wins, other->round_wins);
	print_hp_bar("Your HP:", me->hp);
	print_hp_bar("Player 2 HP:", other->hp);
	printf("Your round power: %s (use with %s, %s)\n",
		power_names[me->round_power], power_moves[me->round_power],
		me->power_activated ? "used" : "not used yet");
	printf("1. Attack\n2. Defend\n3. Counter\n");
	if (!read_choice("Choose your move: ", 1, MOVE_COUNT, &choice)) {
		return 0;
	}
	*move = (Move)(choice - 1);
	*use = PW_NONE;
	if (me->round_power != PW_NONE && !me->power_activated &&
		power_matches_move(me->round_power, *move)) {
		if (!read_choice("Activate your power this turn? (1 yes, 2 no): ", 1, 2, &activate)) {
			return 0;
		}
		if (activate == 1) {
			*use = me->round_power;
		}
	}
	return 1;
}

static int get_move(const char *line, Move *move, Power *use)
{
	int move_choice;
	int power_code;
	char extra;

	if (sscanf(line, "MOVE %d %d %c", &move_choice, &power_code, &extra) != 2 ||
		move_choice < 1 || move_choice > MOVE_COUNT ||
		power_code < 0 || power_code > PW_COUNT) {
		return 0;
	}
	*move = (Move)(move_choice - 1);
	*use = power_code == 0 ? PW_NONE : (Power)(power_code - 1);
	return 1;
}

static int choose_round_power(Game *game)
{
	int slot;
	int choice;
	Power available;

	printf("\nChoose your power for this round:\n");
	for (slot = 0; slot < LOADOUT_SIZE; ++slot) {
		available = game->players[0].remaining_powers[slot];
		if (available != PW_NONE) {
			printf("%d. %s\n", available + 1, power_names[available]);
		}
	}

	while (1) {
		if (!read_choice("Choose a power: ", 1, PW_COUNT, &choice)) {
			return 0;
		}
		if (game_pick_round_power(
				game, 0, (Power)(choice - 1)) == GAME_OK) {
			return 1;
		}
		printf("That power is unavailable. Choose one shown above.\n");
	}
}

static void send_pick_prompt(int client_sock, const Game *game)
{
	const Player *client = &game->players[1];
	char line[MAX_MESSAGE_LENGTH];

	snprintf(line, sizeof(line), "PICK %d %d %d\n",
		client->remaining_powers[0] + 1, client->remaining_powers[1] + 1,
		client->remaining_powers[2] + 1);
	send_or_die(client_sock, line);
}

static void print_result(const Move moves[PLAYER_COUNT], const Power use[PLAYER_COUNT],
	const int damage[PLAYER_COUNT], const int hp[PLAYER_COUNT],
	const int poison[PLAYER_COUNT], const int bleed[PLAYER_COUNT])
{
	int player;

	printf("\nYou used %s!\n", move_names[moves[0]]);
	printf("Player 2 used %s!\n", move_names[moves[1]]);
	for (player = 0; player < PLAYER_COUNT; ++player) {
		if (damage[player] > 0) {
			printf("%s took %d damage.\n", player == 0 ? "You" : "Player 2", damage[player]);
		}
		if (use[player] != PW_NONE) {
			printf("%s activated their power.\n", player == 0 ? "You" : "Player 2");
		}

		printf("%s HP: %d\n", player == 0 ? "Your" : "Player 2", hp[player]);

		if (poison[player] > 0) {
			printf("%s poisoned for %d more turn(s).\n",
				player == 0 ? "You are" : "Player 2 is", poison[player]);
		}
		if (bleed[player] > 0) {
			printf("%s bleeding for %d more turn(s).\n",
				player == 0 ? "You are" : "Player 2 is", bleed[player]);
		}
	}
}

int main(int argc, char *argv[])
{
	int server_sock;
	int client_sock;
	int port_no;
	int option = 1;
	int hp[PLAYER_COUNT];
	int damage[PLAYER_COUNT];
	int poison[PLAYER_COUNT];
	int bleed[PLAYER_COUNT];
	int match_winner = -1;
	int player;
	int need_power_pick = 1;
	socklen_t client_size;
	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
	Move moves[PLAYER_COUNT];
	Power use[PLAYER_COUNT];
	Game game;
	Power loadouts[PLAYER_COUNT][LOADOUT_SIZE];
	TurnResult result;
	char line[MAX_MESSAGE_LENGTH];

	if (argc != 2) {
		printf("Usage: %s port_no\n", argv[0]);
		return EXIT_FAILURE;
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
	print_how_to_play();
	send_or_die(client_sock, "LOADOUT\n");

	if (!choose_loadout(loadouts[0])) {
		leave_match(client_sock, "Input ended while choosing a loadout.");
	}

	while (1) {
		printf("Waiting for Player 2 to choose a loadout ...\n");
		fflush(stdout);

		if (!receive_line(client_sock, line, sizeof(line))) {
			leave_match(client_sock, "Player 2 disconnected during loadout selection.");
		}

		if (get_loadout(line, loadouts[1]) && game_init(&game, loadouts) == GAME_OK) {
			send_or_die(client_sock, "OK\n");
			break;
		}

		send_or_die(client_sock, "RETRY\n");
		send_or_die(client_sock, "LOADOUT\n");
	}

	send_or_die(client_sock, "READY 2\n");

	while (match_winner < 0) {
		while (1) {

			if (need_power_pick) {
				int power_choice;
				char extra;

				printf("\nRound %d begins!\n", game.round_number);
				snprintf(line, sizeof(line), "ROUND %d\n", game.round_number);
				send_or_die(client_sock, line);
				send_pick_prompt(client_sock, &game);

				if (!choose_round_power(&game)) {
					leave_match(client_sock, "Input ended while choosing a power.");
				}

				printf("Waiting for Player 2 to choose a power ...\n");
				fflush(stdout);

				while (1) {
					if (!receive_line(client_sock, line, sizeof(line))) {
						leave_match(client_sock, "Player 2 left the match. You win by forfeit!");
					}

					if (sscanf(line, "PICK %d %c", &power_choice, &extra) == 1 &&
						power_choice >= 1 && power_choice <= PW_COUNT &&
						game_pick_round_power(
							&game, 1, (Power)(power_choice - 1)) == GAME_OK) {
						send_or_die(client_sock, "OK\n");
						break;
					}

					send_or_die(client_sock, "RETRY\n");
					send_pick_prompt(client_sock, &game);
				}

				need_power_pick = 0;
			}

			snprintf(line, sizeof(line), "TURN %d %d %d %d %d %d\n",
				game.players[0].hp, game.players[1].hp,
				game.players[1].round_power + 1, game.players[1].power_activated,
				game.players[0].round_wins, game.players[1].round_wins);

			if (!send_line(client_sock, line)) {
				leave_match(client_sock, "Player 2 left the match. You win by forfeit!");
			}
			if (!choose_move(&game, &moves[0], &use[0])) {
				leave_match(client_sock, "Input ended while choosing a move.");
			}
			printf("Waiting for Player 2 to choose a move ...\n");
			fflush(stdout);
			if (!receive_line(client_sock, line, sizeof(line))) {
				leave_match(client_sock, "Player 2 left the match. You win by forfeit!");
			}
			if (!get_move(line, &moves[1], &use[1]) ||
				resolve_turn(&game, moves, use, &result) != GAME_OK) {
				send_or_die(client_sock, "RETRY\n");
				continue;
			}
			break;
		}

		for (player = 0; player < PLAYER_COUNT; ++player) {
			damage[player] = result.tick_damage[player] + result.move_damage[player] +
				result.stall_penalty[player];
			hp[player] = result.final_hp[player];
			poison[player] = game.players[player].poison_turns;
			bleed[player] = game.players[player].bleed_turns;
		}

		if (result.outcome == TURN_MATCH_WON) {
			match_winner = result.round_winner + 1;
		}

		if (result.outcome == TURN_ROUND_WON || result.outcome == TURN_MUTUAL_KO) {
			need_power_pick = 1;
		}

		print_result(moves, use, damage, hp, poison, bleed);
		if (result.round_winner >= 0) {
			printf("%s won the round!\n", result.round_winner == 0 ? "You" : "Player 2");
		}
		if (result.outcome == TURN_MUTUAL_KO) {
			printf("Double KO! No one wins this round. Replaying with full HP and powers restored.\n");
		}
		snprintf(line, sizeof(line), "RESULT %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
			moves[0], moves[1], use[0] + 1, use[1] + 1, damage[0], damage[1],
			hp[0], hp[1], result.round_winner + 1,
			poison[0], poison[1], bleed[0], bleed[1]);
		send_or_die(client_sock, line);
		if (match_winner >= 0) {
			snprintf(line, sizeof(line), "END %d\n", match_winner);
			send_or_die(client_sock, line);
			break;
		}
		send_or_die(client_sock, "CONTINUE\n");
	}

	printf("%s the match!\n", match_winner == 1 ? "You win" : "Player 2 wins");
	printf("Closing connection ...\n");
	close(client_sock);
	return EXIT_SUCCESS;
}
