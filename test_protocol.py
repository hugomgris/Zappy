#!/usr/bin/env python3

import asyncio
import websockets
import time

async def test_zappy_protocol():
    try:
        async with websockets.connect("ws://localhost:8674") as websocket:
            print("Connected to Zappy server")
            
            # 1. Receive welcome message
            welcome = await websocket.recv()
            print(f"Welcome: {welcome}")
            
            # 2. Send team name
            team_name = "test_team"
            await websocket.send(team_name)
            print(f"Sent team name: {team_name}")
            
            # 3. Receive client number
            client_num = await websocket.recv()
            print(f"Client number: {client_num}")
            
            # 4. Receive world dimensions
            dimensions = await websocket.recv()
            print(f"World dimensions: {dimensions}")
            
            # 5. Test some commands
            commands = ["voir", "inventaire", "avance", "droite", "prend linemate"]
            
            for command in commands:
                print(f"\nSending command: {command}")
                await websocket.send(command)
                
                response = await websocket.recv()
                print(f"Response: {response}")
                
                # Small delay between commands
                await asyncio.sleep(0.5)
            
            print("\nProtocol test completed successfully!")
            
    except Exception as e:
        print(f"Test failed: {e}")

if __name__ == "__main__":
    asyncio.run(test_zappy_protocol())
