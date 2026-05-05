#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "../include/shared_data.h"
#include <thread>
#include <vector>
#include <cassert>

using namespace std;

void hiloCliente(MemoriaCompartida* mem, int id_cliente) {
    assert(id_cliente >= 1 && id_cliente <= MAX_CLIENTES);
    cout << "[Cliente " << id_cliente << "] Llegó. Esperando mesa..." << endl;
    
    sem_wait(&mem->sem_mesas_libres);
    cout << "[Cliente " << id_cliente << "] Se sentó en una mesa." << endl;

    sem_wait(&mem->sem_espacio_pedidos);
    sem_wait(&mem->mutex_pedidos);
    mem->cola_pedidos[mem->tail_pedidos] = id_cliente;
    mem->tail_pedidos = (mem->tail_pedidos + 1) % MAX_PEDIDOS;
    sem_post(&mem->mutex_pedidos);
    sem_post(&mem->sem_pedidos_pendientes);
    
    cout << "[Cliente " << id_cliente << "] Ordenó. Esperando comida..." << endl;
    sem_wait(&mem->sem_cliente_comiendo[id_cliente]);
    
    cout << "[Cliente " << id_cliente << "] Está comiendo... ¡Qué rico!" << endl;
    sleep(2);
    
    cout << "[Cliente " << id_cliente << "] Terminó y liberó la mesa." << endl;
    sem_post(&mem->sem_mesas_libres);
}

void hiloCocinero(MemoriaCompartida* mem, int id_cocinero) {
    while (true) {
        sem_wait(&mem->sem_pedidos_pendientes);
        sem_wait(&mem->mutex_pedidos);
        int id_cliente = mem->cola_pedidos[mem->head_pedidos];
        mem->head_pedidos = (mem->head_pedidos + 1) % MAX_PEDIDOS;
        sem_post(&mem->mutex_pedidos);
        sem_post(&mem->sem_espacio_pedidos);
        
        cout << "[Cocinero " << id_cocinero << "] Preparando orden del Cliente " << id_cliente << "..." << endl;
        sleep(rand() % 3 + 2);
        
        sem_wait(&mem->sem_espacio_listos);
        sem_wait(&mem->mutex_listos);
        mem->cola_listos[mem->tail_listos] = id_cliente;
        mem->tail_listos = (mem->tail_listos + 1) % MAX_PEDIDOS;
        sem_post(&mem->mutex_listos);
        sem_post(&mem->sem_comida_lista);
    }
}

void hiloMesero(MemoriaCompartida* mem, int id_mesero) {
    while(true) {
        sem_wait(&mem->sem_comida_lista);
        sem_wait(&mem->mutex_listos);
        int id_cliente = mem->cola_listos[mem->head_listos];
        mem->head_listos = (mem->head_listos + 1) % MAX_PEDIDOS;
        sem_post(&mem->mutex_listos);
        sem_post(&mem->sem_espacio_listos);
        
        cout << "[Mesero " << id_mesero << "] Llevando comida a la mesa del Cliente " << id_cliente << "." << endl;
        sleep(1);
        
        sem_post(&mem->sem_cliente_comiendo[id_cliente]); 
    }
}

const char* SHM_NAME = "/shm_restaurante";

void procesoClientes(MemoriaCompartida* mem) {
    cout << "[Proceso Clientes] Abriendo puertas. PID: " << getpid() << endl;
    srand(time(NULL) ^ getpid());

    vector<thread> clientes;
    int TOTAL_CLIENTES = 5;

    for (int i = 1; i <= TOTAL_CLIENTES; i++) {
        clientes.push_back(thread(hiloCliente, mem, i));
    }

    for (auto& t : clientes) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    cout << "[Proceso Clientes] Todos los clientes han terminado." << endl;
}

void procesoCocina(MemoriaCompartida* mem) {
    cout << "[Proceso Cocina] Encendiendo estufas. PID: " << getpid() << endl;
    srand(time(NULL) ^ getpid());

    vector<thread> cocineros;
    int TOTAL_COCINEROS = 2;

    for (int i = 1; i <= TOTAL_COCINEROS; i++) {
        cocineros.push_back(thread(hiloCocinero, mem, i));
    }

    for (auto& t : cocineros) {
        t.detach();
    }

    while(true) {
        sleep(10);
    }
}

void procesoServicio(MemoriaCompartida* mem) {
    cout << "[Proceso Servicio] Meseros listos. PID: " << getpid() << endl;
    vector<thread> meseros;
    for (int i = 1; i <= 2; i++) {
        meseros.push_back(thread(hiloMesero, mem, i));
        meseros.back().detach();
    }
    while(true) sleep(10);
}

int main() {
    cout << "--- Inicializando Memoria Compartida ---" << endl;

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Error en shm_open");
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(MemoriaCompartida)) == -1) {
        perror("Error en ftruncate");
        return 1;
    }

    MemoriaCompartida* mem = (MemoriaCompartida*) mmap(NULL, sizeof(MemoriaCompartida), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (mem == MAP_FAILED) {
        perror("Error en mmap");
        return 1;
    }

    mem->head_pedidos = 0; mem->tail_pedidos = 0; mem->count_pedidos = 0;
    mem->head_listos = 0; mem->tail_listos = 0; mem->count_listos = 0;

    sem_init(&mem->mutex_pedidos, 1, 1);
    sem_init(&mem->sem_pedidos_pendientes, 1, 0);
    sem_init(&mem->sem_espacio_pedidos, 1, MAX_PEDIDOS);

    sem_init(&mem->mutex_listos, 1, 1);
    sem_init(&mem->sem_comida_lista, 1, 0);
    sem_init(&mem->sem_espacio_listos, 1, MAX_PEDIDOS);

    sem_init(&mem->sem_mesas_libres, 1, MAX_MESAS);
    for(int i = 0; i <= MAX_CLIENTES; i++) {
        sem_init(&mem->sem_cliente_comiendo[i], 1, 0);
    }

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

    pid_t pid_servicio = fork();
    if (pid_servicio == 0) {
        procesoServicio(mem);
        return 0;
    }

    waitpid(pid_clientes, NULL, 0);
    kill(pid_cocina, SIGTERM);
    kill(pid_servicio, SIGTERM);
    waitpid(pid_cocina, NULL, 0);
    waitpid(pid_servicio, NULL, 0);

    cout << "--- Restaurante Cerrado ---" << endl;

    sem_destroy(&mem->mutex_pedidos);
    sem_destroy(&mem->sem_pedidos_pendientes);
    sem_destroy(&mem->sem_espacio_pedidos);
    sem_destroy(&mem->mutex_listos);
    sem_destroy(&mem->sem_comida_lista);
    sem_destroy(&mem->sem_espacio_listos);
    sem_destroy(&mem->sem_mesas_libres);
    for(int i = 0; i <= MAX_CLIENTES; i++) {
        sem_destroy(&mem->sem_cliente_comiendo[i]);
    }
    munmap(mem, sizeof(MemoriaCompartida));
    shm_unlink(SHM_NAME);

    return 0;
}