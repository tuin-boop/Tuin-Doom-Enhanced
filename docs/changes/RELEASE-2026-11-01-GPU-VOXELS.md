# GPU voxel performance update

Voxel meshes now remain in GPU buffers and are reused between frames and actors. Position, rotation and lighting are supplied separately, avoiding repeated CPU geometry transformation and uploads.

- Keeps the full voxel geometry, existing lighting and glowing-eye shading.
- Includes main-view, reflection, mist-depth and bloom-pass rendering.
- Reuses animation meshes with eviction of older unused cached meshes.
- Fixes the extra-hand appearance during the chainsaw starter animation.

The existing LAN multiplayer and other features remain included. Both multiplayer PCs should use this same build. VoxelDoom and commercial IWADs are not bundled.

Validation: all 29 automated tests passed; bounded GPU rendering checks and CPU/GPU visual comparisons completed. A test scene with 115 voxel instances reused 31 meshes with zero geometry bytes uploaded on unchanged frames. Actual performance gains depend on the scene and hardware.

Built on Duct-Tape DOOM; existing engine and mod credits are retained. Older releases remain available.