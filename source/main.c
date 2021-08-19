#include <3ds.h>
#include <citro3d.h>
#include "citro2d.h"
#include "internal.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "program_shbin.h"

static int uLoc_offsets;

float gameTime      = 0;
float timeStep      = 0.08;
float amplitude     = 0.055;
float scanlineSpeed = 0.1;

enum Functions {
  SIN,
  SIN_AT_LEAST_ZERO,
  ABS_SIN,
  NUM_FUNCTIONS
};

enum Functions curFunction;

const char *FUNC_STRINGS[NUM_FUNCTIONS] = {
  "sine",
  "sine, but at least 0",
  "absolute value of sine"
};

static void buildScanlineOffsetTable(void)
{
  #define NUM_OFFSETS 240
  float offsetTable[NUM_OFFSETS];

  memset(offsetTable, 0, sizeof(offsetTable));

  for (int i = 0; i < NUM_OFFSETS; i++) {
    float val;

    switch (curFunction) {
      case SIN:
        val = amplitude*sin(gameTime + scanlineSpeed*i);
        break;

      case SIN_AT_LEAST_ZERO:
        val = amplitude*sin(gameTime + scanlineSpeed*i);
        if (val < 0) val = 0;
        break;

      case ABS_SIN:
        val = fabs(amplitude*sin(gameTime + scanlineSpeed*i));
        break;
    }

    offsetTable[i] = val;
  }

  gameTime += timeStep;
  if      (gameTime >  2*M_PI) gameTime -= 2*M_PI;
  else if (gameTime < -2*M_PI) gameTime += 2*M_PI;

  // Update the uniforms
  C3D_FVUnifMtxNx4(GPU_GEOMETRY_SHADER, uLoc_offsets, (C3D_Mtx*) offsetTable, NUM_OFFSETS/4);
}

bool MyDrawImage(C2D_Image img, const C2D_DrawParams* params, const C2D_ImageTint* tint)
{
  C2Di_Context* ctx = C2Di_GetContext();
  if (!(ctx->flags & C2DiF_Active))
    return false;
  if (6 > (ctx->vtxBufSize - ctx->vtxBufPos))
    return false;

  C2Di_SetCircle(false);
  C2Di_SetTex(img.tex);
  C2Di_Update();

  // Calculate positions
  C2Di_Quad quad;
  C2Di_CalcQuad(&quad, params);

  // Calculate texcoords
  float tcTopLeft[2], tcTopRight[2], tcBotLeft[2], tcBotRight[2];
  Tex3DS_SubTextureTopLeft    (img.subtex, &tcTopLeft[0],  &tcTopLeft[1]);
  Tex3DS_SubTextureTopRight   (img.subtex, &tcTopRight[0], &tcTopRight[1]);
  Tex3DS_SubTextureBottomLeft (img.subtex, &tcBotLeft[0],  &tcBotLeft[1]);
  Tex3DS_SubTextureBottomRight(img.subtex, &tcBotRight[0], &tcBotRight[1]);

  // // Perform flip if needed
  // if (params->pos.w < 0)
  // {
  //  C2Di_SwapUV(tcTopLeft, tcTopRight);
  //  C2Di_SwapUV(tcBotLeft, tcBotRight);
  // }
  // if (params->pos.h < 0)
  // {
  //  C2Di_SwapUV(tcTopLeft, tcBotLeft);
  //  C2Di_SwapUV(tcTopRight, tcBotRight);
  // }

  // Calculate colors
  static const C2D_Tint s_defaultTint = { 0xFF<<24, 0.0f };
  const C2D_Tint* tintTopLeft  = tint ? &tint->corners[C2D_TopLeft]  : &s_defaultTint;
  const C2D_Tint* tintTopRight = tint ? &tint->corners[C2D_TopRight] : &s_defaultTint;
  const C2D_Tint* tintBotLeft  = tint ? &tint->corners[C2D_BotLeft]  : &s_defaultTint;
  const C2D_Tint* tintBotRight = tint ? &tint->corners[C2D_BotRight] : &s_defaultTint;

  // We only need two vertices to know how to draw everything. The geometry shader takes care of the rest
  // From these two vertices, 32 triangles making up 16 one-pixel-tall strips will be drawn.
  C2Di_AppendVtx(quad.topLeft[0],  quad.topLeft[1],  params->depth, tcTopLeft[0],  tcTopLeft[1],  0, tintTopLeft->blend,  tintTopLeft->color);
  // C2Di_AppendVtx(quad.botLeft[0],  quad.botLeft[1],  params->depth, tcBotLeft[0],  tcBotLeft[1],  0, tintBotLeft->blend,  tintBotLeft->color);
  C2Di_AppendVtx(quad.botRight[0], quad.botRight[1], params->depth, tcBotRight[0], tcBotRight[1], 0, tintBotRight->blend, tintBotRight->color);

  // C2Di_AppendVtx(quad.topLeft[0],  quad.topLeft[1],  params->depth, tcTopLeft[0],  tcTopLeft[1],  0, tintTopLeft->blend,  tintTopLeft->color);
  // C2Di_AppendVtx(quad.botRight[0], quad.botRight[1], params->depth, tcBotRight[0], tcBotRight[1], 0, tintBotRight->blend, tintBotRight->color);
  // C2Di_AppendVtx(quad.topRight[0], quad.topRight[1], params->depth, tcTopRight[0], tcTopRight[1], 0, tintTopRight->blend, tintTopRight->color);

  return true;
}

static bool MyDrawSprite(const C2D_Sprite* sprite)
{
  return MyDrawImage(sprite->image, &sprite->params, NULL);
}

