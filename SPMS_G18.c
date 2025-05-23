/* SPMS_Gxx.c
   A simplified Smart Parking Management System for PolyU.
   Modified to fix member token issues and correctly handle
   FCFS, PRIO (with preemption based only on actual resource exhaustion)
   and independent simulation of OPTI scheduling,
   including dynamic calculation of resource utilization in the summary report.
   Written in C (C90 compliant).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      /* for fork(), pipe(), read(), write() */
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_BOOKINGS 200
#define MAX_LINE_LENGTH 256
#define PARKING_CAPACITY 10
#define ESSENTIAL_CAPACITY 3

/* Structure to hold a booking request */
typedef struct {
    char type[20];        /* "Parking", "Reservation", "Event", "Essentials" */
    char member[20];      /* e.g. "member_A" */
    char date[11];        /* YYYY-MM-DD */
    char time[6];         /* hh:mm */
    float duration;       /* Duration in hours */
    char essential1[20];  /* For addParking/addReservation: first extra device */
    char essential2[20];  /* For addParking/addReservation: second extra device */
    char essential3[20];  /* For addEvent: third essential device */
    int requires_parking; /* 1 if a parking slot is required, 0 otherwise */
    int accepted;         /* 1 = accepted, 0 = rejected */
} Booking;

/* Global array for FCFS (原始預約記錄) */
Booking bookings[MAX_BOOKINGS];
int bookingCount = 0;

/* Function prototypes */
int get_start_hour(const char *time_str);
int times_overlap(Booking *b1, Booking *b2);
int essential_requested(Booking *b, const char *ess);
int check_availability(Booking *newBooking);
int check_availability_temp(Booking *tempBookings, int count, Booking *newBooking);
void simulate_OPTI(Booking src[], Booking dest[], int count);
void simulate_PRIO(Booking src[], Booking dest[], int count);
void process_addParking(char *line);
void process_addReservation(char *line);
void process_addEvent(char *line);
void process_bookEssentials(char *line);
void process_addBatch(char *line);
void process_printBookings(char *line);
void process_printSummary(void);
void process_printOptimized(void);
void process_command(char *line);
char *normalize_member(char *token);

/* Priority functions: Event = 3, Reservation = 2, Parking = 1, Essentials = 0 */
int get_priority(const Booking *b) {
    if (strcmp(b->type, "Event") == 0)
        return 3;
    else if (strcmp(b->type, "Reservation") == 0)
        return 2;
    else if (strcmp(b->type, "Parking") == 0)
        return 1;
    else if (strcmp(b->type, "Essentials") == 0)
        return 0;
    return 0;
}

/* Comparator for qsort (descending order by priority) */
int cmp_priority(const void *a, const void *b) {
    const Booking *ba = *(const Booking **)a;
    const Booking *bb = *(const Booking **)b;
    return get_priority(bb) - get_priority(ba);
}

/* 若 token 以 '-' (ASCII) 或 en-dash (UTF-8) 開頭，則跳過該符號 */
char *normalize_member(char *token) {
    if (token == NULL)
        return token;
    if (token[0] == '-')
        return token + 1;
    if ((unsigned char)token[0] == 0xE2 &&
        (unsigned char)token[1] == 0x80 &&
        (unsigned char)token[2] == 0x93)
        return token + 3;
    return token;
}

/* 取得時間字串的時 */
int get_start_hour(const char *time_str) {
    int hour, minute;
    sscanf(time_str, "%d:%d", &hour, &minute);
    return hour;
}

/* 若兩預約時間重疊則回傳 1 */
int times_overlap(Booking *b1, Booking *b2) {
    int start1 = get_start_hour(b1->time);
    int start2 = get_start_hour(b2->time);
    int end1 = start1 + (int)(b1->duration);
    int end2 = start2 + (int)(b2->duration);
    return ((start1 < end2) && (start2 < end1));
}

/* 檢查預約是否要求某項 essential */
int essential_requested(Booking *b, const char *ess) {
    if ((strlen(b->essential1) > 0 && strcmp(b->essential1, ess) == 0) ||
        (strlen(b->essential2) > 0 && strcmp(b->essential2, ess) == 0) ||
        (strlen(b->essential3) > 0 && strcmp(b->essential3, ess) == 0))
        return 1;
    return 0;
}

/* FCFS: 檢查全局預約中資源是否足夠 */
int check_availability(Booking *newBooking) {
    int i, count;
    if (newBooking->requires_parking) {
        count = 0;
        for (i = 0; i < bookingCount; i++) {
            if (bookings[i].accepted &&
                strcmp(bookings[i].date, newBooking->date) == 0 &&
                bookings[i].requires_parking) {
                if (times_overlap(&bookings[i], newBooking))
                    count++;
            }
        }
        if (count >= PARKING_CAPACITY)
            return 0;
    }
    if (strlen(newBooking->essential1) > 0) {
        count = 0;
        for (i = 0; i < bookingCount; i++) {
            if (bookings[i].accepted && strcmp(bookings[i].date, newBooking->date) == 0) {
                if (times_overlap(&bookings[i], newBooking)) {
                    if (essential_requested(&bookings[i], newBooking->essential1))
                        count++;
                }
            }
        }
        if (count >= ESSENTIAL_CAPACITY)
            return 0;
    }
    if (strlen(newBooking->essential2) > 0) {
        count = 0;
        for (i = 0; i < bookingCount; i++) {
            if (bookings[i].accepted && strcmp(bookings[i].date, newBooking->date) == 0) {
                if (times_overlap(&bookings[i], newBooking)) {
                    if (essential_requested(&bookings[i], newBooking->essential2))
                        count++;
                }
            }
        }
        if (count >= ESSENTIAL_CAPACITY)
            return 0;
    }
    if (strlen(newBooking->essential3) > 0) {
        count = 0;
        for (i = 0; i < bookingCount; i++) {
            if (bookings[i].accepted && strcmp(bookings[i].date, newBooking->date) == 0) {
                if (times_overlap(&bookings[i], newBooking)) {
                    if (essential_requested(&bookings[i], newBooking->essential3))
                        count++;
                }
            }
        }
        if (count >= ESSENTIAL_CAPACITY)
            return 0;
    }
    return 1;
}

