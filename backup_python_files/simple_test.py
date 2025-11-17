#!/usr/bin/env python3

import asyncio
import websockets
import sys

async def test_connection():
    try:
        # Connect to the WebSocket server (no SSL)
        async with websockets.connect("ws://localhost:8674") as websocket:
            print("Connected to WebSocket server")
            
            # Read from stdin if available
            if not sys.stdin.isatty():
                message = sys.stdin.read().strip()
                if message:
                    await websocket.send(message)
                    print(f"Sent: {message}")
            
            # Send a test message
            await websocket.send("test message")
            print("Sent: test message")
            
            # Wait for a response
            try:
                response = await asyncio.wait_for(websocket.recv(), timeout=5.0)
                print(f"Received: {response}")
            except asyncio.TimeoutError:
                print("No response received within 5 seconds")
            
    except Exception as e:
        print(f"Connection failed: {e}")

if __name__ == "__main__":
    asyncio.run(test_connection())
