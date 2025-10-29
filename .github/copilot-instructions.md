# GitHub Copilot Instructions for KeeperRL

## Project Overview

KeeperRL is a roguelike dungeon simulator game written in C++. The project allows players to build and manage a dungeon, command minions, and defend against heroes and other threats. This is a complex C++ codebase with game logic, AI, rendering, and data-driven configuration.

## Build System

- **Primary Build**: GNU Make with multiple platform-specific Makefiles
  - `Makefile` - Main Linux/Unix Makefile
  - `Makefile-win` - Windows-specific
  - `Makefile-steam` - Steam integration build
- **Alternative**: CMake (`CMakeLists.txt`) for cross-platform builds
- **Compiler Requirements**: 
  - GCC 4.8.2+ or Clang 3.5+
  - C++14 standard (`-std=c++1y`)

### Build Commands

```bash
# Standard build
make -j 8 OPT=true RELEASE=true

# Debug build
make -j 8 DEBUG=true

# CMake build
mkdir build && cd build
cmake ..
make
```

## Project Structure

- **Root Directory**: Contains all `.cpp` and `.h` source files (flat structure)
- **`data_free/`**: Free game assets and configuration files
  - `game_config/` - Game data definitions (creatures, items, spells, etc.)
- **`data_contrib/`**: Contributed assets
- **`extern/`**: External dependencies
- **`server/`**: Server-related code
- **`cmake/`**: CMake modules
- **`mac/`**: macOS-specific files

## Code Conventions

### File Headers
All source files must include the GPL v2 license header:
```cpp
/* Copyright (C) 2013-2025 Michal Brzozowski (rusolis@poczta.fm)

   This file is part of KeeperRL.

   KeeperRL is free software; you can redistribute it and/or modify it under the terms of the
   GNU General Public License as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   KeeperRL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
   even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program.
   If not, see http://www.gnu.org/licenses/ . */
```

### Code Style
- **Formatting**: LLVM style with customizations (see `.clang-format`)
- **Indentation**: 2 spaces, no tabs
- **Column Limit**: 120 characters
- **Pointer Alignment**: Left (`Type* ptr`, not `Type *ptr`)
- **Includes**: 
  - First include should be `"stdafx.h"` in `.cpp` files
  - Do not sort includes automatically
- **Namespaces**: Inner indentation
- **Naming**:
  - Classes/Types: PascalCase (e.g., `Creature`, `MonsterAI`)
  - Functions/methods: camelCase
  - Files: snake_case matching class names (e.g., `monster_ai.cpp` for `MonsterAI`)

### Design Patterns
- Heavy use of inheritance and polymorphism
- Factory patterns for creating game entities (`CreatureFactory`, `ItemFactory`, etc.)
- Controller pattern for entity behavior (`Controller`, `Monster`, `Player`)
- Event-driven architecture (`EventListener`, `EventGenerator`)
- Position-based spatial system (`Position`, `Level`)
- Serialization support throughout codebase

## Key Components

### Game Entities
- **`Creature`**: Player characters, monsters, NPCs
- **`Item`**: All in-game items
- **`Furniture`**: Interactive map objects
- **`Level`**: Game map and spatial organization
- **`Collective`**: Group management (dungeons, villages)

### AI & Control
- **`Controller`**: Base class for entity control
- **`Monster`**: AI controller for hostile creatures
- **`Player`**: Player controller
- **`MonsterAI`**: AI decision-making

### Game Systems
- **`Game`**: Main game state and loop
- **`Model`**: Game world model
- **`View`**: UI rendering abstraction
- **`WindowView`**: Main UI implementation

### Content & Data
- **`ContentFactory`**: Loads and manages game data
- Game configuration in `data_free/game_config/` as text files
- Data-driven design for creatures, items, spells, etc.

## Dependencies

- **SDL2**: Window management and input
- **SDL2_image**: Image loading
- **OpenGL**: Graphics rendering
- **OpenAL**: Audio
- **libvorbis/libogg**: Audio decoding
- **libtheora**: Video playback
- **CURL**: Network operations
- **zlib**: Compression
- **Steamworks SDK**: Steam integration (optional)

## Testing

- Minimal test infrastructure exists (`test.cpp`, `test.h`)
- Focus on manual testing and gameplay validation
- When adding features, ensure they don't break core gameplay mechanics

## Important Notes

### When Contributing Code:
1. Always include the GPL v2 license header in new files
2. Match the existing code style (use clang-format)
3. Keep the flat file structure in the root directory
4. Consider data-driven approaches for game content
5. Test builds with both Make and CMake if possible
6. Be mindful of platform differences (Linux, macOS, Windows)
7. Avoid breaking serialization compatibility when modifying data structures
8. Consider performance - this is a real-time game simulation

### Common Tasks:
- **Adding a new entity type**: Create factory methods, add to content definitions
- **Modifying game data**: Edit files in `data_free/game_config/`
- **UI changes**: Work with `View`, `WindowView`, `GuiElem`
- **AI behavior**: Modify `MonsterAI` and related controllers
- **New features**: Often require changes across multiple systems (model, view, data)

## Gotchas

- **stdafx.h**: Must be first include in .cpp files (precompiled header)
- **Serialization**: Many classes support save/load - be careful with member changes
- **Position system**: Creatures/items are tied to positions, not raw coordinates
- **Memory management**: Mix of raw pointers and unique_ptr - be consistent
- **Build artifacts**: `obj/` and `obj-opt/` directories for object files

## Resources

- Official website: http://keeperrl.com
- Steam page: http://store.steampowered.com/app/329970
- Itch.io: https://miki151.itch.io/keeperrl
- Compilation guide (Windows): http://keeperrl.com/compiling-keeperrl-on-windows/
