#include "common.h"

// ---------------------------------------
// Estrutura de dados para armazenar info
// sobre outros sensores do mesmo tipo
// ---------------------------------------
typedef struct neighbor_s {
    int x;
    int y;
    float measurement;
} neighbor_t;

// Até 50 sensores do mesmo tipo, no máximo (poderia ser dinâmico).
#define MAX_SENSORS_SAME_TYPE 50
static neighbor_t g_known_sensors[MAX_SENSORS_SAME_TYPE];
static int g_num_known_sensors = 0;

// Protege acesso a g_known_sensors e medição local
static pthread_mutex_t g_data_mutex = PTHREAD_MUTEX_INITIALIZER;

// Dados do sensor local
static char  g_type[MAX_TYPE_LEN] = "";
static int   g_x = 0, g_y = 0;
static float g_my_measurement = 0.0f;

// Intervalo de envio (em segundos) para este tipo
static int   g_send_interval = 5; // default p/ temperature

// ---------------------------------------
// Funções auxiliares
// ---------------------------------------
static void print_usage_and_exit(void) {
    fprintf(stderr,
        "Usage: ./client <server_ip> <port> -type <temperature|humidity|air_quality> -coords <x> <y>\n");
    exit(EXIT_FAILURE);
}

// Retorna 1 se string == "temperature" ou "humidity" ou "air_quality"
static int valid_type(const char *t) {
    return (
        (strcmp(t, "temperature") == 0) ||
        (strcmp(t, "humidity") == 0)    ||
        (strcmp(t, "air_quality") == 0)
    );
}

// Retorna se (x,y) é válido (0..9)
static int coords_valid(int x, int y) {
    return (x >= 0 && x <= 9 && y >= 0 && y <= 9);
}

// Intervalo de envio e limites para cada tipo
static float get_random_measurement(const char *t) {
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    float scale = (float)rand() / (float)RAND_MAX; // 0..1
    if (strcmp(t, "temperature") == 0) {
        // 20..40
        g_send_interval = TEMP_INTERVAL; // 5
        return TEMP_MIN + scale * (TEMP_MAX - TEMP_MIN);
    } else if (strcmp(t, "humidity") == 0) {
        // 10..90
        g_send_interval = HUM_INTERVAL; // 7
        return HUM_MIN + scale * (HUM_MAX - HUM_MIN);
    } else {
        // air_quality 15..30
        g_send_interval = AIR_INTERVAL; // 10
        return AIR_MIN + scale * (AIR_MAX - AIR_MIN);
    }
}

// ---------------------------------------
// Retorna os 3 índices de sensores mais próximos
// (ou menos, se houver <3) no array g_known_sensors.
// ---------------------------------------
static void get_top3_neighbors(int *out_idx, int *out_count) {
    pthread_mutex_lock(&g_data_mutex);

    // Calcula distâncias de todos os sensores conhecidos
    float dist[MAX_SENSORS_SAME_TYPE];
    for (int i = 0; i < g_num_known_sensors; i++) {
        dist[i] = distance_euclid(g_known_sensors[i].x, g_known_sensors[i].y, g_x, g_y);
    }

    // Ordena por distância (encontrando os 3 menores)
    int used[MAX_SENSORS_SAME_TYPE] = {0};
    int found = 0;
    for (int k = 0; k < 3; k++) {
        float best_dist = 999999.0f;
        int best_i = -1;
        for (int i = 0; i < g_num_known_sensors; i++) {
            if (!used[i] && dist[i] < best_dist) {
                best_dist = dist[i];
                best_i = i;
            }
        }
        if (best_i >= 0) {
            out_idx[found] = best_i;
            used[best_i] = 1;
            found++;
        }
    }

    *out_count = found;
    pthread_mutex_unlock(&g_data_mutex);
}

