// pong circa 1972
// this is a translation of the Go pong example from https://sdl2.veandco/tutorials/go/
// with some additions (keeping ball square, rally timing and ball speed up, 
// random slam speed, stereo)
// 12/7/21-SP

// cc pong.c -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h> // for getpid
#include <string.h>
#include <time.h>
#include <math.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

//#define PROCINFO // if defined, process stats written to pid.$pid at end

#define WINWIDTH 1700
#define WINHEIGHT 800

const char *win_title = "Pong circa 1972";
int win_width = WINWIDTH;
int win_height = WINHEIGHT;
float aspect = WINWIDTH/WINHEIGHT; // used to keep ball square
const int FPS = 80;

const char *ballpaddle_soundpath = "assets/sounds/ping_pong_8bit_beeep.ogg";
const char *ballwall_soundpath   = "assets/sounds/ping_pong_8bit_plop.ogg";
const char *score_soundpath      = "assets/sounds/ping_pong_8bit_peeeeeep.ogg";
const char *fontpath             = "assets/fonts/SatellaRegular-ZVVaz.ttf";
const int fontsize = 64;
const char *paddle_glow_imgpath  = "assets/images/paddle-glow-red.png";
const char *ball_glow_imgpath    = "assets/images/ball-glow-yellow.png";

// SDL items
Uint32 ball_color, paddle_color, bg_color; // surface encoding
SDL_Color rally_color = {0, 128, 0}; // rendered color for font
SDL_Color score_color = {255, 255, 255};
Mix_Chunk *ballpaddle_sound, *ballwall_sound, *score_sound;
TTF_Font *rally_font, *score_font;
SDL_Surface *surface, *paddle_glow_surface, *ball_glow_surface;

struct Ball {
    SDL_FRect rect;
    SDL_FPoint velocity;
} ball;
const float ball_speed_start = 0.3; // 1.0 fastest reasonable speed
float ball_speed;

struct Paddle {
    SDL_FRect rect;
    SDL_FPoint velocity;
} paddle1, paddle2;
const float paddle_speed = 1.2;

struct Game {
    bool running;
    int score1, score2;
} game;

int rally = 0, rally_duration, rally_max;
long rally_start;

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
    if (side == LEFTSPKR)
        Mix_SetPanning(0, 255, 0);
    else if (side == RIGHTSPKR)
        Mix_SetPanning(0, 0, 255);
    else
        Mix_SetPanning(0, 255, 255);

    Mix_PlayChannel(-1, sound, 0);
}

// floating-point rect type (FRect) not well supported in SDL2
// following 2 functions should be in the SDL2 library
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
    float slam = randf() < 0.05? 2 : 1;
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
    ball.rect.h = aspect * 0.01;
    randomize_ball_velocity(randn(2));
    
    // new ball, obviously rally stops
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
    rect.h = ball.rect.h * win_height;
    SDL_FillRect(surface, &rect, ball_color);

    // glow outline
    rect.x = (ball.rect.x - 0.005) * win_width;
    rect.y = (ball.rect.y - aspect * 0.005) * win_height;
    rect.w = (ball.rect.w + 0.01) * win_width;
    rect.h = (ball.rect.h + aspect * 0.01) * win_height;
    SDL_BlitScaled(ball_glow_surface, NULL, surface, &rect);

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
    rect.x = p->rect.x * win_width; rect.y = p->rect.y * win_height;
    rect.w = p->rect.w * win_width; rect.h = p->rect.h * win_height;
    SDL_FillRect(surface, &rect, paddle_color);

    // glow outline
    rect.x = (p->rect.x - 0.005) * win_width;
    rect.y = (p->rect.y - 0.005) * win_height;
    rect.w = (p->rect.w + 0.01) * win_width;
    rect.h = (p->rect.h + 0.01) * win_height;
    SDL_BlitScaled(paddle_glow_surface, NULL, surface, &rect);
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
        if (ball_speed < 1.0)
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
    static bool wallhit = false; // when ball scoots along top/bottom boundary

    if (ball.rect.x < 0 || ball.rect.x + ball.rect.w >= 1 ||
        ball.rect.y < 0 || ball.rect.y + ball.rect.h >= 1) {
      
        // at either end
        if (ball.rect.y >= 0 && ball.rect.y + ball.rect.h < 1) {
            if (ball.rect.x > 0.5) 
                ++game.score1;
            else
                ++game.score2;
            play(score_sound, BOTHSPKR);
            new_ball();

        // at top or bottom
        } else if (ball.rect.x >= 0 && ball.rect.x + ball.rect.w < 1) {
            if (!wallhit) {
                ball.velocity.y = - ball.velocity.y;
                play(ballwall_sound, ball.rect.x < .5? LEFTSPKR : RIGHTSPKR);
                wallhit = true;
                if (randf() < 0.5) // vary ball speed on bounce
                    ball_speed += 0.02;
                else
                    ball_speed -= 0.02;
                return;
            }
        }
        wallhit = false;
    }
}

