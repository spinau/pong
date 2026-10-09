# pong

Reproduction of the Atari, Inc. *Pong* game from the 1970s, written in C with SDL2 rendering (as opposed to the older SDL1 surface blitting).

Based on code from the [Go SDL2 tutorial](https://sdl2.veandco/tutorials/go).

> Only tested on GNU/Linux.

## Build

```sh
make
```

This builds two binaries:

| Binary  | Description                                  |
|---------|-----------------------------------------------|
| `pong`  | Requires the `assets/` directory at runtime.  |
| `epong` | Assets embedded in the binary; no `assets/` needed. |

### Requirements

- [SDL2](https://www.libsdl.org/download-2.0.php)
- [SDL2_ttf](https://www.libsdl.org/projects/SDL_ttf)
- [SDL2_mixer](https://www.libsdl.org/projects/SDL_mixer)
- [SDL2_image](https://www.libsdl.org/projects/SDL_image)

## Usage

```sh
pong [options] [width height]
```

### Options

| Flag      | Argument | Description                                      | Default        |
|-----------|----------|---------------------------------------------------|----------------|
| `-b`      | *float*  | Ball speed                                        | `0.3`          |
| `-p`      | *float*  | Paddle speed                                      | `1.1`          |
| `-f`      | *int*    | Frames per second (disables vsync)                | vsync          |
| `-F`      |          | Start in full screen                              | windowed       |
| `-m`      | *file*   | MP3 or OGG file to play as background music       | `assets/sounds/bg_music.mp3` |
| `-v`      | *int*    | Background music volume (`0`-`128`)               | `48`           |
| `-h`      |          | Show usage and exit                               |                |

### Keyboard controls

| Key        | Action                       |
|------------|-------------------------------|
| `W` / `S`  | Player 1 paddle (up/down)     |
| `↑` / `↓`  | Player 2 paddle (up/down)     |
| `Space`    | Pause / unpause               |
| `F`        | Toggle fullscreen / windowed  |
| `M`        | Mute / unmute                 |
| `P`        | Pause / resume background music |
| `G`        | Show / hide FPS                |
| `Esc`      | Quit                          |
