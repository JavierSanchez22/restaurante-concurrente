#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <semaphore.h>

#define MAX_PEDIDOS 10
#define MAX_MESAS 3      // Recurso limitado: Solo hay 3 mesas
#define MAX_CLIENTES 10  // Límite de clientes para esta prueba

struct MemoriaCompartida {
    // 1. Cola de Pedidos (De Cliente a Cocina)
    int cola_pedidos[MAX_PEDIDOS];
    int head_pedidos, tail_pedidos, count_pedidos;
    sem_t mutex_pedidos;
    sem_t sem_pedidos_pendientes;
    sem_t sem_espacio_pedidos;

    // 2. Cola de Comida Lista (De Cocina a Meseros)
    int cola_listos[MAX_PEDIDOS];
    int head_listos, tail_listos, count_listos;
    sem_t mutex_listos;
    sem_t sem_comida_lista;
    sem_t sem_espacio_listos;

    // 3. Recursos y Sincronización del Cliente
    sem_t sem_mesas_libres;
    sem_t sem_cliente_comiendo[MAX_CLIENTES + 1]; // Semáforo individual por cliente
};

#endif