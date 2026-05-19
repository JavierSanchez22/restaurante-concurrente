#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <thread>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <errno.h>
#include "../include/shared_data.h"

using namespace std;

const char* SHM_NAME   = "/shm_restaurante";
const char* LOG_PATH   = "logs/restaurante.log";

// ─── Logging ────────────────────────────────────────────────────────────────

sem_t* g_mutex_log = nullptr; // Apunta al mutex dentro de la shm

void escribirLog(const string& tipo, const string& mensaje) {
    time_t ahora = time(nullptr);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&ahora));

    // Formato: TIMESTAMP|TIPO|MENSAJE
    string linea = string(ts) + "|" + tipo + "|" + mensaje + "\n";

    if (g_mutex_log) sem_wait(g_mutex_log);
    ofstream f(LOG_PATH, ios::app);
    if (f.is_open()) {
        f << linea;
        f.flush();
    }
    if (g_mutex_log) sem_post(g_mutex_log);

    cout << "[" << ts << "] " << mensaje << endl;
}

// ─── Hilo Cliente ────────────────────────────────────────────────────────────

void hiloCliente(MemoriaCompartida* mem, int id_cliente) {
    assert(id_cliente >= 1 && id_cliente <= MAX_CLIENTES);

    time_t t_llegada = time(nullptr);

    int prioridad = (rand() % 3 == 0) ? PRIORIDAD_URGENTE : PRIORIDAD_NORMAL;
    string prio_str = (prioridad == PRIORIDAD_URGENTE) ? "URGENTE" : "normal";

    escribirLog("CLIENTE", "[Cliente " + to_string(id_cliente) + "] Llegó (pedido " + prio_str + "). Esperando mesa...");

    sem_wait(&mem->sem_mesas_libres); // --- BLOQUEO si no hay mesas ---
    escribirLog("MESA", "[Cliente " + to_string(id_cliente) + "] Se sentó en una mesa.");

    // Registrar pedido con prioridad
    sem_wait(&mem->sem_espacio_pedidos);
    sem_wait(&mem->mutex_pedidos);

    Pedido p;
    p.id_cliente    = id_cliente;
    p.prioridad     = prioridad;
    p.tiempo_llegada = t_llegada;

    mem->cola_pedidos[mem->tail_pedidos] = p;
    mem->tail_pedidos = (mem->tail_pedidos + 1) % MAX_PEDIDOS;
    mem->count_pedidos++;

    sem_post(&mem->mutex_pedidos);
    sem_post(&mem->sem_espacio_pedidos);

    // Señal al semaforo correcto segun prioridad
    if (prioridad == PRIORIDAD_URGENTE)
        sem_post(&mem->sem_pedidos_urgentes);
    else
        sem_post(&mem->sem_pedidos_normales);

    escribirLog("PEDIDO", "[Cliente " + to_string(id_cliente) + "] Ordenó (" + prio_str + "). Esperando comida...");

    sem_wait(&mem->sem_cliente_comiendo[id_cliente]); // --- BLOQUEO esperando mesero ---

    time_t t_atendido = time(nullptr);
    double espera = difftime(t_atendido, t_llegada);

    // Actualizar estadísticas
    sem_wait(&mem->mutex_stats);
    mem->tiempo_espera_total += espera;
    mem->clientes_atendidos++;
    if (prioridad == PRIORIDAD_URGENTE)
        mem->pedidos_urgentes_atendidos++;
    else
        mem->pedidos_normales_atendidos++;
    sem_post(&mem->mutex_stats);

    escribirLog("COMIENDO", "[Cliente " + to_string(id_cliente) + "] Está comiendo. Espera total: " + to_string((int)espera) + "s");
    sleep(2);

    escribirLog("MESA", "[Cliente " + to_string(id_cliente) + "] Terminó y liberó la mesa.");
    sem_post(&mem->sem_mesas_libres);
}

// ─── Hilo Cocinero (con deadlock real) ──────────────────────────────────────

