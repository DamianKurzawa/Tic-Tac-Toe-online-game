#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <syslog.h>
#include <netdb.h>
#include <ctype.h>
#include <signal.h> 
#include <errno.h>
#include <fcntl.h>

#define BOARD_SIZE 9
#define MAX_CLIENTS 100
#define MAX_LINE 256
#define MAX_NICK_LEN 50
#define MULTICAST_ADDR "239.0.0.1"
#define MULTICAST_PORT 12345
#define SERVER_ADDR "192.168.56.2"
#define SERVER_PORT 13

typedef struct {
    char ip[50];
    char nick[50];
    int wins;
    int losses;
    int draws;
} Player;
Player players[100];


//Segment do sprawdzania czy w bazie istnieje dany nick, bądź czy Adres IP nie wpisał złego nicku----------------------------------------------
// Funkcja wczytująca dane graczy z pliku
int load_players(const char *filename, Player players[], int *count) {
    FILE *file = fopen(filename, "r");

    // Jeśli plik nie istnieje, otwórz go do zapisu (i utwórz nowy plik)
    if (!file) {
        file = fopen(filename, "w");
        if (!file) {
            return 0;  // Jeśli nadal nie udało się otworzyć pliku
        }
        fclose(file);  // Zamknij nowo utworzony plik (nic w nim nie ma)
        return 1;  // Plik został stworzony, ale nie zawiera danych
    }

    char line[MAX_LINE];
    *count = 0;

    while (fgets(line, sizeof(line), file)) {
        sscanf(line, "IP: %s nick: %s Wygrane: %d Przegrane: %d Remisy: %d\n",
               players[*count].ip, players[*count].nick, 
               &players[*count].wins, &players[*count].losses, &players[*count].draws);
        (*count)++;
    }

    fclose(file);
    return 1;
}
// Funkcja sprawdzająca unikalność nicka i adresu IP
int is_nick_valid(const char *ip, const char *nick, Player players[], int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(players[i].nick, nick) == 0) {
            // Nick zajęty przez inny adres IP
            if (strcmp(players[i].ip, ip) != 0) {
                return 0; // Nick jest zajęty
            }
        }
        if (strcmp(players[i].ip, ip) == 0) {
            // Ten adres IP już istnieje, sprawdź, czy nick się zgadza
            if (strcmp(players[i].nick, nick) != 0) {
                return -1; // Próba zmiany nicka
            }
        }
    }
    return 1; // Nick jest unikalny
}
// Funkcja zapisująca dane graczy do pliku
void save_players(const char *filename, Player players[], int count) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        syslog(LOG_ERR, "Nie można zapisać pliku: %s", strerror(errno));
        return;
    }

    for (int i = 0; i < count; i++) {
        fprintf(file, "IP: %s nick: %s Wygrane: %d Przegrane: %d Remisy: %d\n", players[i].ip, players[i].nick, 
                players[i].wins, players[i].losses, players[i].draws);
    }
    fclose(file);
}
//Koniec Segmentu ----------------------------------------------------------------------------------------------------------------------


// Sprawdzenie, czy dany ciąg znaków jest liczbą całkowitą
int is_number(const char *str) {
    while (*str) {
        if (!isdigit(*str)) {
            return 0;  // Jeśli napotkamy znak, który nie jest cyfrą, zwracamy 0 (fałsz)
        }
        str++;
    }
    return 1;  // Jeśli wszystkie znaki to cyfry, zwracamy 1 (prawda)
}

//Atkualizacja danych po skończeniu gry --------------------------------------------------------------------------------------------------------------------------------
// Funkcja do sprawdzenia, czy gracz już istnieje w pliku
int player_exists_in_file(const char *ip, const char *nick, Player *player) {
    FILE *file = fopen("players_log.txt", "r");
    if (file == NULL) {
        return 0;  // Jeśli plik nie istnieje, gracz na pewno nie istnieje
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        // Przeszukujemy linię, aby znaleźć gracza
        if (strstr(line, ip) != NULL || strstr(line, nick) != NULL) {
            // Parsujemy dane gracza
           sscanf(line, "IP: %49s nick: %49s Wygrane: %d Przegrane: %d Remisy: %d",
       player->ip, player->nick, &player->wins, &player->losses, &player->draws);
            fclose(file);
            return 1;  // Gracz już istnieje, dane są załadowane
        }
    }

    fclose(file);
    return 0;  // Gracz nie istnieje
}