/* 與上面類似，但作用於傳入的 tempBookings 陣列 */
int check_availability_temp(Booking *tempBookings, int count, Booking *newBooking) {
    int i, c;
    if (newBooking->requires_parking) {
        c = 0;
        for (i = 0; i < count; i++) {
            if (tempBookings[i].accepted &&
                strcmp(tempBookings[i].date, newBooking->date) == 0 &&
                tempBookings[i].requires_parking) {
                if (times_overlap(&tempBookings[i], newBooking))
                    c++;
            }
        }
        if (c >= PARKING_CAPACITY)
            return 0;
    }
    if (strlen(newBooking->essential1) > 0) {
        c = 0;
        for (i = 0; i < count; i++) {
            if (tempBookings[i].accepted && strcmp(tempBookings[i].date, newBooking->date) == 0) {
                if (times_overlap(&tempBookings[i], newBooking)) {
                    if (essential_requested(&tempBookings[i], newBooking->essential1))
                        c++;
                }
            }
        }
        if (c >= ESSENTIAL_CAPACITY)
            return 0;
    }
    if (strlen(newBooking->essential2) > 0) {
        c = 0;
        for (i = 0; i < count; i++) {
            if (tempBookings[i].accepted && strcmp(tempBookings[i].date, newBooking->date) == 0) {
                if (times_overlap(&tempBookings[i], newBooking)) {
                    if (essential_requested(&tempBookings[i], newBooking->essential2))
                        c++;
                }
            }
        }
        if (c >= ESSENTIAL_CAPACITY)
            return 0;
    }
    if (strlen(newBooking->essential3) > 0) {
        c = 0;
        for (i = 0; i < count; i++) {
            if (tempBookings[i].accepted && strcmp(tempBookings[i].date, newBooking->date) == 0) {
                if (times_overlap(&tempBookings[i], newBooking)) {
                    if (essential_requested(&tempBookings[i], newBooking->essential3))
                        c++;
                }
            }
        }
        if (c >= ESSENTIAL_CAPACITY)
            return 0;
    }
    return 1;
}

/* 模擬 OPTI 調度：複製 src 到 dest，並嘗試調整未被接受預約的開始時間（08:00-20:00），不改變全局資料 */
void simulate_OPTI(Booking src[], Booking dest[], int count) {
    int i, h;
    char newTime[6];
    char oldTime[6];
    for (i = 0; i < count; i++) {
        dest[i] = src[i];
    }
    for (i = 0; i < count; i++) {
        if (!dest[i].accepted) {
            strcpy(oldTime, dest[i].time);
            for (h = 8; h <= 20; h++) {
                sprintf(newTime, "%02d:00", h);
                strcpy(dest[i].time, newTime);
                if (check_availability_temp(dest, count, &dest[i])) {
                    dest[i].accepted = 1;
                    break;
                } else {
                    strcpy(dest[i].time, oldTime);
                }
            }
        }
    }
}

/* 模擬 PRIO 調度（搶占機制）：
   先複製 src 到 dest，按到達順序處理，
   只有在某資源（如停車位或必需設備）使用數量達到上限時，
   才嘗試搶占與新預約重疊且優先權較低的預約，否則直接接受。
*/
void simulate_PRIO(Booking src[], Booking dest[], int count) {
    int i, j;
    /* 複製所有預約，並初始化 accepted 為 0 */
    for (i = 0; i < count; i++) {
        dest[i] = src[i];
        dest[i].accepted = 0;
    }
    /* 按到達順序處理每筆預約 */
    for (i = 0; i < count; i++) {
        if (check_availability_temp(dest, i, &dest[i])) {
            dest[i].accepted = 1;
        } else {
            /* 檢查各項資源是否真正耗盡，若是，則嘗試搶占低優先權預約 */
            /* 停車位 */
            if (dest[i].requires_parking) {
                int count_parking = 0;
                for (j = 0; j < i; j++) {
                    if (dest[j].accepted &&
                        strcmp(dest[j].date, dest[i].date) == 0 &&
                        dest[j].requires_parking &&
                        times_overlap(&dest[j], &dest[i]))
                    {
                        count_parking++;
                    }
                }
                if (count_parking >= PARKING_CAPACITY) {
                    for (j = 0; j < i; j++) {
                        if (dest[j].accepted &&
                            strcmp(dest[j].date, dest[i].date) == 0 &&
                            dest[j].requires_parking &&
                            times_overlap(&dest[j], &dest[i]) &&
                            get_priority(&dest[j]) < get_priority(&dest[i]))
                        {
                            dest[j].accepted = 0;
                            if (check_availability_temp(dest, i, &dest[i]))
                                break;
                        }
                    }
                }
            }
            /* essential1 */
            if (strlen(dest[i].essential1) > 0) {
                int count_ess1 = 0;
                for (j = 0; j < i; j++) {
                    if (dest[j].accepted &&
                        strcmp(dest[j].date, dest[i].date) == 0 &&
                        times_overlap(&dest[j], &dest[i]) &&
                        essential_requested(&dest[j], dest[i].essential1))
                    {
                        count_ess1++;
                    }
                }
                if (count_ess1 >= ESSENTIAL_CAPACITY) {
                    for (j = 0; j < i; j++) {
                        if (dest[j].accepted &&
                            strcmp(dest[j].date, dest[i].date) == 0 &&
                            times_overlap(&dest[j], &dest[i]) &&
                            essential_requested(&dest[j], dest[i].essential1) &&
                            get_priority(&dest[j]) < get_priority(&dest[i]))
                        {
                            dest[j].accepted = 0;
                            if (check_availability_temp(dest, i, &dest[i]))
                                break;
                        }
                    }
                }
            }
            /* essential2 */
            if (strlen(dest[i].essential2) > 0) {
                int count_ess2 = 0;
                for (j = 0; j < i; j++) {
                    if (dest[j].accepted &&
                        strcmp(dest[j].date, dest[i].date) == 0 &&
                        times_overlap(&dest[j], &dest[i]) &&
                        essential_requested(&dest[j], dest[i].essential2))
                    {
                        count_ess2++;
                    }
                }
                if (count_ess2 >= ESSENTIAL_CAPACITY) {
                    for (j = 0; j < i; j++) {
                        if (dest[j].accepted &&
                            strcmp(dest[j].date, dest[i].date) == 0 &&
                            times_overlap(&dest[j], &dest[i]) &&
                            essential_requested(&dest[j], dest[i].essential2) &&
                            get_priority(&dest[j]) < get_priority(&dest[i]))
                        {
                            dest[j].accepted = 0;
                            if (check_availability_temp(dest, i, &dest[i]))
                                break;
                        }
                    }
                }
            }
            /* essential3 */
            if (strlen(dest[i].essential3) > 0) {
                int count_ess3 = 0;
                for (j = 0; j < i; j++) {
                    if (dest[j].accepted &&
                        strcmp(dest[j].date, dest[i].date) == 0 &&
                        times_overlap(&dest[j], &dest[i]) &&
                        essential_requested(&dest[j], dest[i].essential3))
                    {
                        count_ess3++;
                    }
                }
                if (count_ess3 >= ESSENTIAL_CAPACITY) {
                    for (j = 0; j < i; j++) {
                        if (dest[j].accepted &&
                            strcmp(dest[j].date, dest[i].date) == 0 &&
                            times_overlap(&dest[j], &dest[i]) &&
                            essential_requested(&dest[j], dest[i].essential3) &&
                            get_priority(&dest[j]) < get_priority(&dest[i]))
                        {
                            dest[j].accepted = 0;
                            if (check_availability_temp(dest, i, &dest[i]))
                                break;
                        }
                    }
                }
            }
            if (check_availability_temp(dest, i, &dest[i])) {
                dest[i].accepted = 1;
            } else {
                dest[i].accepted = 0;
            }
        }
    }
}