void hiloCocinero(MemoriaCompartida* mem, int id_cocinero) {
    while (true) {
        int id_cliente = -1;
        int prioridad  = PRIORIDAD_NORMAL;

        // Revisar urgentes primero (sin bloqueo), luego normales
        int val_urgentes = 0;
        sem_getvalue(&mem->sem_pedidos_urgentes, &val_urgentes);

        if (val_urgentes > 0) {
            sem_wait(&mem->sem_pedidos_urgentes);
            prioridad = PRIORIDAD_URGENTE;
        } else {
            sem_wait(&mem->sem_pedidos_normales); // --- BLOQUEO si no hay pedidos ---
            prioridad = PRIORIDAD_NORMAL;
        }

        // Sacar pedido de la cola
        sem_wait(&mem->mutex_pedidos);
        // Buscar el pedido de la prioridad correcta
        for (int i = 0; i < MAX_PEDIDOS; i++) {
            int idx = (mem->head_pedidos + i) % MAX_PEDIDOS;
            if (mem->cola_pedidos[idx].prioridad == prioridad && mem->cola_pedidos[idx].id_cliente != -1) {
                id_cliente = mem->cola_pedidos[idx].id_cliente;
                mem->cola_pedidos[idx].id_cliente = -1; // Marcar como tomado
                if (i == 0) {
                    mem->head_pedidos = (mem->head_pedidos + 1) % MAX_PEDIDOS;
                    mem->count_pedidos--;
                }
                break;
            }
        }
        sem_post(&mem->mutex_pedidos);
        sem_post(&mem->sem_espacio_pedidos);

        if (id_cliente == -1) continue;

        string prio_str = (prioridad == PRIORIDAD_URGENTE) ? "URGENTE" : "normal";
        escribirLog("COCINA", "[Cocinero " + to_string(id_cocinero) + "] Preparando orden " + prio_str + " del Cliente " + to_string(id_cliente) + "...");

        // ── SIMULACIÓN DE DEADLOCK ──────────────────────────────────────────
        // Cocineros impares: agarran horno primero, luego sarten
        // Cocineros pares:   agarran sarten primero, luego horno
        // Con 2+ cocineros trabajando al mismo tiempo -> deadlock real
        bool deadlock_simulado = false;

        if (id_cocinero % 2 != 0) {
            // Cocinero impar: horno -> sarten
            sem_wait(&mem->sem_horno); // <--- Tomamos el recurso
            escribirLog("RECURSO", "[Cocinero " + to_string(id_cocinero) + "] Tomó el HORNO. Esperando sartén...");

            sleep(1); // <--- LA CLAVE: Forzamos el cambio de contexto para que el otro hilo actúe

            // Intento con timeout de 3s para la sarten
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 3;

            int ret = sem_timedwait(&mem->sem_sarten, &ts);
            if (ret == -1 && errno == ETIMEDOUT) {
                // DEADLOCK DETECTADO
                deadlock_simulado = true;
                sem_wait(&mem->mutex_stats);
                mem->deadlocks_detectados++;
                sem_post(&mem->mutex_stats);

                escribirLog("DEADLOCK", "[DEADLOCK DETECTADO] Cocinero " + to_string(id_cocinero) + " liberó el HORNO para romper el ciclo");
                sem_post(&mem->sem_horno); // Suelta el horno

                // Espera un momento y reintenta en orden seguro
                sleep(1);
                escribirLog("DEADLOCK", "[DEADLOCK RESOLVIENDO] Cocinero " + to_string(id_cocinero) + " reintentando en orden seguro...");

                sem_wait(&mem->sem_sarten);
                sem_wait(&mem->sem_horno);

                sem_wait(&mem->mutex_stats);
                mem->deadlocks_resueltos++;
                sem_post(&mem->mutex_stats);

                escribirLog("DEADLOCK", "[DEADLOCK RESUELTO] Cocinero " + to_string(id_cocinero) + " continuó con la orden");
            }
        } else {
            // Cocinero par: sarten -> horno
            sem_wait(&mem->sem_sarten); // <--- Tomamos el recurso
            escribirLog("RECURSO", "[Cocinero " + to_string(id_cocinero) + "] Tomó la SARTÉN. Esperando horno...");

            sleep(1); // <--- LA CLAVE AQUÍ TAMBIÉN

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 3;

            int ret = sem_timedwait(&mem->sem_horno, &ts);
            if (ret == -1 && errno == ETIMEDOUT) {
                deadlock_simulado = true;
                sem_wait(&mem->mutex_stats);
                mem->deadlocks_detectados++;
                sem_post(&mem->mutex_stats);

                escribirLog("DEADLOCK", "[DEADLOCK DETECTADO] Cocinero " + to_string(id_cocinero) + " liberó la SARTÉN para romper el ciclo");
                sem_post(&mem->sem_sarten);

                sleep(1);
                escribirLog("DEADLOCK", "[DEADLOCK RESOLVIENDO] Cocinero " + to_string(id_cocinero) + " reintentando en orden seguro...");

                sem_wait(&mem->sem_horno);
                sem_wait(&mem->sem_sarten);

                sem_wait(&mem->mutex_stats);
                mem->deadlocks_resueltos++;
                sem_post(&mem->mutex_stats);

                escribirLog("DEADLOCK", "[DEADLOCK RESUELTO] Cocinero " + to_string(id_cocinero) + " continuó con la orden");
            }
        }

        // Cocinar
        sleep(rand() % 3 + 2);

        // Liberar recursos de cocina
        sem_post(&mem->sem_horno);
        sem_post(&mem->sem_sarten);
        escribirLog("RECURSO", "[Cocinero " + to_string(id_cocinero) + "] Liberó HORNO y SARTÉN.");

        // Pasar a cola de listos
        sem_wait(&mem->sem_espacio_listos);
        sem_wait(&mem->mutex_listos);
        mem->cola_listos[mem->tail_listos] = id_cliente;
        mem->tail_listos = (mem->tail_listos + 1) % MAX_PEDIDOS;
        mem->count_listos++;
        sem_post(&mem->mutex_listos);
        sem_post(&mem->sem_comida_lista);

        escribirLog("COCINA", "[Cocinero " + to_string(id_cocinero) + "] Orden del Cliente " + to_string(id_cliente) + " lista.");
    }
}

