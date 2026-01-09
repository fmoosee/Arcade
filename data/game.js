// --- SISTEMA CENTRAL ---
// [CHANGE] Encapsulamento da conexão para permitir reconectar
let ws;
let myId = null;
let currentGame = null;
let isPaused = false;
const canvas = document.getElementById('gameCanvas');
const ctx = canvas.getContext('2d');

function connectWs() {
    ws = new WebSocket(`ws://${window.location.hostname}/ws`);
    
    ws.onopen = () => {
        document.getElementById('connection-status').innerText = "🟢 Conectado";
        ws.send(JSON.stringify({type: "join"}));
    };

    ws.onclose = () => {
        document.getElementById('connection-status').innerText = "🔴 Desconectado. Tentando reconectar...";
        // [CHANGE] Tenta reconectar em 2 segundos
        setTimeout(connectWs, 2000); 
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
            currentGame.handleNetworkMessage(msg);
        }
    };
}

// Inicia conexão
connectWs();

// Ajusta Canvas
function resizeCanvas() {
    canvas.width = window.innerWidth > 400 ? 400 : window.innerWidth - 20;
    canvas.height = window.innerHeight * 0.7;
}
window.addEventListener('resize', resizeCanvas);
resizeCanvas();

// --- CONTROLES GERAIS ---
function startGame(gameName) {
    document.getElementById('lobby-screen').classList.remove('active');
    document.getElementById('game-screen').classList.add('active');
    
    if (gameName === 'pingpong') currentGame = new PingPong();
    else if (gameName === 'meteoro') currentGame = new Meteoro();
    
    gameLoop();
}

function exitGame() {
    currentGame = null;
    document.getElementById('game-screen').classList.remove('active');
    document.getElementById('lobby-screen').classList.add('active');
}

function sendPause() { ws.send(JSON.stringify({type: "pause"})); }
function sendReset() { ws.send(JSON.stringify({type: "reset"})); }

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

// [CHANGE] Função auxiliar de Interpolação Linear (Suavização)
// O valor vai de 'start' para 'end' em passos de 'amt' (0.0 a 1.0)
function lerp(start, end, amt) {
    return (1 - amt) * start + amt * end;
}

// ==========================================================
// === JOGO 1: PING PONG
// ==========================================================
class PingPong {
    constructor() {
        this.paddleW = 80;
        this.paddleH = 10;
        this.isP1 = (myId % 2) === 0; 
        
        this.x = canvas.width / 2 - this.paddleW / 2;
        
        // [CHANGE] targetRemoteX guarda onde o inimigo quer ir
        // remoteX é onde ele está sendo desenhado agora (suavizado)
        this.remoteX = canvas.width / 2 - this.paddleW / 2;
        this.targetRemoteX = this.remoteX;
        
        this.ball = {x: 100, y: 100, vx: 2, vy: 2, r: 5};
        
        canvas.addEventListener('touchmove', (e) => {
            e.preventDefault();
            const touch = e.touches[0];
            const rect = canvas.getBoundingClientRect();
            this.x = touch.clientX - rect.left - this.paddleW/2;
            ws.send(JSON.stringify({type: "move", x: this.x}));
        }, {passive: false});
    }

    handleNetworkMessage(msg) {
        if (msg.type === "move" && msg.id !== myId) {
            // [CHANGE] Não teleportamos, apenas definimos o alvo
            this.targetRemoteX = msg.x; 
        }
    }

    update() {
        // [CHANGE] Suaviza o movimento do oponente (0.2 = 20% do caminho por frame)
        // Isso remove o efeito "travado" se a rede oscilar.
        this.remoteX = lerp(this.remoteX, this.targetRemoteX, 0.2);

        this.ball.x += this.ball.vx;
        this.ball.y += this.ball.vy;

        if (this.ball.x < 0 || this.ball.x > canvas.width) this.ball.vx *= -1;
        if (this.ball.y < 0 || this.ball.y > canvas.height) this.ball.vy *= -1;
    }

    draw() {
        // Player
        ctx.fillStyle = "#0f0";
        ctx.fillRect(this.x, canvas.height - 20, this.paddleW, this.paddleH);
        
        // Oponente
        ctx.fillStyle = "#f00";
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

class Meteoro {
    constructor() {}
    handleNetworkMessage(msg) {}
    update() {}
    draw() {
        ctx.fillStyle = "yellow";
        ctx.font = "20px Arial";
        ctx.fillText("Meteoro - Em Breve", 50, canvas.height/2);
    }
    reset() {}
}