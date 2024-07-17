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

#define NUM_RECTANGLES 1

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

#define SCREEN_XRES 320
#define SCREEN_YRES 240

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

	int vel[4] = {1, 3, 5, 7};

	int x[NUM_RECTANGLES];
	int y[NUM_RECTANGLES];

	int r[NUM_RECTANGLES];
	int g[NUM_RECTANGLES];
	int b[NUM_RECTANGLES];

	int dx[NUM_RECTANGLES];
	int dy[NUM_RECTANGLES];

	TILE *tiles[NUM_RECTANGLES];

	int baseW = 32;
	int baseH = 32;

	int w = baseW;
	int h = baseH;

	int i;
	uint16_t last_buttons = 0xffff;
	int curVel = 0;

	for(i = 0; i < NUM_RECTANGLES; i++)
	{
		x[i] = rand() % (SCREEN_XRES - w); //tra 0 e 320 - la grandezza del quadrato
		y[i] = rand() % (SCREEN_YRES - h);

		r[i] = rand() % 256;
		g[i] = rand() % 256;
		b[i] = rand() % 256;

		dx[i] = rand() % 6 + 1;
		dy[i] = rand() % 6 + 1;
	}

	for (;;) 
	{
		int controls[4] = {0, 0, 0, 0};
		char debug[64];

		for(i = 0; i < NUM_RECTANGLES; i++)
		{
			//update_position(&x[i], &y[i], &dx[i], &dy[i], w, h); //aggiornare posizione di un quadrato alla volta
			draw_rectangle(&ctx, &tiles[i], x[i], y[i], i + 1, w, h, r[i], g[i], b[i]);
		}



		PADTYPE *pad = (PADTYPE *) pad_buff[0];
	
		// if (
		// 	//(pad->type != PAD_ID_DIGITAL) &&
		// 	(pad->type != PAD_ID_ANALOG_STICK) &&
		// 	(pad->type != PAD_ID_ANALOG)
		// )
		// 	continue;
		if (!pad->stat)
		{
			if (!(pad->btn & PAD_RIGHT))
			{
				int pos = x[0] + vel[curVel];

				if(pos > SCREEN_XRES - w)
					pos = SCREEN_XRES - w;

				x[0] = pos;
				
				controls[0] = 1;
			} 
			else if(!(pad->btn & PAD_LEFT))
			{
				int pos = x[0] - vel[curVel];

				if(pos < 0)
					pos = 0;

				x[0] = pos;

				controls[1] = 1;
			}

			if (!(pad->btn & PAD_UP))
			{
				int pos = y[0] - vel[curVel];

				if(pos < 0)
					pos = 0;
				
				y[0] = pos;

				controls[2] = 1;
			} 
			else if(!(pad->btn & PAD_DOWN))
			{
				int pos = y[0] + vel[curVel];

				if(pos > SCREEN_YRES - h)
					pos = SCREEN_YRES - h;
				
				y[0] = pos;

				controls[3] = 1;
			}

			//per evitare di switchare a manetta
			if((last_buttons & PAD_CROSS) && !(pad->btn & PAD_CROSS))			
			{
				r[0] = rand() % 256;
				g[0] = rand() % 256;
				b[0] = rand() % 256;
			}
			if((last_buttons & PAD_SQUARE) && !(pad->btn & PAD_SQUARE))			
			{
				int v = curVel + 1;

				if(v == 4)
					v = 0;

				curVel = v;
			}

			//rimpicciolimento
			if((last_buttons & PAD_L1) && !(pad->btn & PAD_L1))			
			{
				int curW = w - baseW;
				int curH = h - baseH;

				if(curW < 32)
					curW = 32;

				if(curH < 32)
					curH = 32;

				w = curW;
				h = curH;
			}
			//ingrandimento
			if((last_buttons & PAD_R1) && !(pad->btn & PAD_R1))			
			{
				int curW = w + baseW;
				int curH = h + baseH;

				if(curW > SCREEN_XRES - 32)
					curW = SCREEN_XRES - 32;

				if(curH > SCREEN_YRES - 32)
					curH = SCREEN_YRES - 32;

				w = curW;
				h = curH;
			}

			last_buttons = pad->btn;
		}

		sprintf(debug, "VELOCITY: %d", vel[curVel]);

		draw_text(&ctx, 8, 16, 0, debug);

		sprintf(debug, "PAD R L U D: %d %d %d %d", 
						controls[0],
						controls[1],
						controls[2],
						controls[3]);

		draw_text(&ctx, 8, 32, 0, debug);

		//draw_text(&ctx, 8, 48, 0, "Benchmark");

		flip_buffers(&ctx);
	}

	return 0;
}
