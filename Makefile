LIBS = -lSDL2 -lSDL2_ttf -lSDL2_image -lSDL2_mixer -lm

BG_MUSIC=assets/sounds/bg_music.mp3 # link

ASSETS = assets/fonts/SatellaRegular-ZVVaz.ttf \
assets/images/ball-glow-yellow.png \
assets/images/paddle-glow-red.png \
assets/sounds/ping_pong_8bit_peeeeeep.ogg \
assets/sounds/ping_pong_8bit_beeep.ogg \
assets/sounds/ping_pong_8bit_plop.ogg \
$(BG_MUSIC)

# ln desired mp3 file to assets/sounds/bg_music.mp3
# before running make otherwise default is used:
MUSIC=assets/sounds/AlexBeroza_-_Art_Now.mp3

CFLAGS=-Wall -Wextra -O2

all: pong epong $(BG_MUSIC)

pong: pong.c
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

epong: pong.c embed_assets.c
	$(CC) -DEMBED -o $@ pong.c $(CFLAGS) $(LIBS)

$(BG_MUSIC):
	ln $(MUSIC) $(BG_MUSIC)

embed_assets.c: 
	@for f in $(ASSETS) ; do echo //xxd -i $$f ;\
		(cd $$(dirname $$f) ; xxd -i $$(basename $$f)); done >$@
clean:
	rm -f epong pong *.o embed_assets.c
