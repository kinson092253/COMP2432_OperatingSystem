#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

#define NUM_CHILD 4
#define HAND_SIZE 13
#define TOTAL_CARDS 52
#define MSG_SIZE 128

// Return the order index for a card rank.
// The rank order is: 3,4,5,6,7,8,9,T,J,Q,K,A,2.
int rank_order(char r) {
    char ranks[] = "3456789TJQKA2";
    char *ptr = strchr(ranks, r);
    return ptr ? (int)(ptr - ranks) : -1;
}

// Return the order index for a card suit.
// The suit order is: S > H > C > D.
int suit_order(char s) {
    switch(s) {
        case 'S': return 3;
        case 'H': return 2;
        case 'C': return 1;
        case 'D': return 0;
        default: return -1;
    }
}

// Compare two cards (each represented by two characters) in ascending order.
int card_compare(const char *a, const char *b) {
    int r1 = rank_order(a[1]);
    int r2 = rank_order(b[1]);
    if (r1 != r2)
        return r1 - r2;
    int s1 = suit_order(a[0]);
    int s2 = suit_order(b[0]);
    return s1 - s2;
}

// Sort the hand using insertion sort.
void sort_hand(char hand[][3], int n) {
    int i, j;
    char temp[3];
    for (i = 1; i < n; i++) {
        strcpy(temp, hand[i]);
        j = i - 1;
        while (j >= 0 && card_compare(hand[j], temp) > 0) {
            strcpy(hand[j+1], hand[j]);
            j--;
        }
        strcpy(hand[j+1], temp);
    }
}

// Remove the card at index idx from the hand (shifting the array).
void remove_card(char hand[][3], int *n, int idx) {
    int i;
    for(i = idx; i < (*n)-1; i++){
        strcpy(hand[i], hand[i+1]);
    }
    (*n)--;
}

// Child process function.
// 1. Reads the INIT message from the parent to receive its hand.
// 2. When receiving "ASK D3", if the hand contains D3, it removes D3 and replies "PLAY D3".
// 3. When receiving "CARD <card>", it searches its sorted hand for the smallest card that beats the given card.
//    If found, it plays that card (printing "Child X: play Y"); otherwise, it prints "Child X: pass" and replies "PASS".
// 4. When receiving "RESET", it plays the smallest card in its hand.
// 5. When the child has no more cards, it appends "COMPLETE" to its response and prints "I complete!".
void child_process(int idx, int p2c_fd, int c2p_fd) {
    char buf[MSG_SIZE];
    int handCount = 0;
    char hand[HAND_SIZE][3];  // Each card is two characters
    int i;

    // Read the INIT message from the parent to receive the hand.
    int n = read(p2c_fd, buf, MSG_SIZE-1);
    if(n <= 0) exit(1);
    buf[n] = '\0';
    if(strncmp(buf, "INIT ", 5) == 0) {
        char *token = strtok(buf + 5, " ");
        while(token != NULL && handCount < HAND_SIZE) {
            strncpy(hand[handCount], token, 2);
            hand[handCount][2] = '\0';
            handCount++;
            token = strtok(NULL, " ");
        }
        sort_hand(hand, handCount);
    }
    
    // Print initial hand information.
    printf("Child %d, pid %d: I have %d cards\n", idx+1, getpid(), handCount);
    printf("Child %d, pid %d:", idx+1, getpid());
    for(i = 0; i < handCount; i++) {
        printf(" %s", hand[i]);
    }
    printf("\n");
    fflush(stdout);
    
    // Main loop: wait for commands from the parent.
    while(1) {
        n = read(p2c_fd, buf, MSG_SIZE-1);
        if(n <= 0) break;
        buf[n] = '\0';
        
        // Command "ASK D3": check if the hand contains D3.
        if(strncmp(buf, "ASK", 3) == 0) {
            if(strstr(buf, "D3") != NULL) {
                int found = 0;
                for(i = 0; i < handCount; i++) {
                    if(strcmp(hand[i], "D3") == 0) {
                        found = 1;
                        // Remove D3 from hand.
                        remove_card(hand, &handCount, i);
                        // Print child's play message.
                        printf("Child %d: play D3\n", idx+1);
                        fflush(stdout);
                        break;
                    }
                }
                if(found) {
                    write(c2p_fd, "PLAY D3", 7);
                    continue;
                } else {
                    write(c2p_fd, "NO", 2);
                    continue;
                }
            }
        }
        // Command "CARD <card>": try to play a card that beats the given card.
        else if(strncmp(buf, "CARD", 4) == 0) {
            char current[3];
            sscanf(buf, "CARD %2s", current);
            int found = 0, chosen = -1;
            // Since the hand is sorted, search for the smallest card that beats current.
            for(i = 0; i < handCount; i++) {
                if ((rank_order(hand[i][1]) > rank_order(current[1])) ||
                    (rank_order(hand[i][1]) == rank_order(current[1]) && suit_order(hand[i][0]) > suit_order(current[0]))) {
                    found = 1;
                    chosen = i;
                    break;
                }
            }
            if(found) {
                char played[3];
                strcpy(played, hand[chosen]);
                remove_card(hand, &handCount, chosen);
                // Print child's play message.
                printf("Child %d: play %s\n", idx+1, played);
                fflush(stdout);
                if(handCount == 0) {
                    char resp[MSG_SIZE];
                    sprintf(resp, "PLAY %s COMPLETE", played);
                    write(c2p_fd, resp, strlen(resp));
                    printf("<child %d> I complete!\n", idx+1);
                    fflush(stdout);
                    break;
                } else {
                    char resp[MSG_SIZE];
                    sprintf(resp, "PLAY %s", played);
                    write(c2p_fd, resp, strlen(resp));
                }
            } else {
                // Print child's pass message.
                printf("Child %d: pass\n", idx+1);
                fflush(stdout);
                write(c2p_fd, "PASS", 4);
            }
        }
        // Command "RESET": play the smallest card in hand.
        else if(strncmp(buf, "RESET", 5) == 0) {
            if(handCount > 0) {
                char played[3];
                strcpy(played, hand[0]);
                remove_card(hand, &handCount, 0);
                // Print child's play message.
                printf("Child %d: play %s\n", idx+1, played);
                fflush(stdout);
                if(handCount == 0) {
                    char resp[MSG_SIZE];
                    sprintf(resp, "PLAY %s COMPLETE", played);
                    write(c2p_fd, resp, strlen(resp));
                    printf("child %d I complete!\n", idx+1);
                    fflush(stdout);
                    break;
                } else {
                    char resp[MSG_SIZE];
                    sprintf(resp, "PLAY %s", played);
                    write(c2p_fd, resp, strlen(resp));
                }
            }
        }
    }
    close(p2c_fd);
    close(c2p_fd);
    exit(0);
}

