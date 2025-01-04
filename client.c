#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

#define BUFFER_SIZE 256

typedef struct {
    char type[20];
    int x;
    int y;
    float measurement;
} Sensor;

void *send_updates(void *socket);
float generate_random_measurement(const char *type);

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s [server IP] [port] -type [sensor_type] -coords [x] [y]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    char *sensor_type = argv[4];
    int x = atoi(argv[5]);
    int y = atoi(argv[6]);

    if (strcmp(sensor_type, "temperature") != 0 &&
        strcmp(sensor_type, "humidity") != 0 &&
        strcmp(sensor_type, "air_quality") != 0) {
        fprintf(stderr, "Invalid sensor type. Valid types are temperature, humidity, air_quality.\n");
        exit(EXIT_FAILURE);
    }

    if (x < 0 || x > 9 || y < 0 || y > 9) {
        fprintf(stderr, "Coordinates must be in the range [0, 9].\n");
        exit(EXIT_FAILURE);
    }

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server at %s:%d\n", server_ip, port);

    Sensor sensor;
    strcpy(sensor.type, sensor_type);
    sensor.x = x;
    sensor.y = y;
    sensor.measurement = generate_random_measurement(sensor_type);

    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "%s %d %d %.4f", sensor.type, sensor.x, sensor.y, sensor.measurement);
    send(client_socket, buffer, strlen(buffer), 0);

    pthread_t update_thread;
    if (pthread_create(&update_thread, NULL, send_updates, (void *)(intptr_t)client_socket) != 0) {
        perror("Thread creation failed");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    pthread_join(update_thread, NULL);
    close(client_socket);
    return 0;
}

void *send_updates(void *socket) {
    int client_socket = (intptr_t)socket;
    char buffer[BUFFER_SIZE];
    srand(time(NULL));

    while (1) {
        float new_measurement = generate_random_measurement("temperature");
        snprintf(buffer, BUFFER_SIZE, "update %.4f", new_measurement);
        send(client_socket, buffer, strlen(buffer), 0);
        sleep(5); // Send updates every 5 seconds
    }

    pthread_exit(NULL);
}

float generate_random_measurement(const char *type) {
    if (strcmp(type, "temperature") == 0) {
        return (float)(rand() % 50 + 1); // Random temperature between 1 and 50
    } else if (strcmp(type, "humidity") == 0) {
        return (float)(rand() % 100 + 1); // Random humidity between 1 and 100
    } else if (strcmp(type, "air_quality") == 0) {
        return (float)(rand() % 200 + 1); // Random air quality index between 1 and 200
    }
    return 0.0;
}