void
check_paddlewall_collision(struct Paddle *paddle)
{
    if (paddle->rect.y + paddle->rect.h > 1)
        paddle->rect.y = 1 - paddle->rect.h;
    else if (paddle->rect.y < 0)
        paddle->rect.y = 0;
}

void 
draw_scoreboard()
{
    SDL_Surface *s;
    SDL_Rect r;
    char str[10];

    if (rally > 1) {
        rally_duration = SDL_GetTicks() - rally_start;
        sprintf(str, "%d/%d", rally_duration/1000, rally_max/1000);
        s = TTF_RenderUTF8_Solid(rally_font, str, rally_color);
        r.x = win_width/2; r.y = win_height/10; r.w = win_width/5; r.h = win_height/10;
        SDL_BlitSurface(s, NULL, surface, &r);
        SDL_FreeSurface(s);
    }

    sprintf(str, "%d", game.score1);
    s = TTF_RenderUTF8_Solid(score_font, str, score_color);
    r.x = win_width/5; r.y = win_height/10; r.w = win_width/5; r.h = win_height/10;
    SDL_BlitSurface(s, NULL, surface, &r);
    SDL_FreeSurface(s);

    sprintf(str, "%d", game.score2);
    s = TTF_RenderUTF8_Solid(score_font, str, score_color);
    r.x = win_width*0.8; r.y = win_height*0.1; r.w = win_width*0.2; r.h = win_height*0.1;
    SDL_BlitSurface(s, NULL, surface, &r);
    SDL_FreeSurface(s);
}

void
draw_game()
{
    draw_paddle(&paddle1);
    draw_paddle(&paddle2);
    draw_ball();
    draw_scoreboard();
}

void
handle_input(SDL_Window *w)
{
    SDL_Event event;
    Uint32 startx, starty, stopx, stopy;

    while (SDL_PollEvent(&event)) {
#define SHOWEVENT
#ifdef SHOWEVENT // ln evinfo.o
        extern char *evinfo(SDL_Event *);
        puts(evinfo(&event));
#endif
        switch (event.type) {
        case SDL_QUIT:
            game.running = false;
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
            case SDLK_f:
                if (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) {
                    SDL_SetWindowFullscreen(w, 0);
                    SDL_SetWindowSize(w, win_width = WINWIDTH, win_height = WINHEIGHT);
                    SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                } else
                    SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN);
                //TODO does setwindowfullscreen invalidate old surface?
                surface = SDL_GetWindowSurface(w);
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
                //for mouse resizing?
                break;
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                win_width = event.window.data1;
                win_height = event.window.data2;
                aspect = (float)win_width/(float)win_height;
                printf("new w=%d h=%d aspect=%f\n",win_width,win_height,aspect);
                SDL_FillRect(surface, NULL, bg_color);
                break;
            case SDL_WINDOWEVENT_EXPOSED:
                //draw_game();
                break;
            case SDL_WINDOWEVENT_CLOSE:
                event.type = SDL_QUIT;
                SDL_PushEvent(&event);
                break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            SDL_GetMouseState(&startx, &starty);
            //draw_outline(w, x, y);
            break;
        case SDL_MOUSEBUTTONUP:
            //draw_outline(w, -1, -1); // erase outline
            break;
        case SDL_MOUSEMOTION:
            // draw_outline(w, x, y);
            break;
        default:
            break;
        }
    }
}

