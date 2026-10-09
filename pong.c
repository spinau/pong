// Atari Inc. pong circa 1972
// this implementation based on 2021 Go/SDL2 tutorial by veandco

// features: square ball constraint, rally acceleration, random slam speed,
// stereo panning, muting, pausing, asset embedding, and renderer optimizations,
// press 'g' during play for fps count

// usage: pong [options] [width height]

// keys:
// f - toggle fullscreen/window
// space - pause/unpause
// m - mute/unmute
// p - stop/start background music
// s/w - player 1 paddle
// ↑/↓ - player 2 paddle
// esc - exit

// compile time options:
//#define PROCINFO // if defined, process stats written to pid.$pid at end
//#define EMBED // if defined, assets are embedded (see Makefile for preprocessing steps)

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h> // getpid for PROCINFO
#include <string.h>
#include <time.h>
#include <math.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

// tournament ping pong table is 9:5; original pong was 858x525
#define WINWIDTH 900
#define WINHEIGHT 500

const char *win_title = "Pong circa 1972";
int win_width = WINWIDTH;
int win_height = WINHEIGHT;
float aspect = (float)WINWIDTH / (float)WINHEIGHT; // used to keep ball square
int target_fps = 80;
bool vsync = true; // -f opts out of vsync in favor of a manual fps cap
bool mute = false;
bool start_fullscreen = false;
unsigned bg_music_volume = 48; // 0-128 (MIX_MAX_VOLUME)

// assets
#ifdef EMBED
#include "embed_assets.c" // generated file; see Makefile
const char *bg_music_path        = "<embedded audio>";
#else
const char *ballpaddle_soundpath = "assets/sounds/ping_pong_8bit_beeep.ogg";
const char *ballwall_soundpath   = "assets/sounds/ping_pong_8bit_plop.ogg";
const char *score_soundpath      = "assets/sounds/ping_pong_8bit_peeeeeep.ogg";
const char *fontpath             = "assets/fonts/SatellaRegular-ZVVaz.ttf";
const char *paddle_glow_imgpath  = "assets/images/paddle-glow-red.png";
const char *ball_glow_imgpath    = "assets/images/ball-glow-yellow.png";
const char *bg_music_path        = "assets/sounds/bg_music.mp3";
#endif
const int fontsize = 64;

// SDL items
SDL_Color rally_color = {0, 128, 0, 255}; // rendered color for font
SDL_Color score_color = {255, 255, 255, 255};
Mix_Chunk *ballpaddle_sound = NULL, *ballwall_sound = NULL, *score_sound = NULL;
Mix_Music *bg_music = NULL;
TTF_Font *rally_font = NULL, *score_font = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *paddle_glow_texture = NULL, *ball_glow_texture = NULL;

// Textures for cached UI rendering
SDL_Texture *score1_texture = NULL, *score2_texture = NULL, *rally_texture = NULL;
int score1_w = 0, score1_h = 0, score2_w = 0, score2_h = 0, rally_w = 0, rally_h = 0;
int cached_score1 = -1, cached_score2 = -1, cached_rally_sec = -1;

// fps HUD (toggled with 'g')
bool show_fps = false;
double fps_smoothed = 0.0;
SDL_Texture *fps_texture = NULL;
int fps_w = 0, fps_h = 0, cached_fps_display = -1;

struct Ball {
    SDL_FRect rect;
    SDL_FPoint velocity;
} ball;
float ball_speed, ball_speed_start = 0.3f; // 1.0 fastest reasonable speed

struct Paddle {
    SDL_FRect rect;
    SDL_FPoint velocity;
} paddle1, paddle2;
float paddle_speed = 1.1f;

bool running;
bool paused = false;
int score[2];
int rally = 0, rally_duration = 0, rally_max = 0;
Uint64 rally_start_ticks = 0;
Uint64 pause_start_ticks = 0;

// returns pseudo-random number in [0.0, 1.0)
float randf(void) {
    return (float)rand() / ((float)RAND_MAX + 1.0f);
}