// ---------------------------------------
// Atualiza ou insere sensor remetente na lista
// Retorna índice do sensor no array
// ---------------------------------------
static int update_or_insert_sensor(int rx, int ry, float rmeasurement) {
    pthread_mutex_lock(&g_data_mutex);
    // Se já existe, atualiza measurement
    for (int i = 0; i < g_num_known_sensors; i++) {
        if (g_known_sensors[i].x == rx && g_known_sensors[i].y == ry) {
            g_known_sensors[i].measurement = rmeasurement;
            pthread_mutex_unlock(&g_data_mutex);
            return i;
        }
    }
    // Senao insere se tiver espaço
    if (g_num_known_sensors < MAX_SENSORS_SAME_TYPE) {
        g_known_sensors[g_num_known_sensors].x = rx;
        g_known_sensors[g_num_known_sensors].y = ry;
        g_known_sensors[g_num_known_sensors].measurement = rmeasurement;
        g_num_known_sensors++;
        pthread_mutex_unlock(&g_data_mutex);
        return (g_num_known_sensors - 1);
    }
    // Se estourar, apenas ignora o novo (caso limite)
    pthread_mutex_unlock(&g_data_mutex);
    return -1;
}

// ---------------------------------------
// Remove sensor com coords (rx, ry)
// ---------------------------------------
// Remove sensor com coords (rx, ry)
static void remove_sensor(int rx, int ry) {
    pthread_mutex_lock(&g_data_mutex);
    for (int i = 0; i < g_num_known_sensors; i++) {
        if (g_known_sensors[i].x == rx && g_known_sensors[i].y == ry) {
            // Sobrescreve com o último sensor e reduz o tamanho da lista
            g_known_sensors[i] = g_known_sensors[g_num_known_sensors - 1];
            g_num_known_sensors--;
            break;
        }
    }
    pthread_mutex_unlock(&g_data_mutex);
}


// ---------------------------------------
// Thread de envio periódico
// ---------------------------------------
static int g_socket_fd = -1; // socket global

void* sender_thread(void* arg) {
    (void)arg; // unused
    while (1) {
        // Monta a struct e envia
        struct sensor_message msg;
        memset(&msg, 0, sizeof(msg));

        pthread_mutex_lock(&g_data_mutex);
        strncpy(msg.type, g_type, MAX_TYPE_LEN);
        msg.coords[0] = g_x;
        msg.coords[1] = g_y;
        msg.measurement = g_my_measurement;
        pthread_mutex_unlock(&g_data_mutex);

        send(g_socket_fd, &msg, sizeof(msg), 0);
        // Dorme
        sleep(g_send_interval);
    }
    return NULL;
}

// ---------------------------------------
// Thread de recepção
// ---------------------------------------
void* receiver_thread(void* arg) {
    (void)arg;
    struct sensor_message in_msg;

    while (1) {
        ssize_t n = recv(g_socket_fd, &in_msg, sizeof(in_msg), 0);
        if (n <= 0) {
            // Desconexão ou erro
            fprintf(stderr, "Conexão com o servidor encerrada.\n");
            close(g_socket_fd);
            exit(EXIT_FAILURE);
        }
        // Recebemos uma mensagem
        // Monta log do cliente:
        // log:
        // <type> sensor in (<x>,<y>)
        // measurement: <measurement>
        // action: <???>
        printf("log:\n%s sensor in (%d,%d)\nmeasurement: %.4f\n",
               in_msg.type,
               in_msg.coords[0], in_msg.coords[1],
               in_msg.measurement);

        // Verifica se é do mesmo local do cliente
        if (in_msg.coords[0] == g_x && in_msg.coords[1] == g_y) {
            // same location -> descartar
            printf("action: same location\n\n");
            continue;
        }

        // Verifica se é -1 => removed
        if (fabsf(in_msg.measurement - (-1.0f)) < 0.0001f) {
            // Remove e imprime "removed"
            remove_sensor(in_msg.coords[0], in_msg.coords[1]);
            printf("action: removed\n\n");
            continue;
        }

        // Caso normal: checar se está no top 3
        // 1) Atualiza/insere info do sensor
        int idx = update_or_insert_sensor(in_msg.coords[0],
                                          in_msg.coords[1],
                                          in_msg.measurement);

        // 2) Obter top 3 e ver se esse sensor está lá
        int topIdx[3];
        int topCount;
        get_top3_neighbors(topIdx, &topCount);

        // Verifica se esse sensor está no top3
        int is_top3 = 0;
        for (int i = 0; i < topCount; i++) {
            if (idx == topIdx[i]) {
                is_top3 = 1;
                break;
            }
        }
        if (idx < 0) {
            // Não conseguimos inserir -> ou a lista está cheia
            // mas se não estava, já demos "action: not neighbor"? 
            // Vamos considerar que não neighbor se a lista está saturada
            printf("action: not neighbor\n\n");
            continue;
        }
        if (!is_top3) {
            // Não está entre os três vizinhos mais próximos -> descartar
            printf("action: not neighbor\n\n");
            continue;
        }


        if (is_top3) {
            // Aplica fórmula de correção
            float old_val;
            pthread_mutex_lock(&g_data_mutex);
            old_val = g_my_measurement;
            float d = distance_euclid(g_x, g_y, in_msg.coords[0], in_msg.coords[1]);
            float diff = in_msg.measurement - old_val;
            float correction = 0.1f * (diff) / (d + 1.0f);
            g_my_measurement = old_val + correction;

            // Ajustar limites
            if (strcmp(g_type, "temperature") == 0) {
                g_my_measurement = clamp(g_my_measurement, TEMP_MIN, TEMP_MAX);
            } else if (strcmp(g_type, "humidity") == 0) {
                g_my_measurement = clamp(g_my_measurement, HUM_MIN, HUM_MAX);
            } else {
                g_my_measurement = clamp(g_my_measurement, AIR_MIN, AIR_MAX);
            }
            pthread_mutex_unlock(&g_data_mutex);

            float delta = g_my_measurement - old_val;
            printf("action: correction of %.4f\n\n", delta);
        }
    }
    return NULL;
}