// Parent process main function:
// 1. Creates two pipes (parent-to-child and child-to-parent) for each child and forks NUM_CHILD children.
// 2. Reads 52 cards from card.txt and randomly distributes 13 cards to each child.
// 3. Asks each child if they have D3. The child that responds with "PLAY D3" is designated as the starting child.
// 4. The game loop: the parent sends commands (either "CARD <card>" or "RESET") to the current child,
//    reads the response, and prints the played card or pass status.
// 5. When a child runs out of cards, its response includes "COMPLETE". The first to complete is declared winner.
//    After a winner is declared, subsequent moves always use the RESET command so that the next child plays its smallest card.
int main(void) {
    int i, j;
    int p2c[NUM_CHILD][2], c2p[NUM_CHILD][2];
    pid_t pids[NUM_CHILD];
    
    // Create pipes for each child.
    for(i = 0; i < NUM_CHILD; i++){
        if(pipe(p2c[i]) < 0) {
            perror("pipe");
            exit(1);
        }
        if(pipe(c2p[i]) < 0) {
            perror("pipe");
            exit(1);
        }
    }
    
    // Fork NUM_CHILD children.
    for(i = 0; i < NUM_CHILD; i++){
        pids[i] = fork();
        if(pids[i] < 0) {
            perror("fork");
            exit(1);
        }
        else if(pids[i] == 0) {
            int k;
            for(k = 0; k < NUM_CHILD; k++){
                if(k != i){
                    close(p2c[k][0]); close(p2c[k][1]);
                    close(c2p[k][0]); close(c2p[k][1]);
                }
            }
            // In child: close unused ends.
            close(p2c[i][1]);  // Child uses the read end of the parent-to-child pipe.
            close(c2p[i][0]);  // Child uses the write end of the child-to-parent pipe.
            child_process(i, p2c[i][0], c2p[i][1]);
            exit(0);
        }
    }
    
    // Parent process: close unused ends.
    for(i = 0; i < NUM_CHILD; i++){
        close(p2c[i][0]);  // Parent writes only.
        close(c2p[i][1]);  // Parent reads only.
    }
    
    // Print the child PIDs.
    printf("Parent: the child players are ");
    for(i = 0; i < NUM_CHILD; i++){
        printf("%d ", pids[i]);
    }
    printf("\n");
    fflush(stdout);
    
    // Read the 52 cards from card.txt (cards are separated by whitespace).
    char deck[TOTAL_CARDS][3];
    FILE *fp = fopen("card.txt", "r");
    if(fp == NULL) {
        perror("fopen card.txt");
        exit(1);
    }
    int count = 0;
    while(count < TOTAL_CARDS && fscanf(fp, "%2s", deck[count]) == 1) {
        deck[count][2] = '\0';
        count++;
    }
    fclose(fp);
    if(count != TOTAL_CARDS) {
        fprintf(stderr, "Not enough cards in card.txt (need 52 cards).\n");
        exit(1);
    }
    
    // Shuffle the deck.
    srand(time(NULL));
    for(i = 0; i < TOTAL_CARDS; i++){
        int r = rand() % TOTAL_CARDS;
        char temp[3];
        strcpy(temp, deck[i]);
        strcpy(deck[i], deck[r]);
        strcpy(deck[r], temp);
    }
    
    // Distribute the 52 cards evenly to the children (13 cards each).
    for(i = 0; i < NUM_CHILD; i++){
        char handMsg[MSG_SIZE];
        handMsg[0] = '\0';
        strcat(handMsg, "INIT ");
        for(j = 0; j < HAND_SIZE; j++){
            int index = i * HAND_SIZE + j;
            strcat(handMsg, deck[index]);
            if(j < HAND_SIZE - 1)
                strcat(handMsg, " ");
        }
        write(p2c[i][1], handMsg, strlen(handMsg));
    }
    
    // Ask each child if they have D3 to determine the starting child.
    int starting_child = -1;
    for(i = 0; i < NUM_CHILD; i++){
        write(p2c[i][1], "ASK D3", 7);
        char resp[MSG_SIZE];
        int n = read(c2p[i][0], resp, MSG_SIZE-1);
        resp[n] = '\0';
        if(strncmp(resp, "PLAY D3", 7) == 0) {
            starting_child = i;
            break;
        }
    }
    if(starting_child == -1) {
        printf("No child has D3. Game error!\n");
        exit(1);
    }
    printf("<parent> Child %d plays D3\n", starting_child+1);
    
    // Game flow:
    // After the starting child plays D3, the turn passes to the next child.
    int current_turn = (starting_child + 1) % NUM_CHILD;
    int round_starter = starting_child;  // The starter of the current round remains the child who played D3.
    char current_card[3] = "D3";
    int pass_count = 0;
    int finished[NUM_CHILD] = {0};
    int finish_count = 0;
    int first_winner_reported = 0;
    
    while(finish_count < 3) {
        if(finished[current_turn]) {
            current_turn = (current_turn + 1) % NUM_CHILD;
            continue;
        }
        char cmd[MSG_SIZE];
        // If a winner has been declared, force a RESET command (play the smallest card) for subsequent moves.
        if(first_winner_reported) {
            strcpy(cmd, "RESET");
        } else {
            if(strlen(current_card) > 0)
                sprintf(cmd, "CARD %s", current_card);
            else {
                strcpy(cmd, "RESET");
                round_starter = current_turn;  // New round starter.
            }
        }
        write(p2c[current_turn][1], cmd, strlen(cmd));
        
        char resp[MSG_SIZE];
        int n = read(c2p[current_turn][0], resp, MSG_SIZE-1);
        if(n <= 0) {
            finished[current_turn] = 1;
            finish_count++;
            current_turn = (current_turn+1) % NUM_CHILD;
            continue;
        }
        resp[n] = '\0';
        
        if(strncmp(resp, "PASS", 4) == 0) {
            printf("<parent> Child %d passes\n", current_turn+1);
            pass_count++;
            int active_count = 0;
            for(i = 0; i < NUM_CHILD; i++){
                if(!finished[i]) active_count++;
            }
            if(pass_count >= active_count - 1) {
                strcpy(current_card, "");
                pass_count = 0;
                current_turn = round_starter;
                continue;
            }
        }
        else if(strncmp(resp, "PLAY", 4) == 0) {
            char played[3];
            sscanf(resp, "PLAY %2s", played);
            printf("<parent> Child %d plays %s\n", current_turn+1, played);
            round_starter = current_turn;
            strcpy(current_card, played);
            pass_count = 0;
            if(strstr(resp, "COMPLETE") != NULL) {
                finished[current_turn] = 1;
                finish_count++;
                if(!first_winner_reported) {
                    printf("<parent> Child %d is winner\n", current_turn+1);
                    first_winner_reported = 1;
                } else {
                    printf("<parent> Child %d completes\n", current_turn+1);
                }
            }
        }
        current_turn = (current_turn + 1) % NUM_CHILD;
    }
    
    // Identify the remaining child as the loser.
    for(i = 0; i < NUM_CHILD; i++){
        if(!finished[i]) {
            printf("<parent> Child %d is loser\n", i+1);
            break;
        }
    }
    
    // Close all pipes.
    for(i = 0; i < NUM_CHILD; i++){
        close(p2c[i][1]);
        close(c2p[i][0]);
    }
    
    // Wait for all children to finish.
    for(i = 0; i < NUM_CHILD; i++){
        wait(NULL);
    }
    
    return 0;
}
