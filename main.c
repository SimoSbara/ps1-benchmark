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
#include <stdbool.h>
#include <stdint.h>
#include <psxgpu.h>
#include <psxetc.h>
#include <psxapi.h>
#include <psxpad.h>
#include <psxgte.h>
#include <psxspu.h>
#include <stdlib.h>
#include <psxcd.h>
#include <hwregs_c.h>

#include "stream.h"

// Size of the ring buffer in main RAM in bytes.
// per audio
#define RAM_BUFFER_SIZE 0x18000

// Minimum number of sectors that will be read from the CD-ROM at once. Higher
// values will improve efficiency at the cost of requiring a larger buffer in
// order to prevent underruns and glitches in the audio output.
#define REFILL_THRESHOLD 24

// numero massimo figure
#define NUM_RECTANGLES 100

// numero massimo canzoni
#define MAX_SONGS 4

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

/* .VAG header structure */

typedef struct {
	uint32_t magic;			// 0x69474156 ("VAGi") for interleaved files
	uint32_t version;
	uint32_t interleave;	// Little-endian, size of each channel buffer
	uint32_t size;			// Big-endian, in bytes
	uint32_t sample_rate;	// Big-endian, in Hertz
	uint16_t _reserved[5];
	uint16_t channels;		// Little-endian, channel count (stereo if 0)
	char     name[16];
} VAG_Header;

typedef struct {
	int start_lba, stream_length, sample_rate;

	volatile int    next_sector;
	volatile size_t refill_length;
} StreamReadContext;

/* Helper functions */

#define DUMMY_BLOCK_ADDR   0x1000
#define STREAM_BUFFER_ADDR 0x1010

#define SCREEN_XRES 320
#define SCREEN_YRES 240

#define BASE_W	32
#define BASE_H	32

#define MENU_START_Y	SCREEN_YRES / 3
#define MENU_CHOICE_DY	16 //distanza tra una voce e l'altra in Y
#define MENU_X			SCREEN_XRES / 3

#define TRACK_LIST_START_Y	8
#define TRACK_LIST_DY	16 //distanza tra una voce e l'altra in Y

#define START_VEL		1
#define START_TRACK		0

enum menuChoices
{
	STRESS_TEST = 0,
	MOV_TEST,
	AUDIO_TEST,
	BACK_CHOICE,
	NUM_CHOICES
};

static Stream_Context    stream_ctx[MAX_SONGS];
static StreamReadContext read_ctx[MAX_SONGS];

static Stream_Context    masterStreamCtx;
static StreamReadContext masterReadCtx;

static int currentTrackIndex = START_TRACK;
static bool loadedTracks[MAX_SONGS];
static bool pausedTracks[MAX_SONGS];
static int 	sampleRate[MAX_SONGS];

static char names[MAX_SONGS][64] = 
{
	{"TRACK 1"},
	{"TRACK 2"},
	{"TRACK 3"},
	{"TRACK 4"}
};

extern const uint32_t tilesc[]; //riferimento tile con tutte le texture
TIM_IMAGE timImage;

//può essere TILE o può essere POLY_FT4
//POLY_FT4 può avere la texture invece TILE no essendo una figura semplice
//POLY_FT4 è un quadrilatero generico
void *tiles[NUM_RECTANGLES]; 

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
static int useTexture = 0;

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

// This isn't actually required for this example, however it is necessary if the
// stream buffers are going to be allocated into a region of SPU RAM that was
// previously used (to make sure the IRQ is not going to be triggered by any
// inactive channels).
void reset_spu_channels(void) {
	SpuSetKey(0, 0x00ffffff);

	for (int i = 0; i < 24; i++) {
		SPU_CH_ADDR(i) = getSPUAddr(DUMMY_BLOCK_ADDR);
		SPU_CH_FREQ(i) = 0x1000;
	}

	SpuSetKey(1, 0x00ffffff);
}

void cd_read_handler(CdlIntrResult event, uint8_t *payload) 
{
	// Mark the data that has just been read as valid.
	if (event != CdlDiskError)
		Stream_Feed(&stream_ctx[currentTrackIndex], read_ctx[currentTrackIndex].refill_length * 2048);
}

