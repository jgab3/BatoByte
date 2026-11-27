#include "game.h"
#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

static void leave_match(int socket_fd, const char *message)
{
	printf("%s\n", message);
	close(socket_fd);
	exit(EXIT_FAILURE);
}

static void win_by_forfeit(int socket_fd)
{
	printf("Player 2 left the match. You win by forfeit!\n");
	close(socket_fd);
	exit(EXIT_SUCCESS);
}

static void send_or_die(int socket_fd, const char *line)
{
	if (!send_line(socket_fd, line)) {
		leave_match(socket_fd, "Player 2 disconnected during loadout selection.");
	}
}

static void send_or_forfeit(int socket_fd, const char *line)
{
	if (!send_line(socket_fd, line)) {
		win_by_forfeit(socket_fd);
	}
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

static int choose_move(const Game *game, int turn_number, Move *move, Power *use)
{
	const Player *me = &game->players[0];
	const Player *other = &game->players[1];
	int choice;
	int activate = 2;
	Move menu_move;

	if (turn_number == 1) {
		print_hp_bar("Your HP:", me->hp, 0, 0, 0);
		print_hp_bar("Player 2 HP:", other->hp, 0, 0, 0);
	}
	printf("Your round power: %s (use with %s, %s)\n",
		power_names[me->round_power], power_moves[me->round_power],
		me->power_activated ? "used" : "not used yet");
	for (menu_move = MOVE_ATTACK; menu_move < MOVE_COUNT; menu_move = (Move)(menu_move + 1)) {
		printf("%d. %s", menu_move + 1, move_names[menu_move]);
		if (me->round_power != PW_NONE && !me->power_activated &&
			power_matches_move(me->round_power, menu_move)) {
			printf("  (" COLOR_YELLOW "%s" COLOR_RESET " available)",
				power_names[me->round_power]);
		}
		printf("\n");
	}
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
	send_or_forfeit(client_sock, line);
}

static void print_choice(const char *who, Move move, Power power)
{
	printf("%s: %s", who, move_names[move]);
	if (power != PW_NONE) {
		printf(" + " COLOR_YELLOW "%s" COLOR_RESET, power_names[power]);
	}
}

static void print_result(const Move moves[PLAYER_COUNT], const Power use[PLAYER_COUNT],
	const int damage[PLAYER_COUNT], const int hp[PLAYER_COUNT],
	const int start_hp[PLAYER_COUNT], const int poison[PLAYER_COUNT],
	const int bleed[PLAYER_COUNT], int stall_penalty)
{
	int player;

	printf("\n");
	print_choice("You", moves[0], use[0]);
	printf("   vs   ");
	print_choice("Player 2", moves[1], use[1]);
	printf("\n");
	if (stall_penalty > 0) {
		printf(COLOR_RED "Too much stalling! Both players lose %d HP." COLOR_RESET "\n",
			stall_penalty);
	}
	for (player = 0; player < PLAYER_COUNT; ++player) {
		if (damage[player] > 0) {
			printf(COLOR_RED "%s took %d damage!" COLOR_RESET "\n",
				player == 0 ? "You" : "Player 2", damage[player]);
		}
		if (use[player] == PW_HEAL) {
			printf(COLOR_GREEN "%s healed %d HP." COLOR_RESET "\n",
				player == 0 ? "You" : "Player 2", HEAL_AMT);
		}
	}
	print_hp_bar("Your HP:", hp[0], hp[0] - start_hp[0], poison[0], bleed[0]);
	print_hp_bar("Player 2 HP:", hp[1], hp[1] - start_hp[1], poison[1], bleed[1]);
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
	int start_hp[PLAYER_COUNT];
	int match_winner = -1;
	int player;
	int need_power_pick = 1;
	int turn_number = 1;
	socklen_t client_size;
	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
	Move moves[PLAYER_COUNT];
	Power use[PLAYER_COUNT];
	Game game;
	Power loadouts[PLAYER_COUNT][LOADOUT_SIZE];
	TurnResult result;
	char line[MAX_MESSAGE_LENGTH];
	char turn_line[MAX_MESSAGE_LENGTH];

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

	if (!pick_loadout(loadouts[0])) {
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
		if (need_power_pick) {
			int power_choice;
			char extra;

			turn_number = 1;
			printf("\n==================== ROUND %d ====================\n",
				game.round_number);
			printf("Score: You %d - %d Player 2\n", game.players[0].round_wins,
				game.players[1].round_wins);
			snprintf(line, sizeof(line), "ROUND %d %d %d\n", game.round_number,
				game.players[0].round_wins, game.players[1].round_wins);
			send_or_forfeit(client_sock, line);
			send_pick_prompt(client_sock, &game);

			if (!choose_round_power(&game)) {
				leave_match(client_sock, "Input ended while choosing a power.");
			}

			printf("Waiting for Player 2 to choose a power ...\n");
			fflush(stdout);

			while (1) {
				if (!receive_line(client_sock, line, sizeof(line))) {
					win_by_forfeit(client_sock);
				}

				if (sscanf(line, "PICK %d %c", &power_choice, &extra) == 1 &&
					power_choice >= 1 && power_choice <= PW_COUNT &&
					game_pick_round_power(
						&game, 1, (Power)(power_choice - 1)) == GAME_OK) {
					send_or_forfeit(client_sock, "OK\n");
					break;
				}

				send_or_forfeit(client_sock, "RETRY\n");
				send_pick_prompt(client_sock, &game);
			}

			need_power_pick = 0;
		}
		start_hp[0] = game.players[0].hp;
		start_hp[1] = game.players[1].hp;

		snprintf(turn_line, sizeof(turn_line), "TURN %d %d %d %d %d %d\n",
			game.players[0].hp, game.players[1].hp,
			game.players[1].round_power + 1, game.players[1].power_activated,
			game.round_number, turn_number);

		if (!send_line(client_sock, turn_line)) {
			win_by_forfeit(client_sock);
		}
		printf("\n----- Round %d | Turn %d -----\n", game.round_number, turn_number);
		if (!choose_move(&game, turn_number, &moves[0], &use[0])) {
			leave_match(client_sock, "Input ended while choosing a move.");
		}
		printf("Waiting for Player 2 to choose a move ...\n");
		fflush(stdout);
		while (1) {
			if (!receive_line(client_sock, line, sizeof(line))) {
				win_by_forfeit(client_sock);
			}
			if (get_move(line, &moves[1], &use[1]) &&
				resolve_turn(&game, moves, use, &result) == GAME_OK) {
				break;
			}
			send_or_forfeit(client_sock, "RETRY\n");
			if (!send_line(client_sock, turn_line)) {
				win_by_forfeit(client_sock);
			}
		}
		++turn_number;

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

		print_result(moves, use, damage, hp, start_hp, poison, bleed, result.stall_penalty[0]);
		if (result.round_winner >= 0) {
			printf(COLOR_GREEN "%s won the round!" COLOR_RESET "\n",
				result.round_winner == 0 ? "You" : "Player 2");
		}
		if (result.outcome == TURN_MUTUAL_KO) {
			printf(COLOR_YELLOW "%s" COLOR_RESET "\n",
				"Double KO! No one wins this round. Replaying with full HP and powers restored.");
		}
		snprintf(line, sizeof(line), "RESULT %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
			moves[0], moves[1], use[0] + 1, use[1] + 1, damage[0], damage[1],
			hp[0], hp[1], result.round_winner + 1,
			poison[0], poison[1], bleed[0], bleed[1], result.stall_penalty[0]);
		send_or_forfeit(client_sock, line);
		if (match_winner >= 0) {
			snprintf(line, sizeof(line), "END %d %d %d\n", match_winner,
				game.players[0].round_wins, game.players[1].round_wins);
			send_line(client_sock, line);
			break;
		}
		send_or_forfeit(client_sock, "CONTINUE\n");
	}

	printf("\n=================================================\n");
	printf("%s the match! Final score: You %d - %d Player 2\n",
		match_winner == 1 ? "You win" : "Player 2 wins",
		game.players[0].round_wins, game.players[1].round_wins);
	printf("=================================================\n");
	printf("Closing connection ...\n");
	close(client_sock);
	return EXIT_SUCCESS;
}
