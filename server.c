#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <math.h>
#include <stdint.h>

#define MAX_SENSORS 12
#define BUFFER_SIZE 256
#define PORT 51511

typedef struct {
    char type[20];
    int x;
    int y;
    float measurement;
    int is_connected;
} Sensor;

Sensor sensors[MAX_SENSORS];
int sensor_count = 0;
pthread_mutex_t sensor_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function declarations
void *handle_client(void *client_socket);
float calculate_correction(int index);
void log_sensor_data(Sensor sensor, const char *action);

int main(int argc, char *argv[]) {
    if (argc != 2 || (strcmp(argv[1], "v4") != 0 && strcmp(argv[1], "v6") != 0)) {
        fprintf(stderr, "Usage: %s [v4|v6]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int server_socket;
    if (strcmp(argv[1], "v4") == 0) {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
    } else {
        server_socket = socket(AF_INET6, SOCK_STREAM, 0);
    }

    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    struct sockaddr_in6 server_addr6;

    if (strcmp(argv[1], "v4") == 0) {
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(PORT);

        if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Bind failed");
            close(server_socket);
            exit(EXIT_FAILURE);
        }
    } else {
        memset(&server_addr6, 0, sizeof(server_addr6));
        server_addr6.sin6_family = AF_INET6;
        server_addr6.sin6_addr = in6addr_any;
        server_addr6.sin6_port = htons(PORT);

        if (bind(server_socket, (struct sockaddr *)&server_addr6, sizeof(server_addr6)) < 0) {
            perror("Bind failed");
            close(server_socket);
            exit(EXIT_FAILURE);
        }
    }

    if (listen(server_socket, MAX_SENSORS) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server is running and listening on port %d\n", PORT);

    while (1) {
        int client_socket;
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)(intptr_t)client_socket) != 0) {
            perror("Thread creation failed");
            close(client_socket);
        }
    }

    close(server_socket);
    return 0;
}

void *handle_client(void *client_socket) {
    int socket = (intptr_t)client_socket;
    char buffer[BUFFER_SIZE];
    Sensor sensor;

    recv(socket, buffer, BUFFER_SIZE, 0);
    sscanf(buffer, "%s %d %d %f", sensor.type, &sensor.x, &sensor.y, &sensor.measurement);

    if (strcmp(sensor.type, "temperature") != 0 &&
        strcmp(sensor.type, "humidity") != 0 &&
        strcmp(sensor.type, "air_quality") != 0) {
        send(socket, "Invalid sensor type\n", 19, 0);
        close(socket);
        pthread_exit(NULL);
    }

    pthread_mutex_lock(&sensor_mutex);
    if (sensor_count < MAX_SENSORS) {
        sensor.is_connected = 1;
        sensors[sensor_count++] = sensor;
        log_sensor_data(sensor, "connected");
    } else {
        send(socket, "Server full\n", 12, 0);
        close(socket);
        pthread_mutex_unlock(&sensor_mutex);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&sensor_mutex);

    while (recv(socket, buffer, BUFFER_SIZE, 0) > 0) {
        float correction = calculate_correction(sensor_count - 1);
        sensor.measurement += correction;
        log_sensor_data(sensor, "updated");
    }

    pthread_mutex_lock(&sensor_mutex);
    sensor.is_connected = 0;
    log_sensor_data(sensor, "disconnected");
    pthread_mutex_unlock(&sensor_mutex);

    close(socket);
    pthread_exit(NULL);
}

float calculate_correction(int index) {
    int i, count = 0;
    float correction = 0.0;
    for (i = 0; i < sensor_count; i++) {
        if (i != index && sensors[i].is_connected) {
            float distance = sqrt(pow(sensors[i].x - sensors[index].x, 2) +
                                  pow(sensors[i].y - sensors[index].y, 2));
            correction += (sensors[i].measurement / (distance + 1));
            count++;
            if (count == 3) break;
        }
    }
    return (count > 0) ? correction / count : 0.0;
}

void log_sensor_data(Sensor sensor, const char *action) {
    printf("Sensor [%s] at (%d,%d): %.4f - %s\n",
           sensor.type, sensor.x, sensor.y, sensor.measurement, action);
}
