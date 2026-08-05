
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void InitializeInGameSystems(void)

{
  byte bVar1;
  HWND hWnd;
  byte *pbVar2;
  short *psVar3;
  uint *puVar4;
  char *pcVar5;
  int iVar6;
  int *piVar7;
  undefined4 *puVar8;
  tagRECT *lpRect;
  tagRECT Coordinate;
  
  InputMode = 0;
  FUN_00567600(FUN_00489b00);
  DAT_0087a7bc = LogicalTick;
  DAT_0087235c = 0xff000;
  if (PlayerOptions5 == 0) {
    DAT_0087235c = 0xfb000;
  }
  if (PlayerOptions6 == 0) {
    DAT_0087235c = DAT_0087235c ^ 0x8000;
  }
  lpRect = &Coordinate;
  hWnd = GetDesktopWindow();
  GetWindowRect(hWnd,lpRect);
  Coordinate.left = 0;
  Coordinate.top = 0;
  Coordinate.right = 639;
  Coordinate.bottom = 479;
  ClipCursor(&Coordinate);
  puVar8 = &AdditionalMessageLog;
  for (iVar6 = 0x500; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  _CHAT_RESET_FLAG = 0;
  puVar8 = &IncomingStatusMessage;
  for (iVar6 = 0x500; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  _CHAT_LOG2_INDEX = 0;
  puVar8 = &ChatLog;
  for (iVar6 = 0x500; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  _CHAT_LOG2_ACTIVE = 0;
  puVar8 = &StatusLinePacking;
  for (iVar6 = 0x500; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  _CHAT_LOG1_ACTIVE = 0;
  ZeroGlobals();
  DeleteKeyboardArray();
  UserInputLimit = 0;
  puVar8 = &UserInputLimit;
  for (iVar6 = 0x101; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  puVar8 = &InputQueueCount;
  for (iVar6 = 0x101; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  puVar8 = &DAT_00872158;
  for (iVar6 = 0x80; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  if ((GameInputSystem != 0) && (GameRunning != 0)) {
    PollMouseInput();
  }
  _InGameHUDSwitchGlobal3 = CameraOrbitCounter;
  puVar8 = &DAT_008727f8;
  for (iVar6 = 0x300; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  puVar8 = &DAT_00878c14;
  for (iVar6 = 0x300; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  DAT_00872358 = 0;
  puVar8 = &DAT_00879bbc;
  for (iVar6 = 0x300; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  iVar6 = 0;
  piVar7 = &DAT_008727f8;
  pbVar2 = (byte *)&CommandDefinitionTable;
  do {
    if (((*(short *)(pbVar2 + -4) != 0) && ((*pbVar2 & 4) != 0)) &&
       ((pbVar2[0x16] != 0 || *(short *)(pbVar2 + 0x14) != 0) || *(short *)(pbVar2 + 0x12) != 0)) {
      *piVar7 = iVar6;
      DAT_00872358 = DAT_00872358 + 1;
      piVar7 = piVar7 + 1;
    }
    pbVar2 = pbVar2 + 100;
    iVar6 = iVar6 + 1;
  } while ((int)pbVar2 < 0x63a7f4);
  iVar6 = 0;
  piVar7 = &DAT_008727f8 + DAT_00872358;
  pbVar2 = (byte *)&CommandDefinitionTable;
  do {
    if (((*(short *)(pbVar2 + -4) != 0) && ((*pbVar2 & 4) != 0)) &&
       ((pbVar2[0x16] == 0 && *(short *)(pbVar2 + 0x14) == 0) && *(short *)(pbVar2 + 0x12) == 0)) {
      *piVar7 = iVar6;
      DAT_00872358 = DAT_00872358 + 1;
      piVar7 = piVar7 + 1;
    }
    pbVar2 = pbVar2 + 100;
    iVar6 = iVar6 + 1;
  } while ((int)pbVar2 < 0x63a7f4);
  DAT_00879bb8 = 0;
  iVar6 = 0;
  piVar7 = &DAT_00878c14;
  psVar3 = &DAT_00627c02;
  do {
    if (((psVar3[-9] != 0) && ((*(uint *)(psVar3 + -7) & 0x300000) != 0 || *psVar3 != 0)) &&
       (psVar3[3] != 0 || psVar3[2] != 0)) {
      *piVar7 = iVar6;
      DAT_00879bb8 = DAT_00879bb8 + 1;
      piVar7 = piVar7 + 1;
    }
    psVar3 = psVar3 + 0x32;
    iVar6 = iVar6 + 1;
  } while ((int)psVar3 < 0x63a802);
  iVar6 = 0;
  piVar7 = &DAT_00878c14 + DAT_00879bb8;
  psVar3 = &DAT_00627c02;
  do {
    if (((psVar3[-9] != 0) && ((*(uint *)(psVar3 + -7) & 0x300000) != 0 || *psVar3 != 0)) &&
       (psVar3[3] == 0 && psVar3[2] == 0)) {
      *piVar7 = iVar6;
      DAT_00879bb8 = DAT_00879bb8 + 1;
      piVar7 = piVar7 + 1;
    }
    psVar3 = psVar3 + 0x32;
    iVar6 = iVar6 + 1;
  } while ((int)psVar3 < 0x63a802);
  iVar6 = 0;
  DAT_008737fc = 0;
  piVar7 = &DAT_00879bbc;
  puVar4 = &CommandDefinitionTable;
  do {
    if (((*(short *)(puVar4 + -1) != 0) &&
        (bVar1 = *(byte *)(puVar4 + 4), (DAT_0087235c & *puVar4) != 0 || bVar1 != 0)) &&
       ((*(char *)((int)puVar4 + 0x16) != '\0' || *(short *)((int)puVar4 + 0x12) != 0 &&
        (((INGAME_DIFFCULTY_RELATED_9 != 0 || (bVar1 < 0x81)) || (0x84 < bVar1)))))) {
      DAT_008737fc = DAT_008737fc + 1;
      *piVar7 = iVar6;
      piVar7 = piVar7 + 1;
    }
    puVar4 = puVar4 + 0x19;
    iVar6 = iVar6 + 1;
  } while ((int)puVar4 < 0x63a7f4);
  iVar6 = 0;
  piVar7 = &DAT_00879bbc + DAT_008737fc;
  puVar4 = &CommandDefinitionTable;
  do {
    if ((((*(short *)(puVar4 + -1) != 0) &&
         (bVar1 = *(byte *)(puVar4 + 4), (DAT_0087235c & *puVar4) != 0 || bVar1 != 0)) &&
        (*(char *)((int)puVar4 + 0x16) == '\0' && *(short *)((int)puVar4 + 0x12) == 0)) &&
       (((INGAME_DIFFCULTY_RELATED_9 != 0 || (bVar1 < 0x81)) || (0x84 < bVar1)))) {
      DAT_008737fc = DAT_008737fc + 1;
      *piVar7 = iVar6;
      piVar7 = piVar7 + 1;
    }
    puVar4 = puVar4 + 0x19;
    iVar6 = iVar6 + 1;
  } while ((int)puVar4 < 0x63a7f4);
  puVar8 = &DAT_008723b8;
  for (iVar6 = 0x110; iVar6 != 0; iVar6 = iVar6 + -1) {
    *puVar8 = 0;
    puVar8 = puVar8 + 1;
  }
  KEYPRESS_YES = 0x59;
  KEYPRESS_NO = 0x4e;
  KEYPRESS_RESTART = 0x52;
  KEYPRESS_ADVANCED = 0x41;
  KEYPRESS_SAVE = 0x53;
  pcVar5 = (char *)StringLookupHelper(s_KeyPress_0063aa24,s_STRKEYPRESS_YES_0063aa30);
  if (*pcVar5 != '\0') {
    KEYPRESS_YES = (int)*pcVar5;
  }
  pcVar5 = (char *)StringLookupHelper(s_KeyPress_0063aa24,s_STRKEYPRESS_NO_0063aa14);
  if (*pcVar5 != '\0') {
    KEYPRESS_NO = (int)*pcVar5;
  }
  pcVar5 = (char *)StringLookupHelper(s_KeyPress_0063aa24,s_STRKEYPRESS_RESTART_0063aa00);
  if (*pcVar5 != '\0') {
    KEYPRESS_RESTART = (int)*pcVar5;
  }
  pcVar5 = (char *)StringLookupHelper(s_KeyPress_0063aa24,s_STRKEYPRESS_ADVANCED_0063a9e8);
  if (*pcVar5 != '\0') {
    KEYPRESS_ADVANCED = (int)*pcVar5;
  }
  pcVar5 = (char *)StringLookupHelper(s_KeyPress_0063aa24,s_STRKEYPRESS_SAVE_0063a9d4);
  if (*pcVar5 != '\0') {
    KEYPRESS_SAVE = (int)*pcVar5;
  }
  return;
}

