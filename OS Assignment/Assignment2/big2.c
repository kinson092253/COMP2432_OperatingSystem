#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

#define MAX_CARDS 52
#define CARD_LEN 3
#define RESPONSE_LEN 9 /* 足以容納 "COMPLETE" */

typedef struct {
    char command[6]; // 儲存 "PLAY", "FIRST", "LEAD", etc.
    char card[CARD_LEN]; // 儲存卡牌字串，例如 "D3"
} Message;

int is_duplicate(cards, count, card)
char cards[MAX_CARDS][CARD_LEN];
int count;
char *card;
{
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(cards[i], card) == 0) {
            return 1;
        }
    }
    return 0;
}

void shuffle_cards(cards, count)
char cards[MAX_CARDS][CARD_LEN];
int count;
{
    char temp[CARD_LEN];
    int i;
    int j;

    srand((unsigned int)time(NULL));
    for (i = count - 1; i > 0; i--) {
        j = rand() % (i + 1);
        strcpy(temp, cards[i]);
        strcpy(cards[i], cards[j]);
        strcpy(cards[j], temp);
    }
}

int get_card_value(card)
char *card;
{
    int value;
    int suit;

    /* 計算點數：3 < 4 < 5 < 6 < 7 < 8 < 9 < T < J < Q < K < A < 2 */
    switch (card[1]) {
        case '3': value = 3; break;
        case '4': value = 4; break;
        case '5': value = 5; break;
        case '6': value = 6; break;
        case '7': value = 7; break;
        case '8': value = 8; break;
        case '9': value = 9; break;
        case 'T': value = 10; break;
        case 'J': value = 11; break;
        case 'Q': value = 12; break;
        case 'K': value = 13; break;
        case 'A': value = 14; break;
        case '2': value = 15; break;
        default: value = 0; break;
    }

    /* 計算花色：D < C < H < S */
    switch (card[0]) {
        case 'D': suit = 1; break;
        case 'C': suit = 2; break;
        case 'H': suit = 3; break;
        case 'S': suit = 4; break;
        default: suit = 0; break;
    }

    /* 總價值：點數 * 10 + 花色 */
    return value * 10 + suit;
}

int find_min_card_index(cards, card_count)
char **cards;
int card_count;
{
    int i;
    int min_value;
    int min_index;

    min_value = 9999;
    min_index = 0;
    for (i = 0; i < card_count; i++) {
        int card_value = get_card_value(cards[i]);
        if (card_value < min_value) {
            min_value = card_value;
            min_index = i;
        }
    }
    return min_index;
}

int find_largest_card_index_greater_than(char **cards, int card_count, int target_value) {
    int i;
    int min_value = 9999;  // 設為大數，尋找最小的較大值
    int min_index = -1;

    for (i = 0; i < card_count; i++) {
        int card_value = get_card_value(cards[i]);

        if (card_value > target_value && card_value < min_value) {
            min_value = card_value;
            min_index = i;
        }
    }
    return min_index;
}


