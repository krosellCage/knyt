# knyt

A 3D renderer and its platform layer, written from scratch in C-style C++ with
no engine, no framework, and no maths library. X11 and ALSA for the platform,
OpenGL 3.3 core for the rendering.

There is no GLFW, no GLAD, no GLM. The window, the input, the audio, the
extension loading, the matrix maths and the camera are all in this repository.

**[▶ Demo video](handmade/misc/cubes.mp4)** — lit cube, orbiting light, fly camera.

## Building

Linux, X11. You will need a compiler and the X11, GL and ALSA development
headers:

```sh
sudo apt install build-essential libx11-dev libgl1-mesa-dev libasound2-dev
```

Then:

```sh
./handmade/code/build.sh
cd build && ./x_knyt
```

The build produces two artifacts: `debug/libknyt.so`, which is the game, and
`x_knyt`, which is the platform layer that loads it.

## Controls

| | |
|---|---|
| `W` `A` `S` `D` | move |
| mouse | look |
| `Q` `E` | fly slower / faster |
| `F11` | fullscreen |
| `P` | pause |
| `L` | start/stop input recording, then loop playback |
| `Esc` | quit |

## What's in here

**Platform layer** — `x_knyt.cpp`, `x_opengl.cpp`

Window and input through Xlib, audio through ALSA, gamepad through
`linux/joystick.h`, and live code reloading through `dlfcn` — the game is a
shared object the platform swaps out while it runs. Raw pointer input for the
camera, and an input recording system that captures every frame to disk so a
sequence of play can be replayed deterministically.

**Renderer** — `handmade_opengl.h`, `win32`-free by design

OpenGL entry points are resolved at startup into a dispatch struct that the
game calls through, so the game layer never touches GLX or any platform header.
It is the same job GLAD does, written by hand.

**Shaders** — `handmade_shader.h`

Compiling, linking, and hot reloading. Shader files are watched by modification
time and rebuilt while the program runs; a shader that fails to compile logs the
driver's own error and leaves the last working version on screen rather than
taking the frame down.

**Maths** — `handmade_math.h`

Column-major `mat4`, `vec3`, `vec4`, operator overloads, perspective and
look-at construction, Rodrigues axis-angle rotation, dot/cross/normalize.

**Camera** — `handmade_camera.h`

A fly camera as a plain struct and free functions rather than a class, so it can
live directly in the game's persistent memory block and survive a code reload.

**Sound** — `handmade_sound.h`

A RIFF/WAVE parser that walks the chunk list, widens mono to stereo, and streams
into the platform's audio ring buffer with looping.

**Lighting**

Phong — ambient, diffuse and specular — computed in view space, with each light
type as its own GLSL struct and its own function. Six lights are summed per
fragment: one directional, four point lights with distance attenuation, and a
spotlight held at the camera with a soft-edged cone.

Because the maths runs in view space, the camera is the origin looking down −Z
by definition, so the flashlight needs no position or direction uniform at all.

**Model loading** — `handmade_obj.h`

A Wavefront OBJ and `.mtl` parser, replacing the book's use of Assimp. It walks
the file twice — once to count so the arena can allocate exactly, once to fill —
fans polygons of any size into triangles as it goes, and resolves OBJ's
1-based, sometimes-negative, three-lists-per-corner indexing into the single
index per vertex OpenGL requires.

Duplicate vertices are merged by bucketing on position index rather than
hashing, and the index buffer is grouped by material with a counting sort so
each material is one `glDrawElements` call. Materials come from a `.mtl`;
those with no texture bind a 1×1 white pixel, so one shader path covers both.


## Attribution

This is a learning project and it stands on other people's work.

- **[Handmade Hero](https://handmadehero.org)** by Casey Muratori. The platform
  layer's architecture follows his — the game-as-a-reloadable-library split, the
  `game_memory` / `game_input` interface, the memory arena, and the input
  recording system are all his design. His code is not freely distributable and
  none of it is reproduced here; the X11 implementation is written from scratch
  against a different set of APIs.

- **[LearnOpenGL](https://learnopengl.com)** by **Joey de Vries**
  ([@JoeyDeVriez](https://twitter.com/JoeyDeVriez)). The renderer follows this
  book, and the shaders and lighting maths here are derived from its code
  samples. Where the book uses GLFW, GLAD and GLM, this project uses its own
  equivalents, so the surrounding code differs throughout even where the
  concepts do not.

  Those code samples are © Joey de Vries and licensed
  [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/). The
  **NonCommercial** term travels with anything derived from them, which
  includes the GLSL in `handmade/data`. No LearnOpenGL textures are
  redistributed here — the renderer loads its own model and materials.

- **[xcb_handmade](https://github.com/nxsy/xcb_handmade)** by Neil Blakey-Milner
  and contributors, BSD licensed. A reference while writing the Linux platform
  layer. That project targets XCB and this one targets Xlib, so the windowing
  code differs, but the approach was a guide.

- **[stb_image](https://github.com/nothings/stb)** by Sean Barrett, public
  domain. Image decoding.

### Assets

`handmade/data` holds the shaders, `cube.obj` and `peugeot.mtl` — all written
here. The model itself is not mine to redistribute, so it is gitignored and has
to be fetched:

| file | where to get it |
|---|---|
| `peugeot.obj` | Peugeot Onyx Concept, free on [Sketchfab](https://sketchfab.com) / [Free3D](https://free3d.com). Rename the `.obj` to `peugeot.obj` |
| `Tyre.png`, `Car.Brake-Disk.BMP.png` | ship alongside that model |

`peugeot.mtl` is written by hand, because that model was distributed without
one. Its material names match the OBJ's `usemtl` lines; the colour values are
chosen to look right, not measured.

Any OBJ works — point `GameInitOpenGL` at a different file. Without one the
load fails, logs which file it could not read, and the scene renders empty
rather than crashing.

## License

Not a single licence — see [LICENSE](LICENSE). In short: the platform layer,
maths, camera, sound and shader tooling are MIT; the GLSL in `handmade/data` is
derived from LearnOpenGL's code samples and carries their CC BY-NC 4.0 terms,
so it is **not** free for commercial use.
