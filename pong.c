// Atari Inc. pong circa 1972
// an exercise using SDL2 based on Go code and assets from https://sdl2.veandco/tutorials/go/
// with changes:
// keep ball square, rally count and ball speed-up, random slam speed, 
// stereo, mute, pause, embed assets option, options, use renderer, code refactoring, etc.
// 12/7/21-SP

// usage: pong [options] [width height]

// keys:
// f - toggle fullscreen/window
// space - pause/unpause
// m - mute/unmute
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
float aspect = (float)(WINWIDTH)/(float)(WINHEIGHT); // used to keep ball square
int fps = 80;
bool mute = false;

// assets
#ifdef EMBED
#include "embed_assets.c" // generated file; see Makefile
#else
const char *ballpaddle_soundpath = "assets/sounds/ping_pong_8bit_beeep.ogg";
const char *ballwall_soundpath   = "assets/sounds/ping_pong_8bit_plop.ogg";
const char *score_soundpath      = "assets/sounds/ping_pong_8bit_peeeeeep.ogg";
const char *fontpath             = "assets/fonts/SatellaRegular-ZVVaz.ttf";
const char *paddle_glow_imgpath  = "assets/images/paddle-glow-red.png";
const char *ball_glow_imgpath    = "assets/images/ball-glow-yellow.png";
#endif
const int fontsize = 64;

// SDL items
SDL_Color rally_color = {0, 128, 0}; // rendered color for font
SDL_Color score_color = {255, 255, 255};
Mix_Chunk *ballpaddle_sound, *ballwall_sound, *score_sound;
TTF_Font *rally_font, *score_font;
SDL_Renderer *renderer;
SDL_Texture *paddle_glow_texture, *ball_glow_texture;

struct Ball {
    SDL_FRect rect;
    SDL_FPoint velocity;
} ball;
float ball_speed, ball_speed_start = 0.3; // 1.0 fastest reasonable speed

struct Paddle {
    SDL_FRect rect;
    SDL_FPoint velocity;
} paddle1, paddle2;
float paddle_speed = 1.1;

// game state:
bool running;
int score[2];
int rally = 0, rally_duration, rally_max;
long rally_start;
long pause_time = 0;

 ///////////////////////
// utility functions //
//////////////////////

// returns pseudo-random number in [0.0, 1.0)
float
randf()
{
    return rand() / (RAND_MAX + 1.0);
}

// returns pseudo-random number in [0, n)
int
randn(int n)
{
    return (int) (randf() * (float)n);
}

#define LEFTSPKR 1
#define RIGHTSPKR 2
#define BOTHSPKR 3

void
play(Mix_Chunk *sound, int side)
{
    if (mute)
        return;
    if (side == LEFTSPKR)
        Mix_SetPanning(0, 255, 0);
    else if (side == RIGHTSPKR)
        Mix_SetPanning(0, 0, 255);
    else
        Mix_SetPanning(0, 255, 255);

    Mix_PlayChannel(-1, sound, 0);
}

// floating-point rect type (FRect) not well supported in SDL2
// following 2 fns should be in the SDL2 library, these are from gosdl:
bool
SDL_FRectEmpty(SDL_FRect *a)
{
    return a == NULL || a->w <= 0 || a->h <= 0;
}

bool
SDL_IntersectRectF(SDL_FRect *a, SDL_FRect *b, SDL_FRect *res)
{
    if (a == NULL || b == NULL || res == NULL) 
        return false;

    if (SDL_FRectEmpty(a) || SDL_FRectEmpty(b)) {
        res->w = res->h = 0;
        return false;
    }

    float amin = a->x, amax = a->x + a->w;
    float bmin = b->x, bmax = b->x + b->w;
    if (bmin > amin)
        amin = bmin;
    res->x = amin;
    if (bmax < amax)
        amax = bmax;
    res->w = amax - amin;

    amin = a->y;
    amax = amin + a->h;
    bmin = b->y;
    bmax = bmin + b->h;
    if (bmin > amin)
        amin = bmin;
    res->y = amin;
    if (bmax < amax)
        amax = bmax;
    res->h = amax - amin;

    return !SDL_FRectEmpty(res);
}

 //////////
