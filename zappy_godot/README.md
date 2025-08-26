# Zappy GUI Visualizer (Godot)

## Overview
This is a real-time 3D visualization tool for the Zappy game built using Godot Engine. It provides a comprehensive view of the game state including map, players, resources, and team statistics.

## Features Implemented

### 🗺️ **Map Visualization**
- **3D Tile-based Map**: Dynamic map generation based on server data
- **Resource Visualization**: Tiles are color-coded based on their dominant resource type
  - 🟢 Green: Food (nourriture)
  - 🟡 Yellow: Linemate
  - 🔵 Blue: Deraumere
  - 🔴 Red: Sibur
  - 🟣 Purple: Mendiane
  - 🟠 Orange: Phiras
  - 🔵 Cyan: Thystame
- **Resource Intensity**: Color intensity indicates resource abundance

### 👥 **Player Visualization**
- **3D Player Models**: Capsule-shaped players with team-based colors
- **Team Colors**:
  - 🔴 Red: Alpha Team
  - 🔵 Blue: Beta Team
  - 🟢 Green: Gamma Team
  - 🟡 Yellow: Delta Team
- **Status Indicators**: Visual changes based on player status
  - Lighter colors during incantation
  - Yellowish tint when broadcasting
- **Smooth Movement**: Animated transitions between positions
- **3D Labels**: Player ID display above each player

### 📊 **Real-time UI Panels**

#### Game Information Panel
- Current game tick
- Time unit settings
- Real-time game status

#### Player Information Panel
- Selected player details (use number keys 1-9 to select)
- Position, level, team, and status
- Complete inventory display
- Resource quantities

#### Team Statistics Panel
- All team information
- Player count per team
- Available connection slots

#### Resource Statistics Panel
- Global resource totals across the map
- Real-time resource tracking

### 🔄 **Data Management**
- **GameData Singleton**: Centralized game state management
- **Event-driven Updates**: Efficient UI updates only when data changes
- **JSON Protocol Support**: Parses the server's JSON game state format
- **Signal System**: Decoupled communication between components

### 🎮 **Controls**
- **Number Keys (1-9)**: Select and view specific player information
- **Mouse**: Camera navigation (if implemented)

## Technical Architecture

### Core Components
1. **GameData.gd**: Singleton managing all game state data
2. **Main.gd**: 3D scene controller and visual update manager
3. **UI.gd**: UI panel controller and player selection handler
4. **NetworkManager.gd**: Server communication handler

### Data Flow
```
Server JSON → NetworkManager → GameData → Visual Updates + UI Updates
```

### Scene Structure
```
Main (Node3D)
├── Camera3D
├── DirectionalLight3D
├── MapRoot (Node3D) - Contains all tile instances
├── PlayerRoot (Node3D) - Contains all player instances
├── NetworkManager (Node) - Handles server communication
└── UI (Control) - Contains all UI panels
```

## Demo Features for Meeting

### Live Data Simulation
The visualizer includes test data loading that demonstrates:
- 2x2 map with different resource distributions
- 3 players from 2 different teams
- Various player statuses (normal, incantation, broadcasting)
- Team statistics and resource totals

### Visual Highlights
- **Resource mapping**: Each tile shows its dominant resource through color
- **Team visualization**: Players are clearly distinguishable by team colors
- **Status awareness**: Player activities are visually represented
- **Information density**: Comprehensive game state at a glance

## Future Enhancements
- Egg visualization
- Broadcast message display
- Event log panel
- Camera controls and zoom
- Audio feedback for events
- Performance optimizations for larger maps
- Custom player models and animations

## Getting Started
1. Open the project in Godot 4.4+
2. Run the main scene
3. Use number keys 1-3 to select different players
4. Observe real-time updates to all panels

This foundation provides a solid starting point for a complete Zappy visualization system and demonstrates the potential for rich, real-time game state visualization.