/* 以下為使用者命令處理函式 */

/* addParking -member_X YYYY-MM-DD hh:mm duration [essential1 essential2]; */
void process_addParking(char *line) {
    char *token;
    Booking b;
    int i;
    for (i = 0; i < (int)sizeof(b); i++) {
        ((char *)&b)[i] = 0;
    }
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.member, normalize_member(token));
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.date, token);
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.time, token);
    token = strtok(NULL, " ");
    if (token == NULL) return;
    b.duration = (float)atof(token);
    token = strtok(NULL, " ;\n");
    if (token != NULL) {
        strcpy(b.essential1, token);
        token = strtok(NULL, " ;\n");
        if (token != NULL)
            strcpy(b.essential2, token);
    }
    strcpy(b.type, "Parking");
    b.requires_parking = 1;
    b.accepted = check_availability(&b) ? 1 : 0;
    bookings[bookingCount++] = b;
    printf("-> [Pending]\n");
}

/* addReservation -member_X YYYY-MM-DD hh:mm duration essential1 essential2; */
void process_addReservation(char *line) {
    char *token;
    Booking b;
    int i;
    for (i = 0; i < (int)sizeof(b); i++) {
        ((char *)&b)[i] = 0;
    }
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.member, normalize_member(token));
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.date, token);
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.time, token);
    token = strtok(NULL, " ");
    if (token == NULL) return;
    b.duration = (float)atof(token);
    token = strtok(NULL, " ;\n");
    if (token != NULL) {
        strcpy(b.essential1, token);
        token = strtok(NULL, " ;\n");
        if (token != NULL)
            strcpy(b.essential2, token);
    }
    strcpy(b.type, "Reservation");
    b.requires_parking = 1;
    b.accepted = check_availability(&b) ? 1 : 0;
    bookings[bookingCount++] = b;
    printf("-> [Pending]\n");
}

/* addEvent -member_X YYYY-MM-DD hh:mm duration essential1 essential2 essential3; */
void process_addEvent(char *line) {
    char *token;
    Booking b;
    int i;
    for (i = 0; i < (int)sizeof(b); i++) {
        ((char *)&b)[i] = 0;
    }
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.member, normalize_member(token));
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.date, token);
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.time, token);
    token = strtok(NULL, " ");
    if (token == NULL) return;
    b.duration = (float)atof(token);
    token = strtok(NULL, " ");
    if (token != NULL) {
        strcpy(b.essential1, token);
        token = strtok(NULL, " ");
        if (token != NULL) {
            strcpy(b.essential2, token);
            token = strtok(NULL, " ;\n");
            if (token != NULL)
                strcpy(b.essential3, token);
        }
    }
    strcpy(b.type, "Event");
    b.requires_parking = 1;
    b.accepted = check_availability(&b) ? 1 : 0;
    bookings[bookingCount++] = b;
    printf("-> [Pending]\n");
}

/* bookEssentials -member_X YYYY-MM-DD hh:mm duration essential; */
void process_bookEssentials(char *line) {
    char *token;
    Booking b;
    int i;
    for (i = 0; i < (int)sizeof(b); i++) {
        ((char *)&b)[i] = 0;
    }
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.member, normalize_member(token));
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.date, token);
    token = strtok(NULL, " ");
    if (token == NULL) return;
    strcpy(b.time, token);
    token = strtok(NULL, " ");
    if (token == NULL) return;
    b.duration = (float)atof(token);
    token = strtok(NULL, " ;\n");
    if (token != NULL)
        strcpy(b.essential1, token);
    strcpy(b.type, "Essentials");
    b.requires_parking = 0;
    b.accepted = check_availability(&b) ? 1 : 0;
    bookings[bookingCount++] = b;
    printf("-> [Pending]\n");
}

/* addBatch -batchfile */
void process_addBatch(char *line) {
    char *token;
    FILE *fp;
    char batchLine[MAX_LINE_LENGTH];
    token = strtok(NULL, " ;\n");
    if (token == NULL) return;
    if (token[0] == '-' || (unsigned char)token[0] == 0xE2)
        token++;  /* Skip leading dash */
    fp = fopen(token, "r");
    if (!fp) {
        printf("Error: Cannot open batch file %s\n", token);
        return;
    }
    while (fgets(batchLine, sizeof(batchLine), fp)) {
        batchLine[strcspn(batchLine, "\n")] = '\0';
        process_command(batchLine);
    }
    fclose(fp);
    printf("-> [Pending]\n");
}

