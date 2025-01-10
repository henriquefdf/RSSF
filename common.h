#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>


// -----------------------------------
// Estrutura do protocolo
// -----------------------------------
#define MAX_TYPE_LEN 12

struct sensor_message {
    char  type[MAX_TYPE_LEN];  // "temperature", "humidity", "air_quality"
    int   coords[2];           // Ex.: [2, 3]
    float measurement;          // Ex.: 25.3478
};

// -----------------------------------
// Constantes de intervalo dos sensores
// -----------------------------------
static const float TEMP_MIN = 20.0f;
static const float TEMP_MAX = 40.0f;
static const float HUM_MIN  = 10.0f;
static const float HUM_MAX  = 90.0f;
static const float AIR_MIN  = 15.0f;
static const float AIR_MAX  = 30.0f;

// Intervalos de envio (em segundos)
static const int TEMP_INTERVAL = 5;
static const int HUM_INTERVAL  = 7;
static const int AIR_INTERVAL  = 10;

// -----------------------------------
// Função de limitar valor em [min, max]
// -----------------------------------
static inline float clamp(float v, float vmin, float vmax) {
    if (v < vmin) return vmin;
    if (v > vmax) return vmax;
    return v;
}

// -----------------------------------
// Função de distância Euclidiana
// -----------------------------------
static inline float distance_euclid(int x1, int y1, int x2, int y2) {
    float dx = (float)(x1 - x2);
    float dy = (float)(y1 - y2);
    return sqrtf(dx*dx + dy*dy);
}

#endif // COMMON_H