// Funkcja do zapisania danych gracza do pliku, z aktualizacją
void save_player_info_to_file(const char *ip, const char *nick, int result) {
    Player player;
    
    // Sprawdzamy, czy gracz już istnieje
    if (player_exists_in_file(ip, nick, &player)) {
        // Aktualizujemy statystyki gracza
        switch (result) {
            case 1:  // Wygrana
                player.wins++;
                break;
            case -1: // Przegrana
                player.losses++;
                break;
            case 2:  // Remis
                player.draws++;
                break;
        }
    } else {
        // Jeśli gracz nie istnieje, tworzymy nowego gracza
        strncpy(player.ip, ip, sizeof(player.ip) - 1);
        strncpy(player.nick, nick, sizeof(player.nick) - 1);
        player.wins = (result == 1) ? 1 : 0;
        player.losses = (result == -1) ? 1 : 0;
        player.draws = (result == 2) ? 1 : 0;
    }

    // Otwieramy plik w trybie do odczytu i zapisu
    FILE *file = fopen("players_log.txt", "r+");
    if (file == NULL) {
        // Jeśli plik nie istnieje, tworzysz go
        file = fopen("players_log.txt", "w+");
        if (file == NULL) {
            syslog(LOG_ERR, "Nie udalo sie owtorzyc lub utworzyc pliku: %s", strerror(errno));
            return;
        }
    }

    char line[MAX_LINE];
    FILE *temp_file = fopen("temp_players_log.txt", "w");
    if (temp_file == NULL) {
        syslog(LOG_ERR, "Nie udalo sie otworzyc pliku: %s", strerror(errno));
        fclose(file);
        return;
    }

    int found = 0;
    // Przechodzimy po liniach w pliku
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, ip) || strstr(line, nick)) {
            // Jeśli znaleziono gracza, aktualizujemy jego dane
            fprintf(temp_file, "IP: %s nick: %s Wygrane: %d Przegrane: %d Remisy: %d\n",
                    player.ip, player.nick, player.wins, player.losses, player.draws);
            found = 1;
        } else {
            // Jeśli gracz nie został znaleziony, zapisujemy oryginalną linię
            fputs(line, temp_file);
        }
    }

    // Jeśli nie znaleziono gracza, dopisujemy go na koniec pliku
    if (!found) {
        fprintf(temp_file, "IP: %s nick: %s Wygrane: %d Przegrane: %d Remisy: %d\n",
                player.ip, player.nick, player.wins, player.losses, player.draws);
    }

    fclose(file);
    fclose(temp_file);

    // Nadpisujemy oryginalny plik tymczasowym
    remove("players_log.txt");
    rename("temp_players_log.txt", "players_log.txt");
}
//Koniec aktualizacji danych ---------------------------------------------------------------------------------------------------------------------------------------

// Funkcja do sortowania graczy według wygranych
int compare_players(const void *a, const void *b) {
    return ((Player *)b)->wins - ((Player *)a)->wins;  // Sortowanie malejąco po liczbie wygranych
}

typedef struct {
    uint8_t type;
    uint8_t length;
    char value[256];
} TLVMessage;

typedef struct {
    int player1;
    int player2;
    char board[BOARD_SIZE];
    int current_turn; // 0 - gracza 1, 1 - gracza 2
    char player1_nick[50]; // Nazwa gracza 1
    char player2_nick[50]; // Nazwa gracza 2
    char player1_ip[INET_ADDRSTRLEN]; // Adres IP gracza 1
    char player2_ip[INET_ADDRSTRLEN]; // Adres IP gracza 2
    int game_started; // Flaga oznaczająca, czy gra się rozpoczęła
    int game_running;
} Game;