#define LEFTSPKR 1
#define RIGHTSPKR 2
#define BOTHSPKR 3

void
play(Mix_Chunk *sound, int side) {
    if (mute || !sound) return;
    if (side == LEFTSPKR)      Mix_SetPanning(0, 255, 0);
    else if (side == RIGHTSPKR) Mix_SetPanning(0, 0, 255);
    else                       Mix_SetPanning(0, 255, 255);

    Mix_PlayChannel(-1, sound, 0);
}

#define BALLRIGHT 0
#define BALLLEFT 1

void
randomize_ball_velocity(int direction)
{
    float rnd_radian = (M_PI_2 * randf() - M_PI_4) + M_PI * (float)direction;
    float slam = (randf() < 0.05f) ? 1.4f : 1.0f;
    ball.velocity.x = cosf(rnd_radian) * ball_speed * slam;
    ball.velocity.y = sinf(rnd_radian) * ball_speed * slam;
}

void
new_ball()
{
    ball_speed = ball_speed_start;
    ball.rect.x = 0.5f;
    ball.rect.y = 0.5f;
    ball.rect.w = 0.01f;
    ball.rect.h = 0.01f;
    randomize_ball_velocity(randf() <= 0.5f ? BALLLEFT : BALLRIGHT);
    
    // new ball, rally stops
    if (rally_duration > rally_max) rally_max = rally_duration;
    rally = 0;
    cached_rally_sec = -1;
}

void
update_ball(float deltaTime)
{
    ball.rect.x += ball.velocity.x * deltaTime;
    ball.rect.y += ball.velocity.y * deltaTime;
}

void
draw_ball(void)
{
    // sub-pixel precision: render directly from the FRect, no int truncation
    SDL_FRect rect = {
        ball.rect.x * win_width,
        ball.rect.y * win_height,
        ball.rect.w * win_width,
        ball.rect.h * win_height * aspect
    };
    SDL_RenderFillRectF(renderer, &rect);

    // glow outline
    if (ball_glow_texture) {
        rect.x = (ball.rect.x - 0.005f) * win_width;
        rect.y = (ball.rect.y - 0.005f) * win_height;
        rect.w = (ball.rect.w + 0.01f) * win_width;
        rect.h = (ball.rect.h * aspect + 0.01f) * win_height;
        SDL_RenderCopyF(renderer, ball_glow_texture, NULL, &rect);
    }
}

void
new_paddle(struct Paddle *paddle, float xpos)
{
    paddle->rect.x = xpos;
    paddle->rect.y = 0.5f - (0.09f / 2.0f); // vertical half-way
    paddle->rect.w = 0.01f;
    paddle->rect.h = 0.09f;
}

void
update_paddle(struct Paddle *paddle, float deltaTime)
{
    paddle->rect.y += paddle->velocity.y * deltaTime;
}

void
draw_paddle(struct Paddle *p)
{
    // sub-pixel precision: render directly from the FRect, no int truncation
    SDL_FRect rect = {
        p->rect.x * win_width,
        p->rect.y * win_height,
        p->rect.w * win_width,
        p->rect.h * win_height
    };
    SDL_RenderFillRectF(renderer, &rect);

    if (paddle_glow_texture) {
        rect.x = (p->rect.x - 0.005f) * win_width;
        rect.y = (p->rect.y - 0.005f) * win_height;
        rect.w = (p->rect.w + 0.01f) * win_width;
        rect.h = (p->rect.h + 0.01f) * win_height;
        SDL_RenderCopyF(renderer, paddle_glow_texture, NULL, &rect);
    }
}

void
rally_timer()
{
    if (rally == 0) {
        rally_start_ticks = SDL_GetTicks64();
        rally = 1;
    } else {
        ++rally;
        ball_speed += ball_speed_start * 0.08f; // also speed up the game
    }
}