/* 輸出預約記錄（依 FCFS 或 PRIO 模式排序） */
void process_printBookings(char *line) {
    char *token;
    int pipefd[2];
    pid_t pid;
    char outBuffer[1024];
    int i, n;
    char algorithm[10];

    // 讀取使用者指定模式 (-fcfs 或 -prio)
    token = strtok(line, " ");
    token = strtok(NULL, " ;\n");  /* 預期為 "-fcfs" 或 "-prio" */
    if (token != NULL) {
        token = normalize_member(token);
        if (strcmp(token, "PRIO") == 0 || strcmp(token, "prio") == 0)
            strcpy(algorithm, "PRIO");
        else
            strcpy(algorithm, "FCFS");
    } else {
        strcpy(algorithm, "FCFS");
    }

    // 根據模式建立要印出的預約陣列
    Booking displayBookings[MAX_BOOKINGS];
    if (strcmp(algorithm, "PRIO") == 0) {
        // PRIO 模式下先模擬優先調度
        simulate_PRIO(bookings, displayBookings, bookingCount);
    } else {
        // FCFS 模式直接使用全局預約記錄
        memcpy(displayBookings, bookings, sizeof(Booking) * bookingCount);
    }
    
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        close(pipefd[1]);
        while ((n = read(pipefd[0], outBuffer, sizeof(outBuffer)-1)) > 0) {
            outBuffer[n] = '\0';
            printf("%s", outBuffer);
        }
        close(pipefd[0]);
        exit(0);
    } else {
        close(pipefd[0]);
        sprintf(outBuffer, "\n** Parking Booking – ACCEPTED / %s **\n", algorithm);
        write(pipefd[1], outBuffer, strlen(outBuffer));

        // 處理已接受的預約
        {
            char *members[] = {"member_A", "member_B", "member_C", "member_D", "member_E"};
            int numMembers = 5;
            int foundAnyAccepted = 0;
            int j, k;
            for (i = 0; i < numMembers; i++) {
                int count = 0;
                Booking *memberBookings[MAX_BOOKINGS];
                int memberCount = 0;
                for (j = 0; j < bookingCount; j++) {
                    if (displayBookings[j].accepted && strcmp(displayBookings[j].member, members[i]) == 0) {
                        memberBookings[memberCount++] = &displayBookings[j];
                        count++;
                    }
                }

                if (count > 0) {
                    foundAnyAccepted = 1;
                    sprintf(outBuffer, "%s has the following bookings:\n", members[i]);
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                    sprintf(outBuffer, "Date       Start End   Type         Device\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                    sprintf(outBuffer, "===========================================================================\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));

                    if (strcmp(algorithm, "PRIO") == 0 && memberCount > 1)
                        qsort(memberBookings, memberCount, sizeof(Booking *), cmp_priority); // 按優先權排序

                    for (k = 0; k < memberCount; k++) {
                        int hour, minute;
                        sscanf(memberBookings[k]->time, "%d:%d", &hour, &minute);
                        int endHour = hour + (int)(memberBookings[k]->duration);
                        char endTime[6];
                        sprintf(endTime, "%02d:%02d", endHour, minute);

                        char typeStr[20];
                        if (strcmp(memberBookings[k]->type, "Essentials") == 0)
                            strcpy(typeStr, "*");
                        else
                            strcpy(typeStr, memberBookings[k]->type);

                        char deviceStr[100] = "";
                        if (strcmp(memberBookings[k]->type, "Essentials") == 0) {
                            if (strlen(memberBookings[k]->essential1) > 0)
                                strcpy(deviceStr, memberBookings[k]->essential1);
                            else
                                strcpy(deviceStr, "*");
                        } else {
                            if (strlen(memberBookings[k]->essential1) > 0)
                                strcpy(deviceStr, memberBookings[k]->essential1);
                            if (strlen(memberBookings[k]->essential2) > 0) {
                                if (strlen(deviceStr) > 0) {
                                    strcat(deviceStr, " ");
                                    strcat(deviceStr, memberBookings[k]->essential2);
                                } else {
                                    strcpy(deviceStr, memberBookings[k]->essential2);
                                }
                            }
                            if (strlen(deviceStr) == 0)
                                strcpy(deviceStr, "*");
                        }

                        char bookingLine[256];
                        sprintf(bookingLine, "%-10s %-5s %-5s %-12s %s\n",
                                memberBookings[k]->date,
                                memberBookings[k]->time,
                                endTime,
                                typeStr,
                                deviceStr);
                        write(pipefd[1], bookingLine, strlen(bookingLine));
                    }
                    sprintf(outBuffer, "\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                }
            }

            if (foundAnyAccepted) {
                sprintf(outBuffer, "- End -\n");
                write(pipefd[1], outBuffer, strlen(outBuffer));
            } else {
                sprintf(outBuffer, "No accepted bookings.\n");
                write(pipefd[1], outBuffer, strlen(outBuffer));
            }

            sprintf(outBuffer, "===========================================================================\n");
            write(pipefd[1], outBuffer, strlen(outBuffer));
        }

        // 輸出拒絕的預約
        sprintf(outBuffer, "\n** Parking Booking – REJECTED / %s **\n", algorithm);
        write(pipefd[1], outBuffer, strlen(outBuffer));

        {
            char *members[] = {"member_A", "member_B", "member_C", "member_D", "member_E"};
            int numMembers = 5;
            int foundAnyRejected = 0;
            int j, k;
            for (i = 0; i < numMembers; i++) {
                int count = 0;
                Booking *memberBookings[MAX_BOOKINGS];
                int memberCount = 0;
                for (j = 0; j < bookingCount; j++) {
                    if (!displayBookings[j].accepted && strcmp(displayBookings[j].member, members[i]) == 0) {
                        memberBookings[memberCount++] = &displayBookings[j];
                        count++;
                    }
                }

                if (count > 0) {
                    foundAnyRejected = 1;
                    sprintf(outBuffer, "%s (there are %d bookings rejected):\n", members[i], count);
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                    sprintf(outBuffer, "Date       Start End   Type         Essentials\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                    sprintf(outBuffer, "===========================================================================\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));

                    if (strcmp(algorithm, "PRIO") == 0 && memberCount > 1)
                        qsort(memberBookings, memberCount, sizeof(Booking *), cmp_priority);

                    for (k = 0; k < memberCount; k++) {
                        int hour, minute;
                        sscanf(memberBookings[k]->time, "%d:%d", &hour, &minute);
                        int endHour = hour + (int)(memberBookings[k]->duration);
                        char endTime[6];
                        sprintf(endTime, "%02d:%02d", endHour, minute);

                        char typeStr[20];
                        strcpy(typeStr, memberBookings[k]->type);

                        char essStr[100] = "";
                        if (strcmp(memberBookings[k]->type, "Essentials") == 0) {
                            if (strlen(memberBookings[k]->essential1) > 0)
                                strcpy(essStr, memberBookings[k]->essential1);
                            else
                                strcpy(essStr, "-");
                        } else {
                            if (strlen(memberBookings[k]->essential1) > 0)
                                strcpy(essStr, memberBookings[k]->essential1);
                            if (strlen(memberBookings[k]->essential2) > 0) {
                                if (strlen(essStr) > 0) {
                                    strcat(essStr, " ");
                                    strcat(essStr, memberBookings[k]->essential2);
                                } else {
                                    strcpy(essStr, memberBookings[k]->essential2);
                                }
                            }
                            if (strlen(essStr) == 0)
                                strcpy(essStr, "-");
                        }

                        char bookingLine[256];
                        sprintf(bookingLine, "%-10s %-5s %-5s %-12s %s\n",
                                memberBookings[k]->date,
                                memberBookings[k]->time,
                                endTime,
                                typeStr,
                                essStr);
                        write(pipefd[1], bookingLine, strlen(bookingLine));
                    }
                    sprintf(outBuffer, "\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                }
            }

            if (foundAnyRejected) {
                sprintf(outBuffer, "- End -\n");
                write(pipefd[1], outBuffer, strlen(outBuffer));
            } else {
                sprintf(outBuffer, "No rejected bookings.\n");
                write(pipefd[1], outBuffer, strlen(outBuffer));
            }
            sprintf(outBuffer, "===========================================================================\n");
            write(pipefd[1], outBuffer, strlen(outBuffer));
        }

        close(pipefd[1]);
        wait(NULL);
        printf("-> [Done!]\n");
    }
}

/* 輸出綜合報告：分別統計 FCFS、PRIO 與 OPTI 模式 */
void process_printSummary(void) {
    int i;
    int total = bookingCount;
    const int available_hours_per_day = 12;

    /* === FCFS 計算 === */
    int fcfs_accepted = 0, fcfs_rejected = 0;
    int fcfs_earliest = 32, fcfs_latest = 0;
    double fcfs_parking_sum = 0.0, fcfs_battery_sum = 0.0, fcfs_cable_sum = 0.0;
    double fcfs_locker_sum = 0.0, fcfs_umbrella_sum = 0.0, fcfs_valet_sum = 0.0, fcfs_inflation_sum = 0.0;
    for (i = 0; i < total; i++) {
        if (bookings[i].accepted) {
            fcfs_accepted++;
            int d = atoi(bookings[i].date + 8); // 取日期中的 "DD" 部分
            if (d < fcfs_earliest) fcfs_earliest = d;
            if (d > fcfs_latest) fcfs_latest = d;
            if (bookings[i].requires_parking)
                fcfs_parking_sum += bookings[i].duration;
            if ((strlen(bookings[i].essential1) > 0 && strcmp(bookings[i].essential1, "battery") == 0) ||
                (strlen(bookings[i].essential2) > 0 && strcmp(bookings[i].essential2, "battery") == 0) ||
                (strlen(bookings[i].essential3) > 0 && strcmp(bookings[i].essential3, "battery") == 0))
                fcfs_battery_sum += bookings[i].duration;
            if (strlen(bookings[i].essential2) > 0 && strcmp(bookings[i].essential2, "cable") == 0)
                fcfs_cable_sum += bookings[i].duration;
            if (strlen(bookings[i].essential1) > 0 && strcmp(bookings[i].essential1, "locker") == 0)
                fcfs_locker_sum += bookings[i].duration;
            if (strlen(bookings[i].essential2) > 0 && strcmp(bookings[i].essential2, "umbrella") == 0)
                fcfs_umbrella_sum += bookings[i].duration;
            if (strlen(bookings[i].essential3) > 0 && strcmp(bookings[i].essential3, "valetPark") == 0)
                fcfs_valet_sum += bookings[i].duration;
            if ((strlen(bookings[i].essential1) > 0 && strcmp(bookings[i].essential1, "inflationService") == 0) ||
                (strlen(bookings[i].essential2) > 0 && strcmp(bookings[i].essential2, "inflationService") == 0) ||
                (strlen(bookings[i].essential3) > 0 && strcmp(bookings[i].essential3, "inflationService") == 0))
                fcfs_inflation_sum += bookings[i].duration;
        } else {
            fcfs_rejected++;
        }
    }
    int fcfs_days = fcfs_latest - fcfs_earliest + 1;
    if (fcfs_days <= 0) fcfs_days = 1;
    double fcfs_parking_available = PARKING_CAPACITY * fcfs_days * available_hours_per_day;
    double fcfs_essential_available = ESSENTIAL_CAPACITY * fcfs_days * available_hours_per_day;
    double fcfs_parking_util = (fcfs_parking_sum / fcfs_parking_available) * 100.0;
    double fcfs_battery_util = (fcfs_battery_sum / fcfs_essential_available) * 100.0;
    double fcfs_cable_util = (fcfs_cable_sum / fcfs_essential_available) * 100.0;
    double fcfs_locker_util = (fcfs_locker_sum / fcfs_essential_available) * 100.0;
    double fcfs_umbrella_util = (fcfs_umbrella_sum / fcfs_essential_available) * 100.0;
    double fcfs_valet_util = (fcfs_valet_sum / fcfs_essential_available) * 100.0;
    double fcfs_inflation_util = (fcfs_inflation_sum / fcfs_essential_available) * 100.0;

    /* === PRIO 模擬計算 === */
    Booking prio_bookings[MAX_BOOKINGS];
    simulate_PRIO(bookings, prio_bookings, total);
    int prio_accepted = 0, prio_rejected = 0;
    int prio_earliest = 32, prio_latest = 0;
    double prio_parking_sum = 0.0, prio_battery_sum = 0.0, prio_cable_sum = 0.0;
    double prio_locker_sum = 0.0, prio_umbrella_sum = 0.0, prio_valet_sum = 0.0, prio_inflation_sum = 0.0;
    for (i = 0; i < total; i++) {
        if (prio_bookings[i].accepted) {
            prio_accepted++;
            int d = atoi(prio_bookings[i].date + 8);
            if (d < prio_earliest) prio_earliest = d;
            if (d > prio_latest) prio_latest = d;
            if (prio_bookings[i].requires_parking)
                prio_parking_sum += prio_bookings[i].duration;
            if ((strlen(prio_bookings[i].essential1) > 0 && strcmp(prio_bookings[i].essential1, "battery") == 0) ||
                (strlen(prio_bookings[i].essential2) > 0 && strcmp(prio_bookings[i].essential2, "battery") == 0) ||
                (strlen(prio_bookings[i].essential3) > 0 && strcmp(prio_bookings[i].essential3, "battery") == 0))
                prio_battery_sum += prio_bookings[i].duration;
            if (strlen(prio_bookings[i].essential2) > 0 && strcmp(prio_bookings[i].essential2, "cable") == 0)
                prio_cable_sum += prio_bookings[i].duration;
            if (strlen(prio_bookings[i].essential1) > 0 && strcmp(prio_bookings[i].essential1, "locker") == 0)
                prio_locker_sum += prio_bookings[i].duration;
            if (strlen(prio_bookings[i].essential2) > 0 && strcmp(prio_bookings[i].essential2, "umbrella") == 0)
                prio_umbrella_sum += prio_bookings[i].duration;
            if (strlen(prio_bookings[i].essential3) > 0 && strcmp(prio_bookings[i].essential3, "valetPark") == 0)
                prio_valet_sum += prio_bookings[i].duration;
            if ((strlen(prio_bookings[i].essential1) > 0 && strcmp(prio_bookings[i].essential1, "inflationService") == 0) ||
                (strlen(prio_bookings[i].essential2) > 0 && strcmp(prio_bookings[i].essential2, "inflationService") == 0) ||
                (strlen(prio_bookings[i].essential3) > 0 && strcmp(prio_bookings[i].essential3, "inflationService") == 0))
                prio_inflation_sum += prio_bookings[i].duration;
        } else {
            prio_rejected++;
        }
    }
    int prio_days = prio_latest - prio_earliest + 1;
    if (prio_days <= 0) prio_days = 1;
    double prio_parking_available = PARKING_CAPACITY * prio_days * available_hours_per_day;
    double prio_essential_available = ESSENTIAL_CAPACITY * prio_days * available_hours_per_day;
    double prio_parking_util = (prio_parking_sum / prio_parking_available) * 100.0;
    double prio_battery_util = (prio_battery_sum / prio_essential_available) * 100.0;
    double prio_cable_util = (prio_cable_sum / prio_essential_available) * 100.0;
    double prio_locker_util = (prio_locker_sum / prio_essential_available) * 100.0;
    double prio_umbrella_util = (prio_umbrella_sum / prio_essential_available) * 100.0;
    double prio_valet_util = (prio_valet_sum / prio_essential_available) * 100.0;
    double prio_inflation_util = (prio_inflation_sum / prio_essential_available) * 100.0;

    /* === OPTI 模擬計算 === */
    Booking opti_bookings[MAX_BOOKINGS];
    simulate_OPTI(bookings, opti_bookings, total);
    int opti_accepted = 0, opti_rejected = 0;
    int opti_earliest = 32, opti_latest = 0;
    double opti_parking_sum = 0.0, opti_battery_sum = 0.0, opti_cable_sum = 0.0;
    double opti_locker_sum = 0.0, opti_umbrella_sum = 0.0, opti_valet_sum = 0.0, opti_inflation_sum = 0.0;
    for (i = 0; i < total; i++) {
        if (opti_bookings[i].accepted) {
            opti_accepted++;
            int d = atoi(opti_bookings[i].date + 8);
            if (d < opti_earliest) opti_earliest = d;
            if (d > opti_latest) opti_latest = d;
            if (opti_bookings[i].requires_parking)
                opti_parking_sum += opti_bookings[i].duration;
            if ((strlen(opti_bookings[i].essential1) > 0 && strcmp(opti_bookings[i].essential1, "battery") == 0) ||
                (strlen(opti_bookings[i].essential2) > 0 && strcmp(opti_bookings[i].essential2, "battery") == 0) ||
                (strlen(opti_bookings[i].essential3) > 0 && strcmp(opti_bookings[i].essential3, "battery") == 0))
                opti_battery_sum += opti_bookings[i].duration;
            if (strlen(opti_bookings[i].essential2) > 0 && strcmp(opti_bookings[i].essential2, "cable") == 0)
                opti_cable_sum += opti_bookings[i].duration;
            if (strlen(opti_bookings[i].essential1) > 0 && strcmp(opti_bookings[i].essential1, "locker") == 0)
                opti_locker_sum += opti_bookings[i].duration;
            if (strlen(opti_bookings[i].essential2) > 0 && strcmp(opti_bookings[i].essential2, "umbrella") == 0)
                opti_umbrella_sum += opti_bookings[i].duration;
            if (strlen(opti_bookings[i].essential3) > 0 && strcmp(opti_bookings[i].essential3, "valetPark") == 0)
                opti_valet_sum += opti_bookings[i].duration;
            if ((strlen(opti_bookings[i].essential1) > 0 && strcmp(opti_bookings[i].essential1, "inflationService") == 0) ||
                (strlen(opti_bookings[i].essential2) > 0 && strcmp(opti_bookings[i].essential2, "inflationService") == 0) ||
                (strlen(opti_bookings[i].essential3) > 0 && strcmp(opti_bookings[i].essential3, "inflationService") == 0))
                opti_inflation_sum += opti_bookings[i].duration;
        } else {
            opti_rejected++;
        }
    }
    int opti_days = opti_latest - opti_earliest + 1;
    if (opti_days <= 0) opti_days = 1;
    double opti_parking_available = PARKING_CAPACITY * opti_days * available_hours_per_day;
    double opti_essential_available = ESSENTIAL_CAPACITY * opti_days * available_hours_per_day;
    double opti_parking_util = (opti_parking_sum / opti_parking_available) * 100.0;
    double opti_battery_util = (opti_battery_sum / opti_essential_available) * 100.0;
    double opti_cable_util = (opti_cable_sum / opti_essential_available) * 100.0;
    double opti_locker_util = (opti_locker_sum / opti_essential_available) * 100.0;
    double opti_umbrella_util = (opti_umbrella_sum / opti_essential_available) * 100.0;
    double opti_valet_util = (opti_valet_sum / opti_essential_available) * 100.0;
    double opti_inflation_util = (opti_inflation_sum / opti_essential_available) * 100.0;

    /* === 使用 pipe 與 fork 輸出綜合報告 === */
    int pipefd[2];
    pid_t pid;
    char outBuffer[1024];
    int nBytes;
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0) {  /* 子行程：讀取 pipe 並印出 */
        close(pipefd[1]);
        while ((nBytes = read(pipefd[0], outBuffer, sizeof(outBuffer)-1)) > 0) {
            outBuffer[nBytes] = '\0';
            printf("%s", outBuffer);
        }
        close(pipefd[0]);
        exit(0);
    } else {  /* 父行程：將報告內容寫入 pipe */
        close(pipefd[0]);
        sprintf(outBuffer, "\n** Parking Booking Manager – Summary Report **\n\n");
        write(pipefd[1], outBuffer, strlen(outBuffer));

        sprintf(outBuffer, "\nPerformance:\n\n");
        write(pipefd[1], outBuffer, strlen(outBuffer));

        /* FCFS 報告 */
        sprintf(outBuffer, "For FCFS:\n");
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Total Number of Bookings Received: %d\n", total);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Number of Bookings Assigned: %d (%.1f%%)\n", fcfs_accepted, total > 0 ? (fcfs_accepted * 100.0 / total) : 0.0);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Number of Bookings Rejected: %d (%.1f%%)\n", fcfs_rejected, total > 0 ? (fcfs_rejected * 100.0 / total) : 0.0);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Utilization of Time Slot:\n");
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Parking: %.1f%%\n", fcfs_parking_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Battery: %.1f%%\n", fcfs_battery_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Cable: %.1f%%\n", fcfs_cable_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Locker: %.1f%%\n", fcfs_locker_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Umbrella: %.1f%%\n", fcfs_umbrella_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Valet Parking: %.1f%%\n", fcfs_valet_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Inflation Service: %.1f%%\n\n", fcfs_inflation_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));

        /* PRIO 報告 */
        sprintf(outBuffer, "For PRIO:\n");
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Total Number of Bookings Received: %d\n", total);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Number of Bookings Assigned: %d (%.1f%%)\n", prio_accepted, total > 0 ? (prio_accepted * 100.0 / total) : 0.0);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Number of Bookings Rejected: %d (%.1f%%)\n", prio_rejected, total > 0 ? (prio_rejected * 100.0 / total) : 0.0);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Utilization of Time Slot:\n");
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Parking: %.1f%%\n", prio_parking_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Battery: %.1f%%\n", prio_battery_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Cable: %.1f%%\n", prio_cable_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Locker: %.1f%%\n", prio_locker_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Umbrella: %.1f%%\n", prio_umbrella_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Valet Parking: %.1f%%\n", prio_valet_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Inflation Service: %.1f%%\n\n", prio_inflation_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));

        /* OPTI 報告 */
        sprintf(outBuffer, "For OPTI:\n");
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Total Number of Bookings Received: %d\n", total);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Number of Bookings Assigned: %d (%.1f%%)\n", opti_accepted, total > 0 ? (opti_accepted * 100.0 / total) : 0.0);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Number of Bookings Rejected: %d (%.1f%%)\n", opti_rejected, total > 0 ? (opti_rejected * 100.0 / total) : 0.0);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "  Utilization of Time Slot:\n");
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Parking: %.1f%%\n", opti_parking_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Battery: %.1f%%\n", opti_battery_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Cable: %.1f%%\n", opti_cable_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Locker: %.1f%%\n", opti_locker_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Umbrella: %.1f%%\n", opti_umbrella_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Valet Parking: %.1f%%\n", opti_valet_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        sprintf(outBuffer, "    Inflation Service: %.1f%%\n\n", opti_inflation_util);
        write(pipefd[1], outBuffer, strlen(outBuffer));

        close(pipefd[1]);
        wait(NULL);
        printf("-> [Done!]\n");
    }
}