int server_fd;
Game games[MAX_CLIENTS / 2];
int game_count = 0;
pthread_mutex_t lock;
int game_running=1;
int gameindex;
void broadcast_game_state(Game *game);

// Signal handler gdy serwer jest wyłączany
void handle_signal(int signal) {
    syslog(LOG_NOTICE, "Zamykanie serwera...");
    close(server_fd);
    closelog();
    exit(0);
}
void handle_sigpipe(int signal) {
    syslog(LOG_NOTICE, "Wykryto sygnal SIGPIPE.Gracz się rozlaczyl.");
     // Oznaczenie, że gra nie jest już aktywna
    games[gameindex].game_running = 0;
    games[gameindex].game_started = 0; // Gra zakończona
    close(games[gameindex].player1);
    close(games[gameindex].player2);
}

// Inicjalizacja planszy
void init_board(char *board) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        board[i] = ' ';
    }
}

// wysyłanie wiadomości tlv
int send_tlv(int sockfd, uint8_t type, const char *value) {
    TLVMessage msg;
    msg.type = type;
    msg.length = strlen(value);
    strncpy(msg.value, value, sizeof(msg.value) - 1);
    msg.value[sizeof(msg.value) - 1] = '\0';
    
    if(send(sockfd, &msg, sizeof(msg.type) + sizeof(msg.length) + msg.length, 0) == -1){
        syslog(LOG_ERR, "Klient zakończył połączenie: %s", strerror(errno));
        close(sockfd);
        return 1;
    }
    return 0;
}

// Sprawdzanie czy gracz wygrał
int check_winner(char *board) {
    int win_conditions[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, // wiersze
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, // kolumny
        {0, 4, 8}, {2, 4, 6}            // po skosie
    };

    for (int i = 0; i < 8; i++) {
        if (board[win_conditions[i][0]] != ' ' &&
            board[win_conditions[i][0]] == board[win_conditions[i][1]] &&
            board[win_conditions[i][1]] == board[win_conditions[i][2]]) {
            return 1;
        }
    }
    return 0;
}

// Sprawdzanie czy plansza jest zapełniona (remis)
int check_draw(char *board) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        if (board[i] == ' ') return 0;
    }
    return 1;
}

// Szukanie lub tworzenie gry dla klienta
int find_or_create_game(int sockfd) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < game_count; i++) {
        if (games[i].player2 == -1 && !games[i].game_started) {
            char buffer[1024];
            // Drugi gracz się połączył
            games[i].player2 = sockfd;
            games[i].game_started = 1;  // Gra się zaczyna

            // Powiadomienie pierwszego gracza, że drugi gracz się połączył
            send_tlv(games[i].player1, 0, "Drugi gracz dołączył");
            sleep(1);

            // Powiadamiamy obu graczy, że gra się rozpoczęła
            send_tlv(games[i].player1, 0, "--------------- Gra się zaczęła -------------\n");
            send_tlv(games[i].player2, 0, "--------------- Gra się zaczęła -------------\n");
            // Wysyłanie komunikatu o turze gracza
            sleep(1);
            send_tlv(games[i].player1, 4, "--------------- Twoja kolej ---------------\n");
            snprintf(buffer, sizeof(buffer), "---------------Kolej gracza: %s ---------------\n", games[i].player1_nick);
            send_tlv(games[i].player2, 4, buffer);
            sleep(1);
            broadcast_game_state(&games[i]);

            pthread_mutex_unlock(&lock);
            return i;
        }
    }

    // Jeśli nie znaleziono gry, tworzymy nową
    games[game_count].player1 = sockfd;
    games[game_count].player2 = -1;
    init_board(games[game_count].board);
    games[game_count].current_turn = 0;
    games[game_count].game_started = 0;  // Gra nie rozpoczęta
    game_count++;

    //komunikat o stworzeniu gry
    syslog(LOG_INFO, "Stworzono nową gre %d, Gracz 1: %d\n", game_count - 1, sockfd);

    pthread_mutex_unlock(&lock);
    return game_count - 1;
}

