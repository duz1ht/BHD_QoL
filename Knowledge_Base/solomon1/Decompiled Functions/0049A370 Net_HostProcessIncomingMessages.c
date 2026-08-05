
void __cdecl Net_HostProcessIncomingMessages(Net_Player *NetPlayer,char *PlayerMessage)

{
  DWORD TimePointer;
  uint uVar1;
  char *pcVar2;
  int Counter;
  undefined RawMessage [1024];
  undefined Formattedmessage [1024];
  undefined2 *Data;
  undefined4 Flag;
  undefined4 *Size;
  int Address;
  char MessageType;
  Net_Player *NetPlayerPointer;
  Player *PlayerPointer;
  
  if (((NetPlayer->Status != 8) && (NetPlayer->Status != 9)) && (NetPlayer->ActiveFlag2 == 0)) {
    return;
  }
  TimePointer = GetTickCount();
  if (TimePointer - *(int *)&NetPlayer->MessageTimestamp < 1000) {
    return;
  }
  *(DWORD *)&NetPlayer->MessageTimestamp = TimePointer;
  MessageType = *PlayerMessage;
  ExtractandFormatChatMessage(NetPlayer,PlayerMessage + 1,RawMessage,0x400,Formattedmessage,0x400);
  if (MessageType == '\x02') {
    PlayerPointer = NetPlayerBase->PlayerPointer;
    Counter = 0;
    NetPlayerPointer = NetPlayerBase;
    if (0 < *(int *)NetPlayerBase) {
      do {
        if (((PlayerPointer[0x28].ActiveFlag != '\0') &&
            ((*(int *)&PlayerPointer[0x28].PlayerState == 8 ||
             (*(int *)&PlayerPointer[0x28].PlayerState == 9)))) &&
           (((*(byte *)((int)&PlayerPointer[0x28].EmplacedYaw + *(int *)&NetPlayer->field_0xc) & 1)
             == 0 && ((*(char *)((int)&PlayerPointer->MyBasePointer->TeamID + 2) ==
                       *(char *)((int)&NetPlayer->MyBasePtr->TeamID + 2) ||
                      (PlayerPointer[0x438].field_0x1e1 != '\0')))))) {
          TickBitMask = 1;
          Net_PacketData._0_1_ = '\x02';
          NetPlayerPtr = PlayerPointer;
          if (&stack0x00000000 == (undefined *)0x414) {
            Net_PacketData._1_1_ = 0;
          }
          else {
            CopyCString(0x9e47b1,Formattedmessage,0x3ff);
          }
          uVar1 = 0xffffffff;
          pcVar2 = (char *)((int)&Net_PacketData + 1);
          do {
            if (uVar1 == 0) break;
            uVar1 = uVar1 - 1;
            MessageType = *pcVar2;
            pcVar2 = pcVar2 + 1;
          } while (MessageType != '\0');
          Net_PacketSize = ~uVar1 + 1;
                    /* Team */
          Net_HostQueueOutgoingPacket(0xf,1,0x50,&Net_PacketData,Net_PacketSize);
          NetPlayerPointer = NetPlayerBase;
        }
        Counter = Counter + 1;
        PlayerPointer = (Player *)&PlayerPointer[0x440].SoundResourcePtr;
      } while (Counter < *(int *)NetPlayerPointer);
    }
  }
  else {
    if (MessageType != '\x04') {
      if (MessageType == '\x05') {
        PlayerPointer = NetPlayerBase->PlayerPointer;
        Counter = 0;
        NetPlayerPointer = NetPlayerBase;
        if (0 < *(int *)NetPlayerBase) {
          do {
            if ((PlayerPointer[0x28].ActiveFlag != '\0') &&
               (((*(int *)&PlayerPointer[0x28].PlayerState == 8 ||
                 (*(int *)&PlayerPointer[0x28].PlayerState == 9)) &&
                (*(char *)((int)&PlayerPointer->MyBasePointer->TeamID + 2) == '\x01')))) {
              TickBitMask = 1;
              Net_PacketData._0_1_ = '\x02';
              NetPlayerPtr = PlayerPointer;
              if (&stack0x00000000 == (undefined *)0x414) {
                Net_PacketData._1_1_ = 0;
              }
              else {
                CopyCString(0x9e47b1,Formattedmessage,0x3ff);
              }
              uVar1 = 0xffffffff;
              pcVar2 = (char *)((int)&Net_PacketData + 1);
              do {
                if (uVar1 == 0) break;
                uVar1 = uVar1 - 1;
                MessageType = *pcVar2;
                pcVar2 = pcVar2 + 1;
              } while (MessageType != '\0');
              Net_PacketSize = ~uVar1 + 1;
              Net_HostQueueOutgoingPacket(0xf,1,0x50,&Net_PacketData,Net_PacketSize);
              NetPlayerPointer = NetPlayerBase;
            }
            Counter = Counter + 1;
            PlayerPointer = (Player *)&PlayerPointer[0x440].SoundResourcePtr;
          } while (Counter < *(int *)NetPlayerPointer);
        }
        if (Net_ListenServer != 0) {
          return;
        }
        TickBitMask = 0x40;
        Data = &Net_PacketData;
        Flag = 0x400;
        Size = &Net_PacketSize;
        Address = 0x9e47b1;
        Net_PacketData._0_1_ = '\x02';
        WriteStringToBuffer(&Data,Formattedmessage);
      }
      else if (MessageType == '\a') {
        if (NetPlayer->field_0xade09 == '\0') {
          return;
        }
        PlayerPointer = NetPlayerBase->PlayerPointer;
        Counter = 0;
        NetPlayerPointer = NetPlayerBase;
        if (0 < *(int *)NetPlayerBase) {
          do {
            if (((PlayerPointer[0x28].ActiveFlag != '\0') &&
                ((*(int *)&PlayerPointer[0x28].PlayerState == 8 ||
                 (*(int *)&PlayerPointer[0x28].PlayerState == 9)))) &&
               ((*(byte *)((int)&PlayerPointer[0x28].EmplacedYaw + *(int *)&NetPlayer->field_0xc) &
                1) == 0)) {
              TickBitMask = 1;
              Data = &Net_PacketData;
              Flag = 0x400;
              Size = &Net_PacketSize;
              Address = 0x9e47b1;
              Net_PacketData._0_1_ = '\a';
              NetPlayerPtr = PlayerPointer;
              WriteStringToBuffer(&Data,RawMessage);
              Net_PacketSize = Address - (int)Data;
              Net_HostQueueOutgoingPacket(0xf,1,0x50,&Net_PacketData,Net_PacketSize);
              NetPlayerPointer = NetPlayerBase;
            }
            Counter = Counter + 1;
            PlayerPointer = (Player *)&PlayerPointer[0x440].SoundResourcePtr;
          } while (Counter < *(int *)NetPlayerPointer);
        }
        if (Net_ListenServer != 0) {
          return;
        }
        TickBitMask = 0x40;
        Data = &Net_PacketData;
        Flag = 0x400;
        Size = &Net_PacketSize;
        Address = 0x9e47b1;
        Net_PacketData._0_1_ = '\a';
        WriteStringToBuffer(&Data,RawMessage);
      }
      else {
        PlayerPointer = NetPlayerBase->PlayerPointer;
        Counter = 0;
        NetPlayerPointer = NetPlayerBase;
        if (0 < *(int *)NetPlayerBase) {
          do {
            if (((PlayerPointer[0x28].ActiveFlag != '\0') &&
                ((*(int *)&PlayerPointer[0x28].PlayerState == 8 ||
                 (*(int *)&PlayerPointer[0x28].PlayerState == 9)))) &&
               ((*(byte *)((int)&PlayerPointer[0x28].EmplacedYaw + *(int *)&NetPlayer->field_0xc) &
                1) == 0)) {
              TickBitMask = 1;
              Data = &Net_PacketData;
              Flag = 0x400;
              Size = &Net_PacketSize;
              Address = 0x9e47b1;
              NetPlayerPtr = PlayerPointer;
              Net_PacketData._0_1_ = MessageType;
              WriteStringToBuffer(&Data,Formattedmessage);
              Net_PacketSize = Address - (int)Data;
                    /* All */
              Net_HostQueueOutgoingPacket(0xf,1,0x50,&Net_PacketData,Net_PacketSize);
              NetPlayerPointer = NetPlayerBase;
            }
            Counter = Counter + 1;
            PlayerPointer = (Player *)&PlayerPointer[0x440].SoundResourcePtr;
          } while (Counter < *(int *)NetPlayerPointer);
        }
        if (Net_ListenServer != 0) {
          return;
        }
        TickBitMask = 0x40;
        Data = &Net_PacketData;
        Flag = 0x400;
        Size = &Net_PacketSize;
        Address = 0x9e47b1;
        Net_PacketData._0_1_ = MessageType;
        WriteStringToBuffer(&Data,Formattedmessage);
      }
      Net_PacketSize = Address - (int)Data;
                    /* Team */
      Net_HostQueueOutgoingPacket(0xf,1,0x50,&Net_PacketData,Net_PacketSize);
      return;
    }
    PlayerPointer = NetPlayerBase->PlayerPointer;
    Counter = 0;
    NetPlayerPointer = NetPlayerBase;
    if (0 < *(int *)NetPlayerBase) {
      do {
        if (((PlayerPointer[0x28].ActiveFlag != '\0') &&
            ((*(int *)&PlayerPointer[0x28].PlayerState == 8 ||
             (*(int *)&PlayerPointer[0x28].PlayerState == 9)))) &&
           (*(char *)((int)&PlayerPointer->MyBasePointer->TeamID + 2) == '\x02')) {
          TickBitMask = 1;
          Net_PacketData._0_1_ = '\x02';
          NetPlayerPtr = PlayerPointer;
          if (&stack0x00000000 == (undefined *)0x414) {
            Net_PacketData._1_1_ = 0;
          }
          else {
            CopyCString(0x9e47b1,Formattedmessage,0x3ff);
          }
          uVar1 = 0xffffffff;
          pcVar2 = (char *)((int)&Net_PacketData + 1);
          do {
            if (uVar1 == 0) break;
            uVar1 = uVar1 - 1;
            MessageType = *pcVar2;
            pcVar2 = pcVar2 + 1;
          } while (MessageType != '\0');
          Net_PacketSize = ~uVar1 + 1;
          Net_HostQueueOutgoingPacket(0xf,1,0x50,&Net_PacketData,Net_PacketSize);
          NetPlayerPointer = NetPlayerBase;
        }
        Counter = Counter + 1;
        PlayerPointer = (Player *)&PlayerPointer[0x440].SoundResourcePtr;
      } while (Counter < *(int *)NetPlayerPointer);
    }
  }
  if (Net_ListenServer != 0) {
    return;
  }
  TickBitMask = 0x40;
  Net_PacketData._0_1_ = 2;
  if (&stack0x00000000 == (undefined *)0x414) {
    Net_PacketData._1_1_ = 0;
  }
  else {
    CopyCString(0x9e47b1,Formattedmessage,0x3ff);
  }
  uVar1 = 0xffffffff;
  pcVar2 = (char *)((int)&Net_PacketData + 1);
  do {
    if (uVar1 == 0) break;
    uVar1 = uVar1 - 1;
    MessageType = *pcVar2;
    pcVar2 = pcVar2 + 1;
  } while (MessageType != '\0');
  Net_PacketSize = ~uVar1 + 1;
  Net_HostQueueOutgoingPacket(0xf,1,0x50,&Net_PacketData,Net_PacketSize);
  return;
}