void
check_ballpaddle_collision()
{
    static bool paddle1hit = false, paddle2hit = false;
    SDL_FRect res;

    if (SDL_IntersectFRect(&ball.rect, &paddle1.rect, &res)) {
        if (!paddle1hit) {
            randomize_ball_velocity(BALLRIGHT);
            paddle1hit = true;
            rally_timer();
            play(ballpaddle_sound, ball.rect.x < 0.5f ? LEFTSPKR : RIGHTSPKR);
        }
    } else {
        paddle1hit = false;
    }

    if (SDL_IntersectFRect(&ball.rect, &paddle2.rect, &res)) {
        if (!paddle2hit) {
            randomize_ball_velocity(BALLLEFT);
            paddle2hit = true;
            rally_timer();
            play(ballpaddle_sound, ball.rect.x < 0.5f ? LEFTSPKR : RIGHTSPKR);
        }
    } else {
        paddle2hit = false;
    }
}

void
check_ballwall_collision()
{
    static bool scooting = false; // when ball scoots along side

    if (ball.rect.x < 0.0f || ball.rect.x + ball.rect.w > 1.0f) { // hit an end
        ++score[ball.rect.x < 0.5f ? 1 : 0];
        play(score_sound, BOTHSPKR);
        new_ball();
    } else if (ball.rect.y < 0.0f || ball.rect.y + ball.rect.h > 1.0f) { // hit a side
        if (!scooting) {
            ball.velocity.y = -ball.velocity.y;
            play(ballwall_sound, ball.rect.x < 0.5f ? LEFTSPKR : RIGHTSPKR);
            scooting = true;
            ball_speed += (randf() < 0.5f) ? 0.02f : -0.02f;
        }
    } else {
        scooting = false;
    }
}

void
check_paddlewall_collision(struct Paddle *paddle)
{
    if (paddle->rect.y + paddle->rect.h > 1.0f)
        paddle->rect.y = 1.0f - paddle->rect.h;
    else if (paddle->rect.y < 0.0f)
        paddle->rect.y = 0.0f;
}

void
update_cached_text(SDL_Texture **texture, int *w, int *h, 
        TTF_Font *font, const char *text, 
        SDL_Color color)
{
    if (*texture)
        SDL_DestroyTexture(*texture);
    *texture = NULL;
    SDL_Surface *s = TTF_RenderUTF8_Solid(font, text, color);
    if (s) {
        *w = s->w;
        *h = s->h;
        *texture = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FreeSurface(s);
    }
}

void
draw_scoreboard()
{
    SDL_Rect r;
    char str[32];

    if (rally > 1 && !paused) {
        rally_duration = (int)(SDL_GetTicks64() - rally_start_ticks);
        int current_sec = rally_duration / 1000;
        if (current_sec != cached_rally_sec) {
            cached_rally_sec = current_sec;
            snprintf(str, sizeof(str), "%d/%d", current_sec, rally_max / 1000);
            update_cached_text(&rally_texture, &rally_w, &rally_h, rally_font, str, rally_color);
        }
        if (rally_texture) {
            r.w = rally_w; r.h = rally_h;
            r.x = win_width / 2 - r.w / 2;
            r.y = win_height / 10 + r.h / 2;
            SDL_RenderCopy(renderer, rally_texture, NULL, &r);
        }
    }

    if (score[0] != cached_score1) {
        cached_score1 = score[0];
        snprintf(str, sizeof(str), "%d", score[0]);
        update_cached_text(&score1_texture, &score1_w, &score1_h, score_font, str, score_color);
    }

    if (score1_texture) {
        r.w = score1_w; r.h = score1_h;
        r.x = (int)(paddle1.rect.x * win_width) + win_width / 10;
        r.y = win_height / 10;
        SDL_RenderCopy(renderer, score1_texture, NULL, &r);
    }

    if (score[1] != cached_score2) {
        cached_score2 = score[1];
        snprintf(str, sizeof(str), "%d", score[1]);
        update_cached_text(&score2_texture, &score2_w, &score2_h, score_font, str, score_color);
    }

    if (score2_texture) {
        r.w = score2_w; r.h = score2_h;
        r.x = (int)(paddle2.rect.x * win_width) - score2_w - win_width / 10;
        r.y = win_height / 10;
        SDL_RenderCopy(renderer, score2_texture, NULL, &r);
    }
}

