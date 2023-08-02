# game-engine-3d
Planimeter Game Engine 3D

## Dependencies
* [PhysicsFS 3.2.0](https://github.com/icculus/physfs/releases/tag/release-3.2.0)
* [SDL 2.26.4](https://github.com/libsdl-org/SDL/releases/tag/release-2.26.4)
* [GLM 0.9.9.8](https://github.com/g-truc/glm/releases/tag/0.9.9.8)

### Vulkan® 1.3.260
* [Vulkan SDK 1.3.239.0](https://vulkan.lunarg.com/sdk/home) ([Specification](https://registry.khronos.org/vulkan/specs/1.3-extensions/html/index.html))
* [volk 1.3.215](https://github.com/zeux/volk/releases/tag/1.3.215)
* [Vulkan Memory Allocator v3.0.1](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/releases/tag/v3.0.1)

## Build
```sh
cd game-engine-3d
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## License
GNU General Public License v2.0
