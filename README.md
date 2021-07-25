## pong

Reproduction of the Atari Inc. pong game, circa 1972.

Based on code from the Go tutorial at https://sdl2.veandco/tutorials/go.
Rewritten in C and using SDL2 rendering (as opposed to the older SDL1 surface blitting).

Not tested on anything other than GNU Linux. Make will make two versions: **pong** requires the assets/ directory,
**epong** has the assets embedded.

###### Usage: 

pong [ *options* ] [ *width height* ]

###### Options:

**-b***float* &emsp; ball speed (0.3 is nice, 1.0 is too fast)\
**-p***float* &emsp; paddle speed (1.0 or thereabouts is ok)\
**-f***int* &emsp; frames per second (see how far it can be pushed)

###### Keyboard when running:

**f** &emsp; toggle fullscreen/window\
**space** &emsp; pause/unpause\
**m** &emsp; mute/unmute\
**s**/**w** &emsp; player 1 paddle\
**↑**/**↓** &emsp; player 2 paddle\
**esc** &emsp; quit

###### Requires

https://www.libsdl.org/download-2.0.php  
https://www.libsdl.org/projects/SDL_ttf  
https://www.libsdl.org/projects/SDL_mixer  
https://www.libsdl.org/projects/SDL_image