// ---------------------------------------
// MAIN (client)
// ---------------------------------------
int main(int argc, char *argv[]) {
    // Exemplo de parsing robusto
    // Esperado:
    // ./client <server_ip> <port> -type <temperature|humidity|air_quality> -coords <x> <y>
    if (argc < 7) {
        fprintf(stderr, "Error: Invalid number of arguments\n");
        print_usage_and_exit();
    }

    char *server_ip   = argv[1];
    char *server_port = argv[2];

    // Precisamos encontrar -type e -coords na ordem
    // Formato fixo: argv[3] = "-type", argv[4] = <tipo>
    //               argv[5] = "-coords", argv[6] = <x>, argv[7] = <y>
    if (strcmp(argv[3], "-type") != 0) {
        fprintf(stderr, "Error: Expected '-type' argument\n");
        print_usage_and_exit();
    }

    if (!valid_type(argv[4])) {
        fprintf(stderr, "Error: Invalid sensor type\n");
        print_usage_and_exit();
    }
    strncpy(g_type, argv[4], MAX_TYPE_LEN);

    if (strcmp(argv[5], "-coords") != 0) {
        fprintf(stderr, "Error: Expected '-coords' argument\n");
        print_usage_and_exit();
    }

    if (argc < 8) {
        // Falta x e y
        fprintf(stderr, "Error: Invalid number of arguments\n");
        print_usage_and_exit();
    }

    g_x = atoi(argv[6]);
    g_y = atoi(argv[7]);
    if (!coords_valid(g_x, g_y)) {
        fprintf(stderr, "Error: Coordinates must be in the range 0-9\n");
        print_usage_and_exit();
    }

    // Pronto, parse OK. Gera medição inicial
    g_my_measurement = get_random_measurement(g_type);

    // Conectar ao servidor (IPv4 ou IPv6)
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = AF_UNSPEC; // tanto faz IPv4 ou IPv6

    int err = getaddrinfo(server_ip, server_port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct addrinfo *rp;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) continue; // tenta próximo

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            // Conectou
            break;
        }
        close(sockfd);
    }
    if (rp == NULL) {
        fprintf(stderr, "Não foi possível conectar ao servidor.\n");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(res);
    g_socket_fd = sockfd;

    // Cria threads de envio e recepção
    pthread_t stid, rtid;
    pthread_create(&stid, NULL, sender_thread, NULL);
    pthread_create(&rtid, NULL, receiver_thread, NULL);

    // Aguarda as threads (na prática, ficam em loop infinito)
    pthread_join(stid, NULL);
    pthread_join(rtid, NULL);

    close(sockfd);
    return 0;
}
