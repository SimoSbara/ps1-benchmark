/*
 * PSn00bSDK basic graphics example
 * (C) 2020-2023 Lameguy64, spicyjpeg - MPL licensed
 *
 * A comprehensive "advanced hello world" example showing how to set up the
 * screen with double buffering, draw basic graphics (a bouncing square) and use
 * PSn00bSDK's debug font API to quickly print some text, all while following
 * best practices. This is not necessarily the simplest hello world example and
 * may look daunting at first glance, but it is a good starting point for more
 * complex programs.
 *
 * In order to avoid cluttering the program with global variables (as many Sony
 * SDK examples and other PSn00bSDK examples written before this one do) two
 * custom structures are employed:
 *
 * - a RenderBuffer structure containing the DISPENV and DRAWENV objects that
 *   represent the location of the framebuffer in VRAM, as well as the ordering
 *   table (OT) used to sort GPU commands/primitives by their Z index and the
 *   actual buffer commands will be written to;
 * - a RenderContext structure holding two RenderBuffer instances plus some
 *   variables to keep track of which buffer is currently being drawn and how
 *   much of its primitive buffer has been filled up so far.
 *
 * A C++ version of this example is also available (see examples/hellocpp).
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <stdlib.h>

#define NUM_RECTANGLES 100

// Length of the ordering table, i.e. the range Z coordinates can have, 0-15 in
// this case. Larger values will allow for more granularity with depth (useful
// when drawing a complex 3D scene) at the expense of RAM usage and performance.
#define OT_LENGTH NUM_RECTANGLES + 1

// Size of the buffer GPU commands and primitives are written to. If the program
// crashes due to too many primitives being drawn, increase this value.
#define BUFFER_LENGTH 8192

/* Framebuffer/display list class */

typedef struct {
	DISPENV disp_env;
	DRAWENV draw_env;

	uint32_t ot[OT_LENGTH];
	uint8_t  buffer[BUFFER_LENGTH];
} RenderBuffer;

typedef struct {
	RenderBuffer buffers[2];
	uint8_t      *next_packet;
	int          active_buffer;
} RenderContext;

//strutture benchmark vari

#define SCREEN_XRES 320
#define SCREEN_YRES 240

#define BASE_W	32
#define BASE_H	32

#define MENU_START_Y	SCREEN_YRES / 3
#define MENU_CHOICE_DY	16 //distanza tra una voce e l'altra in Y
#define MENU_X			SCREEN_XRES / 3

#define START_VEL		1

enum menuChoices
{
	STRESS_TEST = 0,
	MOV_TEST,
	AUDIO_TEST,
	BACK_CHOICE,
	NUM_CHOICES
};

TILE *tiles[NUM_RECTANGLES];

TILE *menuTile;

static int x[NUM_RECTANGLES];
static int y[NUM_RECTANGLES];

static int r[NUM_RECTANGLES];
static int g[NUM_RECTANGLES];
static int b[NUM_RECTANGLES];

static int dx[NUM_RECTANGLES];
static int dy[NUM_RECTANGLES];

static int w[NUM_RECTANGLES];
static int h[NUM_RECTANGLES];

static int curMode = STRESS_TEST;
static int curMenuChoice = 0;
static int isInMenu = 1;

static uint16_t lastButtons = 0xffff;

static int curVel = START_VEL;
static int vel[4] = {1, 3, 5, 7};

static char menuChoicesText[NUM_CHOICES][64] =
{
	{"STRESS TEST"},
	{"MOVEMENT TEST"},
	{"AUDIO TEST"},
	{"BACK"}
};

void setup_context(RenderContext *ctx, int w, int h, int r, int g, int b) {
	// Place the two framebuffers vertically in VRAM.
	SetDefDrawEnv(&(ctx->buffers[0].draw_env), 0, 0, w, h);
	SetDefDispEnv(&(ctx->buffers[0].disp_env), 0, 0, w, h);
	SetDefDrawEnv(&(ctx->buffers[1].draw_env), 0, h, w, h);
	SetDefDispEnv(&(ctx->buffers[1].disp_env), 0, h, w, h);

	// Set the default background color and enable auto-clearing.
	setRGB0(&(ctx->buffers[0].draw_env), r, g, b);
	setRGB0(&(ctx->buffers[1].draw_env), r, g, b);
	ctx->buffers[0].draw_env.isbg = 1;
	ctx->buffers[1].draw_env.isbg = 1;

	// Initialize the first buffer and clear its OT so that it can be used for
	// drawing.
	ctx->active_buffer = 0;
	ctx->next_packet   = ctx->buffers[0].buffer;
	ClearOTagR(ctx->buffers[0].ot, OT_LENGTH);

	// Turn on the video output.
	SetDispMask(1);
}

