
void Net_ManagePlaceableSatchelCharge(DynamicObject *BasePtr)

{
  ObjectResource *pOVar1;
  longlong lVar2;
  longlong lVar3;
  DynamicObject *ObjectPointer;
  uint uVar4;
  dword dVar5;
  int iVar6;
  dword *pdVar7;
  undefined4 uVar8;
  uint uVar9;
  Object *Base;
  ushort uVar10;
  uint uVar11;
  DynamicObject *pDVar12;
  int iVar13;
  dword *pdVar14;
  undefined local_ec [12];
  dword local_e0 [4];
  dword local_d0;
  dword local_cc;
  uint local_c8;
  Object *local_c4;
  uint local_c0;
  dword local_bc;
  dword local_b4;
  dword local_b0;
  dword local_ac;
  dword local_a8;
  dword local_a4;
  dword local_a0;
  dword local_9c;
  int local_88;
  dword local_6c;
  int local_68;
  dword local_5c;
  Object *local_58;
  dword local_54;
  undefined2 local_50;
  short local_4e;
  undefined4 local_4c;
  undefined4 local_48;
  dword local_44;
  dword local_40;
  dword local_3c;
  undefined4 local_38;
  DynamicObject *local_34;
  Object *local_30;
  uint local_2c;
  int local_28;
  uint local_24;
  dword local_20;
  Object *local_1c;
  uint local_18;
  ObjectResource *ObjResource;
  Object *EntityPointer;
  dword local_c;
  dword local_8;
  
  ObjectPointer = BasePtr;
  pOVar1 = (ObjectResource *)(ResourceBase + *(int *)&BasePtr->field_0x16c * 0xcc);
  local_24 = 0;
  local_2c = 0;
  local_30 = (Object *)0x0;
  EntityPointer = (Object *)0x0;
  if (BasePtr->OriginX != 0) {
    local_8 = pOVar1->Velocity;
    lVar2 = (longlong)(int)local_8 * (longlong)(int)BasePtr->OriginX;
    uVar4 = (uint)lVar2 >> 0x10 | (int)((ulonglong)lVar2 >> 0x20) << 0x10;
    BasePtr->OriginX = uVar4;
    BasePtr->XPos = BasePtr->XPos + uVar4;
    if ((int)((uVar4 ^ (int)uVar4 >> 0x1f) - ((int)uVar4 >> 0x1f)) < 0x100) {
      BasePtr->OriginX = 0;
    }
  }
  if (BasePtr->OriginY != 0) {
    local_8 = pOVar1->Velocity;
    lVar2 = (longlong)(int)local_8 * (longlong)(int)BasePtr->OriginY;
    uVar4 = (uint)lVar2 >> 0x10 | (int)((ulonglong)lVar2 >> 0x20) << 0x10;
    BasePtr->OriginY = uVar4;
    BasePtr->YPos = BasePtr->YPos + uVar4;
    if ((int)((uVar4 ^ (int)uVar4 >> 0x1f) - ((int)uVar4 >> 0x1f)) < 0x100) {
      BasePtr->OriginY = 0;
    }
  }
  ObjResource = pOVar1;
  if (*(int *)&BasePtr->field_0x13c != 0) {
    FUN_004d1320(BasePtr,1);
  }
  if (ENV_WaterLevel < (int)BasePtr->ZPos) {
    dVar5 = BasePtr->OriginZ - 0x37;
  }
  else {
    dVar5 = BasePtr->OriginZ - 0xa7;
  }
  BasePtr->ZPos = BasePtr->ZPos + dVar5;
  *(int *)&BasePtr->field_0x14 = *(int *)&BasePtr->field_0x14 - *(int *)&BasePtr->field_0x78;
  BasePtr->OriginZ = dVar5;
  *(int *)&BasePtr->field_0x18 = *(int *)&BasePtr->field_0x18 - *(int *)&BasePtr->field_0x7c;
  if ((int)dVar5 < 1) {
    local_58 = *(Object **)&BasePtr->field_0x58;
    local_54 = *(dword *)&BasePtr->field_0x5c;
    local_5c = *(dword *)&BasePtr->field_0x54;
    local_18 = local_54 - 0x50000;
    local_20 = local_5c;
    local_1c = local_58;
    FUN_004c3a30(BasePtr,&local_5c,&local_20);
    local_24 = local_18;
  }
  if ((int)BasePtr->ZPos <= ENV_WaterLevel) {
    if (ENV_WaterLevel < *(int *)&BasePtr->field_0x5c) {
      local_44 = BasePtr->XPos;
      local_40 = BasePtr->YPos;
      local_3c = BasePtr->ZPos;
      local_4c = 0;
      local_48 = (Object *)0x0;
      local_34 = BasePtr;
      local_38 = -0x40000000;
      InitializeEntity(pOVar1,0xb,&local_4c);
    }
    if ((int)BasePtr->ZPos <= ENV_WaterLevel) {
      lVar2 = (longlong)*(int *)&BasePtr->field_0x78 * 0xfae1;
      *(uint *)&BasePtr->field_0x78 = (uint)lVar2 >> 0x10 | (int)((ulonglong)lVar2 >> 0x20) << 0x10;
      lVar2 = (longlong)*(int *)&BasePtr->field_0x7c * 0xfae1;
      *(uint *)&BasePtr->field_0x7c = (uint)lVar2 >> 0x10 | (int)((ulonglong)lVar2 >> 0x20) << 0x10;
      lVar2 = (longlong)*(int *)&BasePtr->field_0x80 * 0xfae1;
      *(uint *)&BasePtr->field_0x80 = (uint)lVar2 >> 0x10 | (int)((ulonglong)lVar2 >> 0x20) << 0x10;
      lVar2 = (longlong)(int)BasePtr->OriginX * 0xf0a3;
      BasePtr->OriginX = (uint)lVar2 >> 0x10 | (int)((ulonglong)lVar2 >> 0x20) << 0x10;
      lVar2 = (longlong)(int)BasePtr->OriginY * 0xf0a3;
      BasePtr->OriginY = (uint)lVar2 >> 0x10 | (int)((ulonglong)lVar2 >> 0x20) << 0x10;
      lVar2 = (longlong)(int)BasePtr->OriginZ * 0xf0a3;
      BasePtr->OriginZ = (uint)lVar2 >> 0x10 | (int)((ulonglong)lVar2 >> 0x20) << 0x10;
    }
  }
  local_8 = BasePtr->OriginZ;
  local_c = BasePtr->OriginY;
  BasePtr = (DynamicObject *)
            (int)ROUND(SQRT((float10)(int)local_8 * (float10)(int)local_8 +
                            (float10)(int)local_c * (float10)(int)local_c +
                            (float10)(int)BasePtr->OriginX * (float10)(int)BasePtr->OriginX));
  if ((int)BasePtr < 0x100) {
    BasePtr = (DynamicObject *)0x0;
  }
  if (((*(uint *)&ObjectPointer->field_0xd8 & 0x200) == 0) &&
     ((ObjectPointer->field_0x20 & 0x10) == 0)) {
    uVar4 = TerrainHeightSampler(ObjectPointer->XPos,ObjectPointer->YPos);
    if ((int)local_24 < (int)uVar4) {
      local_24 = uVar4;
    }
    local_2c = uVar4;
    if ((int)ObjectPointer->ZPos < (int)uVar4) {
      local_28 = FUN_005406c0(ObjectPointer->XPos,ObjectPointer->YPos);
      ObjectPointer->ZPos = uVar4;
      ObjectPointer->OriginX = 0;
      ObjectPointer->OriginY = 0;
      ObjectPointer->OriginZ = 0;
      *(undefined4 *)&ObjectPointer->field_0x13c = 0;
      BasePtr = (DynamicObject *)0x0;
      ObjectPointer->field_0x125 = ObjectPointer->field_0x125 + '\x01';
      EntityPointer = (Object *)0x1;
    }
  }
  if (*(int *)&ObjectPointer->field_0x13c != 0) {
    FUN_0046d010(ObjectPointer);
  }
  Base = local_30;
  if (BasePtr == (DynamicObject *)0x0) {
LAB_004d1b37:
    local_88 = local_28;
    if (EntityPointer == (Object *)0x0) goto LAB_004d1b94;
  }
  else {
    iVar6 = (int)(0x100000000 / (longlong)(int)BasePtr);
    lVar2 = (longlong)(int)ObjectPointer->OriginX * (longlong)iVar6;
    lVar3 = (longlong)(int)ObjectPointer->OriginY * (longlong)iVar6;
    local_1c = (Object *)((uint)lVar3 >> 0x10 | (int)((ulonglong)lVar3 >> 0x20) << 0x10);
    local_c = ObjectPointer->OriginZ;
    local_18 = (uint)((longlong)(int)local_c * (longlong)iVar6) >> 0x10 |
               (int)((ulonglong)((longlong)(int)local_c * (longlong)iVar6) >> 0x20) << 0x10;
    pdVar7 = local_e0;
    for (iVar6 = 0x21; iVar6 != 0; iVar6 = iVar6 + -1) {
      *pdVar7 = 0;
      pdVar7 = pdVar7 + 1;
    }
    local_e0[0] = *(dword *)&ObjectPointer->field_0x54;
    local_e0[1] = *(dword *)&ObjectPointer->field_0x58;
    local_e0[2] = *(dword *)&ObjectPointer->field_0x5c;
    local_e0[3] = ObjectPointer->XPos;
    local_d0 = ObjectPointer->YPos;
    local_cc = ObjectPointer->ZPos;
    local_9c = ObjectPointer->OwnerPtr;
    local_c4 = local_1c;
    local_c0 = local_18;
    local_bc = (dword)BasePtr;
    local_8 = (dword)BasePtr;
    local_b4 = local_e0[3];
    local_a8 = local_e0[0];
    if ((int)local_e0[0] < (int)local_e0[3]) {
      local_b4 = local_e0[0];
      local_a8 = local_e0[3];
    }
    local_b0 = local_d0;
    local_a4 = local_e0[1];
    if ((int)local_e0[1] < (int)local_d0) {
      local_b0 = local_e0[1];
      local_a4 = local_d0;
    }
    local_ac = local_cc;
    local_a0 = local_e0[2];
    if ((int)local_e0[2] < (int)local_cc) {
      local_ac = local_e0[2];
      local_a0 = local_cc;
    }
    iVar13 = -1;
    local_c8 = (uint)lVar2 >> 0x10 | (int)((ulonglong)lVar2 >> 0x20) << 0x10;
    iVar6 = FUN_0048f6c0(BuildingBase,Engine_MapBuildings,local_e0,1);
    Base = local_30;
    if ((iVar6 == 0) && ((int)local_6c < (int)BasePtr)) {
      local_8 = local_6c;
      iVar13 = 1;
      Base = (Object *)(BuildingBase + local_68 * 0x29c);
    }
    iVar6 = FUN_0048f6c0(ObjectBase,Engine_MapObjects,local_e0,1);
    if ((iVar6 == 0) && ((int)local_6c < (int)local_8)) {
      iVar13 = 2;
      Base = ObjectBase + local_68;
    }
    else if (iVar13 == -1) goto LAB_004d1b37;
    local_20 = *(dword *)&ObjectPointer->field_0x54;
    local_1c = *(Object **)&ObjectPointer->field_0x58;
    local_18 = *(uint *)&ObjectPointer->field_0x5c;
    if ((iVar13 != 0) && (Base != (Object *)0x0)) {
      pdVar7 = (dword *)FUN_0048eed0(local_ec,ObjectPointer,local_e0,Base);
      local_5c = *pdVar7;
      local_58 = (Object *)pdVar7[1];
      local_54 = pdVar7[2];
      local_8 = (dword)ROUND(SQRT((float10)(int)(((uint)((longlong)(int)local_58 *
                                                        (longlong)(int)local_58) >> 0x10 |
                                                 (int)((ulonglong)
                                                       ((longlong)(int)local_58 *
                                                       (longlong)(int)local_58) >> 0x20) << 0x10) +
                                                ((uint)((longlong)(int)local_5c *
                                                       (longlong)(int)local_5c) >> 0x10 |
                                                (int)((ulonglong)
                                                      ((longlong)(int)local_5c *
                                                      (longlong)(int)local_5c) >> 0x20) << 0x10))));
      fpatan((float10)(int)local_58,(float10)(int)local_5c);
      EntityPointer = local_58;
      local_c = local_5c;
      uVar8 = __ftol();
      *(undefined4 *)&ObjectPointer->field_0x14 = uVar8;
      local_c = local_8 << 8;
      fpatan((float10)(int)local_54,(float10)(int)local_c);
      uVar8 = __ftol();
      *(undefined4 *)&ObjectPointer->field_0x18 = uVar8;
      *(undefined4 *)&ObjectPointer->field_0x1c = 0;
      *(undefined4 *)&ObjectPointer->field_0x7c = 0;
      *(undefined4 *)&ObjectPointer->field_0x78 = 0;
    }
    switch(iVar13) {
    default:
      break;
    case 3:
      local_88 = ((Base != (Object *)MyBase) - 1 & 0xffffffeb) + 0x13;
      break;
    case 4:
      local_88 = 7;
    }
    ObjectPointer->XPos = local_20;
    ObjectPointer->YPos = (dword)local_1c;
    ObjectPointer->ZPos = local_18;
    *(Object **)&ObjectPointer->field_0x13c = Base;
    ObjectPointer->field_0x125 = ObjectPointer->field_0x125 + '\x01';
  }
  local_44 = ObjectPointer->XPos;
  local_40 = ObjectPointer->YPos;
  local_3c = ObjectPointer->ZPos;
  local_4c = 0;
  local_34 = ObjectPointer;
  local_38 = (((char)ObjectPointer->field_0x125 < '\x06') - 1 & 0x80000000) + 0x80000400;
  local_48 = Base;
  if (local_88 != 0) {
    InitializeEntity(ObjResource,local_88 + 4,&local_4c);
  }
LAB_004d1b94:
  uVar4 = ObjectPointer->ZPos;
  uVar9 = local_24;
  uVar11 = local_2c;
  if (((int)uVar4 < (int)local_24) && ((int)ObjectPointer->OriginZ < 0)) {
    ObjectPointer->ZPos = local_24;
    *(undefined4 *)&ObjectPointer->field_0x13c = 0;
  }
  else if ('\x05' < (char)ObjectPointer->field_0x125) {
    ObjectPointer->OriginZ = 0;
    ObjectPointer->OriginX = 0;
    ObjectPointer->OriginY = 0;
    uVar9 = uVar4;
    uVar11 = uVar4;
  }
  uVar4 = ObjectPointer->ZPos;
  if (((-1 < (int)(uVar4 - uVar9)) && ((int)(uVar4 - uVar9) < 0x100)) &&
     ((uVar11 == uVar9 || ((BasePtr == (DynamicObject *)0x0 || ((int)ObjectPointer->OriginZ < 1)))))
     ) {
    if (*(int *)&ObjectPointer->field_0x13c == 0) {
      if (uVar4 == uVar11) {
        ObjectPointer->ZPos = uVar4 + 0x1000;
        *(undefined4 *)&ObjectPointer->field_0x18 = 0xc0000040;
        *(undefined4 *)&ObjectPointer->FunctionPointer3 = 0;
      }
    }
    else {
      *(code **)&ObjectPointer->FunctionPointer3 = FUN_0046d010;
    }
    *(undefined4 *)&ObjectPointer->field_0x7c = 0;
    *(undefined4 *)&ObjectPointer->field_0x78 = 0;
    ObjectPointer->OriginZ = 0;
    if (*(int *)&ObjectPointer->field_0x174 != 0) {
      if (Net_Host == 0) {
        *(uint *)&ObjectPointer->field_0xd8 = *(uint *)&ObjectPointer->field_0xd8 & 0xfffeffff;
        ObjectPointer->Timer = 0x7c;
      }
      else {
        pdVar7 = (dword *)SpawnDynamicObject(ObjectPointer);
        *(undefined4 *)&ObjectPointer->field_0x174 = 0;
        if (ObjectPointer->MetadataPtr != 0) {
          pDVar12 = ObjectPointer;
          pdVar14 = pdVar7;
          for (iVar6 = 0xa7; iVar6 != 0; iVar6 = iVar6 + -1) {
            *pdVar14 = pDVar12->TypeID;
            pDVar12 = (DynamicObject *)&pDVar12->Unknown;
            pdVar14 = pdVar14 + 1;
          }
          FUN_00415740(pdVar7);
        }
        ObjectPointer->Timer = 1;
        *(uint *)&ObjectPointer->field_0xd8 = *(uint *)&ObjectPointer->field_0xd8 & 0xfffeffff;
        if (pdVar7[0x4f] == 0) {
          pdVar7[0x89] = 0;
        }
        else {
          pdVar7[0x89] = (dword)FUN_0046d010;
        }
        uVar10 = (short)((ulonglong)(uint)((int)pdVar7 - (int)OrganicBase) * 0x621b97c3 >> 0x28) + 1
        ;
        local_50 = *(undefined2 *)pdVar7;
        local_4c = CONCAT22(*(undefined2 *)&ObjResource->field_0x14,*(undefined2 *)&ObjResource->ID)
        ;
        local_4e = (short)((ulonglong)(pdVar7[0x4d] - (int)OrganicBase) * 0x621b97c3 >> 0x28) + 1;
        local_44 = ObjectPointer->XPos;
        local_40 = ObjectPointer->YPos;
        local_3c = ObjectPointer->ZPos;
        local_38 = CONCAT22((short)((uint)*(undefined4 *)&ObjectPointer->field_0x18 >> 0x10),
                            (short)((uint)*(undefined4 *)&ObjectPointer->field_0x14 >> 0x10));
        local_34 = (DynamicObject *)
                   CONCAT22(local_34._2_2_,
                            (short)((uint)*(undefined4 *)&ObjectPointer->field_0x1c >> 0x10));
        if (pdVar7[0x4f] == 0) {
          local_48 = (Object *)(uint)uVar10;
        }
        else {
          local_48 = (Object *)
                     CONCAT22((short)((ulonglong)(pdVar7[0x4f] - (int)OrganicBase) * 0x621b97c3 >>
                                     0x28) + 1,uVar10);
        }
        TickBitMask = 0xc;
        Net_HostQueueOutgoingPacket(0x56,1,0xffffffff,&local_50,0x20);
        local_28 = *(int *)(ObjectPointer->MetadataPtr + 0x30);
        iVar6 = 0;
        BasePtr = (DynamicObject *)0x0;
        EntityPointer = (Object *)0x0;
        if (0 < (int)Engine_MapObjects) {
          ObjResource = Engine_MapObjects;
          Base = ObjectBase;
          do {
            if (((*(int *)Base != 0) && (Base->OwnerPtr == ObjectPointer->OwnerPtr)) &&
               (*(int *)(Base->MetadataPtr + 0x30) == local_28)) {
              BasePtr = (DynamicObject *)((int)BasePtr + 1);
              if (*(int *)&Base->field_0x218 < iVar6) {
                iVar6 = *(int *)&Base->field_0x218;
                EntityPointer = Base;
              }
            }
            Base = Base + 1;
            ObjResource = (ObjectResource *)&ObjResource[-1].field_0x5a;
          } while (ObjResource != (ObjectResource *)0x0);
                    /* Limit Satchel Charges to 8 per player. */
          if (8 < (int)BasePtr) {
            ObjectDespawner(EntityPointer);
          }
        }
        ObjectPointer->OriginX = 0;
        ObjectPointer->OriginY = 0;
        ObjectPointer->OriginZ = 0;
        pdVar7[0x1b] = 0;
        pdVar7[0x1c] = 0;
        pdVar7[0x1d] = 0;
      }
    }
  }
  if (*(int *)&ObjectPointer->field_0x12c != 0) {
    VectorTransformer(&ObjectPointer->field_0x88,&ObjectPointer->XPos,
                      *(int *)&ObjectPointer->field_0x12c);
    pdVar7 = &ObjectPointer->XPos;
    pdVar14 = (dword *)&ObjectPointer->field_0x54;
    for (iVar6 = 6; iVar6 != 0; iVar6 = iVar6 + -1) {
      *pdVar14 = *pdVar7;
      pdVar7 = pdVar7 + 1;
      pdVar14 = pdVar14 + 1;
    }
    return;
  }
  if ((ObjectPointer->MetadataPtr != 0) &&
     (iVar6 = *(int *)(ObjectPointer->MetadataPtr + 0x184), iVar6 != 0)) {
    VectorTransformer(&ObjectPointer->field_0x88,&ObjectPointer->XPos,iVar6);
    pdVar7 = &ObjectPointer->XPos;
    pdVar14 = (dword *)&ObjectPointer->field_0x54;
    for (iVar6 = 6; iVar6 != 0; iVar6 = iVar6 + -1) {
      *pdVar14 = *pdVar7;
      pdVar7 = pdVar7 + 1;
      pdVar14 = pdVar14 + 1;
    }
    return;
  }
  TransformGeometry(&ObjectPointer->field_0x88,&ObjectPointer->XPos);
  pdVar7 = &ObjectPointer->XPos;
  pdVar14 = (dword *)&ObjectPointer->field_0x54;
  for (iVar6 = 6; iVar6 != 0; iVar6 = iVar6 + -1) {
    *pdVar14 = *pdVar7;
    pdVar7 = pdVar7 + 1;
    pdVar14 = pdVar14 + 1;
  }
  return;
}

