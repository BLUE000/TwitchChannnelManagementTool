// =========================================================================
// TwitchChannelManagementTool Default Overlay Script
// Screensaver Background + Bouncing Cards (compatible with general comments)
// =========================================================================

// --- 1. Background Screensaver (Mystify-like Morphing Lines) ---
const canvas = document.getElementById('screensaverCanvas');
const ctx = canvas.getContext('2d');

let width = canvas.width = window.innerWidth;
let height = canvas.height = window.innerHeight;

window.addEventListener('resize', () => {
    width = canvas.width = window.innerWidth;
    height = canvas.height = window.innerHeight;
});

// Mystify Screensaver Parameters
const shapesCount = 2;
const pointsCount = 4;
const shapes = [];

// Initialize shapes
for (let s = 0; s < shapesCount; s++) {
    const points = [];
    for (let p = 0; p < pointsCount; p++) {
        points.push({
            x: Math.random() * width,
            y: Math.random() * height,
            vx: (Math.random() - 0.5) * 2.5,
            vy: (Math.random() - 0.5) * 2.5
        });
    }
    shapes.push({
        points: points,
        history: [],
        colorHue: Math.random() * 360,
        colorSpeed: 0.15
    });
}

function updateScreensaver() {
    ctx.clearRect(0, 0, width, height);

    shapes.forEach(shape => {
        // Move points and bounce off bounds
        shape.points.forEach(pt => {
            pt.x += pt.vx;
            pt.y += pt.vy;

            if (pt.x < 0 || pt.x > width) pt.vx *= -1;
            if (pt.y < 0 || pt.y > height) pt.vy *= -1;
        });

        // Save history for tail effect
        const currentPoints = shape.points.map(pt => ({ x: pt.x, y: pt.y }));
        shape.history.push(currentPoints);
        if (shape.history.length > 25) {
            shape.history.shift();
        }

        // Draw tails
        shape.colorHue = (shape.colorHue + shape.colorSpeed) % 360;
        shape.history.forEach((pts, index) => {
            const alpha = index / shape.history.length * 0.18;
            ctx.strokeStyle = `hsla(${shape.colorHue}, 80%, 60%, ${alpha})`;
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(pts[0].x, pts[0].y);
            for (let i = 1; i < pts.length; i++) {
                ctx.lineTo(pts[i].x, pts[i].y);
            }
            ctx.closePath();
            ctx.stroke();
        });
    });

    requestAnimationFrame(updateScreensaver);
}

// Start background screensaver
updateScreensaver();


// --- 2. Bouncing Cards Physics System ---
const container = document.getElementById('overlay-container');
const bouncingItems = [];

class BouncingItem {
    constructor(element, widthVal, heightVal, type = 'card') {
        this.element = element;
        this.width = widthVal;
        this.height = heightVal;
        this.type = type;

        // Random starting coordinates & speeds
        this.x = Math.random() * (window.innerWidth - this.width - 40) + 20;
        this.y = Math.random() * (window.innerHeight - this.height - 40) + 20;
        
        const speedMultiplier = type === 'logo' ? 1.0 : 1.5;
        this.vx = (Math.random() > 0.5 ? 1 : -1) * (0.8 + Math.random() * 0.8) * speedMultiplier;
        this.vy = (Math.random() > 0.5 ? 1 : -1) * (0.8 + Math.random() * 0.8) * speedMultiplier;

        // Apply initial positioning
        this.element.style.left = `${this.x}px`;
        this.element.style.top = `${this.y}px`;

        bouncingItems.push(this);
    }

    update() {
        // Move item
        this.x += this.vx;
        this.y += this.vy;

        // Bounce off screen boundaries
        const maxX = window.innerWidth - this.width;
        const maxY = window.innerHeight - this.height;

        if (this.x <= 0) {
            this.x = 0;
            this.vx *= -1;
        } else if (this.x >= maxX) {
            this.x = maxX;
            this.vx *= -1;
        }

        if (this.y <= 0) {
            this.y = 0;
            this.vy *= -1;
        } else if (this.y >= maxY) {
            this.y = maxY;
            this.vy *= -1;
        }

        // Apply positioning to DOM element
        this.element.style.left = `${this.x}px`;
        this.element.style.top = `${this.y}px`;
    }

    remove() {
        // Fade out transition
        this.element.style.transition = 'opacity 0.6s ease, transform 0.6s ease';
        this.element.style.opacity = '0';
        this.element.style.transform = 'scale(0.8)';
        
        setTimeout(() => {
            if (this.element.parentNode) {
                this.element.parentNode.removeChild(this.element);
            }
            const idx = bouncingItems.indexOf(this);
            if (idx > -1) bouncingItems.splice(idx, 1);
        }, 600);
    }
}

// Initialize Main Logo Card
const logoCard = document.getElementById('logoCard');
const mainLogoItem = new BouncingItem(logoCard, 280, 160, 'logo');

