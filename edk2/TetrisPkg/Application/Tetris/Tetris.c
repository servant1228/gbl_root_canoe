/** @file
  Standalone GOP-based Tetris UEFI application for AArch64.

  Control mapping for Qualcomm ABL-like environments:
  - Volume+  -> move left  (SCAN_UP)
  - Volume-  -> move right (SCAN_DOWN)
  - Any other key -> rotate

  Top and bottom 100 pixels are intentionally left unused.

  SPDX-License-Identifier: GPLv3
**/

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>

#define BOARD_W            10
#define BOARD_H            20
#define SAFE_MARGIN_Y      100
#define TICK_MS            300

typedef struct {
  INTN  X;
  INTN  Y;
  UINTN Type;
  UINTN Rot;
} ACTIVE_PIECE;

STATIC UINT8 mBoard[BOARD_H][BOARD_W];
STATIC ACTIVE_PIECE mPiece;
STATIC BOOLEAN mGameOver;
STATIC UINT32 mRand;
STATIC BOOLEAN mRenderReady;
STATIC BOOLEAN mLastGameOverState;
STATIC UINTN mCellSize;
STATIC UINTN mBoardBaseX;
STATIC UINTN mBoardBaseY;
STATIC UINTN mBoardPixelW;
STATIC UINTN mBoardPixelH;
STATIC UINT8 mLastFrame[BOARD_H][BOARD_W];