//utilizza double buffer
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

void *new_primitive(RenderContext *ctx, int z, size_t size) 
{
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
void draw_text(RenderContext *ctx, int x, int y, int z, const char *text) 
{
	RenderBuffer *buffer = &(ctx->buffers[ctx->active_buffer]);

	ctx->next_packet = (uint8_t *)
		FntSort(&(buffer->ot[z]), ctx->next_packet, x, y, text);

	assert(ctx->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));
}

//incremento in verticale
void drawTextList(RenderContext *ctx, int x, int *y, int z, int dy, const char *text)
{
	draw_text(ctx, x, *y, z, text);

	*y += dy;
}

void drawImmediateText(RenderContext *ctx, int x, int y, int z, const char *text)
{
	draw_text(ctx, x, y, z, text);

	flip_buffers(ctx);
}

/* Main */

void setup_stream(const CdlLOC *pos, StreamReadContext* cur_read_ctx, Stream_Context* cur_stream_ctx);
bool feed_stream(StreamReadContext* cur_read_ctx, Stream_Context* cur_stream_ctx);

void setup_stream(const CdlLOC *pos, StreamReadContext* cur_read_ctx, Stream_Context* cur_stream_ctx) 
{
	// Read the .VAG header from the first sector of the file.
	uint32_t header[512];
	CdControl(CdlSetloc, pos, 0);

	CdReadCallback(0);
	CdRead(1, header, CdlModeSpeed);
	CdReadSync(0, 0);

	VAG_Header    *vag = (VAG_Header *) header;
	Stream_Config config;

	int num_channels = vag->channels ? vag->channels : 2;
	int num_chunks   =
		(__builtin_bswap32(vag->size) + vag->interleave - 1) / vag->interleave;

	__builtin_memset(&config, 0, sizeof(Stream_Config));

	config.spu_address = STREAM_BUFFER_ADDR;
	config.interleave  = vag->interleave;
	config.buffer_size = RAM_BUFFER_SIZE;
	config.sample_rate = __builtin_bswap32(vag->sample_rate);

	// Use the first N channels of the SPU and pan them left/right in pairs
	// (this assumes the stream contains one or more stereo tracks).
	for (int ch = 0; ch < num_channels; ch++) {
		config.channel_mask = (config.channel_mask << 1) | 1;

		SPU_CH_VOL_L(ch) = (ch % 2) ? 0x0000 : 0x3fff;
		SPU_CH_VOL_R(ch) = (ch % 2) ? 0x3fff : 0x0000;
	}

	Stream_Init(cur_stream_ctx, &config);

	cur_read_ctx->start_lba     = CdPosToInt(pos) + 1;
	cur_read_ctx->stream_length =
		(num_channels * num_chunks * vag->interleave + 2047) / 2048;
	cur_read_ctx->sample_rate   = config.sample_rate;
	cur_read_ctx->next_sector   = 0;
	cur_read_ctx->refill_length = 0;

	// Ensure the buffer is full before starting playback.
	while (feed_stream(&masterReadCtx, &masterStreamCtx))
		__asm__ volatile("");
}

bool feed_stream(StreamReadContext* cur_read_ctx, Stream_Context* cur_stream_ctx) 
{
	// Do nothing if the drive is already busy reading a chunk.
	if (CdReadSync(1, 0) > 0)
		return true;

	// To improve efficiency, do not start refilling immediately but wait until
	// there is enough space in the buffer (see REFILL_THRESHOLD).
	if (Stream_GetRefillLength(cur_stream_ctx) < (REFILL_THRESHOLD * 2048))
		return false;

	uint8_t *ptr;
	size_t  refill_length = Stream_GetFeedPtr(cur_stream_ctx, &ptr) / 2048;

	// Figure out how much data can be read in one shot. If the end of the file
	// would be reached before the buffer is full, split the read into two
	// separate reads.
	int next_sector = cur_read_ctx->next_sector;
	int max_length  = cur_read_ctx->stream_length - next_sector;

	while (max_length <= 0) {
		next_sector -= cur_read_ctx->stream_length;
		max_length  += cur_read_ctx->stream_length;
	}

	if (refill_length > max_length)
		refill_length = max_length;

	// Start reading the next chunk from the CD-ROM into the buffer.
	CdlLOC pos;

	CdIntToPos(cur_read_ctx->start_lba + next_sector, &pos);
	CdControl(CdlSetloc, &pos, 0);
	CdReadCallback(&cd_read_handler);
	CdRead(refill_length, (uint32_t *) ptr, CdlModeSpeed);

	cur_read_ctx->next_sector   = next_sector + refill_length;
	cur_read_ctx->refill_length = refill_length;

	return true;
}

