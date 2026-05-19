const express = require('express');
const { spawn, exec } = require('child_process');
const fs = require('fs');
const path = require('path');

const app = express();
app.use(express.json());
app.use(express.static('public')); // Aquí serviremos el index.html

let simulador = null;

app.post('/api/start', (req, res) => {
    if (simulador) {
        return res.status(400).json({ error: 'La simulación ya está corriendo.' });
    }

    // Limpiar el log antes de iniciar
    if (!fs.existsSync('logs')) fs.mkdirSync('logs');
    fs.writeFileSync('logs/restaurante.log', '');

    const { clientes, cocineros, meseros, mesas, hornos, sartenes } = req.body;
    
    // Construir argumentos
    const args = [
        '--clientes', clientes || 5,
        '--cocineros', cocineros || 2,
        '--meseros', meseros || 2,
        '--mesas', mesas || 3,
        '--hornos', hornos || 1,
        '--sartenes', sartenes || 1
    ];

    console.log('Iniciando binario con args:', args);
    simulador = spawn('./build/restaurante', args, { detached: true });

    simulador.on('close', (code) => {
        console.log(`Simulación finalizada con código ${code}`);
        simulador = null;
    });

    res.json({ message: 'Simulación iniciada.' });
});

app.post('/api/stop', (req, res) => {
    if (!simulador) {
        return res.status(400).json({ error: 'No hay ninguna simulación en curso.' });
    }

    // Matar a todo el grupo de procesos (anteponiendo - al PID)
    try {
        process.kill(-simulador.pid, 'SIGTERM');
    } catch (e) {
        console.error('Error al matar proceso:', e);
        // Si falla el grupo, intentamos con el proceso solo
        simulador.kill('SIGTERM');
    }
    
    simulador = null;
    
    // Limpieza forzada de memoria compartida
    exec('rm -f /dev/shm/shm_restaurante', (err) => {
        if (err) console.error('Error limpiando SHM:', err);
    });

    res.json({ message: 'Simulación detenida forzosamente.' });
});

app.get('/api/logs', (req, res) => {
    try {
        const logs = fs.readFileSync('logs/restaurante.log', 'utf-8');
        res.send(logs);
    } catch (e) {
        res.send('');
    }
});

const PORT = 3000;
app.listen(PORT, () => {
    console.log(`Servidor de control corriendo en http://localhost:${PORT}`);
});