STATIC CONST UINT8 mTetromino[7][4][4][4] = {
  {
    { {0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0} },
    { {0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0} },
    { {0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0} },
    { {0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0} }
  },
  {
    { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
    { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
    { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
    { {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} }
  },
  {
    { {0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
    { {0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} },
    { {0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0} },
    { {0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0} }
  },
  {
    { {0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0} },
    { {0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0} },
    { {0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0} },
    { {0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0} }
  },
  {
    { {1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
    { {0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} },
    { {1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
    { {0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} }
  },
  {
    { {1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
    { {0,1,1,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0} },
    { {0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0} },
    { {0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0} }
  },
  {
    { {0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
    { {0,1,0,0}, {0,1,0,0}, {0,1,1,0}, {0,0,0,0} },
    { {0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0} },
    { {1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0} }
  }
};

STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL mColors[8] = {
  { 0x00, 0x00, 0x00, 0x00 },
  { 0xD0, 0x90, 0x20, 0x00 },
  { 0x20, 0xB0, 0xD0, 0x00 },
  { 0x20, 0xA0, 0x20, 0x00 },
  { 0xC0, 0x30, 0x30, 0x00 },
  { 0x80, 0x60, 0xD0, 0x00 },
  { 0xD0, 0x70, 0xB0, 0x00 },
  { 0x90, 0xD0, 0x40, 0x00 }
};

STATIC UINT32
Rand32 (VOID)
{
  mRand = mRand * 1664525U + 1013904223U;
  return mRand;
}

STATIC BOOLEAN
CanPlace (
  IN INTN   X,
  IN INTN   Y,
  IN UINTN  Type,
  IN UINTN  Rot
  )
{
  UINTN I;
  UINTN J;
  INTN  Bx;
  INTN  By;

  for (I = 0; I < 4; I++) {
    for (J = 0; J < 4; J++) {
      if (mTetromino[Type][Rot][I][J] == 0) {
        continue;
      }

      Bx = X + (INTN)J;
      By = Y + (INTN)I;

      if (Bx < 0 || Bx >= BOARD_W || By >= BOARD_H) {
        return FALSE;
      }

      if (By >= 0 && mBoard[By][Bx] != 0) {
        return FALSE;
      }
    }
  }

  return TRUE;
}

STATIC VOID
SpawnPiece (VOID)
{
  mPiece.Type = Rand32 () % 7U;
  mPiece.Rot  = 0;
  mPiece.X    = (BOARD_W / 2) - 2;
  mPiece.Y    = -1;

  if (!CanPlace (mPiece.X, mPiece.Y, mPiece.Type, mPiece.Rot)) {
    mGameOver = TRUE;
  }
}

STATIC VOID
LockPiece (VOID)
{
  UINTN I;
  UINTN J;
  INTN  Bx;
  INTN  By;

  for (I = 0; I < 4; I++) {
    for (J = 0; J < 4; J++) {
      if (mTetromino[mPiece.Type][mPiece.Rot][I][J] == 0) {
        continue;
      }

      Bx = mPiece.X + (INTN)J;
      By = mPiece.Y + (INTN)I;
      if (By >= 0 && By < BOARD_H && Bx >= 0 && Bx < BOARD_W) {
        mBoard[By][Bx] = (UINT8)(mPiece.Type + 1);
      }
    }
  }
}

STATIC VOID
ClearLines (VOID)
{
  INTN Y;
  INTN X;
  INTN PullY;
  BOOLEAN Full;

  for (Y = BOARD_H - 1; Y >= 0; Y--) {
    Full = TRUE;
    for (X = 0; X < BOARD_W; X++) {
      if (mBoard[Y][X] == 0) {
        Full = FALSE;
        break;
      }
    }

    if (!Full) {
      continue;
    }

    for (PullY = Y; PullY > 0; PullY--) {
      CopyMem (mBoard[PullY], mBoard[PullY - 1], BOARD_W);
    }
    SetMem (mBoard[0], BOARD_W, 0);
    Y++;
  }
}

STATIC VOID
StepDown (VOID)
{
  if (CanPlace (mPiece.X, mPiece.Y + 1, mPiece.Type, mPiece.Rot)) {
    mPiece.Y++;
    return;
  }

  LockPiece ();
  ClearLines ();
  SpawnPiece ();
}

STATIC VOID
DrawRect (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL            *Gop,
  IN UINTN                                   X,
  IN UINTN                                   Y,
  IN UINTN                                   W,
  IN UINTN                                   H,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL           *Color
  )
{
  Gop->Blt (
         Gop,
         Color,
         EfiBltVideoFill,
         0,
         0,
         X,
         Y,
         W,
         H,
         0
         );
}

STATIC BOOLEAN
PrepareRenderLayout (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  UINTN ScreenW;
  UINTN ScreenH;
  UINTN PlayH;

  ScreenW = Gop->Mode->Info->HorizontalResolution;
  ScreenH = Gop->Mode->Info->VerticalResolution;

  if (ScreenH <= (SAFE_MARGIN_Y * 2 + BOARD_H)) {
    return FALSE;
  }

  PlayH = ScreenH - (SAFE_MARGIN_Y * 2);
  mCellSize = PlayH / BOARD_H;
  if (mCellSize == 0) {
    return FALSE;
  }

  mBoardPixelW = BOARD_W * mCellSize;
  mBoardPixelH = BOARD_H * mCellSize;

  if (mBoardPixelW > ScreenW) {
    mCellSize = ScreenW / BOARD_W;
    if (mCellSize == 0) {
      return FALSE;
    }
    mBoardPixelW = BOARD_W * mCellSize;
    mBoardPixelH = BOARD_H * mCellSize;
  }

  mBoardBaseX = (ScreenW - mBoardPixelW) / 2;
  mBoardBaseY = SAFE_MARGIN_Y + (PlayH - mBoardPixelH) / 2;

  return TRUE;
}

STATIC VOID
DrawCell (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINTN                         X,
  IN UINTN                         Y,
  IN UINT8                         CellType
  )
{
  DrawRect (
    Gop,
    mBoardBaseX + X * mCellSize,
    mBoardBaseY + Y * mCellSize,
    mCellSize - 1,
    mCellSize - 1,
    &mColors[CellType]
  );
}

STATIC VOID
ComposeFrame (
  OUT UINT8  Frame[BOARD_H][BOARD_W]
  )
{
  UINTN I;
  UINTN J;
  INTN  Bx;
  INTN  By;

  CopyMem (Frame, mBoard, sizeof (mBoard));

  for (I = 0; I < 4; I++) {
    for (J = 0; J < 4; J++) {
      if (mTetromino[mPiece.Type][mPiece.Rot][I][J] == 0) {
        continue;
      }

      Bx = mPiece.X + (INTN)J;
      By = mPiece.Y + (INTN)I;
      if (Bx >= 0 && Bx < BOARD_W && By >= 0 && By < BOARD_H) {
        Frame[By][Bx] = (UINT8)(mPiece.Type + 1);
      }
    }
  }
}

STATIC VOID
DrawGame (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  UINT8 CurrentFrame[BOARD_H][BOARD_W];
  UINTN X;
  UINTN Y;
  BOOLEAN GameOverChanged;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Bg;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Border;

  if (!PrepareRenderLayout (Gop)) {
    return;
  }

  if (!mRenderReady) {
    UINTN ScreenW;
    UINTN ScreenH;

    ScreenW = Gop->Mode->Info->HorizontalResolution;
    ScreenH = Gop->Mode->Info->VerticalResolution;

    Bg.Blue = 0;
    Bg.Green = 0;
    Bg.Red = 0;
    Bg.Reserved = 0;

    Border.Blue = 0x30;
    Border.Green = 0x30;
    Border.Red = 0x30;
    Border.Reserved = 0;

    DrawRect (Gop, 0, 0, ScreenW, ScreenH, &Bg);
    DrawRect (Gop, mBoardBaseX - 2, mBoardBaseY - 2, mBoardPixelW + 4, mBoardPixelH + 4, &Border);

    SetMem (mLastFrame, sizeof (mLastFrame), 0xFF);
    mLastGameOverState = FALSE;
    mRenderReady = TRUE;
  }

  GameOverChanged = (BOOLEAN)(mGameOver != mLastGameOverState);
  if (GameOverChanged && !mGameOver) {
    SetMem (mLastFrame, sizeof (mLastFrame), 0xFF);
  }

  ComposeFrame (CurrentFrame);

  for (Y = 0; Y < BOARD_H; Y++) {
    for (X = 0; X < BOARD_W; X++) {
      if (CurrentFrame[Y][X] != mLastFrame[Y][X]) {
        DrawCell (Gop, X, Y, CurrentFrame[Y][X]);
        mLastFrame[Y][X] = CurrentFrame[Y][X];
      }
    }
  }

  if (GameOverChanged && mGameOver) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Fail;
    Fail.Blue = 0x20;
    Fail.Green = 0x20;
    Fail.Red = 0xC0;
    Fail.Reserved = 0;

    DrawRect (
      Gop,
      mBoardBaseX + mCellSize,
      mBoardBaseY + (BOARD_H / 2) * mCellSize,
      mBoardPixelW - 2 * mCellSize,
      2 * mCellSize,
      &Fail
    );
  }

  mLastGameOverState = mGameOver;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_EVENT                      TickEvent;
  EFI_EVENT                      WaitList[2];
  UINTN                          Index;
  EFI_INPUT_KEY                  Key;

  ZeroMem (mBoard, sizeof (mBoard));
  mGameOver = FALSE;
  mRenderReady = FALSE;
  mLastGameOverState = FALSE;
  mRand = (UINT32)((UINTN)ImageHandle ^ 0xA5A55A5AU);

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status) || Gop == NULL) {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &TickEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->SetTimer (TickEvent, TimerPeriodic, (UINT64)TICK_MS * 10000ULL);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (TickEvent);
    return Status;
  }

  SpawnPiece ();
  DrawGame (Gop);

  WaitList[0] = TickEvent;
  WaitList[1] = gST->ConIn->WaitForKey;

  while (!mGameOver) {
    Status = gBS->WaitForEvent (2, WaitList, &Index);
    if (EFI_ERROR (Status)) {
      break;
    }

    if (Index == 0) {
      StepDown ();
    } else {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (!EFI_ERROR (Status)) {
        if (Key.ScanCode == SCAN_UP) {
          if (CanPlace (mPiece.X - 1, mPiece.Y, mPiece.Type, mPiece.Rot)) {
            mPiece.X--;
          }
        } else if (Key.ScanCode == SCAN_DOWN) {
          if (CanPlace (mPiece.X + 1, mPiece.Y, mPiece.Type, mPiece.Rot)) {
            mPiece.X++;
          }
        } else {
          if (CanPlace (mPiece.X, mPiece.Y, mPiece.Type, (mPiece.Rot + 1) % 4)) {
            mPiece.Rot = (mPiece.Rot + 1) % 4;
          }
        }
      }
    }

    DrawGame (Gop);
  }

  DrawGame (Gop);
  gBS->Stall (1000000);

  gBS->CloseEvent (TickEvent);
  return EFI_SUCCESS;
}