void update_position(int *x, int *y, int *dx, int *dy, int w, int h)
{
	if (*x < 0 || *x > (SCREEN_XRES - w))
		*dx = -*dx;
	if (*y < 0 || *y > (SCREEN_YRES - h))
		*dy = -*dy;

	*x += *dx;
	*y += *dy;
}

void draw_rectangle(RenderContext* ctx, void** prim, int texture, int ux, int uy, int x, int y, int z, int w, int h, int r, int g, int b)
{
	if(!texture)
	{
		TILE* tile = new_primitive(ctx, z, sizeof(TILE));

		setTile(tile);
		setXY0 (tile, x, y);
		setWH  (tile, w, h);
		setRGB0(tile, r, g, b);

		*prim = tile;
	}
	else
	{
		POLY_FT4* poly = (POLY_FT4*)new_primitive(ctx, z, sizeof(POLY_FT4));

		setPolyFT4(poly);

		//setXY4 ordine dei vertici
		//1---2
		//|   |
		//|   |
		//3---4

		setXY4(poly, x, y, 
					x + w, y,
					x, y + h,
					x + w, y + h);
		setRGB0(poly, r, g, b);
		poly->tpage = getTPage(timImage.mode, 0, timImage.prect->x, timImage.prect->y);

		// Set CLUT
		setClut(poly, timImage.crect->x, timImage.crect->y);
		
		// Set texture coordinates
		setUVWH(poly, ux, uy, 32, 32);

		*prim = poly;
	}
}

void InitRandomRectangle(int i)
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
		InitRandomRectangle(i);
	}
}

void InitMovableTest()
{
	curVel = START_VEL;

	InitRandomRectangle(0);
}

void InitAudioTest()
{
	int i;

	currentTrackIndex = START_TRACK;

	for(i = 0; i < MAX_SONGS; i++)
	{
		//resetto posizione
		read_ctx[i].next_sector = 0;
		pausedTracks[i] = 0;
	}

	if(loadedTracks[currentTrackIndex])
		Stream_Start(&stream_ctx[currentTrackIndex], true);
}

void EndAudioTest()
{
	if(loadedTracks[currentTrackIndex] && !pausedTracks[currentTrackIndex])
		Stream_Stop();
}

void PauseAudioTest()
{
	EndAudioTest();
}

void ResumeAudioTest()
{
	if(loadedTracks[currentTrackIndex] && !pausedTracks[currentTrackIndex])
		Stream_Start(&stream_ctx[currentTrackIndex], true);
}

void DrawStressTest(RenderContext* ctx)
{
	int i;

	for(i = 0; i < NUM_RECTANGLES; i++)
	{
		update_position(&x[i], &y[i], &dx[i], &dy[i], w[i], h[i]); //aggiornare posizione di un quadrato alla volta
		draw_rectangle(ctx, &tiles[i], useTexture, 32, 0, x[i], y[i], i + 1, w[i], h[i], r[i], g[i], b[i]);
	}
}

void DrawMovableTest(RenderContext* ctx)
{
	//utilizzo solo il primo
	draw_rectangle(ctx, &tiles[0], useTexture, 32, 0, x[0], y[0], 1, w[0], h[0], r[0], g[0], b[0]);
}