void
draw_fps()
{
    if (!show_fps) return;

    int fps_int = (int)(fps_smoothed + 0.5);
    if (fps_int != cached_fps_display) {
        cached_fps_display = fps_int;
        char str[16];
        snprintf(str, sizeof(str), "%d fps", fps_int);
        update_cached_text(&fps_texture, &fps_w, &fps_h, rally_font, str, score_color);
    }
    if (fps_texture) {
        SDL_Rect r = { 10, 10, fps_w, fps_h };
        SDL_RenderCopy(renderer, fps_texture, NULL, &r);
    }
}

void
draw_game()
{
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    draw_paddle(&paddle1);
    draw_paddle(&paddle2);
    draw_ball();
    draw_scoreboard();
    draw_fps();
}

void
handle_input(SDL_Window *w)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
//#define SHOWEVENT
#ifdef SHOWEVENT // ld evname.o
        extern char *evname(SDL_Event *);
        puts(evname(&event));
#endif
        switch (event.type) {
        case SDL_QUIT:
            running = false;
            break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_w:      paddle1.velocity.y = -paddle_speed; break;
            case SDLK_s:      paddle1.velocity.y = paddle_speed;  break;
            case SDLK_UP:     paddle2.velocity.y = -paddle_speed; break;
            case SDLK_DOWN:   paddle2.velocity.y = paddle_speed;  break;
            case SDLK_m:
                mute = !mute;
                Mix_VolumeMusic(mute ? 0 : bg_music_volume);
                break;
            case SDLK_g:      show_fps = !show_fps; break;
            case SDLK_SPACE:  paused = !paused; break;
            case SDLK_p:
                if (Mix_PlayingMusic()) {
                    if (Mix_PausedMusic()) Mix_ResumeMusic();
                    else                   Mix_PauseMusic();
                } else if (bg_music) {
                    Mix_PlayMusic(bg_music, -1);
                }
                break;
            case SDLK_f:
                if (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN)
                    SDL_SetWindowFullscreen(w, 0);
                else
                    SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN_DESKTOP);
                break;
            case SDLK_ESCAPE:
                running = false;
                break;
            }
            break;
        case SDL_KEYUP:
            switch (event.key.keysym.sym) {
            case SDLK_w:
            case SDLK_s:    paddle1.velocity.y = 0; break;
            case SDLK_UP:
            case SDLK_DOWN: paddle2.velocity.y = 0; break;
            }
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                win_width = event.window.data1;
                win_height = event.window.data2;
                aspect = (float)win_width / (float)win_height;
            }
            break;
        }
    }
}

void
new_game()
{
    srand((unsigned)SDL_GetPerformanceCounter()); // current time in nanoseconds
    score[0] = score[1] = 0;
    cached_score1 = cached_score2 = -1;
    new_paddle(&paddle1, 0.1f);
    new_paddle(&paddle2, 0.9f - 0.01f);
    new_ball();
    running = true;
}

void
game_update(float deltaTime)
{
    update_paddle(&paddle1, deltaTime);
    update_paddle(&paddle2, deltaTime);
    update_ball(deltaTime);
    check_ballpaddle_collision();
    check_ballwall_collision();
    check_paddlewall_collision(&paddle1);
    check_paddlewall_collision(&paddle2);
}