// ball //
/////////

#define BALLRIGHT 0
#define BALLLEFT 1

void
randomize_ball_velocity(int direction)
{
    float rnd_radian = (M_PI/2*randf() - M_PI/4) + M_PI * (float)direction;
    float slam = randf() < 0.05? 1.4 : 1;
    ball.velocity.x  = cosf(rnd_radian) * ball_speed * slam;
    ball.velocity.y  = sinf(rnd_radian) * ball_speed * slam;
}

void
new_ball()
{
    ball_speed = ball_speed_start;
    ball.rect.x = 0.5;
    ball.rect.y = 0.5;
    ball.rect.w = 0.01;
    ball.rect.h = 0.01;
    randomize_ball_velocity(randf() <= 0.5? BALLLEFT : BALLRIGHT);
    
    // new ball, rally stops
    if (rally_duration > rally_max)
        rally_max = rally_duration;
    rally = 0;
}

void
update_ball(float deltaTime)
{
    ball.rect.x += ball.velocity.x * deltaTime;
    ball.rect.y += ball.velocity.y * deltaTime;
}

void
draw_ball()
{
    // convert floating-point rect to integer rect
    SDL_Rect rect;
    rect.x = ball.rect.x * win_width;
    rect.y = ball.rect.y * win_height;
    rect.w = ball.rect.w * win_width;
    rect.h = ball.rect.h * win_height * aspect;
    SDL_RenderFillRect(renderer, &rect);

    // glow outline
    rect.x = (ball.rect.x - 0.005) * win_width;
    rect.y = (ball.rect.y - 0.005) * win_height;
    rect.w = (ball.rect.w + 0.01) * win_width;
    rect.h = (ball.rect.h * aspect + 0.01) * win_height;
    SDL_RenderCopy(renderer, ball_glow_texture, NULL, &rect);
}

 ////////////
// paddle //
///////////

void
new_paddle(struct Paddle *paddle, float xpos)
{
    paddle->rect.x = xpos;
    paddle->rect.y = 0.5 - (0.09 / 2); // vertical half-way
    paddle->rect.w = 0.01;
    paddle->rect.h = 0.09;
}

void
update_paddle(struct Paddle *paddle, float deltaTime)
{
    paddle->rect.y += paddle->velocity.y * deltaTime;
}

void
draw_paddle(struct Paddle *p)
{
    // convert floating-point rect to integer rect
    SDL_Rect rect;
    rect.x = p->rect.x * win_width; 
    rect.y = p->rect.y * win_height;
    rect.w = p->rect.w * win_width; 
    rect.h = p->rect.h * win_height;
    SDL_RenderFillRect(renderer, &rect);

    // glow outline
    rect.x = (p->rect.x - 0.005) * win_width;
    rect.y = (p->rect.y - 0.005) * win_height;
    rect.w = (p->rect.w + 0.01) * win_width;
    rect.h = (p->rect.h + 0.01) * win_height;
    if (SDL_RenderCopy(renderer, paddle_glow_texture, NULL, &rect) < 0)
        puts(SDL_GetError());
}
    
 //////////
// game //
/////////

// called on ballpaddle collision
void
rally_timer()
{
    if (rally == 0) {
        rally_start = SDL_GetTicks();
        rally = 1;
    } else {
        ++rally;
        ball_speed += ball_speed_start * .08; // also speed up the game
    }
}

void
check_ballpaddle_collision()
{
    static bool paddle1hit = false, paddle2hit = false;
    SDL_FRect res;

    if (SDL_IntersectRectF(&ball.rect, &paddle1.rect, &res)) {
        if (!paddle1hit) {
            randomize_ball_velocity(BALLRIGHT);
            paddle1hit = true;
            rally_timer();
            play(ballpaddle_sound, ball.rect.x < .5? LEFTSPKR : RIGHTSPKR);
        }
    } else
        paddle1hit=false;


    if (SDL_IntersectRectF(&ball.rect, &paddle2.rect, &res)) {
        if (!paddle2hit) {
            randomize_ball_velocity(BALLLEFT);
            paddle2hit = true;
            rally_timer();
            play(ballpaddle_sound, ball.rect.x < .5? LEFTSPKR : RIGHTSPKR);
        } 
    } else
        paddle2hit = false;

}

