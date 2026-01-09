// --- SISTEMA CENTRAL ---
const ws = new WebSocket(`ws://${window.location.hostname}/ws`);
let myId = null;
let currentGame = null;
let isPaused = false;
const canvas = document.getElementById('gameCanvas');
const ctx = canvas.getContext('2d');

// Ajusta Canvas
function resizeCanvas() {
    canvas.width = window.innerWidth > 400 ? 400 : window.innerWidth - 20;
    canvas.height = window.innerHeight * 0.7;
}
window.addEventListener('resize', resizeCanvas);
resizeCanvas();

// --- WEBSOCKET ---
ws.onopen = () => {
    document.getElementById('connection-status').innerText = "🟢 Conectado";
    ws.send(JSON.stringify({type: "join"}));
};

ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    
    if (msg.type === 'welcome') {
        myId = msg.id;
        document.getElementById('bat-val').innerText = msg.bat;
    } 
    else if (msg.type === 'battery') {
        document.getElementById('bat-val').innerText = msg.val;
    }
    else if (msg.type === 'players') {
        document.getElementById('players-val').innerText = msg.count;
    }
    else if (msg.type === 'pause') {
        isPaused = true;
        document.getElementById('pause-overlay').classList.remove('hidden');
    }
    else if (msg.type === 'reset') {
        isPaused = false;
        document.getElementById('pause-overlay').classList.add('hidden');
        if (currentGame) currentGame.reset();
    }
    else if (currentGame) {
        // Passa mensagem para o jogo atual lidar (ex: movimento do oponente)
        currentGame.handleNetworkMessage(msg);
    }
};

// --- CONTROLES GERAIS ---
function startGame(gameName) {
    document.getElementById('lobby-screen').classList.remove('active');
    document.getElementById('game-screen').classList.add('active');
    
    if (gameName === 'pingpong') currentGame = new PingPong();
    else if (gameName === 'meteoro') currentGame = new Meteoro();
    // Adicionar outros...
    
    gameLoop();
}

function exitGame() {
    currentGame = null;
    document.getElementById('game-screen').classList.remove('active');
    document.getElementById('lobby-screen').classList.add('active');
}

function sendPause() {
    ws.send(JSON.stringify({type: "pause"})); // Envia comando de pausa para todos
}

function sendReset() {
    ws.send(JSON.stringify({type: "reset"})); // Reseta para todos
}

// --- GAME LOOP ---
function gameLoop() {
    if (!currentGame) return;
    
    if (!isPaused) {
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        currentGame.update();
        currentGame.draw();
    }
    requestAnimationFrame(gameLoop);
}

// ==========================================================
// === JOGO 1: PING PONG (Lógica Cliente)
// ==========================================================
class PingPong {
    constructor() {
        this.paddleW = 80;
        this.paddleH = 10;
        // Eu sou Player 1 (Bottom) ou Player 2 (Top)?
        // Lógica simples: IDs pares são P1, ímpares P2 (pode melhorar depois)
        this.isP1 = (myId % 2) === 0; 
        
        this.x = canvas.width / 2 - this.paddleW / 2;
        this.remoteX = canvas.width / 2 - this.paddleW / 2;
        
        this.ball = {x: 100, y: 100, vx: 2, vy: 2, r: 5};
        
        // Controles de Toque
        canvas.addEventListener('touchmove', (e) => {
            e.preventDefault();
            const touch = e.touches[0];
            const rect = canvas.getBoundingClientRect();
            this.x = touch.clientX - rect.left - this.paddleW/2;
            
            // Envia minha posição para o servidor
            ws.send(JSON.stringify({type: "move", x: this.x}));
        }, {passive: false});
    }

    handleNetworkMessage(msg) {
        if (msg.type === "move" && msg.id !== myId) {
            // Atualiza posição do oponente
            this.remoteX = msg.x; 
        }
    }

    update() {
        // Física da bola (Simplificada - roda localmente em cada celular)
        // O ideal é um "Host" decidir, mas para LAN WiFi funciona bem assim
        this.ball.x += this.ball.vx;
        this.ball.y += this.ball.vy;

        // Colisões parede
        if (this.ball.x < 0 || this.ball.x > canvas.width) this.ball.vx *= -1;
        if (this.ball.y < 0 || this.ball.y > canvas.height) this.ball.vy *= -1;

        // Colisões raquete (Minha e Oponente)
        // ... (Adicionar lógica de colisão básica aqui)
    }

    draw() {
        // Desenha Minha Raquete (Verde)
        ctx.fillStyle = "#0f0";
        ctx.fillRect(this.x, canvas.height - 20, this.paddleW, this.paddleH);
        
        // Desenha Oponente (Vermelho - topo)
        ctx.fillStyle = "#f00";
        // Inverte o X do oponente para espelhar a tela
        ctx.fillRect(canvas.width - this.remoteX - this.paddleW, 10, this.paddleW, this.paddleH);
        
        // Bola
        ctx.fillStyle = "#fff";
        ctx.beginPath();
        ctx.arc(this.ball.x, this.ball.y, this.ball.r, 0, Math.PI*2);
        ctx.fill();
    }
    
    reset() {
        this.ball = {x: canvas.width/2, y: canvas.height/2, vx: 2, vy: 2, r: 5};
    }
}

// ==========================================================
// === JOGO 2: METEORO (Placeholder)
// ==========================================================
class Meteoro {
    constructor() {
        this.shipX = canvas.width / 2;
    }
    handleNetworkMessage(msg) {}
    update() {}
    draw() {
        ctx.fillStyle = "yellow";
        ctx.font = "20px Arial";
        ctx.fillText("Meteoro - Em Breve", 50, canvas.height/2);
    }
    reset() {}
}

// Para Galaga e Flappy Bird, siga a estrutura da classe 'Meteoro'