enum Sprites {
  PIPE_TL,
  PIPE_TR,
  PIPE_BL,
  PIPE_BR,
  QUESTION_BLOCK,
  DRAGON_COIN_T,
  DRAGON_COIN_B,
  TURN_BLOCK,
  CEMENT_BLOCK,
  NUM_SPRITES
};

C2D_SpriteSheet spriteSheet;
C2D_Sprite sprites[NUM_SPRITES];

int main()
{
  romfsInit();
  gfxInitDefault();
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
  C2D_Prepare();
  consoleInit(GFX_BOTTOM, NULL);

  // Create screens
  C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

  // Load graphics
  spriteSheet = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
  if (!spriteSheet) svcBreak(USERBREAK_PANIC);

  size_t numSprites = C2D_SpriteSheetCount(spriteSheet);

  for (size_t i = 0; i < numSprites; i++) {
    C2D_SpriteFromSheet(&sprites[i], spriteSheet, i);
  }

  C2Di_Context* ctx = C2Di_GetContext();
  uLoc_offsets = shaderInstanceGetUniformLocation(ctx->program.geometryShader, "offsets");

  // Main loop
  while (aptMainLoop())
  {
    hidScanInput();

    // Respond to user input
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();
    if (kDown & KEY_START)
      break; // break in order to return to hbmenu

    if (kHeld & KEY_UP)
      timeStep += 0.01;
    else if (kHeld & KEY_DOWN)
      timeStep -= 0.01;

    if (kHeld & KEY_RIGHT)
      amplitude += 0.005;
    else if (kHeld & KEY_LEFT)
      amplitude -= 0.005;

    if (kHeld & KEY_R)
      scanlineSpeed += 0.001;
    else if (kHeld & KEY_L)
      scanlineSpeed -= 0.001;

    if (kDown & KEY_A) {
      curFunction++;
      if (curFunction == NUM_FUNCTIONS) curFunction = 0;
    }

    printf("\x1b[1;1HTime Step:      %1.4f\x1b[K", timeStep);
    printf("\x1b[2;1HAmplitude:      %1.4f\x1b[K", amplitude);
    printf("\x1b[3;1HScanline Speed: %1.4f\x1b[K", scanlineSpeed);
    printf("\x1b[4;1HFunction:       %s\x1b[K", FUNC_STRINGS[curFunction]);
    printf("\x1b[6;1Hdown/up to change the time step");
    printf("\x1b[7;1Hleft/right to change the amplitude");
    printf("\x1b[8;1HL/R to change the scanline speed");
    printf("\x1b[9;1HA to cycle through the pattern functions");

    // Render the scene
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    buildScanlineOffsetTable();
    C2D_TargetClear(top, C2D_Color32f(0.3f, 0.7f, 1.0f, 1.0f));
    C2D_SceneBegin(top);

    C2D_SpriteSetPos(&sprites[PIPE_TL], 300,    6*16);
    C2D_SpriteSetPos(&sprites[PIPE_TR], 300+16, 6*16);
    MyDrawSprite(&sprites[PIPE_TL]);
    MyDrawSprite(&sprites[PIPE_TR]);

    for (int i = 7; i < 240/16+1; i++) {
      C2D_SpriteSetPos(&sprites[PIPE_BL], 300,    i*16);
      C2D_SpriteSetPos(&sprites[PIPE_BR], 300+16, i*16);

      MyDrawSprite(&sprites[PIPE_BL]);
      MyDrawSprite(&sprites[PIPE_BR]);
    }

    C2D_SpriteSetPos(&sprites[QUESTION_BLOCK], 80, 60);
    MyDrawSprite(&sprites[QUESTION_BLOCK]);
    C2D_SpriteSetPos(&sprites[QUESTION_BLOCK], 250, 200);
    MyDrawSprite(&sprites[QUESTION_BLOCK]);

    for (int y = 0; y < 5; y++) {
      for (int x = 0; x < 3; x++) {
        C2D_SpriteSetPos(&sprites[CEMENT_BLOCK], 252+x*16, 0+y*16);
        MyDrawSprite(&sprites[CEMENT_BLOCK]);
      }
    }

    C2D_SpriteSetPos(&sprites[DRAGON_COIN_T], 50, 180);
    C2D_SpriteSetPos(&sprites[DRAGON_COIN_B], 50, 180+16);
    MyDrawSprite(&sprites[DRAGON_COIN_T]);
    MyDrawSprite(&sprites[DRAGON_COIN_B]);

    C2D_SpriteSetPos(&sprites[TURN_BLOCK], 10, 50);
    MyDrawSprite(&sprites[TURN_BLOCK]);
    C2D_SpriteSetPos(&sprites[TURN_BLOCK], 200, 20);
    MyDrawSprite(&sprites[TURN_BLOCK]);
    C2D_SpriteSetPos(&sprites[TURN_BLOCK], 120, 70);
    MyDrawSprite(&sprites[TURN_BLOCK]);
    C2D_SpriteSetPos(&sprites[TURN_BLOCK], 170, 140);
    MyDrawSprite(&sprites[TURN_BLOCK]);
    C2D_SpriteSetPos(&sprites[TURN_BLOCK], 40, 100);
    MyDrawSprite(&sprites[TURN_BLOCK]);

    C3D_FrameEnd(0);
  }

  // Deinitialize graphics
  C2D_SpriteSheetFree(spriteSheet);

  // Deinit libs
  C2D_Fini();
  C3D_Fini();
  gfxExit();
  romfsExit();
  return 0;
}