void DrawAudioTest(RenderContext* ctx)
{
	char buffer[128];
	int xPos = 8;
	int yPos = TRACK_LIST_START_Y;

	int sample_rate = sampleRate[currentTrackIndex];

	bool buffering = feed_stream(&read_ctx[currentTrackIndex], &stream_ctx[currentTrackIndex]);

	if(!loadedTracks[currentTrackIndex])
	{
		sprintf(buffer, "TRACK %d NOT LOADED", currentTrackIndex + 1);
		drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, buffer);
		return;
	}

	sprintf(buffer, "PLAYING %s",  names[currentTrackIndex]);
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, buffer);

	sprintf(buffer,  "CD STATUS: %s", buffering ? "READING" : "IDLE");
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, buffer);

	sprintf(buffer,  "BUFFER USAGE: %d/%d", stream_ctx[currentTrackIndex].buffer.length, stream_ctx[currentTrackIndex].config.buffer_size);
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, buffer);

	sprintf(buffer,  "POSITION SECTOR: %d/%d", read_ctx[currentTrackIndex].next_sector, read_ctx[currentTrackIndex].stream_length);
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, buffer);

	sprintf(buffer,  "SAMPLE RATE: %5d HZ", sample_rate);
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY * 2, buffer);

	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, "COMMANDS:");
	
	sprintf(buffer,  "[SELECT]			%s", pausedTracks[currentTrackIndex] ? "RESUME" : "PAUSE");
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, buffer);
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, "[LEFT/RIGHT] SEEK");
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, "[O]          RESET POSITION");
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, "[UP/DOWN]    CHANGE SAMPLE RATE");
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, "[X]          RESET SAMPLE RATE");
	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY * 2, "[TRIANGLE]   CHANGE TRACK");

	drawTextList(ctx, xPos, &yPos, 0, TRACK_LIST_DY, "PAUSE AND RESUME IF IT DOESN'T START!");
}

void DrawMenu(RenderContext* ctx)
{
	int i;
	int y = MENU_START_Y;

	for(i = 0; i < NUM_CHOICES; i++)
	{
		if(curMenuChoice == i)
			draw_rectangle(ctx, &menuTile, 1, 0, 0, MENU_X - 14, y, 0, 8, 8, 255, 0, 0);

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

	HandleTextureCommand(pad);
}

void HandleStressTestCommands(PADTYPE* pad)
{
	if((lastButtons & PAD_SELECT) && !(pad->btn & PAD_SELECT))
	{
		InitStressTest();
	}

	HandleTextureCommand(pad);
}

void HandleAudioTestCommands(PADTYPE* pad)
{
	int sectors_per_chunk = (stream_ctx[currentTrackIndex].chunk_size + 2047) / 2048;

	if ((lastButtons & PAD_SELECT) && !(pad->btn & PAD_SELECT)) 
	{
		pausedTracks[currentTrackIndex] ^= 1;
		if (pausedTracks[currentTrackIndex])
			Stream_Stop();
		else
			Stream_Start(&stream_ctx[currentTrackIndex], true);
	}

	// Note that seeking will only work correctly with .VAG files whose
	// interleave (chunk size) is a multiple of 2048.
	if (!(pad->btn & PAD_LEFT) && (read_ctx[currentTrackIndex].next_sector > 0))
		read_ctx[currentTrackIndex].next_sector -= sectors_per_chunk;
	if (!(pad->btn & PAD_RIGHT))
		read_ctx[currentTrackIndex].next_sector += sectors_per_chunk;
	if ((lastButtons & PAD_CIRCLE) && !(pad->btn & PAD_CIRCLE))
		read_ctx[currentTrackIndex].next_sector = 0;

	if (!(pad->btn & PAD_DOWN) && (sampleRate[currentTrackIndex] > 11000)) 
	{
		sampleRate[currentTrackIndex] -= 100;
		Stream_SetSampleRate(&stream_ctx[currentTrackIndex], sampleRate[currentTrackIndex]);
	}
	if (!(pad->btn & PAD_UP) && (sampleRate[currentTrackIndex] < 88200)) 
	{
		sampleRate[currentTrackIndex] += 100;
		Stream_SetSampleRate(&stream_ctx[currentTrackIndex], sampleRate[currentTrackIndex]);
	}
	if ((lastButtons & PAD_CROSS) && !(pad->btn & PAD_CROSS)) 
	{
		sampleRate[currentTrackIndex] = read_ctx[currentTrackIndex].sample_rate;
		Stream_SetSampleRate(&stream_ctx[currentTrackIndex], sampleRate[currentTrackIndex]);
	}
	if ((lastButtons & PAD_TRIANGLE) && !(pad->btn & PAD_TRIANGLE)) 
	{
		Stream_Stop();
		currentTrackIndex++;

		if(currentTrackIndex >= MAX_SONGS)
			currentTrackIndex = 0;
		
		if(loadedTracks[currentTrackIndex] && !pausedTracks[currentTrackIndex])
			Stream_Start(&stream_ctx[currentTrackIndex], true);
	}
}

void OpenMenu()
{
	isInMenu = 1;
	curMenuChoice = 0;

	PauseAudioTest();
}

void CloseMenu()
{
	isInMenu = 0;
}

void EndCurrentMode()
{
	switch(curMode)
	{
		case AUDIO_TEST:
			EndAudioTest();
		break;
	}
}

void ResumeCurrentMode()
{
	switch(curMode)
	{
		case AUDIO_TEST:
			ResumeAudioTest();
		break;
	}
}

void PauseCurrentMode()
{
	switch(curMode)
	{
		case AUDIO_TEST:
			PauseAudioTest();
		break;
	}
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
			choice = NUM_CHOICES - 1;

		curMenuChoice = choice;
	}
	else if(!(pad->btn & PAD_CROSS))
	{
		CloseMenu();

		switch(curMenuChoice)
		{
			case STRESS_TEST:
				EndCurrentMode();
				InitStressTest();
			break;
			case MOV_TEST:
				EndCurrentMode();
				InitMovableTest();
			break;
			case AUDIO_TEST:
				EndCurrentMode();
				InitAudioTest();
			break;
			case BACK_CHOICE:
				ResumeCurrentMode();
			return;
		}

		curMode = curMenuChoice;
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
				HandleAudioTestCommands(pad);
				break;
			}

			if(!(pad->btn & PAD_START))
			{
				PauseCurrentMode();
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
		DrawAudioTest(ctx);
		break;
	}
}

