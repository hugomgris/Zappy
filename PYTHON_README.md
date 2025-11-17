# Zappy Python Testing Infrastructure

## 🎯 Current Files

### **Main Development Server**
- **`mock_zappy_server.py`** - Advanced WebSocket server
  - ✅ SSL/TLS support (wss://)
  - ✅ JSON and plain text message handling  
  - ✅ Simulates your partner's server protocol
  - ✅ Observer mode with game state broadcasting
  - 🚀 **Use this for Godot WebSocket testing**

### **Protocol Testing**
- **`test_protocol.py`** - Validates Zappy protocol compliance
  - Tests: BIENVENUE → team_name → client_number → world_dimensions → commands
  - Useful for ensuring your server follows standard Zappy protocol

### **Simple Development Server (Optional)**
- **`simple_mock_server.py`** - Basic WebSocket server
  - No SSL, plain text only
  - Standard Zappy protocol (non-JSON)
  - Useful for basic protocol debugging

## 🚀 Usage

### Start the main development server:
```bash
python3 mock_zappy_server.py
```
Server runs on `wss://localhost:8674` (SSL) or falls back to `ws://localhost:8674`

### Test the protocol:
```bash
python3 test_protocol.py
```

### Start simple server (if needed):
```bash
python3 simple_mock_server.py
```

## 🔧 SSL Certificates

Self-signed certificates are included:
- `server.crt` - SSL certificate
- `server.key` - SSL private key

## 📦 Backup

Obsolete development files moved to `backup_python_files/`:
- `test.py` - Interactive SSL test client
- `simple_test.py` - Basic connection test  
- `multi_test.py` - Load test with 50 clients

## 🎯 For Weekend Meeting

1. **Demo your WebSocket infrastructure**: `python3 mock_zappy_server.py`
2. **Show protocol compliance**: `python3 test_protocol.py`
3. **Connect your Godot client** to the running server
4. **Switch to your partner's server** by changing IP/port in Godot