/* 根據使用者輸入的命令進行處理 */
void process_command(char *line) {
    char commandCopy[MAX_LINE_LENGTH];
    char *token;
    strcpy(commandCopy, line);
    token = strtok(commandCopy, " ");
    if (token == NULL)
        return;
    if (strcmp(token, "addParking") == 0)
        process_addParking(line);
    else if (strcmp(token, "addReservation") == 0)
        process_addReservation(line);
    else if (strcmp(token, "addEvent") == 0)
        process_addEvent(line);
    else if (strcmp(token, "bookEssentials") == 0)
        process_bookEssentials(line);
    else if (strcmp(token, "addBatch") == 0)
        process_addBatch(line);
    else if (strcmp(token, "printBookings") == 0) {
        token = strtok(NULL, " ");
        if (token != NULL) {
            char norm[20];
            strcpy(norm, normalize_member(token));
            if (strcmp(norm, "ALL;") == 0 || strcmp(norm, "ALL") == 0){
                process_printSummary();
            }
            else if (strcmp(norm, "OPTI") == 0 || strcmp(norm, "OPTI;") == 0)
                process_printOptimized();
            else
                process_printBookings(line);
        } else {
            process_printBookings(line);
        }
    }
    else if (strncmp(token, "endProgram", 10) == 0) {
        printf("Bye!\n");
        exit(0);
    }
    else {
        printf("Unknown command.\n");
    }
}