void
check_ballwall_collision()
{
    static bool scooting = false; // when ball scoots along side

    if (ball.rect.x < 0 || ball.rect.x+ball.rect.w > 1.0) { // hit an end
        ++score[ball.rect.x < 0.5? 1 : 0];
        play(score_sound, BOTHSPKR);
        new_ball();
    } else if (ball.rect.y < 0 || ball.rect.y+ball.rect.h > 1.0) { // hit a side
        if (!scooting) {
            ball.velocity.y = -ball.velocity.y;
            play(ballwall_sound, ball.rect.x < 0.5? LEFTSPKR : RIGHTSPKR);
            scooting = true;
            ball_speed += randf() < 0.5? 0.02 : -0.02;
        }
    } else
        scooting = false;
}

void
check_paddlewall_collision(struct Paddle *paddle)
{
    if (paddle->rect.y + paddle->rect.h > 1.0)
        paddle->rect.y = 1 - paddle->rect.h;
    else if (paddle->rect.y < 0)
        paddle->rect.y = 0;
}

// TODO cache rendered fonts rather than continually rerendering see https://github.com/grimfang4/SDL_FontCache
void 
draw_scoreboard()
{
    SDL_Rect r;
    SDL_Surface *s;
    SDL_Texture *t;
    char str[18];

    if (rally > 1) {
        rally_duration = SDL_GetTicks() - rally_start;
        sprintf(str, "%d/%d", rally_duration/1000, rally_max/1000);
        TTF_SizeUTF8(rally_font, str, &r.w, &r.h);
        r.x = win_width/2 - r.w/2; 
        r.y = win_height/10 + r.h/2; 
        s = TTF_RenderUTF8_Solid(rally_font, str, rally_color);
        t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_RenderCopy(renderer, t, NULL, &r);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    }

    sprintf(str, "%d", score[0]);
    TTF_SizeUTF8(score_font, str, &r.w, &r.h);
    r.x = paddle1.rect.x * win_width + win_width/10; // place relative to paddle
    r.y = win_height/10; 
    s = TTF_RenderUTF8_Solid(score_font, str, score_color);
    t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_RenderCopy(renderer, t, NULL, &r);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);

    sprintf(str, "%d", score[1]);
    TTF_SizeUTF8(score_font, str, &r.w, &r.h);
    r.x = paddle2.rect.x * win_width - r.w - win_width/10; // place relative to paddle
    r.y = win_height/10;
    s = TTF_RenderUTF8_Solid(score_font, str, score_color);
    t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_RenderCopy(renderer, t, NULL, &r);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

void
draw_game()
{
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    draw_paddle(&paddle1);
    draw_paddle(&paddle2);
    draw_ball();
    draw_scoreboard();
}