void flip_buffers(RenderContext *ctx) {
	// Wait for the GPU to finish drawing, then wait for vblank in order to
	// prevent screen tearing.
	DrawSync(0);
	VSync(0);

	RenderBuffer *draw_buffer = &(ctx->buffers[ctx->active_buffer]);
	RenderBuffer *disp_buffer = &(ctx->buffers[ctx->active_buffer ^ 1]);

	// Display the framebuffer the GPU has just finished drawing and start
	// rendering the display list that was filled up in the main loop.
	PutDispEnv(&(disp_buffer->disp_env));
	DrawOTagEnv(&(draw_buffer->ot[OT_LENGTH - 1]), &(draw_buffer->draw_env));

	// Switch over to the next buffer, clear it and reset the packet allocation
	// pointer.
	ctx->active_buffer ^= 1;
	ctx->next_packet    = disp_buffer->buffer;
	ClearOTagR(disp_buffer->ot, OT_LENGTH);
}

void *new_primitive(RenderContext *ctx, int z, size_t size) {
	// Place the primitive after all previously allocated primitives, then
	// insert it into the OT and bump the allocation pointer.
	RenderBuffer *buffer = &(ctx->buffers[ctx->active_buffer]);
	uint8_t      *prim   = ctx->next_packet;

	addPrim(&(buffer->ot[z]), prim);
	ctx->next_packet += size;

	// Make sure we haven't yet run out of space for future primitives.
	assert(ctx->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));

	return (void *) prim;
}

// A simple helper for drawing text using PSn00bSDK's debug font API. Note that
// FntSort() requires the debug font texture to be uploaded to VRAM beforehand
// by calling FntLoad().
void draw_text(RenderContext *ctx, int x, int y, int z, const char *text) {
	RenderBuffer *buffer = &(ctx->buffers[ctx->active_buffer]);

	ctx->next_packet = (uint8_t *)
		FntSort(&(buffer->ot[z]), ctx->next_packet, x, y, text);

	assert(ctx->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));
}

/* Main */


int update_position(int *x, int *y, int *dx, int *dy, int w, int h)
{
	if (*x < 0 || *x > (SCREEN_XRES - w))
		*dx = -*dx;
	if (*y < 0 || *y > (SCREEN_YRES - h))
		*dy = -*dy;

	*x += *dx;
	*y += *dy;
}

int draw_rectangle(RenderContext* ctx, TILE** tile, int x, int y, int z, int w, int h, int r, int g, int b)
{
	*tile = (TILE *) new_primitive(ctx, z, sizeof(TILE));

	setTile(*tile);
	setXY0 (*tile, x, y);
	setWH  (*tile, w, h);
	setRGB0(*tile, r, g, b);
}

void InitRectangle(int i)
{
	x[i] = rand() % (SCREEN_XRES - BASE_W); //tra 0 e 320 - la grandezza del quadrato
	y[i] = rand() % (SCREEN_YRES - BASE_H);

	r[i] = rand() % 256;
	g[i] = rand() % 256;
	b[i] = rand() % 256;

	dx[i] = rand() % 6 + 1;
	dy[i] = rand() % 6 + 1;

	w[i] = BASE_W;
	h[i] = BASE_H;
}

void InitStressTest()
{
	int i;

	for(i = 0; i < NUM_RECTANGLES; i++)
	{
		InitRectangle(i);
	}
}

void InitMovableTest()
{
	curVel = START_VEL;

	InitRectangle(0);
}

void DrawStressTest(RenderContext* ctx)
{
	int i;

	for(i = 0; i < NUM_RECTANGLES; i++)
	{
		update_position(&x[i], &y[i], &dx[i], &dy[i], w[i], h[i]); //aggiornare posizione di un quadrato alla volta
		draw_rectangle(ctx, &tiles[i], x[i], y[i], i + 1, w[i], h[i], r[i], g[i], b[i]);
	}
}

void DrawMovableTest(RenderContext* ctx)
{
	//utilizzo solo il primo
	draw_rectangle(ctx, &tiles[0], x[0], y[0], 1, w[0], h[0], r[0], g[0], b[0]);
}

void DrawMenu(RenderContext* ctx)
{
	int i;
	int y = MENU_START_Y;

	for(i = 0; i < NUM_CHOICES; i++)
	{
		if(curMenuChoice == i)
			draw_rectangle(ctx, &menuTile, MENU_X - BASE_W, y, 0, 8, 8, 255, 0, 0);

		draw_text(ctx, MENU_X, y, 0, menuChoicesText[i]);

		y += MENU_CHOICE_DY;
	}
}