#if 0
// draw outline where x,y is lower-left corner
draw_outline(SDL_Window *w, int x, int y)
{
    static SDL_Renderer *r = NULL;

    if (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) {
        return; // no resizing in full screen

    if (r == NULL)  // init
        if ((r = SDL_CreateRenderer(w, -1, 0)) == NULL)
            puts(SDL_GetError()), return;

    SDL_SetRenderDrawColor(r, 255, 50, 50, SDL_ALPHA_OPAQUE);
    SDL_RenderDrawLines(r, points, 4);
    SDL_RenderPresent(r);
        
}
#endif

void 
new_game()
{
    srand((unsigned) SDL_GetPerformanceCounter()); // current time in nanoseconds
    game.score1 = game.score2 = 0;
    new_paddle(&paddle1, 0.1);
    new_paddle(&paddle2, 0.9-0.01);
    new_ball();
    game.running = true;
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
run_game(SDL_Window *window)
{
    int curr_time, prev_time = SDL_GetTicks(); // GetTicks will wrap if run > 49 days
    float delta_time;

    while (game.running) {
        curr_time = SDL_GetTicks();

        SDL_FillRect(surface, NULL, bg_color);
        handle_input(window);
        game_update(delta_time = ((float)curr_time - (float)prev_time) / 1000);
        draw_game(surface);
        SDL_UpdateWindowSurface(window);

        int delay_ms = (1000/FPS) - (SDL_GetTicks() - curr_time);
        if (delay_ms > 0)
            SDL_Delay(delay_ms);
        else if (delay_ms < 0) {
            printf("missed frame delay_ms %d (%dFPS = %dms)\n", delay_ms, FPS, 1000/FPS);
        }

        prev_time = curr_time;
    }

    printf("Final score %d/%d\n", game.score1, game.score2);
    if (rally_duration > rally_max)
        rally_max = rally_duration;
    if (rally_max > 0)
        printf("Best rally %d\n", rally_max/1000);
}

///////////
// main //
/////////

#define ERROR 1
#define OK 0

int
start()
{
    SDL_Init(SDL_INIT_EVERYTHING);
    TTF_Init();

    // images
    if ((paddle_glow_surface = IMG_Load(paddle_glow_imgpath)) == NULL)
        puts(SDL_GetError()); // not really fatal
    if ((ball_glow_surface = IMG_Load(ball_glow_imgpath)) == NULL)
        puts(SDL_GetError());

    // fonts
    if ((score_font = TTF_OpenFont(fontpath, fontsize)) == NULL)
        return ERROR;
    if ((rally_font = TTF_OpenFont(fontpath, fontsize/2)) == NULL)
        return ERROR;

    // sound
    Mix_Init(MIX_INIT_OGG);
    if (Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, 2/*stereo*/, 512) < 0)
        return ERROR;

    if ((ballpaddle_sound = Mix_LoadWAV(ballpaddle_soundpath)) == NULL)
        return ERROR;

    if ((ballwall_sound = Mix_LoadWAV(ballwall_soundpath)) == NULL)
        return ERROR;

    if ((score_sound = Mix_LoadWAV(score_soundpath)) == NULL)
        return ERROR;

    // game window
    SDL_Window *window;
    if ((window = SDL_CreateWindow(
            win_title,
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            win_width, win_height,
            SDL_WINDOW_SHOWN)) == NULL)
        return ERROR;

    // simple 2D--use old surface method
    surface = SDL_GetWindowSurface(window);

    // surface colors
    ball_color   = SDL_MapRGB(surface->format, 255, 255, 255);
    paddle_color = SDL_MapRGB(surface->format, 255, 255, 255);
    bg_color     = SDL_MapRGB(surface->format, 0, 0, 0);

    // play
    new_game();
    run_game(window);

#ifdef PROCINFO
    char p[40];
    sprintf(p, "cat /proc/%d/status >pid.%d", getpid(), getpid());
    system(p); // report proc stats, e.g. VmHWM for max. RAM used
#endif 

    SDL_FreeSurface(surface);
    SDL_DestroyWindow(window);
    Mix_Quit();
    return OK;
}

void
main()
{
    if (start() == ERROR) {
        printf("error: %s\n", SDL_GetError());
        exit(1);
    }
    exit(0);
}
