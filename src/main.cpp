#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "../include/shared_data.h"

using namespace std;

// Nombre del bloque de memoria en /dev/shm
const char* SHM_NAME = "/shm_restaurante";

void procesoClientes(MemoriaCompartida* mem) {
    cout << "[Clientes] Proceso iniciado. PID: " << getpid() << endl;
    
    // Ejemplo rápido de exclusión mutua:
    // sem_wait(&mem->sem_espacio_cola); // Esperar a que haya espacio
    // sem_wait(&mem->mutex_cola);       // Bloquear la cola para escribir
    // ... agregar pedido ...
    // sem_post(&mem->mutex_cola);       // Desbloquear la cola
    // sem_post(&mem->sem_pedidos_pendientes); // Avisar a cocina
    
    sleep(1);
}

void procesoCocina(MemoriaCompartida* mem) {
    cout << "[Cocina] Proceso iniciado. PID: " << getpid() << endl;
    sleep(1);
}

int main() {
    cout << "--- Inicializando Memoria Compartida ---" << endl;

    // Objeto de memoria compartida
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Error en shm_open");
        return 1;
    }

    // Tamaño de la memoria compartida
    if (ftruncate(shm_fd, sizeof(MemoriaCompartida)) == -1) {
        perror("Error en ftruncate");
        return 1;
    }

    // Mapear la memoria a un puntero
    MemoriaCompartida* mem = (MemoriaCompartida*) mmap(NULL, sizeof(MemoriaCompartida), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (mem == MAP_FAILED) {
        perror("Error en mmap");
        return 1;
    }

    // Inicializar variables de la cola
    mem->head = 0;
    mem->tail = 0;
    mem->count = 0;

    // Inicializar los semáforos
    // El segundo parámetro en '1' indica que el semáforo es compartido entre procesos
    sem_init(&mem->mutex_cola, 1, 1); // Inicializado en 1 (Desbloqueado)
    sem_init(&mem->sem_pedidos_pendientes, 1, 0); // Inicializado en 0 (No hay pedidos)
    sem_init(&mem->sem_espacio_cola, 1, MAX_PEDIDOS); // Espacio disponible

    cout << "--- Iniciando Simulador de Restaurante ---" << endl;

    pid_t pid_clientes = fork();
    if (pid_clientes == 0) {
        procesoClientes(mem);
        return 0; 
    } 

    pid_t pid_cocina = fork();
    if (pid_cocina == 0) {
        procesoCocina(mem);
        return 0;
    }

    // Orquestador espera
    waitpid(pid_clientes, NULL, 0);
    waitpid(pid_cocina, NULL, 0);

    cout << "--- Restaurante Cerrado ---" << endl;

    // Limpieza (Evitar fugas de memoria en el sistema operativo)
    sem_destroy(&mem->mutex_cola);
    sem_destroy(&mem->sem_pedidos_pendientes);
    sem_destroy(&mem->sem_espacio_cola);
    munmap(mem, sizeof(MemoriaCompartida));
    shm_unlink(SHM_NAME); // Borra el archivo virtual de /dev/shm

    return 0;
}