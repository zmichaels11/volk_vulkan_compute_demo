# Minimum Vulkan Compute Shader Demo
Demo executes a single compute shader and exits

Uses [Volk](https://github.com/zeux/volk) as a Vulkan Loader

# Dependencies
* [LunarG SDK](https://vulkan.lunarg.com/)

# How to compile shaders
``` bash
$ glslc -c src/main/glsl/square.comp -o square.comp.spv
```