void
handle_input(SDL_Window *w)
{
    SDL_Event event;
    bool pausing = false;

    while (SDL_PollEvent(&event) || pausing) {
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
            case SDLK_w:
                paddle1.velocity.y = -paddle_speed;
                break;
            case SDLK_s:
                paddle1.velocity.y = paddle_speed;
                break;
            case SDLK_UP:
                paddle2.velocity.y = -paddle_speed;
                break;
            case SDLK_DOWN:
                paddle2.velocity.y = paddle_speed;
                break;
            case SDLK_m:
                mute = !mute;
                break;
            case SDLK_SPACE:
                if (pausing) {
                    pause_time = SDL_GetTicks() - pause_time;
                    pausing = false;
                } else {
                    pause_time = SDL_GetTicks();
                    pausing = true;
                }
                break;
            case SDLK_f:
                if (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN)
                    SDL_SetWindowFullscreen(w, 0);
                else
                    SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN_DESKTOP);
                break;
            case SDLK_ESCAPE:
                event.type = SDL_QUIT;
                SDL_PushEvent(&event);
                break;
            }
            break;
        case SDL_KEYUP:
            switch (event.key.keysym.sym) {
            case SDLK_w:
            case SDLK_s:
                paddle1.velocity.y = 0;
                break;
            case SDLK_UP:
            case SDLK_DOWN:
                paddle2.velocity.y = 0;
                break;
            }
            break;
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
                break;
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                win_width = event.window.data1;
                win_height = event.window.data2;
                aspect = (float)win_width/(float)win_height;
                break;
            case SDL_WINDOWEVENT_EXPOSED:
                break;
            case SDL_WINDOWEVENT_CLOSE:
                event.type = SDL_QUIT;
                SDL_PushEvent(&event);
                break;
            }
            break;
        }
    }
}

void 
new_game()
{
    srand((unsigned) SDL_GetPerformanceCounter()); // current time in nanoseconds
    score[0] = score[1] = 0;
    new_paddle(&paddle1, 0.1);
    new_paddle(&paddle2, 0.9-0.01);
    new_ball();
    running = true;
}

void game_update(float deltaTime)
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
    int start_time, prev_time = 0; // GetTicks will wrap if run > 49 days


    while (running) {
        pause_time = 0;
        start_time = SDL_GetTicks();

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        handle_input(w);
        start_time += pause_time;
        prev_time += pause_time;
        rally_start += pause_time;
        game_update((float)(start_time - prev_time) / 1000);
        draw_game();

        SDL_RenderPresent(renderer);

        int delay_ms = (1000/fps) - (SDL_GetTicks() - start_time);
        if (delay_ms > 0)
            SDL_Delay(delay_ms);
        else if (delay_ms < 0) 
            printf("missed frame delay_ms %d (%dFPS = %.1fms)\n", delay_ms, fps, (float)1000/fps);

        prev_time = start_time;
    }

    printf("Final score %d/%d\n", score[0], score[1]);
    if (rally_duration > rally_max)
        rally_max = rally_duration;
    if (rally_max > 0)
        printf("Best rally %d\n", rally_max/1000);
}

///////////
// main //
/////////

typedef enum { ERROR, OK } result;

result
start()
{
    SDL_Init(SDL_INIT_EVERYTHING);
    TTF_Init();

    // fonts
#ifdef EMBED
    SDL_RWops *buf = SDL_RWFromMem(SatellaRegular_ZVVaz_ttf,
            SatellaRegular_ZVVaz_ttf_len);
    if ((score_font = TTF_OpenFontRW(buf, 0, fontsize)) == NULL)
        return ERROR;

    SDL_RWseek(buf, 0, RW_SEEK_SET);
    if ((rally_font = TTF_OpenFontRW(buf, 1, fontsize/2)) == NULL)
        return ERROR;
#else
    if ((score_font = TTF_OpenFont(fontpath, fontsize)) == NULL)
        return ERROR;

    if ((rally_font = TTF_OpenFont(fontpath, fontsize/2)) == NULL)
        return ERROR;
#endif

    // sound
    Mix_Init(MIX_INIT_OGG);
    if (Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, 2/*stereo*/, 512) < 0)
        return ERROR;

#ifdef EMBED
    buf = SDL_RWFromMem(ping_pong_8bit_beeep_ogg, ping_pong_8bit_beeep_ogg_len);
    if ((ballpaddle_sound = Mix_LoadWAV_RW(buf, 1)) == NULL)
        return ERROR;

    buf = SDL_RWFromMem(ping_pong_8bit_plop_ogg, ping_pong_8bit_plop_ogg_len);

    if ((ballwall_sound = Mix_LoadWAV_RW(buf, 1)) == NULL)
        return ERROR;

    buf = SDL_RWFromMem(ping_pong_8bit_peeeeeep_ogg, ping_pong_8bit_peeeeeep_ogg_len);
    if ((score_sound = Mix_LoadWAV_RW(buf, 1)) == NULL)
        return ERROR;
