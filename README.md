# Third-Person Character Controller

A smooth 3D character controller built with OpenGL, featuring skeletal animation blending, an orbit camera, and procedural visual effects.

## Idea

Run around an infinite grid using a fully animated 3D character. The project focuses on seamless animation state transitions (walking vs. running), one-shot action overrides (jumping, punching, kicking), and dynamic environment interactions like procedural footstep dust.

_[Insert a GIF/Video of your character running, kicking, and leaving dust trails here]_

## Controls

| Key / Input  | Action                            |
| ------------ | --------------------------------- |
| `W` / `S`    | Move forward / backward           |
| `A` / `D`    | Turn left / right                 |
| `Shift`      | Toggle Walk / Run mode            |
| `Space`      | Jump                              |
| `J`          | Punch                             |
| `K`          | Kick                              |
| `Mouse Drag` | Rotate orbit camera (Yaw / Pitch) |
| `Scroll`     | Zoom camera in / out              |
| `ESC`        | Quit                              |

## Technical stuff

**Rendering**

- OpenGL 3.3 Core Profile via GLFW + GLAD
- Multiple custom shaders: skeletal animation (`anim_model`), procedural infinite ground (`infinite_grid`), and particle effects (`dust`)
- Alpha blending enabled for particle fade-outs

**Models & Animation**

- Loaded with Assimp via standard `learnopengl` headers
- Character and skeletal animations sourced from Mixamo (using In-Place `.dae` files to prevent root-motion desync)
- Custom `AnimatorBlend` class handles smooth transitions between states (Idle, Walk, Run, Actions)
- Finite State Machine (FSM) logic pauses movement during one-shot actions (attacks/jumps) and gracefully returns to the previous movement state when finished

**Camera**

- Third-person orbit camera
- Uses spherical coordinates (Yaw, Pitch, Distance) to continuously calculate a smooth `glm::lookAt` matrix centered on the player's head/torso

**Visual Effects**

- **Infinite Ground:** Uses a single full-screen quad. The fragment shader unprojects clip-space coordinates to world-space to draw infinitely repeating grid lines with a distance-based fade, costing zero CPU updates.
- **Footstep Dust:** A custom lightweight particle system. Expanding ring meshes spawn at the character's feet. Spawn rates, expansion speed, and fade duration are dynamically tied to the `WALK` vs `RUN` animation cadence.

**Game loop**

- Standard `deltaTime` based movement
- Input debouncing for action triggers to prevent frame-spamming
- Window title updates dynamically to display the current FSM state and movement mode