// ─── Hilo Mesero ─────────────────────────────────────────────────────────────

void hiloMesero(MemoriaCompartida* mem, int id_mesero) {
    while (true) {
        sem_wait(&mem->sem_comida_lista); // --- BLOQUEO si no hay comida lista ---
        sem_wait(&mem->mutex_listos);
        int id_cliente = mem->cola_listos[mem->head_listos];
        mem->head_listos = (mem->head_listos + 1) % MAX_PEDIDOS;
        mem->count_listos--;
        sem_post(&mem->mutex_listos);
        sem_post(&mem->sem_espacio_listos);

        escribirLog("MESERO", "[Mesero " + to_string(id_mesero) + "] Llevando comida al Cliente " + to_string(id_cliente) + ".");
        sleep(1);

        sem_post(&mem->sem_cliente_comiendo[id_cliente]);
        escribirLog("MESERO", "[Mesero " + to_string(id_mesero) + "] Entregó comida al Cliente " + to_string(id_cliente) + ".");
    }
}

// ─── Procesos ─────────────────────────────────────────────────────────────────

void procesoClientes(MemoriaCompartida* mem) {
    escribirLog("SISTEMA", "[Proceso Clientes] Abriendo puertas. PID: " + to_string(getpid()));
    srand(time(nullptr) ^ getpid());

    int total = mem->num_clientes;
    vector<thread> clientes;

    for (int i = 1; i <= total; i++) {
        clientes.push_back(thread(hiloCliente, mem, i));
    }

    for (auto& t : clientes)
        if (t.joinable()) t.join();

    escribirLog("SISTEMA", "[Proceso Clientes] Todos los clientes han terminado.");
}

void procesoCocina(MemoriaCompartida* mem) {
    escribirLog("SISTEMA", "[Proceso Cocina] Encendiendo estufas. PID: " + to_string(getpid()));
    srand(time(nullptr) ^ getpid());

    int total = mem->num_cocineros;
    vector<thread> cocineros;

    for (int i = 1; i <= total; i++)
        cocineros.push_back(thread(hiloCocinero, mem, i));

    for (auto& t : cocineros)
        t.detach();

    while (true) sleep(10);
}

void procesoServicio(MemoriaCompartida* mem) {
    escribirLog("SISTEMA", "[Proceso Servicio] Meseros listos. PID: " + to_string(getpid()));

    int total = mem->num_meseros;
    vector<thread> meseros;

    for (int i = 1; i <= total; i++) {
        meseros.push_back(thread(hiloMesero, mem, i));
        meseros.back().detach();
    }

    while (true) sleep(10);
}

// ─── Estadísticas finales ─────────────────────────────────────────────────────