int main(argc, argv)
int argc;
char *argv[];
{
    int num_players;
    char cards[MAX_CARDS][CARD_LEN];
    int card_count;
    char temp[CARD_LEN];
    int base_cards;
    int remaining;
    int player_cards[52]; /* 最多 52 個玩家 */
    char *player_hands[52][MAX_CARDS];
    int current_card;
    char used_cards[MAX_CARDS][CARD_LEN];
    int used_count;
    int pipe_to_child[52][2];
    int pipe_from_child[52][2];
    int pids[52];
    int start_player;
    int current_player;
    int last_played_player;
    int completed[52];
    int remaining_players;
    int i;
    int j;
    int k;
    int hand_index;
    char **my_cards;
    int my_card_count;
    char cmd[6];
    char card[CARD_LEN];
    char response[RESPONSE_LEN + 1];
    int n;
    int pass_count;
    int last_card_value;
    char last_card[CARD_LEN];
    int round;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <number of players>\n", argv[0]);
        return 1;
    }

    num_players = atoi(argv[1]);
    if (num_players <= 0 || num_players > 52) {
        fprintf(stderr, "Invalid number of players! Must be between 1 and 52.\n");
        return 1;
    }

    card_count = 0;
    while (scanf("%s", temp) != EOF && card_count < MAX_CARDS) {
        if (is_duplicate(cards, card_count, temp)) {
            printf("Parent: duplicated card %s is discarded\n", temp);
            continue;
        }
        strcpy(cards[card_count], temp);
        card_count++;
    }


    for (i = 0; i < num_players; i++) {
        player_cards[i] = 0;
    }
    current_card = 0;
    used_count = 0;

    // 當還有牌時依序發牌給對應的 child
    while (current_card < card_count) {
        int player = current_card % num_players;  // 決定該牌發給哪個 child
        // 檢查是否為重複牌
        if (is_duplicate(used_cards, used_count, cards[current_card])) {
            printf("Child %d discards duplicated card %s\n", player + 1, cards[current_card]);
            current_card++;
            continue;
        }
        // 記錄該牌已使用
        strcpy(used_cards[used_count], cards[current_card]);
        used_count++;

        // 將牌加入該 child 的手牌中
        int index = player_cards[player];
        player_hands[player][index] = malloc(CARD_LEN);
        strcpy(player_hands[player][index], cards[current_card]);
        player_cards[player]++;

        current_card++;
    }

    for (i = 0; i < num_players; i++) {
        if (pipe(pipe_to_child[i]) == -1 || pipe(pipe_from_child[i]) == -1) {
            perror("pipe");
            exit(1);
        }
    }

    for (i = 0; i < num_players; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            close(pipe_to_child[i][1]);
            close(pipe_from_child[i][0]);

            printf("Child %d, pid %d: I have %d cards\n", i + 1, getpid(), player_cards[i]);
            printf("Child %d, pid %d: ", i + 1, getpid());
            for (j = 0; j < player_cards[i]; j++) {
                printf("%s ", player_hands[i][j]);
            }
            printf("\n");
            fflush(stdout);

            my_cards = malloc(player_cards[i] * sizeof(char *));
            for (j = 0; j < player_cards[i]; j++) {
                my_cards[j] = strdup(player_hands[i][j]);
            }
            my_card_count = player_cards[i];

            while (1) {
                Message msg;
                n = read(pipe_to_child[i][0], &msg, sizeof(Message));
                if(n <= 0) break;

                if (strcmp(msg.command, "FIRST") == 0) {
                    if (my_card_count == 0) {
                        printf("Child %d: I complete\n", i + 1);
                        write(pipe_from_child[i][1], "COMPLETE", 9);
                        break;
                    } else {
                        /* 強制出 D3，如果有 */
                        k = -1;
                        for (j = 0; j < my_card_count; j++) {
                            if (strcmp(my_cards[j], "D3") == 0) {
                                k = j;
                                break;
                            }
                        }
                        if (k != -1) {
                            strcpy(card, my_cards[k]);
                        } else {
                            k = find_min_card_index(my_cards, my_card_count);
                            strcpy(card, my_cards[k]);
                        }
                        printf("Child %d: play %s\n", i + 1, card);
                        write(pipe_from_child[i][1], card, CARD_LEN);

                        free(my_cards[k]);
                        for (j = k; j < my_card_count - 1; j++) {
                            my_cards[j] = my_cards[j + 1];
                        }
                        my_card_count--;
                    }
                } else if (strcmp(msg.command, "PLAY") == 0) {
                    if (my_card_count == 0) {
                        printf("Child %d: I complete\n", i + 1);
                        write(pipe_from_child[i][1], "COMPLETE", 9);
                        break;
                    } else {
                        // 更新 last_card_value 根據收到的 msg.card
                        last_card_value = get_card_value(msg.card);
                        
                        /* 找到比上一張牌大且最小的牌 */
                        k = find_largest_card_index_greater_than(my_cards, my_card_count, last_card_value);
                        if (k == -1) {
                            printf("Child %d: pass\n", i + 1, last_card_value);
                            write(pipe_from_child[i][1], "PASS", 5);
                        } else {
                            strcpy(card, my_cards[k]);
                            printf("Child %d: play %s (value %d)\n", i + 1, card, get_card_value(card));
                            write(pipe_from_child[i][1], card, CARD_LEN);
                            
                            free(my_cards[k]);
                            for (j = k; j < my_card_count - 1; j++) {
                                my_cards[j] = my_cards[j + 1];
                            }
                            my_card_count--;
                        }
                    }
                } else if (strcmp(msg.command, "LEAD") == 0) {
                    if (my_card_count == 0) {
                        printf("Child %d: I complete\n", i + 1);
                        write(pipe_from_child[i][1], "COMPLETE", 9);
                        break;
                    } else {
                        /* 出最小的牌，忽略上一張牌 */
                        k = find_min_card_index(my_cards, my_card_count);
                        strcpy(card, my_cards[k]);
                        printf("Child %d: play %s\n", i + 1, card);
                        write(pipe_from_child[i][1], card, CARD_LEN);

                        free(my_cards[k]);
                        for (j = k; j < my_card_count - 1; j++) {
                            my_cards[j] = my_cards[j + 1];
                        }
                        my_card_count--;
                    }
                }
            }

            for (j = 0; j < my_card_count; j++) {
                free(my_cards[j]);
            }
            free(my_cards);
            close(pipe_to_child[i][0]);
            close(pipe_from_child[i][1]);
            exit(0);
        } else {
            close(pipe_to_child[i][0]);
            close(pipe_from_child[i][1]);
        }
    }

    printf("Parent: the child players are");
    for (i = 0; i < num_players; i++) {
        printf(" %d", pids[i]);
    }
    printf("\n");
    fflush(stdout);

    start_player = -1;
    for (i = 0; i < num_players; i++) {
        for (j = 0; j < player_cards[i]; j++) {
            if (strcmp(player_hands[i][j], "D3") == 0) {
                start_player = i;
                break;
            }
        }
        if (start_player != -1) break;
    }

    if (start_player == -1) {
        fprintf(stderr, "No player has D3!\n");
        exit(1);
    }

    current_player = start_player;
    last_played_player = start_player;
    for (i = 0; i < num_players; i++) {
        completed[i] = 0;
    }
    remaining_players = num_players;
    last_card_value = 0;
    strcpy(last_card, "D0"); /* 初始值，比 D3 小 */
    round = 1;
    pass_count = 0;
    int first_winner = 0;

    while (remaining_players > 1) {

        if (completed[current_player]) {
            current_player = (current_player + 1) % num_players;
            continue;
        }
    
        round++;

        Message msg;
        if (round == 2) { 
            strcpy(msg.command, "FIRST");
            msg.card[0] = '\0';  // 不用卡牌
        } else if (pass_count == remaining_players - 1) {
            strcpy(msg.command, "LEAD");
            msg.card[0] = '\0';
        } else {
            strcpy(msg.command, "PLAY");
            strcpy(msg.card, last_card);
        }
        write(pipe_to_child[current_player][1], &msg, sizeof(Message));

    
        n = read(pipe_from_child[current_player][0], response, RESPONSE_LEN);
        if (n <= 0) {
            completed[current_player] = 1;
            remaining_players--;
            current_player = (current_player + 1) % num_players;
            continue;
        }
        response[n] = '\0';
    
        if (strcmp(response, "COMPLETE") == 0) {
            if (!first_winner) {
                printf("Parent: child %d is winner\n", current_player + 1);
                first_winner = 1;
            } else {
                printf("Parent: child %d completes\n", current_player + 1);
            }
            completed[current_player] = 1;
            remaining_players--;
            current_player = (current_player + 1) % num_players;
        } else if (strcmp(response, "PASS") == 0) {
            printf("Parent: child %d passes\n", current_player + 1);
            pass_count++;
            current_player = (current_player + 1) % num_players;  // 正確輪流
        } else {
            printf("Parent: child %d plays %s\n", current_player + 1, response);
            strcpy(last_card, response);
            last_card_value = get_card_value(last_card);
            last_played_player = current_player;
            pass_count = 0;
                
            // 換到下一位玩家
            current_player = (current_player + 1) % num_players;
        }
    
        if (pass_count == remaining_players - 1) {
            current_player = last_played_player;  // 讓最後出牌的玩家成為新領先者
            pass_count = 0;
            last_card_value = 0;
            strcpy(last_card, "D0"); // 重新開始新的一輪
        }
        
    
    }
    

    for (i = 0; i < num_players; i++) {
        if (!completed[i]) {
            printf("Parent: child %d is loser\n", i + 1);
            break;
        }
    }

    printf("Parent: game completed\n");
    for (i = 0; i < num_players; i++) {
        close(pipe_to_child[i][1]);
        close(pipe_from_child[i][0]);
        wait(NULL);
    }

    for (i = 0; i < num_players; i++) {
        for (j = 0; j < player_cards[i]; j++) {
            free(player_hands[i][j]);
        }
    }

    return 0;
}