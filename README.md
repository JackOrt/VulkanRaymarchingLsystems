# VulkanRaymarchingLsystems


**VulkanRaymarchingLsystems** is a real-time renderer for procedurally generated 3D tree structures, using **GPU raymarching** to visualize and animate L-systems. The project features smooth growth animations between tree states and is optimized for performance using Vulkan compute shaders.

---

## Features

- 🌿 **L-System-Based Trees**  
  Generate 3D plant structures from classic L-system rules.

- ✨ **Raymarching on the GPU**  
  Visualize tree geometry with high-fidelity shading using signed distance functions (SDFs) in compute shaders.

- 🎬 **Animated Growth Transitions**  
  Interpolate smoothly between fully grown and partially grown states.

---

## Directory Structure

```plaintext
├── CMakeLists.txt               # Build configuration
├── BFSSystem.cpp/.hpp          # Procedural branch generation logic
├── Camera.cpp/.hpp             # First-person camera controls
├── CommonHeader.hpp            # Shared includes and defines
├── FileUtils.cpp/.hpp          # Shader loading utility
├── shaders/
│   ├── raymarch_comp.glsl      # Raymarching compute shader
│   └── compile.ps1.txt         # Shader build script
├── src/
│   ├── main.cpp                # Application entry point
│   ├── VulkanRaymarchApp.cpp  # Vulkan setup and animation loop
│   └── VulkanRaymarchApp.hpp  # App class definition
```

---


## Requirements

- Vulkan SDK
- CMake 3.10+
- GLFW and GLM (via vcpkg or system)
- `glslc` for GLSL → SPIR-V shader compilation

---

## License

MIT License – Free to use, modify, and distribute.

---