void imprimirEstadisticas(MemoriaCompartida* mem) {
    double promedio = 0;
    if (mem->clientes_atendidos > 0)
        promedio = mem->tiempo_espera_total / mem->clientes_atendidos;

    string sep = "════════════════════════════════";

    escribirLog("STATS", sep);
    escribirLog("STATS", "       ESTADÍSTICAS FINALES      ");
    escribirLog("STATS", sep);
    escribirLog("STATS", "Clientes atendidos:     " + to_string(mem->clientes_atendidos));
    escribirLog("STATS", "Pedidos urgentes:       " + to_string(mem->pedidos_urgentes_atendidos));
    escribirLog("STATS", "Pedidos normales:       " + to_string(mem->pedidos_normales_atendidos));
    escribirLog("STATS", "Tiempo espera promedio: " + to_string((int)promedio) + "s");
    escribirLog("STATS", "Deadlocks detectados:   " + to_string(mem->deadlocks_detectados));
    escribirLog("STATS", "Deadlocks resueltos:    " + to_string(mem->deadlocks_resueltos));
    escribirLog("STATS", sep);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

void mostrarAyuda(const char* prog) {
    cout << "Uso: " << prog << " [opciones]\n"
         << "  --clientes  N   Numero de clientes (default 5, max " << MAX_CLIENTES << ")\n"
         << "  --cocineros N   Numero de cocineros (default 2)\n"
         << "  --meseros   N   Numero de meseros   (default 2)\n"
         << "  --mesas     N   Numero de mesas     (default 3, max " << MAX_MESAS << ")\n"
         << "  --hornos    N   Numero de hornos    (default 1)\n"
         << "  --sartenes  N   Numero de sartenes  (default 1)\n"
         << "  --ayuda         Muestra este mensaje\n";
}

int main(int argc, char* argv[]) {
    // Valores por defecto
    int num_clientes  = 5;
    int num_cocineros = 2;
    int num_meseros   = 2;
    int num_mesas     = 3;
    int num_hornos    = 1;
    int num_sartenes  = 1;

    // Parsear argumentos
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--clientes")  == 0 && i+1 < argc) num_clientes  = atoi(argv[++i]);
        else if (strcmp(argv[i], "--cocineros") == 0 && i+1 < argc) num_cocineros = atoi(argv[++i]);
        else if (strcmp(argv[i], "--meseros")   == 0 && i+1 < argc) num_meseros   = atoi(argv[++i]);
        else if (strcmp(argv[i], "--mesas")     == 0 && i+1 < argc) num_mesas     = atoi(argv[++i]);
        else if (strcmp(argv[i], "--hornos")    == 0 && i+1 < argc) num_hornos    = atoi(argv[++i]);
        else if (strcmp(argv[i], "--sartenes")  == 0 && i+1 < argc) num_sartenes  = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ayuda")     == 0) { mostrarAyuda(argv[0]); return 0; }
    }

    // Validaciones
    if (num_clientes  < 1 || num_clientes  > MAX_CLIENTES) { cerr << "Error: clientes entre 1 y "  << MAX_CLIENTES << "\n"; return 1; }
    if (num_cocineros < 1)  { cerr << "Error: mínimo 1 cocinero\n";  return 1; }
    if (num_meseros   < 1)  { cerr << "Error: mínimo 1 mesero\n";    return 1; }
    if (num_mesas     < 1 || num_mesas > MAX_MESAS) { cerr << "Error: mesas entre 1 y " << MAX_MESAS << "\n"; return 1; }
    if (num_hornos    < 1)  { cerr << "Error: mínimo 1 horno\n";     return 1; }
    if (num_sartenes  < 1)  { cerr << "Error: mínima 1 sartén\n";    return 1; }

    // Crear carpeta de logs
    system("mkdir -p logs && > logs/restaurante.log");

    cout << "--- Configuración ---\n"
         << "  Clientes:  " << num_clientes  << "\n"
         << "  Cocineros: " << num_cocineros << "\n"
         << "  Meseros:   " << num_meseros   << "\n"
         << "  Mesas:     " << num_mesas     << "\n"
         << "  Hornos:    " << num_hornos    << "\n"
         << "  Sartenes:  " << num_sartenes  << "\n"
         << "---------------------\n";

    // Crear memoria compartida
    shm_unlink(SHM_NAME); // Limpiar residuo de ejecución anterior
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) { perror("shm_open"); return 1; }

    if (ftruncate(shm_fd, sizeof(MemoriaCompartida)) == -1) { perror("ftruncate"); return 1; }

    MemoriaCompartida* mem = (MemoriaCompartida*) mmap(
        nullptr, sizeof(MemoriaCompartida),
        PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0
    );
    if (mem == MAP_FAILED) { perror("mmap"); return 1; }

    // Configuración
    mem->num_clientes  = num_clientes;
    mem->num_cocineros = num_cocineros;
    mem->num_meseros   = num_meseros;
    mem->num_mesas     = num_mesas;
    mem->num_hornos    = num_hornos;
    mem->num_sartenes  = num_sartenes;

    // Colas
    mem->head_pedidos = 0; mem->tail_pedidos = 0; mem->count_pedidos = 0;
    mem->head_listos  = 0; mem->tail_listos  = 0; mem->count_listos  = 0;
    for (int i = 0; i < MAX_PEDIDOS; i++) mem->cola_pedidos[i].id_cliente = -1;

    // Estadísticas
    mem->tiempo_espera_total         = 0;
    mem->clientes_atendidos          = 0;
    mem->pedidos_urgentes_atendidos  = 0;
    mem->pedidos_normales_atendidos  = 0;
    mem->deadlocks_detectados        = 0;
    mem->deadlocks_resueltos         = 0;

    // Semáforos
    sem_init(&mem->mutex_pedidos,          1, 1);
    sem_init(&mem->sem_pedidos_urgentes,   1, 0);
    sem_init(&mem->sem_pedidos_normales,   1, 0);
    sem_init(&mem->sem_espacio_pedidos,    1, MAX_PEDIDOS);

    sem_init(&mem->mutex_listos,           1, 1);
    sem_init(&mem->sem_comida_lista,       1, 0);
    sem_init(&mem->sem_espacio_listos,     1, MAX_PEDIDOS);

    sem_init(&mem->sem_horno,              1, num_hornos);
    sem_init(&mem->sem_sarten,             1, num_sartenes);

    sem_init(&mem->sem_mesas_libres,       1, num_mesas);
    sem_init(&mem->mutex_stats,            1, 1);
    for (int i = 0; i <= MAX_CLIENTES; i++)
        sem_init(&mem->sem_cliente_comiendo[i], 1, 0);

    // El mutex del log apunta al mutex de stats (reutilizado para el archivo)
    // Usamos mutex_stats también para el log
    g_mutex_log = &mem->mutex_stats;

    escribirLog("SISTEMA", "--- Iniciando Simulador de Restaurante ---");
    escribirLog("CONFIG",  "clientes=" + to_string(num_clientes) +
                           " cocineros=" + to_string(num_cocineros) +
                           " meseros=" + to_string(num_meseros) +
                           " mesas=" + to_string(num_mesas) +
                           " hornos=" + to_string(num_hornos) +
                           " sartenes=" + to_string(num_sartenes));

    // Fork de los 3 procesos
    pid_t pid_clientes = fork();
    if (pid_clientes == 0) { procesoClientes(mem); return 0; }

    pid_t pid_cocina = fork();
    if (pid_cocina == 0) { procesoCocina(mem); return 0; }

    pid_t pid_servicio = fork();
    if (pid_servicio == 0) { procesoServicio(mem); return 0; }

    // Padre espera a que todos los clientes terminen
    waitpid(pid_clientes, nullptr, 0);
    sleep(2); // Dar tiempo a que los últimos platos sean entregados

    kill(pid_cocina,   SIGTERM);
    kill(pid_servicio, SIGTERM);
    waitpid(pid_cocina,   nullptr, 0);
    waitpid(pid_servicio, nullptr, 0);

    imprimirEstadisticas(mem);
    escribirLog("SISTEMA", "--- Restaurante Cerrado ---");

    // Limpieza
    sem_destroy(&mem->mutex_pedidos);
    sem_destroy(&mem->sem_pedidos_urgentes);
    sem_destroy(&mem->sem_pedidos_normales);
    sem_destroy(&mem->sem_espacio_pedidos);
    sem_destroy(&mem->mutex_listos);
    sem_destroy(&mem->sem_comida_lista);
    sem_destroy(&mem->sem_espacio_listos);
    sem_destroy(&mem->sem_horno);
    sem_destroy(&mem->sem_sarten);
    sem_destroy(&mem->sem_mesas_libres);
    sem_destroy(&mem->mutex_stats);
    for (int i = 0; i <= MAX_CLIENTES; i++)
        sem_destroy(&mem->sem_cliente_comiendo[i]);

    munmap(mem, sizeof(MemoriaCompartida));
    shm_unlink(SHM_NAME);

    return 0;
}