void
run_game(SDL_Window *w)
{
    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 last_counter = SDL_GetPerformanceCounter();
    double target_frame_time = 1.0 / (double)target_fps;
    bool was_paused = false;

    while (running) {
        Uint64 current_counter = SDL_GetPerformanceCounter();
        double frame_time = (double)(current_counter - last_counter) / (double)perf_freq;
        last_counter = current_counter;

        if (frame_time > 0.0) { // smoothed actual fps, measured before the stall clamp below
            double instant_fps = 1.0 / frame_time;
            fps_smoothed = (fps_smoothed <= 0.0) ? instant_fps : fps_smoothed * 0.9 + instant_fps * 0.1;
        }
        if (frame_time > 0.05) frame_time = 0.05; // clamp stalls (resize/drag) so the ball can't tunnel

        handle_input(w);

        if (paused && !was_paused) {
            pause_start_ticks = SDL_GetTicks64();
        } else if (!paused && was_paused) {
            // shift the rally clock forward by the whole pause so it doesn't jump on resume
            rally_start_ticks += SDL_GetTicks64() - pause_start_ticks;
        }
        was_paused = paused;

        if (!paused) {
            game_update((float)frame_time);
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        draw_game();
        SDL_RenderPresent(renderer);

        if (!vsync) {
            // clamping frame rate using high-resolution counter
            Uint64 frame_end_counter = SDL_GetPerformanceCounter();
            double elapsed_time = (double)(frame_end_counter - current_counter) / (double)perf_freq;
            if (elapsed_time < target_frame_time) {
                Uint32 delay_ms = (Uint32)((target_frame_time - elapsed_time) * 1000.0);
                if (delay_ms > 0) SDL_Delay(delay_ms);
            }
        }
    }

    printf("Final score %d/%d\n", score[0], score[1]);
    if (rally_duration > rally_max)
        rally_max = rally_duration;
    if (rally_max > 0)
        printf("Best rally %d\n", rally_max / 1000);
}

typedef enum { ERROR, OK } result;

result
start()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return ERROR;
    if (TTF_Init() < 0) return ERROR;

#ifdef EMBED
    SDL_RWops *buf = SDL_RWFromMem(SatellaRegular_ZVVaz_ttf, SatellaRegular_ZVVaz_ttf_len);
    score_font = TTF_OpenFontRW(buf, 0, fontsize);
    SDL_RWseek(buf, 0, RW_SEEK_SET);
    rally_font = TTF_OpenFontRW(buf, 1, fontsize / 2);
#else
    score_font = TTF_OpenFont(fontpath, fontsize);
    rally_font = TTF_OpenFont(fontpath, fontsize / 2);
#endif
    if (!score_font || !rally_font) return ERROR;

    // sound
    Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3);
    if (Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, 2, 512) < 0) return ERROR;

#ifdef EMBED
    buf = SDL_RWFromMem(ping_pong_8bit_beeep_ogg, ping_pong_8bit_beeep_ogg_len);
    ballpaddle_sound = Mix_LoadWAV_RW(buf, 1);
    buf = SDL_RWFromMem(ping_pong_8bit_plop_ogg, ping_pong_8bit_plop_ogg_len);
    ballwall_sound = Mix_LoadWAV_RW(buf, 1);
    buf = SDL_RWFromMem(ping_pong_8bit_peeeeeep_ogg, ping_pong_8bit_peeeeeep_ogg_len);
    score_sound = Mix_LoadWAV_RW(buf, 1);
#else
    ballpaddle_sound = Mix_LoadWAV(ballpaddle_soundpath);
    ballwall_sound   = Mix_LoadWAV(ballwall_soundpath);
    score_sound      = Mix_LoadWAV(score_soundpath);
#endif

#ifdef EMBED
    buf = SDL_RWFromMem(bg_music_mp3, bg_music_mp3_len);
    bg_music = Mix_LoadMUS_RW(buf, 1);
#else
    bg_music = Mix_LoadMUS(bg_music_path);
#endif
    if (bg_music) {
        Mix_VolumeMusic(mute ? 0 : bg_music_volume);
        Mix_PlayMusic(bg_music, -1); // -1 loops indefinitely, seamlessly
        printf("bg music: \"%s\" by %s (%s) %s\n",
            Mix_GetMusicTitleTag(bg_music), Mix_GetMusicArtistTag(bg_music),
            Mix_GetMusicAlbumTag(bg_music), Mix_GetMusicCopyrightTag(bg_music));
    } else {
        printf("warning: could not load background music '%s': %s\n", bg_music_path, Mix_GetError());
    }

    // game window
    SDL_Window *window = SDL_CreateWindow(
        win_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_width, win_height,
        SDL_WINDOW_RESIZABLE | (start_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0)
    );
    if (!window) return ERROR;

    renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!renderer) return ERROR;

    SDL_Surface *paddle_s = NULL, *ball_s = NULL;
