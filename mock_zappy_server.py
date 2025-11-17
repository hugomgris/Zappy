import asyncio
import ssl
import websockets
import json
from pathlib import Path

class MockZappyServer:
    def __init__(self):
        self.clients = {}
        self.client_counter = 0
        
    async def handle_client(self, websocket):
        client_id = self.client_counter
        self.client_counter += 1
        self.clients[client_id] = websocket
        
        print(f"Client {client_id} connected from {websocket.remote_address}")
        
        try:
            # Send welcome message (like your partner's server)
            welcome_msg = {
                "type": "bienvenue",
                "msg": "Whoa! Knock knock, whos there?"
            }
            await websocket.send(json.dumps(welcome_msg, indent='\t'))
            print(f"Sent welcome to client {client_id}")
            
            async for message in websocket:
                try:
                    print(f"Client {client_id} sent: {message}")
                    
                    # Try to parse as JSON first
                    try:
                        data = json.loads(message)
                        # Handle different message types
                        if data.get("type") == "login":
                            await self.handle_login(websocket, client_id, data)
                        elif "message" in data:  # For your test.py script
                            await self.handle_test_message(websocket, client_id, data)
                        else:
                            # Echo back unknown messages
                            response = {"type": "echo", "original": data}
                            await websocket.send(json.dumps(response))
                    except json.JSONDecodeError:
                        # Handle plain text messages (from Godot WebSocket extension)
                        await self.handle_plain_text_message(websocket, client_id, message)
                        
                except Exception as e:
                    print(f"Error handling message from client {client_id}: {e}")
                    break
                    
        except websockets.exceptions.ConnectionClosed:
            print(f"Client {client_id} disconnected")
        finally:
            if client_id in self.clients:
                del self.clients[client_id]
    
    async def handle_login(self, websocket, client_id, data):
        print(f"Client {client_id} login attempt: {data}")
        
        # Simulate login response
        if data.get("role") == "observer":
            # Send game state for observer
            game_state = await self.get_mock_game_state()
            await websocket.send(json.dumps(game_state, indent='\t'))
            print(f"Sent game state to observer {client_id}")
            
            # Start sending periodic updates
            asyncio.create_task(self.send_periodic_updates(websocket, client_id))
        else:
            # Regular player login
            login_response = {
                "type": "login_success",
                "player_id": client_id,
                "team": data.get("team", "unknown")
            }
            await websocket.send(json.dumps(login_response))
    
    async def handle_test_message(self, websocket, client_id, data):
        # Handle messages from test.py
        response = {
            "type": "test_response",
            "client_id": data.get("client_id", client_id),
            "echo": data.get("message", ""),
            "length": len(data.get("message", ""))
        }
        await websocket.send(json.dumps(response))
    
    async def get_mock_game_state(self):
        # Load your existing game data
        try:
            game_file = Path("zappy_godot/data/initial_data/game_3x3.json")
            if game_file.exists():
                with open(game_file, 'r') as f:
                    game_data = json.load(f)
                    game_data["type"] = "game_state"
                    return game_data
        except Exception as e:
            print(f"Error loading game data: {e}")
        
        # Fallback mock data
        return {
            "type": "game_state",
            "map": {
                "width": 3,
                "height": 3,
                "tiles": [
                    {"x": 0, "y": 0, "resources": {"nourriture": 2}, "players": [1], "eggs": []},
                    {"x": 1, "y": 0, "resources": {"linemate": 1}, "players": [], "eggs": []},
                    {"x": 2, "y": 0, "resources": {}, "players": [], "eggs": []}
                ]
            },
            "players": [
                {"id": 1, "position": {"x": 0, "y": 0}, "orientation": 1, "team": "Alpha"}
            ],
            "game": {"tick": 100, "teams": ["Alpha", "Beta"]}
        }
    
    async def send_periodic_updates(self, websocket, client_id):
        """Send periodic game updates to observers"""
        try:
            while True:
                await asyncio.sleep(2.0)  # Send update every 2 seconds
                
                # Mock some game events
                updates = [
                    {"type": "player_move", "player_id": 1, "new_orientation": 2},
                    {"type": "resource_spawn", "position": {"x": 1, "y": 1}, "resource": "sibur"},
                    {"type": "player_action", "player_id": 1, "action": "prend", "object": "nourriture"}
                ]
                
                for update in updates:
                    await websocket.send(json.dumps(update))
                    await asyncio.sleep(0.5)
                
        except websockets.exceptions.ConnectionClosed:
            print(f"Observer {client_id} disconnected during updates")
        except Exception as e:
            print(f"Error sending updates to {client_id}: {e}")
    
    async def handle_plain_text_message(self, websocket, client_id, message):
        """Handle plain text messages (from Godot WebSocket extension)"""
        message = message.strip()
        print(f"Plain text message from client {client_id}: {message}")
        
        # Simple text responses for testing
        if message == "test from godot":
            response = {"type": "test_response", "message": "Hello from server!"}
            await websocket.send(json.dumps(response))
        elif message == "ping":
            response = {"type": "pong"}
            await websocket.send(json.dumps(response))
        else:
            # Echo back as JSON
            response = {"type": "echo", "message": message}
            await websocket.send(json.dumps(response))

async def main():
    # Create SSL context for WSS
    ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ssl_context.load_cert_chain("server.crt", "server.key")  # You'll need to create these
    
    server = MockZappyServer()
    
    print("Starting Mock Zappy WebSocket Server on wss://localhost:8674")
    
    try:
        async with websockets.serve(
            server.handle_client, 
            "localhost", 
            8674,
            ssl=ssl_context
        ):
            print("Server started! Waiting for connections...")
            await asyncio.Future()  # Run forever
    except FileNotFoundError:
        print("SSL certificates not found. Starting insecure WebSocket server on ws://localhost:8674")
        # Fallback to non-SSL
        async with websockets.serve(
            server.handle_client, 
            "localhost", 
            8674
        ):
            print("Insecure server started! Waiting for connections...")
            await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())
