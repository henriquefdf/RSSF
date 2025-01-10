#include "common.h"

#define BACKLOG 20   // Quantidade de conexões pendentes permitidas

// Estrutura para guardar info de cada cliente conectado
typedef struct {
    int socket_fd;
    char type[MAX_TYPE_LEN];
    int x, y;
} client_info_t;

// Lista global de clientes
static client_info_t *g_clients = NULL;
static size_t g_num_clients = 0;
static pthread_mutex_t g_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// -------------------------------------------------------------------
// Adiciona um novo cliente na lista global
// -------------------------------------------------------------------
static void add_client(int socket_fd) {
    pthread_mutex_lock(&g_clients_mutex);
    g_clients = realloc(g_clients, (g_num_clients + 1) * sizeof(client_info_t));
    g_clients[g_num_clients].socket_fd = socket_fd;
    // type, coords ficarão vazios até chegar a 1a mensagem
    g_clients[g_num_clients].type[0] = '\0';
    g_clients[g_num_clients].x = -1;
    g_clients[g_num_clients].y = -1;
    g_num_clients++;
    pthread_mutex_unlock(&g_clients_mutex);
}

// -------------------------------------------------------------------
// Remove um cliente da lista global (fechar socket já é feito fora)
// -------------------------------------------------------------------
static void remove_client(int socket_fd) {
    pthread_mutex_lock(&g_clients_mutex);
    for (size_t i = 0; i < g_num_clients; i++) {
        if (g_clients[i].socket_fd == socket_fd) {
            // Sobrescreve com o último e diminui a lista
            g_clients[i] = g_clients[g_num_clients - 1];
            g_num_clients--;
            if (g_num_clients == 0) {
                free(g_clients);
                g_clients = NULL;
            } else {
                g_clients = realloc(g_clients, g_num_clients * sizeof(client_info_t));
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_clients_mutex);
}

// -------------------------------------------------------------------
// Define tipo e coords do cliente na estrutura (ao receber primeira msg)
// -------------------------------------------------------------------
static void set_client_info(int socket_fd, const char *type, int x, int y) {
    pthread_mutex_lock(&g_clients_mutex);
    for (size_t i = 0; i < g_num_clients; i++) {
        if (g_clients[i].socket_fd == socket_fd) {
            strncpy(g_clients[i].type, type, MAX_TYPE_LEN);
            g_clients[i].x = x;
            g_clients[i].y = y;
            break;
        }
    }
    pthread_mutex_unlock(&g_clients_mutex);
}

// -------------------------------------------------------------------
// Envia msg para todos os clientes do mesmo tipo
// -------------------------------------------------------------------
static void broadcast_message(const struct sensor_message *msg) {
    pthread_mutex_lock(&g_clients_mutex);

    for (size_t i = 0; i < g_num_clients; i++) {
        if (strncmp(g_clients[i].type, msg->type, MAX_TYPE_LEN) == 0) {
            // Envia a mensagem a esse socket
            send(g_clients[i].socket_fd, msg, sizeof(*msg), 0);
        }
    }

    pthread_mutex_unlock(&g_clients_mutex);
}

// -------------------------------------------------------------------
// Função da thread que lida com cada cliente
// -------------------------------------------------------------------
static void* client_thread(void *arg) {
    int client_fd = *((int*)arg);
    free(arg);

    struct sensor_message msg;
    ssize_t n;

    while (1) {
        // Tenta receber uma mensagem
        n = recv(client_fd, &msg, sizeof(msg), 0);
        if (n == 0) {
            // Cliente desconectou
            // Precisamos avisar os outros com measurement = -1.0000
            // Primeiro recupera info do cliente
            pthread_mutex_lock(&g_clients_mutex);
            char client_type[MAX_TYPE_LEN] = "";
            int cx = -1, cy = -1;

            for (size_t i = 0; i < g_num_clients; i++) {
                if (g_clients[i].socket_fd == client_fd) {
                    strncpy(client_type, g_clients[i].type, MAX_TYPE_LEN);
                    cx = g_clients[i].x;
                    cy = g_clients[i].y;
                    break;
                }
            }
            pthread_mutex_unlock(&g_clients_mutex);

            if (client_type[0] != '\0') {
                // Log no servidor
                printf("log:\n%s sensor in (%d,%d)\nmeasurement: -1.0000\n\n",
                       client_type, cx, cy);

                // Envia broadcast -1.0000
                struct sensor_message out_msg;
                memset(&out_msg, 0, sizeof(out_msg));
                strncpy(out_msg.type, client_type, MAX_TYPE_LEN);
                out_msg.coords[0] = cx;
                out_msg.coords[1] = cy;
                out_msg.measurement = -1.0000f;
                broadcast_message(&out_msg);
            }

            // Remove cliente da lista e fecha
            remove_client(client_fd);
            close(client_fd);
            pthread_exit(NULL);
        }
        else if (n < 0) {
            // Erro
            perror("recv");
            break;
        }
        else if ((size_t)n < sizeof(msg)) {
            // Dados corrompidos ou parciais - para simplificar, vamos encerrar
            fprintf(stderr, "Mensagem incompleta recebida, encerrando cliente.\n");
            break;
        } else {
            // Recebemos uma struct sensor_message completa
            // Se for a primeira vez, armazenamos info do cliente
            set_client_info(client_fd, msg.type, msg.coords[0], msg.coords[1]);

            // Log no servidor
            printf("log:\n%s sensor in (%d,%d)\nmeasurement: %.4f\n\n",
                   msg.type, msg.coords[0], msg.coords[1], msg.measurement);

            // Retransmite para todos do mesmo tipo
            broadcast_message(&msg);
        }
    }

    // Caso chegue aqui por erro: remove e encerra
    remove_client(client_fd);
    close(client_fd);
    return NULL;
}

// -------------------------------------------------------------------
// Função principal do servidor
// -------------------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <v4|v6> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int use_ipv6 = 0;
    if (strcmp(argv[1], "v4") == 0) {
        use_ipv6 = 0;
    } else if (strcmp(argv[1], "v6") == 0) {
        use_ipv6 = 1;
    } else {
        fprintf(stderr, "Usage: %s <v4|v6> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* port = argv[2];

    // Cria hints
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = use_ipv6 ? AF_INET6 : AF_INET; // IPv6 ou IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE; // Para bind

    struct addrinfo *res;
    int err = getaddrinfo(NULL, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    // Cria socket
    int listen_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_fd < 0) {
        perror("socket");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    // Permitir reuso de endereço
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Faz bind
    if (bind(listen_fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
        freeaddrinfo(res);
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(res);

    // listen
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor iniciado (modo %s) na porta %s.\n", use_ipv6 ? "IPv6" : "IPv4", port);

    // Loop principal de aceitar conexões
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            perror("accept");
            continue; // Tenta de novo
        }

        // Adiciona na lista e cria thread
        add_client(client_fd);

        int *arg = malloc(sizeof(int));
        *arg = client_fd;
        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, arg);
        pthread_detach(tid); // Liberar ao final
    }

    // Nunca chega aqui no fluxo normal
    close(listen_fd);
    return 0;
}