void HandleTextureCommand(PADTYPE* pad)
{
	if((lastButtons & PAD_TRIANGLE) && !(pad->btn & PAD_TRIANGLE))
	{
		useTexture = !useTexture;
	}
}

void LoadTextures()
{
	GetTimInfo( tilesc, &timImage ); /* Get TIM parameters */

	LoadImage( timImage.prect, timImage.paddr );		/* Upload texture to VRAM */
	if( timImage.mode & 0x8 ) 
	{
		LoadImage( timImage.crect, timImage.caddr );	/* Upload CLUT if present */
	}
}

void LoadAudioTracks(RenderContext *ctx)
{
	int i;

	SpuInit();
	reset_spu_channels();

	for(i = 0; i < MAX_SONGS; i++)
	{
		CdlFILE file;
		char filename[32];
		char debug[128];

		sprintf(filename, "\\TRACK-%d.VAG", i + 1);

		bool loaded = CdSearchFile(&file, filename);

		sprintf(debug, "Loading TRACK-%d.VAG: %s", i + 1, loaded ? "SUCCESS" : "FAILED");

		loadedTracks[i] = loaded;
		pausedTracks[i] = false;

		drawImmediateText(ctx, 8, MENU_START_Y, 0, debug);

		if(loaded)
		{
			setup_stream(&file.pos, &read_ctx[i], &stream_ctx[i]);
			sampleRate[i] = read_ctx[i].sample_rate;
		}
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

	//inizializzo CD stream
	CdInit();

	LoadTextures();
	LoadAudioTracks(&ctx);

	// Set up controller polling.
	uint8_t pad_buff[2][34];
	InitPAD(pad_buff[0], 34, pad_buff[1], 34);
	StartPAD();
	ChangeClearPAD(0);

	for (;;) 
	{
		PADTYPE *pad = (PADTYPE *) pad_buff[0];

		HandleCommands(pad);
		DrawCurrentMode(&ctx);

		flip_buffers(&ctx);
	}

	return 0;
}