// Physics Loop
function updatePhysics() {
    bouncingItems.forEach(item => item.update());
    requestAnimationFrame(updatePhysics);
}
updatePhysics();


// --- 3. WebSocket Comment Event Listener (General Format) ---
let socket = null;
const maxComments = 4; // Maximum concurrent bouncing comments to avoid clutter

function connectWebSocket() {
    const wsUrl = `ws://${window.location.hostname || 'localhost'}:${window.location.port || '8081'}/`;
    console.log(`Connecting to WebSocket: ${wsUrl}`);
    
    socket = new WebSocket(wsUrl);

    socket.onopen = () => {
        console.log("WebSocket connected.");
        document.getElementById('statusText').innerText = "Running / Safe";
        const dot = document.querySelector('.status-dot');
        dot.classList.add('connected');
    };

    socket.onmessage = (event) => {
        try {
            const message = JSON.parse(event.data);
            
            // Handle general comments format
            if (message.type === 'comments' && Array.isArray(message.data)) {
                message.data.forEach(commentData => {
                    createNewCommentCard(commentData);
                });
            }
        } catch (e) {
            console.error("Failed to parse WebSocket message:", e);
        }
    };

    socket.onclose = () => {
        console.log("WebSocket connection closed. Retrying in 5 seconds...");
        document.getElementById('statusText').innerText = "Disconnected";
        const dot = document.querySelector('.status-dot');
        dot.classList.remove('connected');
        setTimeout(connectWebSocket, 5000);
    };

    socket.onerror = (error) => {
        console.error("WebSocket error:", error);
    };
}

// Start WebSocket connection
connectWebSocket();


// --- 4. Dynamic Comment Card Creation ---
function createNewCommentCard(comment) {
    // If the comments list exceeds limit, remove the oldest comment card
    const existingComments = bouncingItems.filter(item => item.type === 'comment');
    if (existingComments.length >= maxComments) {
        existingComments[0].remove();
    }

    // Create Card element
    const card = document.createElement('div');
    card.className = 'bouncing-card comment-card';
    
    // Process badges (if any)
    let badgesHtml = '';
    if (comment.badges && Array.isArray(comment.badges)) {
        comment.badges.forEach(badge => {
            badgesHtml += `<img src="${badge.url}" class="badge" title="${badge.label || ''}" alt="${badge.label || ''}">`;
        });
    }

    // Default avatar if none provided
    const avatarUrl = comment.avatar || 'https://static-cdn.jtvnw.net/user-default-pictures-uv/cdd517d2-5261-4c99-a9ee-f9d0e619b99f-profile_image-70x70.png';

    // Parse comment body for images (emotes) if needed
    // In Twitch comments, emotes can be structured. If comment is plain text, just render text.
    const commentBody = comment.comment || '';

    card.innerHTML = `
        <div class="glow-effect"></div>
        <div class="card-content">
            <div class="comment-header">
                <img src="${avatarUrl}" class="avatar" alt="Avatar">
                <div class="user-meta">
                    <span class="username">${comment.displayName || comment.name}</span>
                    <div class="user-badges">
                        ${badgesHtml}
                    </div>
                </div>
            </div>
            <div class="comment-body">
                ${commentBody}
            </div>
        </div>
    `;

    container.appendChild(card);
    
    // Initialize bouncing physics for the card (W: 360, H: 110 approx dimensions)
    new BouncingItem(card, 360, 110, 'comment');
}


// --- 5. Dummy Comment Simulator (For Demo & Offline Screensaver) ---
// If offline or just started, let's inject a few dummy comments periodically so the screensaver is never empty.
const dummyCommentsList = [
    { name: "TwitchViewer", displayName: "たろう", comment: "こんにちは！ツール期待してます！", avatar: "" },
    { name: "StreamingFan", displayName: "さくら", comment: "わー！スクリーンセーバー風でおしゃれ！", avatar: "" },
    { name: "GamerPro", displayName: "たかし", comment: "低スペックPCでもヌルヌル動くの助かる", avatar: "" },
    { name: "ModUser", displayName: "モデレーターA", comment: "コメントが跳ね回るの見てるの飽きないですねｗ", avatar: "" }
];

let dummyIndex = 0;
setInterval(() => {
    // Only simulate if socket is closed or not connected
    if (!socket || socket.readyState !== WebSocket.OPEN) {
        const dummy = dummyCommentsList[dummyIndex];
        createNewCommentCard({
            id: `dummy-${Date.now()}`,
            service: "twitch",
            name: dummy.name,
            displayName: dummy.displayName,
            comment: dummy.comment,
            avatar: dummy.avatar,
            badges: [
                { url: "https://static-cdn.jtvnw.net/badges/v1/55277104-db34-4502-85a1-62e4bc61111d/3", label: "Moderator" }
            ],
            timestamp: Date.now()
        });
        dummyIndex = (dummyIndex + 1) % dummyCommentsList.length;
    }
}, 12000); // Send a dummy comment every 12 seconds when offline