/* Process the optimized scheduling in independent simulation:
   與 process_printSummary 中的 OPTI 模擬類似，但單獨輸出模擬結果
*/
void process_printOptimized(void) {
    int i;
    Booking temp_bookings[MAX_BOOKINGS];
    simulate_OPTI(bookings, temp_bookings, bookingCount);
    
    int pipefd[2];
    pid_t pid;
    char outBuffer[1024];
    int i2, n;
    char algorithm[10] = "OPTI";
    
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0) {
        close(pipefd[1]);
        while ((n = read(pipefd[0], outBuffer, sizeof(outBuffer)-1)) > 0) {
            outBuffer[n] = '\0';
            printf("%s", outBuffer);
        }
        close(pipefd[0]);
        exit(0);
    } else {
        close(pipefd[0]);
        sprintf(outBuffer, "\n** Parking Booking – ACCEPTED / %s **\n", algorithm);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        {
            char *members[] = {"member_A", "member_B", "member_C", "member_D", "member_E"};
            int numMembers = 5;
            int foundAnyAccepted = 0;
            int j, k;
            for (i2 = 0; i2 < numMembers; i2++) {
                int count = 0;
                Booking *memberBookings[MAX_BOOKINGS];
                int memberCount = 0;
                for (j = 0; j < bookingCount; j++) {
                    if (temp_bookings[j].accepted && strcmp(temp_bookings[j].member, members[i2]) == 0) {
                        memberBookings[memberCount++] = &temp_bookings[j];
                        count++;
                    }
                }
                if (count > 0) {
                    foundAnyAccepted = 1;
                    sprintf(outBuffer, "%s has the following bookings:\n", members[i2]);
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                    sprintf(outBuffer, "Date       Start End   Type         Device\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                    sprintf(outBuffer, "===========================================================================\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                    for (k = 0; k < memberCount; k++) {
                        int hour, minute;
                        sscanf(memberBookings[k]->time, "%d:%d", &hour, &minute);
                        int endHour = hour + (int)(memberBookings[k]->duration);
                        char endTime[6];
                        sprintf(endTime, "%02d:%02d", endHour, minute);
                        char typeStr[20];
                        if (strcmp(memberBookings[k]->type, "Essentials") == 0)
                            strcpy(typeStr, "*");
                        else
                            strcpy(typeStr, memberBookings[k]->type);
                        {
                            char deviceStr[100];
                            deviceStr[0] = '\0';
                            if (strcmp(memberBookings[k]->type, "Essentials") == 0) {
                                if (strlen(memberBookings[k]->essential1) > 0)
                                    strcpy(deviceStr, memberBookings[k]->essential1);
                                else
                                    strcpy(deviceStr, "*");
                            } else {
                                if (strlen(memberBookings[k]->essential1) > 0)
                                    strcpy(deviceStr, memberBookings[k]->essential1);
                                if (strlen(memberBookings[k]->essential2) > 0) {
                                    if (strlen(deviceStr) > 0) {
                                        strcat(deviceStr, " ");
                                        strcat(deviceStr, memberBookings[k]->essential2);
                                    } else {
                                        strcpy(deviceStr, memberBookings[k]->essential2);
                                    }
                                }
                                if (strlen(memberBookings[k]->essential3) > 0) {
                                    if (strlen(deviceStr) > 0) {
                                        strcat(deviceStr, " ");
                                        strcat(deviceStr, memberBookings[k]->essential3);
                                    } else {
                                        strcpy(deviceStr, memberBookings[k]->essential3);
                                    }
                                }
                                if (strlen(deviceStr) == 0)
                                    strcpy(deviceStr, "*");
                            }
                            {
                                char bookingLine[256];
                                sprintf(bookingLine, "%-10s %-5s %-5s %-12s %s\n",
                                        memberBookings[k]->date,
                                        memberBookings[k]->time,
                                        endTime,
                                        typeStr,
                                        deviceStr);
                                write(pipefd[1], bookingLine, strlen(bookingLine));
                            }
                        }
                    }
                    
                    sprintf(outBuffer, "\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                }
            }
            if (foundAnyAccepted) {
                sprintf(outBuffer, "- End -\n");
                write(pipefd[1], outBuffer, strlen(outBuffer));
            } else {
                sprintf(outBuffer, "No accepted bookings.\n");
                write(pipefd[1], outBuffer, strlen(outBuffer));
            }
            sprintf(outBuffer, "===========================================================================\n");
            write(pipefd[1], outBuffer, strlen(outBuffer));
        }
        
        /* --- Print Rejected Bookings --- */
        sprintf(outBuffer, "\n** Parking Booking – REJECTED / %s **\n", algorithm);
        write(pipefd[1], outBuffer, strlen(outBuffer));
        {
            char *members[] = {"member_A", "member_B", "member_C", "member_D", "member_E"};
            int numMembers = 5;
            int foundAnyRejected = 0;
            int j, k;
            for (i2 = 0; i2 < numMembers; i2++) {
                int count = 0;
                Booking *memberBookings[MAX_BOOKINGS];
                int memberCount = 0;
                for (j = 0; j < bookingCount; j++) {
                    if (!temp_bookings[j].accepted && strcmp(temp_bookings[j].member, members[i2]) == 0) {
                        memberBookings[memberCount++] = &temp_bookings[j];
                        count++;
                    }
                }
                if (count > 0) {
                    foundAnyRejected = 1;
                    sprintf(outBuffer, "%s (there are %d bookings rejected):\n", members[i2], count);
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                    sprintf(outBuffer, "Date       Start End   Type         Essentials\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                    sprintf(outBuffer, "===========================================================================\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                    for (k = 0; k < memberCount; k++) {
                        int hour, minute;
                        sscanf(memberBookings[k]->time, "%d:%d", &hour, &minute);
                        int endHour = hour + (int)(memberBookings[k]->duration);
                        char endTime[6];
                        sprintf(endTime, "%02d:%02d", endHour, minute);
                        char typeStr[20];
                        strcpy(typeStr, memberBookings[k]->type);
                        {
                            char essStr[100];
                            essStr[0] = '\0';
                            if (strcmp(memberBookings[k]->type, "Essentials") == 0) {
                                if (strlen(memberBookings[k]->essential1) > 0)
                                    strcpy(essStr, memberBookings[k]->essential1);
                                else
                                    strcpy(essStr, "-");
                            } else {
                                if (strlen(memberBookings[k]->essential1) > 0)
                                    strcpy(essStr, memberBookings[k]->essential1);
                                if (strlen(memberBookings[k]->essential2) > 0) {
                                    if (strlen(essStr) > 0) {
                                        strcat(essStr, " ");
                                        strcat(essStr, memberBookings[k]->essential2);
                                    } else {
                                        strcpy(essStr, memberBookings[k]->essential2);
                                    }
                                }
                                if (strlen(memberBookings[k]->essential3) > 0) {
                                    if (strlen(essStr) > 0) {
                                        strcat(essStr, " ");
                                        strcat(essStr, memberBookings[k]->essential3);
                                    } else {
                                        strcpy(essStr, memberBookings[k]->essential3);
                                    }
                                }
                                if (strlen(essStr) == 0)
                                    strcpy(essStr, "-");
                            }
                            {
                                char bookingLine[256];
                                sprintf(bookingLine, "%-10s %-5s %-5s %-12s %s\n",
                                        memberBookings[k]->date,
                                        memberBookings[k]->time,
                                        endTime,
                                        typeStr,
                                        essStr);
                                write(pipefd[1], bookingLine, strlen(bookingLine));
                            }
                        }
                    }
                    sprintf(outBuffer, "\n");
                    write(pipefd[1], outBuffer, strlen(outBuffer));
                }
            }
            if (foundAnyRejected) {
                sprintf(outBuffer, "- End -\n");
                write(pipefd[1], outBuffer, strlen(outBuffer));
            } else {
                sprintf(outBuffer, "No rejected bookings.\n");
                write(pipefd[1], outBuffer, strlen(outBuffer));
            }
            sprintf(outBuffer, "===========================================================================\n");
            write(pipefd[1], outBuffer, strlen(outBuffer));
        }
        
        close(pipefd[1]);
        wait(NULL);
        printf("-> [Done!]\n");
    }
}

/* Main 函式 */
int main() {
    char input[MAX_LINE_LENGTH];
    printf("~ WELCOME TO PolyU ~\n");
    while (1) {
        printf("Please enter booking:\n");
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;
        input[strcspn(input, "\n")] = '\0';
        if (strlen(input) == 0)
            continue;
        process_command(input);
    }
    return 0;
}