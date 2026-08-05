
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void Net_ProcessClientState(void)

{
  DWORD DVar1;
  DWORD Time;
  int IncomingPointer;
  int *Cnt1;
  undefined1 *puVar2;
  undefined4 *puVar3;
  uint uVar4;
  undefined4 *puVar5;
  undefined4 local_40c [256];
  int local_c;
  uint local_8;
  int Pointer;
  
  if ((TickCounter != 0) &&
     (TickCounter = TickCounter + 1, 29760 < (int)(TickCounter - TickReference))) {
    _PayLoadSize = (undefined1 *)0x4;
    _Net_PayLoadData = TickCounter;
    Net_PrimaryQueueOutgoingPacket(&Net_PayLoadQueue,52,1,0xffffffff,&Net_PayLoadData,4);
    TickReference = TickCounter;
  }
  LocalCounter = LocalCounter + 1;
  if ((Net_ListenServer != 0) &&
     ((((Engine_Binoculars = HUD_Binoculars, MyBase == (Player *)0x0 ||
        (*(short *)&MyBase->Health < 1)) || (MissionComplete != 0)) ||
      ((((byte)Engine_MovementActions & 30) != 0 || (Render_3rdView == 1)))))) {
    Engine_Binoculars = 0;
  }
  UpdateClientTimers(&NetPointer);
  Net_ConnectionInterruptedTimer = Net_ConnectionInterruptedTimer + 1;
  IncomingPointer = Net_ProcessIncomingPackets();
  if (0 < IncomingPointer) {
    Net_ConnectionInterruptedTimer = 0;
  }
  if (PauseGame == 0) {
    UpdatePlayerHUDState();
  }
  LocalPlayerTimer = LocalPlayerTimer + 1;
  if (62 < LocalPlayerTimer) {
    Net_GetLocalPlayerInfo((LocalPlayerInfo *)&LocalPlayerTable);
    Net_ResetLocalPlayerInfo(&LocalPlayerTable);
    IncomingPointer = 0;
    if (0 < NumPlayers) {
      Cnt1 = (int *)(LocalPlayerBase2 + 4);
      do {
        Pointer = *Cnt1;
        if (((Pointer != 0) && (*(char *)(Pointer + 0x10) != '\0')) &&
           ((*(int *)(Pointer + 0x20) != 0 && (*(char *)(Pointer + 0x13) != '\0')))) {
          *(char *)(Pointer + 0x13) = *(char *)(Pointer + 0x13) + -1;
        }
        IncomingPointer = IncomingPointer + 1;
        Cnt1 = Cnt1 + 2;
      } while (IncomingPointer < NumPlayers);
    }
    LocalPlayerTimer = 0;
  }
  if (EntityUpdateTimer_Current < EntityUpdateTimer_Target) {
    EntityUpdateTimer_Current = EntityUpdateTimer_Current + 1;
  }
  else if (EntityUpdateTimer_Target == EntityUpdateTimer_Max) {
    EntityUpdate_LastPointer = 0;
    EntityUpdateTimer_Current = 0;
    EntityUpdateTimer_Target = 0;
    EntityUpdateTimer_Max = 0;
  }
  if ((Net_ClientTickCounter == 0) ||
     (Net_ClientTickCounter = Net_ClientTickCounter - 1, Net_ClientTickCounter == 0)) {
    Net_ClientTickCounter = Net_ClientWaitTicks;
    if (Net_MPGame != 0) {
      if ((((Net_Host == 0) && (Net_SuppressClientInput == 0)) && (MissionComplete == 0)) &&
         (Net_ClientStartDelay == 0)) {
        _PayLoadSize = (undefined1 *)Net_SerializeLocalPlayer(&Net_PayLoadData,0x400);
        Net_PrimaryQueueOutgoingPacket(&Net_PayLoadQueue,0x35,0,0,&Net_PayLoadData,_PayLoadSize);
        if (MyBase == (Player *)0x0) {
          _PayLoadSize = (undefined1 *)0x0;
        }
        else if (*(short *)&MyBase->Health == 0) {
          _PayLoadSize = (undefined1 *)0x0;
        }
        else if ((*(byte *)&MyBase->StatusBitfield & 4) == 0) {
          puVar2 = &Net_PayLoadData;
          IncomingPointer = Net_PackEntityUpdate(MyBase,local_40c,0x400,10,&local_8);
          if ((-1 < IncomingPointer) && ((int)local_8 < 0x401)) {
            puVar3 = local_40c;
            puVar5 = (undefined4 *)&Net_PayLoadData;
            for (uVar4 = local_8 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
              *puVar5 = *puVar3;
              puVar3 = puVar3 + 1;
              puVar5 = puVar5 + 1;
            }
            for (uVar4 = local_8 & 3; uVar4 != 0; uVar4 = uVar4 - 1) {
              *(undefined *)puVar5 = *(undefined *)puVar3;
              puVar3 = (undefined4 *)((int)puVar3 + 1);
              puVar5 = (undefined4 *)((int)puVar5 + 1);
            }
            puVar2 = &Net_PayLoadData + local_8;
          }
          _PayLoadSize = puVar2 + -0x71e0b8;
        }
        else {
          _PayLoadSize = (undefined1 *)0x0;
        }
        if (0 < (int)_PayLoadSize) {
          Net_PrimaryQueueOutgoingPacket(&Net_PayLoadQueue,10,4,0,&Net_PayLoadData,_PayLoadSize);
        }
        if (GameTimeBase == 0) {
          _GameTimeLastTick = GetTickCount();
          GameTimeBase = GetTickCount();
          GameTimeZoneOffset = TimeZoneInfo(0);
        }
        else {
          Time = GetTickCount();
          DVar1 = GetTickCount();
          IncomingPointer = TimeZoneInfo(0);
          local_8 = IncomingPointer - GameTimeZoneOffset;
          uVar4 = DVar1 - GameTimeBase;
          local_c = TimeZoneInfo(0);
          if (10000 < uVar4) {
            if (local_8 < 9) {
              QueueInputEvent(3,0,0,0,0);
              GameState = 6;
            }
            GameTimeZoneOffset = local_c;
            GameTimeBase = DVar1;
            _GameTimeLastTick = Time;
          }
        }
      }
                    /* Determine Client Ping */
      if (((Net_MPGame != 0) && (Net_Host == 0)) &&
         ((Net_SuppressClientInput == 0 && (MissionComplete == 0)))) {
        Time = GetTickCount();
        Net_PingSendCooldown = Net_PingSendCooldown - (uint)Net_ClientWaitTicks;
        if (Net_PingSendCooldown < 1) {
          Net_PingSendCooldown = 62;
          DeltaPosZ._0_1_ = 1;
          _PayLoadSize = (undefined1 *)0x5;
          _Net_PayLoadData = Time;
          Net_PrimaryQueueOutgoingPacket(&Net_PayLoadQueue,44,0,0,&Net_PayLoadData,5);
        }
      }
    }
    Net_AdvanceClientTick();
  }
  return;
}

