// include/shared_data.h
#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <semaphore.h>

#define MAX_PEDIDOS 10

// Esta estructura vivirá en la RAM compartida
struct MemoriaCompartida {
    int cola_pedidos[MAX_PEDIDOS];
    int head;   // Por dónde sacamos pedidos (Cocina)
    int tail;   // Por dónde metemos pedidos (Clientes)
    int count;  // Cuántos pedidos hay en la cola actualmente

    // Semáforos sin nombre de POSIX (Ideales para memoria compartida)
    sem_t mutex_cola;             // Para exclusión mutua al leer/escribir en la cola
    sem_t sem_pedidos_pendientes; // Avisa a la cocina que hay pedidos
    sem_t sem_espacio_cola;       // Avisa a los clientes que hay espacio para pedir
};

#endif