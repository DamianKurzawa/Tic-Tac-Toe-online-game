#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stddef.h> // dla NULL

#define PORT 13
#define MULTICAST_ADDR "239.0.0.1"
#define MULTICAST_PORT 12345

typedef struct {
    uint8_t type;
    uint8_t length;
    char value[1024];
} TLVMessage;

int sockfd;
pthread_t receive_thread;
char current_turn[50] = ""; // Zmienna przechowująca, kto ma teraz turę
int has_displayed_board = 0;  // Flaga do wyświetlania planszy tylko raz
int valid_nick = 0; //Flaga do sprawdzania poprawności nicku
int game_result=0; //Flaga do zakończenia gry


// Funkcja do wysyłania wiadomości TLV
void send_tlv(int sockfd, uint8_t type, const char *value) {
    TLVMessage msg;
    msg.type = type;
    msg.length = strlen(value);
    strncpy(msg.value, value, sizeof(msg.value) - 1);
    msg.value[sizeof(msg.value) - 1] = '\0';  // Zabezpieczenie przed brakiem zakończenia

    ssize_t sent_bytes = send(sockfd, &msg, sizeof(msg.type) + sizeof(msg.length) + msg.length, 0);
    if (sent_bytes < 0) {
        perror("Błąd wysyłania wiadomości");
    }
}

// Funkcja do wysyłania ruchu (liczby)
void send_move(int sockfd, char *position) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%s", position);  // Zamiana liczby na string
    send(sockfd, buffer, strlen(buffer), 0);  // Wysyłanie ruchu
}

// Funkcja odbierająca wiadomości
void receive_message(int sockfd) {
    TLVMessage msg;
    char current_turn[50] = ""; // Przechowuje turę (np. "Player 1")

    while (1) {
        ssize_t bytes_received = recv(sockfd, &msg, sizeof(msg.type) + sizeof(msg.length) + 256, 0);
        msg.value[msg.length] = '\0';
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("Serwer zamknął połączenie.\n");
            } else {
                perror("Błąd odbierania danych");
            }
            break;
        }

       if (msg.type == 0) {
            // Wiadomość z serwera
            printf("%s\n", msg.value);
            // Jeśli nick jest poprawny, zmieniamy flagę
            if (strcmp(msg.value, "Nick zaakceptowany.") == 0) {
                valid_nick = 1;  // Akceptacja nicku
            }
        } else if (msg.type == 1) {
            // Otrzymywanie planszy
                printf("%s\n", msg.value);
        } else if (msg.type >=2 && msg.type <=4) {
            printf("%s\n", msg.value);
        } else if (msg.type == 5) {
            printf("%s\n",msg.value);
            sleep(0.5);
            ssize_t last_board = recv(sockfd, &msg, sizeof(msg.type) + sizeof(msg.length) + 256, 0);
            msg.value[msg.length] = '\0';
            printf("%s\n",msg.value);
            game_result=1;
            printf("Wprowadź jakąś cyfre, żeby wrócić do menu\n");
            break;
        } else if (msg.type == 6) {
            printf("%s\n", msg.value);
       }
    }
}

// Funkcja wątku do odbierania wiadomości
void *receive_thread_func(void *arg) {
    receive_message(sockfd);
    return NULL;
}
void clear_recv_buffer(int sockfd) {
    char temp[256];
    while (recv(sockfd, temp, sizeof(temp), MSG_DONTWAIT) > 0) {
        // Opróżnianie bufora
    }
}
//Szukanie serwera poprzez zapytanie na adres multicast
void discover_server(char *server_ip, int *server_port) {
    int multicast_sock;
    struct sockaddr_in multicast_addr, recv_addr;
    char buffer[1024];
    socklen_t recv_addr_len = sizeof(recv_addr);

    multicast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (multicast_sock < 0) {
        perror("Błąd utworzenia gniazda");
        exit(EXIT_FAILURE);
    }

    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_ADDR);
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    strcpy(buffer, "Szukam_serwera");

    //Wysłani wiadomości na adres multicast
    if (sendto(multicast_sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0) {
        perror("Błąd wysyłania wiadomości muticast");
        close(multicast_sock);
        exit(EXIT_FAILURE);
    }
    
    //Odebranie wiadomosci od serwera z jego adresem ip i portem
    ssize_t bytes_received = recvfrom(multicast_sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&recv_addr, &recv_addr_len);
    if (bytes_received < 0) {
        perror("Błąd otrzymywania odpowiedzi serwera");
        close(multicast_sock);
        exit(EXIT_FAILURE);
    }

    buffer[bytes_received] = '\0';
    sscanf(buffer, "SERWER %s %d", server_ip, server_port);

    printf("Znaleziony serwer: %s %d\n", server_ip, *server_port);
    close(multicast_sock);
}

int main(int argc, char **argv) {
    struct sockaddr_in server_addr;
    char server_ip[INET_ADDRSTRLEN];
    int server_port;
    char username[50];
    char choice;
    char position[10];
    TLVMessage msg;

    //Łączenie poprzez multicast
    if (argc != 2){
        discover_server(server_ip, &server_port);
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    //Łączenie poprzez wpisanie adresu serwera jako argument ./klinet
    }else if(argc == 2){
        server_addr.sin_family = AF_INET;
	    server_addr.sin_port   = htons(PORT);	/* echo server */
	    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0){
		    fprintf(stderr,"inet_pton error for %s : \n", argv[1]);
		    return 1;
	    }
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Błąd utworzenia gniazda");
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Błąd połączenia");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    while(1){
        clear_recv_buffer(sockfd);
        printf("1 - Zagraj w gre\n2 - Ranking\n3 - Wyjdź\n");
        scanf("%s", &choice);\
        send_tlv(sockfd, 1, &choice);
        game_result=0;
        if(choice=='1'){
            printf("------------ Podaj nazwe użytkownika: ------------\n");
            do{
                scanf("%s", username);
                // Wyślij nazwę użytkownika do serwera
                send_tlv(sockfd, 2, username);
                ssize_t bytes_received = recv(sockfd, &msg, sizeof(msg.type) + sizeof(msg.length) + 256, 0);
                msg.value[msg.length] = '\0';
                if(msg.type == 0) {
                    printf("%s\n", msg.value);
                    if (strcmp(msg.value, "Nick zaakceptowany.") == 0) {
                    valid_nick = 1;  // Akceptacja nicku
                    }
                }
            }
            while(!valid_nick);

            // Tworzymy nowy wątek do odbierania wiadomości
            pthread_create(&receive_thread, NULL, receive_thread_func, NULL);
            // W tym momencie klient może wykonywać swoje ruchy bez blokowania odbierania nowych wiadomości
            while (!game_result) {
                while (1) {
                    scanf("%s", &position);
                    if(strlen(position) == 1 && position[0] >= '1' && position[0] <= '9'){
                        break;
                    }else{
                        printf("Niewłaściwy ruch. Wpisz numer od 1 do 9: \n");
                    }
                }
                send_tlv(sockfd, 3, position);  // Wysłanie ruchu
            }
        // Czekaj aż wątek odbierający zakończy swoją pracę
        pthread_join(receive_thread, NULL); 
        }else if(choice=='2'){
            ssize_t bytes_received = recv(sockfd, &msg, sizeof(msg.type) + sizeof(msg.length) + 256, 0);
            msg.value[msg.length] = '\0';
            if(msg.type == 7) {
                printf("%s\n", msg.value);
            }
        }else if(choice=='3'){
            break;
        }else {
            printf("Nieprawidłowa wartość, spróbuj ponownie\n");
        }
    }
    close(sockfd);
    return 0;
}