// Obsługa ruchów graczy
void handle_move(Game *game, int sockfd, int position) {
    char buffer[1024];
   
    if (!game->game_running) {
        return;
    }
    // zmiana numeracji z 1-9 na 0-8
    position = position - 1; // Konwertujemy na indeks od 0 do 8

    // Sprawdzamy, czy pole jest już zajęte
    if (game->board[position] != ' ') {
        sleep(0.5);
        send_tlv(sockfd, 2, "Pole jest już zajęte");
        return;
    }    

    // Sprawdzamy, który gracz wykonuje ruch
   if (game->current_turn == 0 && sockfd == game->player1) {
        game->board[position] = 'X';
        game->current_turn = 1;
        if (check_winner(game->board)) {
            send_tlv(game->player1, 5, "--------------- Wygrałeś ---------------\n");
            send_tlv(game->player2, 5, "--------------- Przegrałeś ---------------\n");
            game->game_running=0;
            broadcast_game_state(game);
            save_player_info_to_file(game->player1_ip, game->player1_nick, 1);
            save_player_info_to_file(game->player2_ip, game->player2_nick, -1);
            return;  // Kończymy grę
        }
        if (check_draw(game->board)) {
            send_tlv(game->player1, 5, "--------------- Remis ---------------\n");
            send_tlv(game->player2, 5, "--------------- Remis ---------------\n");
            game->game_running=0;
            broadcast_game_state(game);
            save_player_info_to_file(game->player1_ip, game->player1_nick, 2);
            save_player_info_to_file(game->player2_ip, game->player2_nick, 2);
            return;  // Kończymy grę
        }
        snprintf(buffer, sizeof(buffer), "--------------- Kolej gracza: %s ---------------\n", game->player2_nick);
        if(send_tlv(game->player1, 4, buffer)==1){
            game->game_running=0;
            return; //Drugi gracz opuścił gre;
        }
        if(send_tlv(game->player2, 4, "--------------- Twoja kolej ---------------\n")==1){
            game->game_running=0;
            return; //Drugi gracz opuścił gre;
        }
    } else if (game->current_turn == 1 && sockfd == game->player2) {
        game->board[position] = 'O';
        game->current_turn = 0;
        if (check_winner(game->board)) {
            send_tlv(game->player2, 5, "--------------- Wygrałeś ---------------\n");
            send_tlv(game->player1, 5, "--------------- Przegrałeś ---------------\n");
            game->game_running=0;
            broadcast_game_state(game);
            save_player_info_to_file(game->player1_ip, game->player1_nick, -1);
            save_player_info_to_file(game->player2_ip, game->player2_nick, 1);
            return;  // Kończymy grę
        }
        if (check_draw(game->board)) {
            send_tlv(game->player1, 5, "--------------- Remis ---------------\n");
            send_tlv(game->player2, 5, "--------------- Remis ---------------\n");
            game->game_running=0;
            broadcast_game_state(game);
            save_player_info_to_file(game->player1_ip, game->player1_nick, 0);
            save_player_info_to_file(game->player2_ip, game->player2_nick, 0);
            return;  // Kończymy grę
        }
        sleep(0.5);
        if(send_tlv(game->player1, 4, "--------------- Twoja kolej ---------------\n")==1){
            game->game_running=0;
            return; //Drugi gracz opuścił gre;
        }
        snprintf(buffer, sizeof(buffer), "--------------- Kolej gracza: %s ---------------\n", game->player1_nick);
        if(send_tlv(game->player2, 4, buffer)==1){
            game->game_running=0;
            return; //Drugi gracz opuścił gre;
        }
    } else {
        send_tlv(sockfd, 3, "--------------- Nie twoja kolej ---------------\n");
        return;
    }
    
    broadcast_game_state(game);
}

