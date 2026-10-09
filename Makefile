LIBS = -lSDL2 -lSDL2_ttf -lSDL2_image -lSDL2_mixer -lm

ASSETS = assets/fonts/SatellaRegular-ZVVaz.ttf \
assets/images/ball-glow-yellow.png \
assets/images/paddle-glow-red.png \
assets/sounds/ping_pong_8bit_peeeeeep.ogg \
assets/sounds/ping_pong_8bit_beeep.ogg \
assets/sounds/ping_pong_8bit_plop.ogg \
assets/sounds/bg_music.mp3

# ln desired mp3 file to assets/sounds/bg_music.mp3

CFLAGS=-Wall -Wextra -O2

all: pong epong

pong: pong.c
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

epong: pong.c embed_assets.c
	$(CC) -DEMBED -o $@ pong.c $(CFLAGS) $(LIBS)

embed_assets.c: 
	@for f in $(ASSETS) ; do echo //xxd -i $$f ;\
		(cd $$(dirname $$f) ; xxd -i $$(basename $$f)); done >$@
clean:
	rm -f epong pong *.o embed_assets.c
