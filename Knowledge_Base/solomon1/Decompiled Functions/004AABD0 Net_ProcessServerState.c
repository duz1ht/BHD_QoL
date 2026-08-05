
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void Net_ProcessServerState(void)

{
  char cVar1;
  dword *BasePointer;
  DWORD Time;
  void *pvVar2;
  int *piVar3;
  char *pcVar4;
  int iVar5;
  int NetPlayerPtr;
  int iVar6;
  uint uVar7;
  uint uVar8;
  Player *PlayerBase;
  undefined4 *puVar9;
  Net_Player *NetPlayerPointer;
  char *pcVar10;
  undefined StringPointer [510];
  char acStack_20e [2];
  undefined4 String [128];
  undefined local_c [4];
  int NetPlayer;
  Player *MyBase;
  Net_Player *ptr;
  
  if ((0 < Net_CooldownTimer) &&
     (Net_CooldownTimer = Net_CooldownTimer + -1, Net_CooldownTimer == 0)) {
    Net_TickTime = Net_TickTime + 1;
  }
  if (Net_AliveStateSanitizeTimer != 0) {
    Net_AliveStateSanitizeTimer = Net_AliveStateSanitizeTimer + -1;
  }
  if (Net_AliveStateSanitizeTimer == 0) {
    NetPlayerPtr = 0;
    if (0 < Engine_MapOrganics) {
      BasePointer = &OrganicBase->StatusBitfield;
      do {
        *BasePointer = *BasePointer & 0xf7ffbfff;
        NetPlayerPtr = NetPlayerPtr + 1;
        BasePointer = BasePointer + 0xa7;
      } while (NetPlayerPtr < Engine_MapOrganics);
    }
    NetPlayerPtr = 0;
    if (0 < Engine_MapObjects) {
      BasePointer = &ObjectBase->AliveState;
      do {
        *BasePointer = *BasePointer & 0xf7ffbfff;
        NetPlayerPtr = NetPlayerPtr + 1;
        BasePointer = BasePointer + 0xa7;
      } while (NetPlayerPtr < Engine_MapObjects);
    }
    Net_AliveStateSanitizeTimer = 744;
  }
  if (Net_RespawnScheduler != 0) {
    Net_RespawnScheduler = Net_RespawnScheduler + -1;
  }
  if (Net_RespawnScheduler == 0) {
    Net_UpdateRespawnState();
    Net_RespawnScheduler = 3;
  }
  if ((Net_MPGame == 0) || (Net_Host != 0)) {
    Net_CheckClientPositions();
    Net_ReceiveClientPackets();
    Net_CountActiveObjects();
    Net_UpdateDynamicObjectsPerTick();
    Net_HostSendSoundEventToClient();
    if ((Net_StartDelayTimer == 0) &&
       (Net_MapEventTimer = Net_MapEventTimer + 1, 15 < Net_MapEventTimer)) {
      if (EnvironmentGate == 0) {
        Net_ProcessMapEvents();
      }
      Net_MapEventTimer = 0;
    }
    Net_PlayerListTimer = Net_PlayerListTimer + 1;
    if (310 < Net_PlayerListTimer) {
      Net_BroadcastPlayerListInfo();
      Net_BroadcastPlayerPings();
      Net_PlayerListTimer = 0;
    }
    CooldownTimer();
    Net_BoundingBoxTimer = Net_BoundingBoxTimer + 1;
    if (434 < Net_BoundingBoxTimer) {
      ResetBoundingBoxTimer();
      Net_BoundingBoxTimer = 0;
    }
    Net_MasterServerCounter = Net_MasterServerCounter + -1;
    if (Net_MasterServerCounter < 1) {
      Net_MasterServerCounter = 1860;
      Net_PushMasterServerUpdate(0);
      Net_HeartbeatRotationIndex = Net_HeartbeatRotationIndex + 1;
      if (5 < Net_HeartbeatRotationIndex) {
        Net_HeartbeatRotationIndex = 0;
      }
      Time = GetTickCount();
      NetPlayerPtr = _rand();
      (&Net_HeartbeatSessionTimeStamps)[Net_HeartbeatRotationIndex] =
           (Time + NetPlayerPtr & 0xffffff) + 0x1000000;
      if (Net_ActiveHeartbeats < 6) {
        Net_ActiveHeartbeats = Net_ActiveHeartbeats + 1;
      }
    }
    NetPlayerPtr = 0;
    if (0 < *(int *)NetPlayerBase) {
      piVar3 = (int *)&NetPlayerBase->PlayerPointer->field_0x8c;
      do {
        if ((*(char *)(piVar3 + 0x1a0e) != '\0') && (piVar3[0x1a17] == 8)) {
          *piVar3 = *piVar3 + 1;
        }
        NetPlayerPtr = NetPlayerPtr + 1;
        piVar3 = piVar3 + 0x2bccf;
      } while (NetPlayerPtr < *(int *)NetPlayerBase);
    }
    if ((((Net_MPGame != 0) && (MissionComplete != 0)) &&
        (_DAT_009dae00 = _DAT_009dae00 + -1, _DAT_009dae00 == 0)) &&
       ((Engine_MapCycle == 0 || (GameState = 4, Engine_LastGame != 0)))) {
      GameState = 3;
    }
    if (0 < Net_HostChallengeTimer) {
      Net_HostChallengeTimer = Net_HostChallengeTimer + -1;
    }
    if (Net_HostChallengeTimer == 0) {
      Net_HostChallengeTimer = 62;
      if ((Net_MPGame != 0) && (Net_CooldownTimer == 0)) {
        PlayerBase = NetPlayerBase->PlayerPointer;
        if (0 < *(int *)NetPlayerBase) {
          puVar9 = (undefined4 *)((int)&PlayerBase[0x43f].MetadataPtr + 3);
          NetPlayer = *(int *)NetPlayerBase;
          do {
            if (((*(char *)(puVar9 + -0x2a164) != '\0') && (puVar9[-0x2a15b] == 2)) &&
               (puVar9[0xb] != 0)) {
              puVar9[0xb] = 0;
              *(undefined2 *)(puVar9 + -9) = 0;
              *(undefined2 *)(puVar9 + -10) = 0;
              *(undefined2 *)((int)puVar9 + -0x26) = 0;
              *(undefined2 *)((int)puVar9 + 0x32) = 0;
              puVar9[-3] = 0;
              puVar9[-2] = 0;
              puVar9[-1] = 0;
              *puVar9 = 0;
              puVar9[1] = 0;
              puVar9[2] = 0;
              puVar9[3] = 0;
              puVar9[4] = 0;
              puVar9[5] = 0;
              puVar9[6] = 0;
              puVar9[7] = 0;
              if (puVar9[8] == 0) {
                pvVar2 = _malloc(0x2000);
                puVar9[8] = pvVar2;
                if (pvVar2 == (void *)0x0) {
                  puVar9[9] = 0;
                }
                else {
                  puVar9[9] = 0x200;
                }
              }
              puVar9[10] = 0;
              Time = GetTickCount();
              puVar9[-5] = Time;
              puVar9[0xb] = 0;
              TickBitMask = 1;
              ::NetPlayerPtr = PlayerBase;
              Net_PacketSize =
                   Net_SerializeChallengePacket
                             (&Net_PacketData,0x400,PlayerBase,Net_TickTime,0x200,0x100);
              Net_HostQueueOutgoingPacket(0x66,1,0xffffffff,&Net_PacketData,Net_PacketSize);
              puVar9[-3] = puVar9[-3] + 0x200;
              puVar9[-1] = puVar9[-1] + 1;
            }
            PlayerBase = (Player *)&PlayerBase[0x440].SoundResourcePtr;
            puVar9 = puVar9 + 0x2bccf;
            NetPlayer = NetPlayer + -1;
          } while (NetPlayer != 0);
        }
      }
      Engine_UpdateEntityTypeCounts();
      if (Net_MPGame == 0) {
        Net_StartDelayTimer = 0;
        Net_SpawnProtectionTimer = 0;
      }
      else {
        if (((Net_StartDelayTimer != 0) &&
            (Net_StartDelayTimer = Net_StartDelayTimer + -1, Net_StartDelayTimer == 0)) &&
           ((Net_Host != 0 && (Timer(&ServerGlobal1,3), NetPlayerBase != (Net_Player *)0x0)))) {
          NetPlayerPtr = *(int *)NetPlayerBase;
          if (0 < NetPlayerPtr) {
            piVar3 = (int *)((int)&NetPlayerBase->PlayerPointer[0x440].Unknown + 1);
            do {
              if ((*(char *)(piVar3 + -0x2a27e) != '\0') && (*piVar3 == 2)) {
                Timer(piVar3,3);
              }
              piVar3 = piVar3 + 0x2bccf;
              NetPlayerPtr = NetPlayerPtr + -1;
            } while (NetPlayerPtr != 0);
          }
        }
        if (Net_SpawnProtectionTimer != 0) {
          Net_SpawnProtectionTimer = Net_SpawnProtectionTimer + -1;
        }
      }
      Net_ProcessPlayerObjectives();
      Net_UpdateZoneControl();
      Net_CheckMatchConditions();
      Net_CheckFFKills();
      Net_BroadcastObjectState();
      NetPlayerPtr = 0;
      if (0 < *(int *)NetPlayerBase) {
        piVar3 = (int *)((int)&NetPlayerBase->PlayerPointer[0x438].CamTargetZ + 1);
        do {
          if (((*(char *)(piVar3 + -0x29d4a) != '\0') && (0 < *piVar3)) &&
             (iVar6 = *piVar3 + -1, *piVar3 = iVar6, iVar6 == 0)) {
            piVar3[-2] = 0;
            piVar3[-1] = 0;
          }
          NetPlayerPtr = NetPlayerPtr + 1;
          piVar3 = piVar3 + 0x2bccf;
        } while (NetPlayerPtr < *(int *)NetPlayerBase);
      }
      if ((Net_Host != 0) && (Engine_GameType == 2)) {
        TickBitMask = 0xc;
        _Net_PacketData = CONCAT31(ram0x009e47b1,(undefined)DAT_009dddd4);
        Net_PacketSize = 1;
        Net_HostQueueOutgoingPacket(0x20,0,0,&Net_PacketData,1);
      }
      Net_BroadcastPlayerStates(&HostPlayerStateTable);
      NetPlayerPtr = *(int *)NetPlayerBase;
      Net_ServerLogins = 0;
      if (0 < NetPlayerPtr) {
        piVar3 = (int *)&NetPlayerBase->PlayerPointer[0x28].PlayerState;
        do {
          if ((((*(char *)(piVar3 + -9) != '\0') && (*piVar3 == 8)) && (piVar3[-0x1a34] != 0)) &&
             (((*(byte *)(piVar3[-0x1a34] + 0x20) & 2) == 0 ||
              ((*(char *)((int)piVar3 + 0xa7521) != '\0' && (piVar3[0x2a14f] != 0)))))) {
            Net_ServerLogins = Net_ServerLogins + 1;
          }
          piVar3 = piVar3 + 0x2bccf;
          NetPlayerPtr = NetPlayerPtr + -1;
        } while (NetPlayerPtr != 0);
      }
      PlayerBase = NetPlayerBase->PlayerPointer;
      NetPlayer = 0;
      ptr = NetPlayerBase;
      if (0 < *(int *)NetPlayerBase) {
        do {
          if (PlayerBase[0x28].ActiveFlag != '\0') {
            Net_GetLocalPlayerInfo((LocalPlayerInfo *)&PlayerBase[0x436].field_0x28e);
            Net_ResetLocalPlayerInfo(&PlayerBase[0x436].field_0x28e);
            *(int *)&PlayerBase->field_0x88 = *(int *)&PlayerBase->field_0x88 + 1;
            if (*(int *)&PlayerBase->field_0x78 < 1) {
              *(undefined4 *)&PlayerBase->field_0x78 = 0;
            }
            else {
              *(int *)&PlayerBase->field_0x78 = *(int *)&PlayerBase->field_0x78 + -1;
            }
            if ((int)PlayerBase->VelY < 1) {
              PlayerBase->VelY = 0.0;
            }
            else {
              PlayerBase->VelY = (float)((int)PlayerBase->VelY + -1);
            }
            if ((int)PlayerBase->VelZ < 1) {
              PlayerBase->VelZ = 0.0;
            }
            else {
              PlayerBase->VelZ = (float)((int)PlayerBase->VelZ + -1);
            }
            NetPlayerPtr = *(int *)&PlayerBase->field_0x80;
            if (NetPlayerPtr < 1) {
              if (NetPlayerPtr < 0) {
                *(undefined4 *)&PlayerBase->field_0x80 = 0;
              }
            }
            else {
              *(int *)&PlayerBase->field_0x80 = NetPlayerPtr + -1;
            }
            if ((*(int *)&PlayerBase[0x28].PlayerState == 8) &&
               (PlayerBase->MyBasePointer != (Player *)0x0)) {
              if ((*(byte *)&PlayerBase->MyBasePointer->StatusBitfield & 4) == 0) {
                *(undefined4 *)&PlayerBase[0x28].field_0x1d4 = 0;
              }
              else {
                *(int *)&PlayerBase[0x28].field_0x1d4 = *(int *)&PlayerBase[0x28].field_0x1d4 + 1;
              }
            }
            ptr = NetPlayerBase;
            if ((((Net_MPGame != 0) && (PlayerBase[0x28].Disconnecting == '\0')) &&
                (PlayerBase[0x438].PendingRespawnFlag == '\0')) &&
               ((*(int *)&PlayerBase[0x437].field_0x67 == 0 &&
                (0x168 < *(int *)&PlayerBase[0x28].field_0x1d4)))) {
              if (&stack0x00000000 != (undefined *)0x20c) {
                TimeZoneInfo(local_c);
                pcVar4 = (char *)LogCheatEvent(local_c);
                uVar7 = 0xffffffff;
                do {
                  pcVar10 = pcVar4;
                  if (uVar7 == 0) break;
                  uVar7 = uVar7 - 1;
                  pcVar10 = pcVar4 + 1;
                  cVar1 = *pcVar4;
                  pcVar4 = pcVar10;
                } while (cVar1 != '\0');
                uVar7 = ~uVar7;
                pcVar4 = pcVar10 + -uVar7;
                pcVar10 = (char *)String;
                for (uVar8 = uVar7 >> 2; uVar8 != 0; uVar8 = uVar8 - 1) {
                  *(undefined4 *)pcVar10 = *(undefined4 *)pcVar4;
                  pcVar4 = pcVar4 + 4;
                  pcVar10 = pcVar10 + 4;
                }
                for (uVar7 = uVar7 & 3; uVar7 != 0; uVar7 = uVar7 - 1) {
                  *pcVar10 = *pcVar4;
                  pcVar4 = pcVar4 + 1;
                  pcVar10 = pcVar10 + 1;
                }
                uVar7 = 0xffffffff;
                pcVar4 = (char *)String;
                do {
                  if (uVar7 == 0) break;
                  uVar7 = uVar7 - 1;
                  cVar1 = *pcVar4;
                  pcVar4 = pcVar4 + 1;
                } while (cVar1 != '\0');
                NetPlayerPtr = ~uVar7 - 1;
                if (0 < NetPlayerPtr) {
                  pcVar4 = acStack_20e + ~uVar7;
                  do {
                    iVar6 = LogCheatEvent((int)*pcVar4);
                    if (iVar6 != 0) break;
                    NetPlayerPtr = NetPlayerPtr + -1;
                    *pcVar4 = '\0';
                    pcVar4 = pcVar4 + -1;
                  } while (0 < NetPlayerPtr);
                }
              }
              if (PlayerBase == (Player *)0x0) {
                StringConverter(StringPointer,s__s____s____s_0063e6d8,String,&DAT_00626c08,
                                &lpCaption_0071e014);
              }
              else {
                StringConverter(StringPointer,s__s____2_2ld_____s_____s____s_0063e6e8,String,
                                PlayerBase->PosY,&PlayerBase->PosRoll);
              }
              if (CMD_PUNTTXT != 0) {
                WriteFile(s__PUNT_TXT_0063ba14,StringPointer);
              }
              NetPlayerPtr = NetPlayer;
              Net_PrepareLogEvent(NetPlayer,7,0,0);
              Net_WriteStatusMessage(NetPlayerPtr);
              ptr = NetPlayerBase;
            }
          }
          NetPlayer = NetPlayer + 1;
          PlayerBase = (Player *)&PlayerBase[0x440].SoundResourcePtr;
        } while (NetPlayer < *(int *)ptr);
      }
      Net_ResetGlobals();
    }
                    /* Broadcast Player Stats */
    if (Net_MPGame != 0) {
      if (0 < Net_BroadcastPlayerStatsTimer) {
        Net_BroadcastPlayerStatsTimer = Net_BroadcastPlayerStatsTimer + -1;
      }
      if (Net_BroadcastPlayerStatsTimer == 0) {
        Net_BroadcastPlayerStatsTimer = 310;
        PlayerBase = NetPlayerBase->PlayerPointer;
        NetPlayerPtr = 0;
        ptr = NetPlayerBase;
        if (0 < *(int *)NetPlayerBase) {
          do {
            if (((PlayerBase[0x28].ActiveFlag != '\0') &&
                (*(int *)&PlayerBase[0x28].PlayerState == 8)) &&
               (((PlayerBase[0x437].field_0x1a7 & 1) != 0 && (PlayerBase[0x437].PrevMovementX != 0))
               )) {
              Net_PacketSize = Net_SerializePlayerStats(&Net_PacketData,0x400,PlayerBase,1);
              TickBitMask = 5;
              ::NetPlayerPtr = PlayerBase;
              Net_HostQueueOutgoingPacket(0x4c,1,0xffffffff,&Net_PacketData,Net_PacketSize);
              PlayerBase[0x437].PrevMovementX = 0;
              ptr = NetPlayerBase;
            }
            NetPlayerPtr = NetPlayerPtr + 1;
            PlayerBase = (Player *)&PlayerBase[0x440].SoundResourcePtr;
          } while (NetPlayerPtr < *(int *)ptr);
        }
      }
      if (MissionComplete == 0) {
        if (0 < timer) {
          timer = timer + -1;
        }
        if (timer == 0) {
          timer = 62;
          _Net_PacketData = GetTickCount();
          nettimerrelated = 1;
          Net_PacketSize = 5;
          TickBitMask = 4;
          Net_HostQueueOutgoingPacket(0x54,1,0xffffffff,&Net_PacketData,5);
        }
        if (0 < nettimer) {
          nettimer = nettimer + -1;
        }
        if (nettimer == 0) {
          nettimer = 0x136;
          Net_PacketSize = Net_SetGenericTickType2(&Net_PacketData,0x400);
          TickBitMask = 4;
          Net_HostQueueOutgoingPacket(0x58,1,0xffffffff,&Net_PacketData,Net_PacketSize);
        }
      }
      if ((Net_MPGame != 0) &&
         ((((Engine_GameType == 2 || (Engine_GameType == 1)) || (Engine_GameType == 3)) ||
          (((Engine_GameType == 7 || (Engine_GameType == 5)) ||
           ((Engine_GameType == 6 || (Engine_GameType == 8)))))))) {
        if (0 < Engine_TickCounter) {
          Engine_TickCounter = Engine_TickCounter + -1;
        }
        if (Engine_TickCounter == 0) {
          Engine_TickCounter = 62;
          PlayerBase = NetPlayerBase->PlayerPointer;
          NetPlayerPtr = *(int *)NetPlayerBase;
          if (0 < NetPlayerPtr) {
            do {
              if (((PlayerBase[0x28].ActiveFlag != '\0') &&
                  (MyBase = PlayerBase->MyBasePointer, MyBase != (Player *)0x0)) &&
                 ((*(short *)&MyBase->Health < 1 &&
                  (((MyBase->StatusBitfield & 4) != 0 && ((MyBase->StatusBitfield & 0x18000) == 0)))
                  ))) {
                Net_PacketSize = Net_SetGenericTickType(&Net_PacketData,0x400,PlayerBase,8);
                TickBitMask = 0x14;
                Net_UniversalVariable = (uint)(byte)PlayerBase->field_0x90;
                Net_HostQueueOutgoingPacket(0x43,1,0,&Net_PacketData,Net_PacketSize);
              }
              PlayerBase = (Player *)&PlayerBase[0x440].SoundResourcePtr;
              NetPlayerPtr = NetPlayerPtr + -1;
            } while (NetPlayerPtr != 0);
          }
        }
      }
    }
    NetPlayerPointer = (Net_Player *)NetPlayerBase->PlayerPointer;
    NetPlayerPtr = 0;
    if (0 < *(int *)NetPlayerBase) {
      do {
        if (NetPlayerPointer->ActiveFlag == 0) {
          if ((int)NetPlayerPointer->ConnectedCounter < 0x7fffffff) {
            NetPlayerPointer->ConnectedCounter = NetPlayerPointer->ConnectedCounter + 1;
          }
        }
        else {
          if (*(short *)&NetPlayerPointer->UDPCounter != 0) {
            *(short *)&NetPlayerPointer->UDPCounter = *(short *)&NetPlayerPointer->UDPCounter + -1;
          }
          if (*(short *)&NetPlayerPointer->UDPCounter == 0) {
            *(word *)&NetPlayerPointer->UDPCounter = NetPlayerPointer->UDPThrottle;
            Net_SerializeMapData(NetPlayerPointer);
            Net_BuildAndSendOutgoingPacket(NetPlayerPointer);
            if (((NetPlayerPointer->ActiveFlag != 0) && (NetPlayerPointer->field_0x697c == '\0')) &&
               (NetPlayer = *(int *)&NetPlayerPointer->field_0x6970, NetPlayer != 0)) {
              do {
                iVar6 = *(int *)(NetPlayer + 8);
                if ((NetPlayerPointer->SequenceNum != 0) &&
                   (iVar5 = Net_SessionThrottle(NetPlayer,(uint)NetPlayerPointer->UDPThrottle),
                   iVar5 != 0)) {
                  Net_UnlinkAndResetNode(&NetPlayer);
                }
                NetPlayer = iVar6;
              } while (iVar6 != 0);
              NetPlayer = 0;
            }
          }
        }
        NetPlayerPtr = NetPlayerPtr + 1;
        NetPlayerPointer = (Net_Player *)&NetPlayerPointer->StrideLength;
      } while (NetPlayerPtr < *(int *)NetPlayerBase);
    }
    Net_SentPackets = Net_SentPackets + 1;
  }
  return;
}