// Wysyłanie planszy do obydwu graczy
void broadcast_game_state(Game *game) {
    if (game->player1 != -1) {
        char buffer[1024]={0};
        snprintf(buffer, sizeof(buffer),
        "BOARD\n %c | %c | %c\n---+---+---\n %c | %c | %c\n---+---+---\n %c | %c | %c",
        game->board[0], game->board[1], game->board[2],
        game->board[3], game->board[4], game->board[5],
        game->board[6], game->board[7], game->board[8]);
        sleep(0.5);
        send_tlv(game->player1, 1, buffer);
        if (game->player2 != -1) {
            sleep(0.5);
            send_tlv(game->player2, 1, buffer);
        }
    }
}

// Funkcja obslugi klienta
void *client_handler(void *arg) {
    int sockfd = *(int *)arg;
    free(arg);

    char buffer[1024];
    TLVMessage msg;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int player_count = 0;
    int menu_active=1;

    //Pętla wyboru z menu
    while(menu_active){
    // Wczytanie danych graczy z pliku
    load_players("players_log.txt", players, &player_count);

    // Odbieranie wyboru
    ssize_t bytes_received = recv(sockfd, &msg, sizeof(msg.type) + sizeof(msg.length) + 256, 0);
    msg.value[msg.length] = '\0';
    if (bytes_received <= 0) {
        close(sockfd);
        return NULL;
    }

    if (strcmp(msg.value, "1") == 0 && msg.type == 1) {
        // Graj w grę
        game_running = 1;
    // Pobieranie adres IP klienta
    if (getpeername(sockfd, (struct sockaddr *)&client_addr, &addr_len) == -1) {
        syslog(LOG_ERR, "getpeername failed: %s", strerror(errno));
        close(sockfd);
        return NULL;
    }
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    syslog(LOG_INFO, "Połączony Gracz: %s\n", client_ip);

    // Walidacja nicku gracza
    char nick[MAX_NICK_LEN];
    int valid = 0;
    do {
        // Oczekiwanie na nick od klienta
        ssize_t bytes_received = recv(sockfd, &msg, sizeof(msg.type) + sizeof(msg.length) + 256, 0);
        if (bytes_received <= 0 || msg.type != 2) {
            //Nie otrzymano nicku
            close(sockfd);
            pthread_exit(NULL);
        }
        msg.value[msg.length] = '\0';
        valid = is_nick_valid(client_ip, msg.value, players, player_count);

        if (valid == 0) {
            // Nick zajęty przez innego gracza
            send_tlv(sockfd, 0, "Nick zajęty. Podaj inny nick.\n");
        } else if (valid == -1) {
            // Próba zmiany nicka
            send_tlv(sockfd, 0, "Nie możesz zmienić nicka. Podaj poprawny nick.\n");
        }
    } while (valid <= 0);

    // Jeśli nick jest unikalny, dodaj nowego gracza lub zaktualizuj istniejącego
    int found = 0;
    for (int i = 0; i < player_count; i++) {
        if (strcmp(players[i].ip, client_ip) == 0) {
            found = 1;
            break;
        }
    }
    if (!found) {
        // Dodaj nowego gracza
        strcpy(players[player_count].ip, client_ip);
        strncpy(players[player_count].nick, msg.value, MAX_NICK_LEN - 1);
        players[player_count].nick[MAX_NICK_LEN - 1] = '\0';
        players[player_count].wins = 0;
        players[player_count].losses = 0;
        players[player_count].draws = 0;
        player_count++;
    }

    // Zapisz dane graczy do pliku
    save_players("players_log.txt", players, player_count);

    // Potwierdzenie
    sleep(0.5);
    send_tlv(sockfd, 0, "Nick zaakceptowany.");


    // Tworzymy lub znajdujemy odpowiednią grę
    int game_index = find_or_create_game(sockfd);
    gameindex=game_index;
    games[game_index].game_running=1;

    pthread_mutex_lock(&lock);  // Blokuje mutex podczas aktualizacji gier

    // Przypisanie nicku i IP gracza do gry
    if (games[game_index].player1 == sockfd) {
        strncpy(games[game_index].player1_nick, msg.value, sizeof(games[game_index].player1_nick) - 1);
        games[game_index].player1_nick[sizeof(games[game_index].player1_nick) - 1] = '\0';
        strncpy(games[game_index].player1_ip, client_ip, sizeof(games[game_index].player1_ip) - 1);
        games[game_index].player1_ip[sizeof(games[game_index].player1_ip) - 1] = '\0';
    } else if (games[game_index].player2 == sockfd) {
        strncpy(games[game_index].player2_nick, msg.value, sizeof(games[game_index].player2_nick) - 1);
        games[game_index].player2_nick[sizeof(games[game_index].player2_nick) - 1] = '\0';
        strncpy(games[game_index].player2_ip, client_ip, sizeof(games[game_index].player2_ip) - 1);
        games[game_index].player2_ip[sizeof(games[game_index].player2_ip) - 1] = '\0';
    }
    pthread_mutex_unlock(&lock);  // Odblokuj mutex po aktualizacji gier

    // Czekamy na drugiego gracza, jeśli to konieczne
    if (games[game_index].player2 == -1) {
        sleep(0.5);
        send_tlv(sockfd, 0, "Oczekiwanie na drugiego gracza");
        while (games[game_index].player2 == -1) {
            sleep(0.5);
        }
    }

    // Pętla gry
    while (games[game_index].game_running) {
        memset(buffer, 0, sizeof(buffer));
        if(!games[game_index].game_running){
            break;
        }
        // Oczekiwanie na dane
        ssize_t bytes_received = recv(sockfd, &msg, sizeof(msg.type) + sizeof(msg.length) + 256, 0);
        if (bytes_received <= 0 || msg.type != 3) {
            close(sockfd);
            break;
        }
        // Sprawdzamy, czy dane to liczba (pozycja na planszy)
        int position = atoi(msg.value);
        if (position == 0 && strcmp(buffer, "0") != 0) {
            // Jeżeli wartość to nie liczba, wysyłamy komunikat o błędzie
            sleep(0.5);
            send_tlv(sockfd, 2, "Niewłaściwy ruch");
            continue;
        }

        // Przetwarzanie ruchu
        handle_move(&games[game_index], sockfd, position);
        if(!games[game_index].game_running){
            break;
        }
    }
    } else if (strcmp(msg.value, "2") == 0) {
        // Wyświetl ranking
        qsort(players, player_count, sizeof(Player), compare_players);
        
        char ranking[1024] = "---------- Ranking graczy: ----------\n";
        for (int i = 0; i < player_count; i++) {
            char player_info[256];
            snprintf(player_info, sizeof(player_info), "%s - Wygrane: %d Przegrane: %d Remisy: %d\n", players[i].nick, players[i].wins, players[i].losses, players[i].draws);
            strncat(ranking, player_info, sizeof(ranking) - strlen(ranking) - 1);
        }
        sleep(0.5);
        send_tlv(sockfd, 7, ranking);
    } else if (strcmp(msg.value, "3") == 0){
        menu_active = 0;
        syslog(LOG_INFO, "Klient zamyka połączenie.\n");
    }
    }
    // Zamykanie połączenia z klientem
    close(sockfd);
    pthread_exit(NULL);
}