#else
    if ((ballpaddle_sound = Mix_LoadWAV(ballpaddle_soundpath)) == NULL)
        return ERROR;

    if ((ballwall_sound = Mix_LoadWAV(ballwall_soundpath)) == NULL)
        return ERROR;

    if ((score_sound = Mix_LoadWAV(score_soundpath)) == NULL)
        return ERROR;
#endif

    // game window
    SDL_Window *window;
    if ((window = SDL_CreateWindow(win_title, 
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                win_width, win_height,
                SDL_WINDOW_RESIZABLE)) == NULL)
        return ERROR;

    if ((renderer = SDL_CreateRenderer(window, -1, 0)) == NULL)
        return ERROR;
  
    // images to textures
    SDL_Surface *paddle, *ball;
#ifdef EMBED
    buf = SDL_RWFromMem(paddle_glow_red_png, paddle_glow_red_png_len);
    if ((paddle = IMG_Load_RW(buf, 1)) == NULL)
        return ERROR;

    buf = SDL_RWFromMem(ball_glow_yellow_png, ball_glow_yellow_png_len);
    if ((ball = IMG_Load_RW(buf, 1)) == NULL)
        return ERROR;
#else
    if ((paddle = IMG_Load(paddle_glow_imgpath)) == NULL)
        puts(SDL_GetError()); // report but don't stop

    if ((ball = IMG_Load(ball_glow_imgpath)) == NULL)
        puts(SDL_GetError());
#endif
    paddle_glow_texture = SDL_CreateTextureFromSurface(renderer, paddle);
    ball_glow_texture = SDL_CreateTextureFromSurface(renderer, ball);
    SDL_FreeSurface(paddle);
    SDL_FreeSurface(ball);

    // play
    new_game();
    run_game(window);

#if defined(PROCINFO) && defined(unix)
    char p[40];
    sprintf(p, "cat /proc/%d/status >pid.%d", getpid(), getpid());
    system(p); // report proc stats, e.g. VmHWM for max. RAM used
#endif 

    SDL_DestroyTexture(paddle_glow_texture);
    SDL_DestroyTexture(ball_glow_texture);
    SDL_DestroyWindow(window);
    Mix_Quit();
    return OK;
}

void // no return on error
options(int ac, char *av[])
{
    char *help = "pong [ options ] [ win_width win_height ]\n\
 -bN ball speed (float)\n\
 -pN paddle speed (float)\n\
 -fN frames per second (integer)";
    int i;

    // run-time options
    for (i = 1; i < ac && av[i][0] == '-'; ++i) {
        switch (av[i][1]) {
        case 'b':
            if (isdigit(av[i][2]))
                ball_speed_start = atof(av[i]+2);
            else {
                puts("-b requires number (ball speed)");
                exit(1);
            }
            break;
        case 'f':
            if (isdigit(av[i][2]))
                fps = atoi(av[i]+2);
            else {
                puts("-f requires number (frames per second)");
                exit(1);
            }
            break;
        case 'p':
            if (isdigit(av[i][2]))
                paddle_speed = atof(av[i]+2);
            else {
                puts("-p requires number (paddle speed)");
                exit(1);
            }
            break;
        default:
            goto error;
        }
    }
    if (i < ac) {
        if (i+2 == ac) {
            win_width = atoi(av[i]);
            win_height = atoi(av[i+1]);
        } else 
            goto error;
    }

    return;

error:
    puts(help);
    exit(1);
}

int
main(int ac, char *av[])
{
    options(ac, av);

    printf("ball speed=%.1f\n", ball_speed_start);
    printf("paddle speed=%.1f\n", paddle_speed);
    printf("fps=%d (%.1fms)\n", fps, (float) 1000/fps);
    printf("win_width=%d, win_height=%d\n", win_width, win_height);

    if (start() == ERROR) {
        printf("error: %s\n", SDL_GetError());
        exit(1);
    }
    exit(0);
}
