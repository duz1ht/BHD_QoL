
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void Net_HostSetNetDelay(uint Input)

{
  uint uVar1;
  char *flagValue;
  undefined dataBuffer [100];
  char charFlag;
  
  if ((((Net_MPGame != 0) && (Net_Host != 0)) && (-1 < (int)Input)) && ((int)Input < 101)) {
    _Net_TickDelay = Input;
    TickBitMask = 12;
    _Net_PacketData = Input;
    Net_PacketSize = 4;
    Net_HostQueueOutgoingPacket(0x16,1,0xffffffff,&Net_PacketData,4);
    StringConverter(dataBuffer,s_Delay_set_to__ldms_0063f588,(int)(Net_TickRate * Input * 1000) / 62
                   );
    uVar1 = _Net_PacketData;
    TickBitMask = 12;
    _Net_PacketData = _Net_PacketData & 0xffffff00;
    if (&stack0x00000000 == (undefined *)0x68) {
      _Net_PacketData = uVar1 & 0xffff0000;
    }
    else {
      CopyCString(0x9e47b1,dataBuffer,0x3ff);
    }
    uVar1 = 0xffffffff;
    flagValue = (char *)((int)&Net_PacketData + 1);
    do {
      if (uVar1 == 0) break;
      uVar1 = uVar1 - 1;
      charFlag = *flagValue;
      flagValue = flagValue + 1;
    } while (charFlag != '\0');
    Net_PacketSize = ~uVar1 + 1;
    Net_HostQueueOutgoingPacket(0xf,1,0xffffffff,&Net_PacketData,Net_PacketSize);
  }
  return;
}