#ifdef EMBED
    buf = SDL_RWFromMem(paddle_glow_red_png, paddle_glow_red_png_len);
    paddle_s = IMG_Load_RW(buf, 1);
    buf = SDL_RWFromMem(ball_glow_yellow_png, ball_glow_yellow_png_len);
    ball_s = IMG_Load_RW(buf, 1);
#else
    paddle_s = IMG_Load(paddle_glow_imgpath);
    ball_s   = IMG_Load(ball_glow_imgpath);
#endif

    // play
    if (paddle_s) {
        paddle_glow_texture = SDL_CreateTextureFromSurface(renderer, paddle_s);
        SDL_FreeSurface(paddle_s);
    }
    if (ball_s) {
        ball_glow_texture = SDL_CreateTextureFromSurface(renderer, ball_s);
        SDL_FreeSurface(ball_s);
    }

    new_game();
    run_game(window);

    // cleanup resources
    if (score1_texture) SDL_DestroyTexture(score1_texture);
    if (score2_texture) SDL_DestroyTexture(score2_texture);
    if (rally_texture)  SDL_DestroyTexture(rally_texture);
    if (fps_texture)    SDL_DestroyTexture(fps_texture);
    if (paddle_glow_texture) SDL_DestroyTexture(paddle_glow_texture);
    if (ball_glow_texture)   SDL_DestroyTexture(ball_glow_texture);
    if (ballpaddle_sound) Mix_FreeChunk(ballpaddle_sound);
    if (ballwall_sound)   Mix_FreeChunk(ballwall_sound);
    if (score_sound)      Mix_FreeChunk(score_sound);
    if (bg_music)         Mix_FreeMusic(bg_music);
    if (score_font) TTF_CloseFont(score_font);
    if (rally_font) TTF_CloseFont(rally_font);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    Mix_CloseAudio();
    Mix_Quit();
    TTF_Quit();
    SDL_Quit();

    return OK;
}

void
options(int ac, char *av[])
{
    int opt;
    char *cmd = av[0];

    while ((opt = getopt(ac, av, "b:f:p:m:hv:F")) != -1) {
        switch (opt) {
        case 'b':
            ball_speed_start = atof(optarg);
            break;
        case 'f':
            target_fps = atoi(optarg);
            vsync = false; // turn off hardware pacing
            break;
        case 'p':
            paddle_speed = atof(optarg);
            break;
        case 'm':
            bg_music_path = optarg;
            break;
        case 'F':
            start_fullscreen = true;
            break;
        case 'v':
            bg_music_volume = atoi(optarg);
            if (bg_music_volume > 128) bg_music_volume = 48;
            break;
        case 'h':
        default:
            printf("%s [ options ] [ win_width win_height ] # atari pong clone\n", cmd);
            printf("-p  set initial paddle speed (-p 1.1 default)\n");
            printf("-b  set initial ball speed 0-1.0 (-b 0.3 default)\n");
            printf("-f  set fps and disable vsync (vsync on by default)\n");
            printf("-m  background music file (default: %s)\n", bg_music_path);
            printf("-v  set music volume 0-128 (default: %u)\n", bg_music_volume);
            printf("-F  start in full screen\n");
            exit(1);
        }
    }

    if (optind < ac) {
        win_width = atoi(av[optind++]);
        if (optind < ac)
            win_height = atoi(av[optind]);
    }
}

int
main(int ac, char *av[])
{
    options(ac, av);

    printf("ball speed=%.1f\npaddle speed=%.1f\n", ball_speed_start, paddle_speed);
    if (vsync)
        printf("fps=vsync\n");
    else
        printf("fps=%d\n", target_fps);

    if (start() == ERROR) {
        printf("error: %s\n", SDL_GetError());
        return 1;
    }

    return 0;
}