//Oczekiwanie na zapytanie klienta przez multicast
void *multicast_listener(void *arg) {
    int multicast_sock;
    struct sockaddr_in multicast_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[1024];

    multicast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (multicast_sock < 0) {
        syslog(LOG_ERR, "Multicast socket creation failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    if (bind(multicast_sock, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0) {
        syslog(LOG_ERR, "Bind failed for multicast socket: %s", strerror(errno));
        close(multicast_sock);
        exit(EXIT_FAILURE);
    }

    struct ip_mreq multicast_request;
    multicast_request.imr_multiaddr.s_addr = inet_addr(MULTICAST_ADDR);
    multicast_request.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(multicast_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &multicast_request, sizeof(multicast_request)) < 0) {
        syslog(LOG_ERR, "Failed to join multicast group: %s", strerror(errno));
        close(multicast_sock);
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "Oczekiwanie na zapytanie przez mutlicast...\n");

    while (1) {
        ssize_t bytes_received = recvfrom(multicast_sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (bytes_received < 0) {
            syslog(LOG_ERR, "Failed to receive multicast message: %s", strerror(errno));
            continue;
        }

        buffer[bytes_received] = '\0';
        syslog(LOG_INFO, "Otrzymano zapytanie przez multicast\n");

        if (strcmp(buffer, "Szukam_serwera") == 0) {
            char response[1024];
            snprintf(response, sizeof(response), "SERWER %s %d", SERVER_ADDR, SERVER_PORT);
            if (sendto(multicast_sock, response, sizeof(response), 0, (struct sockaddr *)&client_addr, client_addr_len) < 0) {
                syslog(LOG_ERR, "Failed to send unicast response: %s", strerror(errno));
            } else {
                syslog(LOG_INFO, "Wysłano odpowiedz: %s\n", response);
            }
        }
    }

    close(multicast_sock);
    return NULL;
}

int demon_init(const char *pname, int facility) {
    int i, p;
    pid_t pid;

    // Pierwszy fork - przekształcamy proces w tło
    if ((pid = fork()) < 0)
        return (-1);
    else if (pid)
        exit(0);  // Ojciec kończy działanie

    // Dziecko kontynuuje

    if (setsid() < 0)  // Tworzymy nową sesję i stajemy się liderem
        return (-1);

    signal(SIGHUP, SIG_IGN);  // Ignorowanie sygnału SIGHUP
    if ((pid = fork()) < 0)
        return (-1);
    else if (pid)
        exit(0);  // Pierwsze dziecko kończy

    // Drugie dziecko kontynuuje

    chdir("/tmp");  // Zmiana katalogu roboczego
    // chroot("/tmp"); // Opcjonalnie można użyć chroot, jeśli potrzebne

    // Zamknięcie wszystkich otwartych deskryptorów plików
    for (i = 0; i < sysconf(_SC_OPEN_MAX); i++) {
        close(i);
    }

    // Przekierowanie stdin, stdout i stderr do /dev/null
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);

    openlog(pname, LOG_PID | LOG_CONS, facility);
    return (0);  // Sukces
}