void HandleMovableTestCommands(PADTYPE* pad)
{
	if (!(pad->btn & PAD_RIGHT))
	{
		int pos = x[0] + vel[curVel];

		if(pos > SCREEN_XRES - w[0])
			pos = SCREEN_XRES - w[0];

		x[0] = pos;
	} 
	else if(!(pad->btn & PAD_LEFT))
	{
		int pos = x[0] - vel[curVel];

		if(pos < 0)
			pos = 0;

		x[0] = pos;
	}

	if (!(pad->btn & PAD_UP))
	{
		int pos = y[0] - vel[curVel];

		if(pos < 0)
			pos = 0;
		
		y[0] = pos;
	} 
	else if(!(pad->btn & PAD_DOWN))
	{
		int pos = y[0] + vel[curVel];

		if(pos > SCREEN_YRES - h[0])
			pos = SCREEN_YRES - h[0];
		
		y[0] = pos;
	}

	//per evitare di switchare a manetta
	if((lastButtons & PAD_CROSS) && !(pad->btn & PAD_CROSS))			
	{
		r[0] = rand() % 256;
		g[0] = rand() % 256;
		b[0] = rand() % 256;
	}
	if((lastButtons & PAD_SQUARE) && !(pad->btn & PAD_SQUARE))			
	{
		int v = curVel + 1;

		if(v == 4)
			v = 0;

		curVel = v;
	}

	//rimpicciolimento
	if((lastButtons & PAD_L1) && !(pad->btn & PAD_L1))			
	{
		int curW = w[0] - BASE_W;
		int curH = h[0] - BASE_H;

		if(curW < BASE_W)
			curW = BASE_W;

		if(curH < BASE_H)
			curH = BASE_H;

		w[0] = curW;
		h[0] = curH;
	}
	//ingrandimento
	if((lastButtons & PAD_R1) && !(pad->btn & PAD_R1))			
	{
		int curW = w[0] + BASE_W;
		int curH = h[0] + BASE_H;

		if(curW > SCREEN_XRES - BASE_W)
			curW = SCREEN_XRES - BASE_W;

		if(curH > SCREEN_YRES - BASE_H)
			curH = SCREEN_YRES - BASE_H;

		w[0] = curW;
		h[0] = curH;
	}
}

void HandleStressTestCommands(PADTYPE* pad)
{
	if(!(pad->btn & PAD_SELECT))
	{
		InitStressTest();
	}
}

void OpenMenu()
{
	isInMenu = 1;
	curMenuChoice = 0;
}

void CloseMenu()
{
	isInMenu = 0;
}

void HandleMenuCommands(PADTYPE* pad)
{
	if((lastButtons & PAD_DOWN) && !(pad->btn & PAD_DOWN))
	{
		int choice = curMenuChoice + 1;

		if(choice == NUM_CHOICES)
			choice = 0;

		curMenuChoice = choice;
	}
	else if((lastButtons & PAD_UP) &&!(pad->btn & PAD_UP))
	{
		int choice = curMenuChoice - 1;

		if(choice < 0)
			choice = 0;

		curMenuChoice = choice;
	}
	else if(!(pad->btn & PAD_CROSS))
	{
		switch(curMenuChoice)
		{
			case STRESS_TEST:
				InitStressTest();
			break;
			case MOV_TEST:
				InitMovableTest();
			break;
			case AUDIO_TEST:
			break;
		}

		curMode = curMenuChoice;
		isInMenu = 0;
	}
}

void HandleCommands(PADTYPE* pad)
{
	if (!pad->stat)
	{
		if(!isInMenu)
		{
			switch(curMode)
			{
				case STRESS_TEST:
				HandleStressTestCommands(pad);
				break;

				case MOV_TEST:
				HandleMovableTestCommands(pad);
				break;

				case AUDIO_TEST:
				break;
			}

			if(!(pad->btn & PAD_START))
			{
				OpenMenu();
			}
		}
		else
		{
			HandleMenuCommands(pad);
		}

		lastButtons = pad->btn;
	}
}

void DrawCurrentMode(RenderContext *ctx)
{
	int i;

	if(isInMenu)
	{
		DrawMenu(ctx);

		return;
	}

	switch(curMode)
	{
		case STRESS_TEST:
		DrawStressTest(ctx);
		break;

		case MOV_TEST:
		DrawMovableTest(ctx);
		break;

		case AUDIO_TEST:
		break;
	}
}

int main(int argc, const char **argv) {
	// Initialize the GPU and load the default font texture provided by
	// PSn00bSDK at (960, 0) in VRAM.
	ResetGraph(0);
	FntLoad(960, 0);

	// Set up our rendering context.
	RenderContext ctx;
	setup_context(&ctx, SCREEN_XRES, SCREEN_YRES, 63, 0, 127);

	// Set up controller polling.
	uint8_t pad_buff[2][34];
	InitPAD(pad_buff[0], 34, pad_buff[1], 34);
	StartPAD();
	ChangeClearPAD(0);

	for (;;) 
	{
		PADTYPE *pad = (PADTYPE *) pad_buff[0];
	
		// if (
		// 	//(pad->type != PAD_ID_DIGITAL) &&
		// 	(pad->type != PAD_ID_ANALOG_STICK) &&
		// 	(pad->type != PAD_ID_ANALOG)
		// )
		// 	continue;

		DrawCurrentMode(&ctx);
		HandleCommands(pad);

		flip_buffers(&ctx);
	}

	return 0;
}
