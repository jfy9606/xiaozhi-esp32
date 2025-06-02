// AI chat page JavaScript

// Configuration and state
const aiConfig = {
  ws: null,
  thinking: false,
  messages: [],
};

// Initialize when document is ready
document.addEventListener('DOMContentLoaded', function() {
  initWebSocket();
  initChatUI();
});

// Initialize WebSocket connection
function initWebSocket() {
  if (aiConfig.ws) {
    aiConfig.ws.close();
  }
  
  const wsUrl = `ws://${window.location.host}/ws/ai`;
  aiConfig.ws = new WebSocket(wsUrl);
  
  aiConfig.ws.onopen = function() {
    console.log("WebSocket connection established");
    // Request initial chat history
    sendWebSocketMessage({ cmd: 'get_history' });
  };
  
  aiConfig.ws.onmessage = function(event) {
    try {
      const message = JSON.parse(event.data);
      handleWebSocketMessage(message);
    } catch (e) {
      console.error("Error parsing WebSocket message:", e);
    }
  };
  
  aiConfig.ws.onclose = function() {
    console.log("WebSocket connection closed");
    // Try to reconnect after a delay
    setTimeout(initWebSocket, 5000);
  };
  
  aiConfig.ws.onerror = function(error) {
    console.error("WebSocket error:", error);
  };
}

// Send message through WebSocket
function sendWebSocketMessage(message) {
  if (aiConfig.ws && aiConfig.ws.readyState === WebSocket.OPEN) {
    aiConfig.ws.send(JSON.stringify(message));
  } else {
    console.warn("WebSocket not connected");
  }
}

// Handle incoming WebSocket messages
function handleWebSocketMessage(message) {
  console.log("Received message:", message);
  
  if (message.type === 'chat_message') {
    showChatMessage(message.data);
  } else if (message.type === 'chat_history') {
    loadChatHistory(message.data);
  } else if (message.type === 'thinking_start') {
    showThinking(true);
  } else if (message.type === 'thinking_end') {
    showThinking(false);
  }
}

// Initialize chat UI components
function initChatUI() {
  const chatForm = document.getElementById('chat-form');
  const chatInput = document.getElementById('chat-input');
  
  // Handle sending messages
  if (chatForm) {
    chatForm.addEventListener('submit', function(e) {
      e.preventDefault();
      
      if (chatInput && chatInput.value.trim() !== '') {
        const userMessage = chatInput.value.trim();
        
        // Add user message to UI
        const messageData = {
          text: userMessage,
          sender: 'user',
          timestamp: new Date().toISOString()
        };
        
        showChatMessage(messageData);
        
        // Send to server
        sendWebSocketMessage({
          cmd: 'send_message',
          text: userMessage
        });
        
        // Clear input
        chatInput.value = '';
      }
    });
  }
  
  // Auto-focus the chat input
  if (chatInput) {
    chatInput.focus();
  }
}

// Show chat message in UI
function showChatMessage(message) {
  const messagesContainer = document.getElementById('chat-messages');
  if (!messagesContainer) return;
  
  // Add to messages array
  aiConfig.messages.push(message);
  
  // Create message element
  const messageElement = document.createElement('div');
  messageElement.className = `message message-${message.sender}`;
  
  const contentElement = document.createElement('div');
  contentElement.className = 'message-content';
  contentElement.textContent = message.text;
  
  const timeElement = document.createElement('div');
  timeElement.className = 'message-time';
  timeElement.textContent = formatMessageTime(message.timestamp);
  
  messageElement.appendChild(contentElement);
  contentElement.appendChild(timeElement);
  
  messagesContainer.appendChild(messageElement);
  
  // Scroll to bottom
  messagesContainer.scrollTop = messagesContainer.scrollHeight;
  
  // Hide thinking indicator if we received an AI message
  if (message.sender === 'ai') {
    showThinking(false);
  }
}

// Load chat history
function loadChatHistory(history) {
  const messagesContainer = document.getElementById('chat-messages');
  if (!messagesContainer) return;
  
  // Clear container
  messagesContainer.innerHTML = '';
  
  // Reset state
  aiConfig.messages = [];
  
  // Add each message
  if (Array.isArray(history)) {
    history.forEach(message => {
      showChatMessage(message);
    });
  }
}

// Show/hide thinking indicator
function showThinking(isThinking) {
  const messagesContainer = document.getElementById('chat-messages');
  if (!messagesContainer) return;
  
  // Update state
  aiConfig.thinking = isThinking;
  
  // Remove existing thinking indicators
  const existingIndicator = document.getElementById('ai-thinking-indicator');
  if (existingIndicator) {
    existingIndicator.remove();
  }
  
  // Add new thinking indicator if needed
  if (isThinking) {
    const thinkingElement = document.createElement('div');
    thinkingElement.className = 'ai-thinking';
    thinkingElement.id = 'ai-thinking-indicator';
    
    thinkingElement.innerHTML = `
      AI is thinking<span class="dots">
        <span class="dot"></span>
        <span class="dot"></span>
        <span class="dot"></span>
      </span>
    `;
    
    messagesContainer.appendChild(thinkingElement);
    messagesContainer.scrollTop = messagesContainer.scrollHeight;
  }
}

// Format timestamp for display
function formatMessageTime(timestamp) {
  if (!timestamp) return '';
  
  const date = new Date(timestamp);
  const hours = date.getHours().toString().padStart(2, '0');
  const minutes = date.getMinutes().toString().padStart(2, '0');
  
  return `${hours}:${minutes}`;
} 