#pragma once
#include "types.h"
#include "snes/snes.h"

uint8 *GetPtr(uint32 addr);

void RunEmulatedFuncSilent(uint32 pc, uint16 a, uint16 x, uint16 y, bool mf, bool xf, int b, int whatflags);
void RunEmulatedFunc(uint32 pc, uint16 a, uint16 x, uint16 y, bool mf, bool xf, int b, int whatflags);
void PatchRom(uint8_t *rom);
void SetSnes(Snes *snes);
void RunAudioPlayer();
void CopyStateAfterSnapshotRestore(bool is_reset);
void SaveLoadSlot(int cmd, int which);
void PatchCommand(char cmd);
bool RunOneFrame(Snes *snes, int input_state, bool turbo);
