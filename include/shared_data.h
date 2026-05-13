#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <semaphore.h>
#include <time.h>

#define MAX_PEDIDOS 20
#define MAX_MESAS 10
#define MAX_CLIENTES 20

// Prioridades
#define PRIORIDAD_NORMAL  1
#define PRIORIDAD_URGENTE 2

struct Pedido {
    int id_cliente;
    int prioridad; // PRIORIDAD_NORMAL o PRIORIDAD_URGENTE
    time_t tiempo_llegada;
};

struct MemoriaCompartida {
    // Configuracion (se llena antes del fork)
    int num_clientes;
    int num_cocineros;
    int num_meseros;
    int num_mesas;

    // 1. Cola de Pedidos con prioridad (Cliente -> Cocina)
    Pedido cola_pedidos[MAX_PEDIDOS];
    int head_pedidos, tail_pedidos, count_pedidos;
    sem_t mutex_pedidos;
    sem_t sem_pedidos_urgentes;   // Semaforo separado para urgentes
    sem_t sem_pedidos_normales;   // Semaforo separado para normales
    sem_t sem_espacio_pedidos;

    // 2. Cola de Comida Lista (Cocina -> Meseros)
    int cola_listos[MAX_PEDIDOS];
    int head_listos, tail_listos, count_listos;
    sem_t mutex_listos;
    sem_t sem_comida_lista;
    sem_t sem_espacio_listos;

    // 3. Recursos del deadlock (horno y sarten)
    sem_t sem_horno;   // Solo 1 horno
    sem_t sem_sarten;  // Solo 1 sarten

    // 4. Sincronizacion de clientes
    sem_t sem_mesas_libres;
    sem_t sem_cliente_comiendo[MAX_CLIENTES + 1];

    // 5. Estadisticas
    double tiempo_espera_total;
    int clientes_atendidos;
    int pedidos_urgentes_atendidos;
    int pedidos_normales_atendidos;
    int deadlocks_detectados;
    int deadlocks_resueltos;
    sem_t mutex_stats;
};

#endif