int main() {
    struct sockaddr_in server_addr;
    socklen_t server_addr_len = sizeof(server_addr);
    pthread_t client_tid, multicast_tid;

    // Inicjalizacja demona
    if (demon_init("TicTacToeServer", LOG_USER) == -1) {
        syslog(LOG_ERR, "Demon initialization failed: %s", strerror(errno));
        exit(1);
    }

    // Ustawienie sygnałów
    signal(SIGINT, handle_signal);
    signal(SIGPIPE, handle_signal);

    // Tworzymy gniazdo serwera
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        exit(1);
    }

    if (listen(server_fd, MAX_CLIENTS) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        exit(1);
    }

    syslog(LOG_INFO, "Server started on port %d", SERVER_PORT);

    // Uruchamianie wątku nasłuchującego multicast
    pthread_create(&multicast_tid, NULL, multicast_listener, NULL);

    // Obsługuje połączenia od klientów
    while (1) {
        int *new_socket = malloc(sizeof(int));
        *new_socket = accept(server_fd, (struct sockaddr *)&server_addr, &server_addr_len);
        if (*new_socket < 0) {
            syslog(LOG_ERR, "Accept failed");
            free(new_socket);
            continue;
        }

        pthread_create(&client_tid, NULL, client_handler, new_socket);
        pthread_detach(client_tid);
    }

    syslog(LOG_INFO, "Shutting down server.");
    closelog();
    pthread_mutex_destroy(&lock);
    return 0;
}