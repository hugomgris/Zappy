#!/usr/bin/env python3

import asyncio
import websockets
import json
import random

class MockZappyServer:
    def __init__(self):
        self.clients = {}
        self.world_width = 10
        self.world_height = 10
        
    async def handle_client(self, websocket):
        """Handle a new client connection"""
        client_id = id(websocket)
        self.clients[client_id] = {
            'websocket': websocket,
            'authenticated': False,
            'team': None,
            'x': 0,
            'y': 0,
            'orientation': 'N',
            'level': 1
        }
        
        print(f"Client {client_id} connected")
        
        try:
            # Send welcome message
            await websocket.send("BIENVENUE")
            
            async for message in websocket:
                print(f"Received from client {client_id}: {message}")
                
                # Handle authentication
                if not self.clients[client_id]['authenticated']:
                    # First message should be team name
                    team_name = message.strip()
                    self.clients[client_id]['team'] = team_name
                    self.clients[client_id]['authenticated'] = True
                    
                    # Send client number and world dimensions
                    await websocket.send("0")  # Client number (0 available spots)
                    await websocket.send(f"{self.world_width} {self.world_height}")
                    continue
                
                # Handle commands
                await self.handle_command(client_id, message)
                
        except websockets.exceptions.ConnectionClosed:
            print(f"Client {client_id} disconnected")
        except Exception as e:
            print(f"Error handling client {client_id}: {e}")
        finally:
            if client_id in self.clients:
                del self.clients[client_id]
    
    async def handle_command(self, client_id, command):
        """Handle a command from a client"""
        client = self.clients[client_id]
        websocket = client['websocket']
        
        command = command.strip().lower()
        
        if command == "avance":
            await websocket.send("ok")
        elif command == "droite":
            await websocket.send("ok") 
        elif command == "gauche":
            await websocket.send("ok")
        elif command == "voir":
            # Send a simple view response
            view = ["linemate", "", "player", "food", "", "", "", "", ""]
            await websocket.send(" ".join(view))
        elif command == "inventaire":
            # Send inventory
            await websocket.send("linemate 1, deraumere 0, sibur 0, mendiane 0, phiras 0, thystame 0, nourriture 10")
        elif command == "prend linemate":
            await websocket.send("ok")
        elif command == "pose linemate":
            await websocket.send("ok")
        elif command.startswith("broadcast"):
            await websocket.send("ok")
        elif command == "incantation":
            await websocket.send("elevation en cours")
            # Simulate elevation delay
            await asyncio.sleep(1)
            await websocket.send("niveau actuel: 2")
        elif command == "fork":
            await websocket.send("ok")
        elif command == "connect_nbr":
            await websocket.send("0")
        else:
            await websocket.send("ko")

async def main():
    server = MockZappyServer()
    
    print("Starting Simple Mock Zappy WebSocket Server on ws://localhost:8674")
    
    async with websockets.serve(
        server.handle_client, 
        "localhost", 
        8674
    ):
        print("Server started! Waiting for connections...")
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    asyncio.run(main())
