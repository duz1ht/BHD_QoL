
void __cdecl Net_SerializeEntities(Net_Player *NetPlayer)

{
  int Ptr;
  uint X;
  uint Y;
  uint Z;
  undefined4 *PtrCounter;
  uint XPosPtr;
  uint YPosPtr;
  uint ZPosPtr;
  int *piVar1;
  Object *EntityPointer;
  undefined2 *PacketPointer;
  Player *BasePointer;
  int Cnter;
  undefined4 *puVar2;
  undefined4 local_814 [256];
  undefined4 local_414 [256];
  uint local_14;
  Object *local_10;
  int StrideCounter;
  int EntityCounter;
  Player *MyBase;
  
  if (NetPlayer->Status != 8) {
    return;
  }
  Ptr = Engine_MapObjects + Engine_MapOrganics;
  if (Net_InterestQueue != (int *)0x0) {
    if (*Net_InterestQueue < Ptr) {
      Net_FreeEntitySyncQueue(&Net_InterestQueue);
    }
    if (Net_InterestQueue != (int *)0x0) {
      Net_AllocateEntityQueue();
      goto LAB_004af28c;
    }
  }
  Net_InterestQueue = (int *)Net_AllocateEntityQueue(Ptr);
  if (Net_InterestQueue == (int *)0x0) {
    return;
  }
LAB_004af28c:
  MyBase = NetPlayer->MyBasePtr;
  if (MyBase != (Player *)0x0) {
    local_10 = *(Object **)&MyBase->StandingWorldObject;
    EntityCounter = 0;
    if (0 < Engine_MapOrganics) {
      StrideCounter = 0;
      do {
        BasePointer = (Player *)(&OrganicBase->field_0x0 + StrideCounter);
        if (((*(byte *)&BasePointer->StatusBitfield & 1) == 0) &&
           (BasePointer->MetadataPtr != (MetaData *)0x0)) {
          X = (int)MyBase->PosX - (int)BasePointer->PosX;
          XPosPtr = (int)X >> 0x1f;
          Y = (int)MyBase->PosY - (int)BasePointer->PosY;
          YPosPtr = (int)Y >> 0x1f;
          Z = (int)MyBase->PosZ - (int)BasePointer->PosZ;
          ZPosPtr = (int)Z >> 0x1f;
          Ptr = GetDistance((X ^ XPosPtr) - XPosPtr,(Y ^ YPosPtr) - YPosPtr,(Z ^ ZPosPtr) - ZPosPtr)
          ;
          Net_AppendNetworkMessage(Net_InterestQueue,Ptr >> 16,EntityCounter,BasePointer,0);
        }
        EntityCounter = EntityCounter + 1;
        StrideCounter = StrideCounter + 668;
      } while (EntityCounter < Engine_MapOrganics);
    }
    EntityCounter = 0;
    if (0 < Engine_MapObjects) {
      StrideCounter = 0;
      do {
        EntityPointer = (Object *)(&ObjectBase->field_0x0 + StrideCounter);
        if (EntityPointer == local_10) {
          Ptr = 0;
        }
        else {
          X = (int)MyBase->PosX - (int)EntityPointer->PosX;
          XPosPtr = (int)X >> 0x1f;
          Y = (int)MyBase->PosY - (int)EntityPointer->PosY;
          YPosPtr = (int)Y >> 0x1f;
          Z = (int)MyBase->PosZ - (int)EntityPointer->PosZ;
          ZPosPtr = (int)Z >> 0x1f;
          Ptr = GetDistance((X ^ XPosPtr) - XPosPtr,(Y ^ YPosPtr) - YPosPtr,(Z ^ ZPosPtr) - ZPosPtr)
          ;
          Ptr = Ptr >> 0x10;
        }
        Cnter = EntityCounter;
        if (EntityPointer->MetadataPtr != 0) {
          Net_AppendNetworkMessage(Net_InterestQueue,Ptr,EntityCounter,EntityPointer,1);
        }
        EntityCounter = Cnter + 1;
        StrideCounter = StrideCounter + 668;
      } while (EntityCounter < Engine_MapObjects);
    }
    Ptr = *(int *)&NetPlayer->EntityPriority;
    if (Net_InterestQueue[2] <= Ptr) {
      Ptr = 0;
    }
    PtrCounter = (undefined4 *)(Net_InterestQueue[1] + Ptr * 0x10);
    Cnter = 0;
    if (0 < Net_InterestQueue[2]) {
      do {
        if (4 < Cnter) break;
        *PtrCounter = 0;
        Ptr = Ptr + 1;
        if (Ptr < Net_InterestQueue[2]) {
          PtrCounter = PtrCounter + 4;
        }
        else {
          PtrCounter = (undefined4 *)Net_InterestQueue[1];
          Ptr = 0;
        }
        Cnter = Cnter + 1;
      } while (Cnter < Net_InterestQueue[2]);
    }
    *(int *)&NetPlayer->EntityPriority = Ptr;
    Net_ShellSortElements(Net_InterestQueue);
    EntityCounter = 0;
    StrideCounter = 0;
    if (0 < Net_InterestQueue[2]) {
      piVar1 = (int *)(Net_InterestQueue[1] + 0xc);
      do {
        Ptr = piVar1[-1];
        if (*piVar1 == 0) {
          if (*(int *)(*(int *)(Ptr + 0x24) + 0x13c) != 0) {
            PacketPointer = &Net_PacketData;
            Ptr = Net_SerializeEntity(Ptr,local_414,0x400,0xb,&local_10);
            if ((-1 < Ptr) && ((int)local_10 < 0x401)) {
              PtrCounter = local_414;
              puVar2 = (undefined4 *)&Net_PacketData;
              for (X = (uint)local_10 >> 2; X != 0; X = X - 1) {
                *puVar2 = *PtrCounter;
                PtrCounter = PtrCounter + 1;
                puVar2 = puVar2 + 1;
              }
              for (X = (uint)local_10 & 3; X != 0; X = X - 1) {
                *(undefined *)puVar2 = *(undefined *)PtrCounter;
                PtrCounter = (undefined4 *)((int)PtrCounter + 1);
                puVar2 = (undefined4 *)((int)puVar2 + 1);
              }
              PacketPointer = (undefined2 *)&local_10[0x3ca8].field_0x150;
            }
joined_r0x004af536:
            Net_PacketSize = PacketPointer + -0x4f23d8;
            if (0 < (int)Net_PacketSize) {
              TickBitMask = 1;
              NetPlayerPtr = NetPlayer;
              Net_HostQueueOutgoingPacket(0x12,0,0,&Net_PacketData,Net_PacketSize);
            }
          }
        }
        else if ((*piVar1 == 1) && (*(int *)(*(int *)(Ptr + 0x24) + 0x13c) != 0)) {
          PacketPointer = &Net_PacketData;
          Ptr = Net_SerializeEntity(Ptr,local_814,0x400,0xb,&local_14);
          if ((-1 < Ptr) && ((int)local_14 < 0x401)) {
            PtrCounter = local_814;
            puVar2 = (undefined4 *)&Net_PacketData;
            for (X = local_14 >> 2; X != 0; X = X - 1) {
              *puVar2 = *PtrCounter;
              PtrCounter = PtrCounter + 1;
              puVar2 = puVar2 + 1;
            }
            for (X = local_14 & 3; X != 0; X = X - 1) {
              *(undefined *)puVar2 = *(undefined *)PtrCounter;
              PtrCounter = (undefined4 *)((int)PtrCounter + 1);
              puVar2 = (undefined4 *)((int)puVar2 + 1);
            }
            PacketPointer = (undefined2 *)((int)&Net_PacketData + local_14);
          }
          goto joined_r0x004af536;
        }
        StrideCounter = StrideCounter + 1;
        if (7 < StrideCounter) {
          return;
        }
        EntityCounter = EntityCounter + 1;
        piVar1 = piVar1 + 4;
      } while (EntityCounter < Net_InterestQueue[2]);
    }
  }
  return;
}

