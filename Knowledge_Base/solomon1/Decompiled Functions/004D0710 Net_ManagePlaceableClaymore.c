
/* WARNING: Unable to use type for symbol iVar4 */

void Net_ManagePlaceableClaymore(DynamicObject *BasePointer)

{
  longlong lVar1;
  longlong lVar2;
  DynamicObject *ObjectPointer;
  uint uVar3;
  int iVar5;
  dword *pdVar6;
  dword dVar7;
  Object *pOVar8;
  ushort uVar9;
  dword dVar10;
  int iVar11;
  dword dVar12;
  DynamicObject *pDVar13;
  dword *pdVar14;
  dword local_e0 [4];
  dword local_d0;
  dword local_cc;
  uint local_c8;
  uint local_c4;
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
  uint local_58;
  uint local_54;
  dword local_50;
  uint local_4c;
  dword local_48;
  undefined2 local_44;
  short local_42;
  undefined4 local_40;
  undefined4 local_3c;
  dword x;
  dword y;
  dword z;
  undefined4 local_2c;
  DynamicObject *local_28;
  dword local_24;
  int local_20;
  dword local_1c;
  int local_18;
  Object *local_14;
  Object *OldObject;
  dword local_c;
  dword local_8;
  int iVar4;
  
  ObjectPointer = BasePointer;
  iVar4 = ResourceBase + *(int *)&BasePointer->field_0x16c * 0xcc;
  local_1c = 0;
  local_24 = 0;
  local_14 = (Object *)0x0;
  OldObject = (Object *)0x0;
  if (BasePointer->OriginX != 0) {
    local_8 = *(int *)(iVar4 + 0x1c);
    lVar1 = (longlong)(int)local_8 * (longlong)(int)BasePointer->OriginX;
    uVar3 = (uint)lVar1 >> 0x10 | (int)((ulonglong)lVar1 >> 0x20) << 0x10;
    BasePointer->OriginX = uVar3;
    BasePointer->XPos = BasePointer->XPos + uVar3;
    if ((int)((uVar3 ^ (int)uVar3 >> 0x1f) - ((int)uVar3 >> 0x1f)) < 0x100) {
      BasePointer->OriginX = 0;
    }
  }
  if (BasePointer->OriginY != 0) {
    local_8 = *(int *)(iVar4 + 0x1c);
    lVar1 = (longlong)(int)local_8 * (longlong)(int)BasePointer->OriginY;
    uVar3 = (uint)lVar1 >> 0x10 | (int)((ulonglong)lVar1 >> 0x20) << 0x10;
    BasePointer->OriginY = uVar3;
    BasePointer->YPos = BasePointer->YPos + uVar3;
    if ((int)((uVar3 ^ (int)uVar3 >> 0x1f) - ((int)uVar3 >> 0x1f)) < 0x100) {
      BasePointer->OriginY = 0;
    }
  }
  local_18 = iVar4;
  if (*(int *)&BasePointer->field_0x13c != 0) {
    FUN_004d1320(BasePointer,0);
  }
  dVar10 = BasePointer->OriginZ - 0xa7;
  BasePointer->OriginZ = dVar10;
  BasePointer->ZPos = BasePointer->ZPos + dVar10;
  *(int *)&BasePointer->field_0x14 =
       *(int *)&BasePointer->field_0x14 - *(int *)&BasePointer->field_0x78;
  *(undefined4 *)&BasePointer->field_0x18 = 0;
  if ((int)dVar10 < 1) {
    local_58 = *(uint *)&BasePointer->field_0x58;
    local_54 = *(uint *)&BasePointer->field_0x5c;
    local_5c = *(dword *)&BasePointer->field_0x54;
    local_48 = local_54 - 0x50000;
    local_50 = local_5c;
    local_4c = local_58;
    FUN_004c3a30(BasePointer,&local_5c,&local_50);
    local_1c = local_48;
  }
  if ((int)BasePointer->ZPos <= ENV_WaterLevel) {
    if (ENV_WaterLevel < *(int *)&BasePointer->field_0x5c) {
      x = BasePointer->XPos;
      y = BasePointer->YPos;
      z = BasePointer->ZPos;
      local_40 = 0;
      local_3c = (Object *)0x0;
      local_28 = BasePointer;
      local_2c = -0x40000000;
      InitializeEntity(iVar4,0xb,&local_40);
    }
    if ((int)BasePointer->ZPos <= ENV_WaterLevel) {
      lVar1 = (longlong)(int)BasePointer->OriginX * 0x8000;
      BasePointer->OriginX = (uint)lVar1 >> 0x10 | (int)((ulonglong)lVar1 >> 0x20) << 0x10;
      lVar1 = (longlong)(int)BasePointer->OriginY * 0x8000;
      BasePointer->OriginY = (uint)lVar1 >> 0x10 | (int)((ulonglong)lVar1 >> 0x20) << 0x10;
      if ((int)BasePointer->OriginZ < -0xa7) {
        lVar1 = (longlong)(int)BasePointer->OriginZ * 0x8000;
        BasePointer->OriginZ = (uint)lVar1 >> 0x10 | (int)((ulonglong)lVar1 >> 0x20) << 0x10;
      }
    }
  }
  local_8 = BasePointer->OriginZ;
  local_c = BasePointer->OriginY;
  BasePointer = (DynamicObject *)
                (int)ROUND(SQRT((float10)(int)local_8 * (float10)(int)local_8 +
                                (float10)(int)local_c * (float10)(int)local_c +
                                (float10)(int)BasePointer->OriginX *
                                (float10)(int)BasePointer->OriginX));
  if ((int)BasePointer < 0x100) {
    BasePointer = (DynamicObject *)0x0;
  }
  if ((((*(uint *)&ObjectPointer->field_0xd8 & 0x200) == 0) &&
      ((ObjectPointer->field_0x20 & 0x10) == 0)) &&
     (dVar10 = TerrainHeightSampler(ObjectPointer->XPos,ObjectPointer->YPos), local_24 = dVar10,
     local_1c = dVar10, (int)ObjectPointer->ZPos < (int)dVar10)) {
    local_20 = FUN_005406c0(ObjectPointer->XPos,ObjectPointer->YPos);
    ObjectPointer->ZPos = dVar10;
    ObjectPointer->OriginX = 0;
    ObjectPointer->OriginY = 0;
    ObjectPointer->OriginZ = 0;
    *(undefined4 *)&ObjectPointer->field_0x13c = 0;
    BasePointer = (DynamicObject *)0x0;
    ObjectPointer->field_0x125 = ObjectPointer->field_0x125 + '\x01';
    OldObject = (Object *)0x1;
  }
  if (*(int *)&ObjectPointer->field_0x13c != 0) {
    FUN_0046d010(ObjectPointer);
  }
  pOVar8 = local_14;
  if (BasePointer == (DynamicObject *)0x0) {
LAB_004d0c09:
    iVar5 = local_20;
    if (OldObject == (Object *)0x0) goto LAB_004d0c66;
  }
  else {
    iVar5 = (int)(0x100000000 / (longlong)(int)BasePointer);
    lVar1 = (longlong)(int)ObjectPointer->OriginX * (longlong)iVar5;
    lVar2 = (longlong)(int)ObjectPointer->OriginY * (longlong)iVar5;
    local_58 = (uint)lVar2 >> 0x10 | (int)((ulonglong)lVar2 >> 0x20) << 0x10;
    local_c = ObjectPointer->OriginZ;
    local_54 = (uint)((longlong)(int)local_c * (longlong)iVar5) >> 0x10 |
               (int)((ulonglong)((longlong)(int)local_c * (longlong)iVar5) >> 0x20) << 0x10;
    pdVar6 = local_e0;
    for (iVar5 = 0x21; iVar5 != 0; iVar5 = iVar5 + -1) {
      *pdVar6 = 0;
      pdVar6 = pdVar6 + 1;
    }
    local_e0[0] = *(dword *)&ObjectPointer->field_0x54;
    local_e0[1] = *(dword *)&ObjectPointer->field_0x58;
    local_e0[2] = *(dword *)&ObjectPointer->field_0x5c;
    local_e0[3] = ObjectPointer->XPos;
    local_d0 = ObjectPointer->YPos;
    local_cc = ObjectPointer->ZPos;
    local_9c = ObjectPointer->OwnerPtr;
    local_c4 = local_58;
    local_c0 = local_54;
    local_bc = (dword)BasePointer;
    local_8 = (dword)BasePointer;
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
    iVar11 = -1;
    local_c8 = (uint)lVar1 >> 0x10 | (int)((ulonglong)lVar1 >> 0x20) << 0x10;
    iVar5 = FUN_0048f6c0(BuildingBase,Engine_MapBuildings,local_e0,1);
    pOVar8 = local_14;
    if ((iVar5 == 0) && ((int)local_6c < (int)BasePointer)) {
      local_8 = local_6c;
      iVar11 = 1;
      pOVar8 = (Object *)(BuildingBase + local_68 * 0x29c);
    }
    iVar5 = FUN_0048f6c0(ObjectBase,Engine_MapObjects,local_e0,1);
    if ((iVar5 == 0) && ((int)local_6c < (int)local_8)) {
      iVar11 = 2;
      pOVar8 = ObjectBase + local_68;
    }
    else if (iVar11 == -1) goto LAB_004d0c09;
    local_50 = *(dword *)&ObjectPointer->field_0x54;
    local_4c = *(uint *)&ObjectPointer->field_0x58;
    local_48 = *(dword *)&ObjectPointer->field_0x5c;
    if (iVar11 == 0) {
LAB_004d0bd6:
      iVar5 = local_88;
    }
    else {
      if (pOVar8 != (Object *)0x0) {
        pdVar6 = (dword *)FUN_0048eed0(&local_5c,ObjectPointer,local_e0,pOVar8);
        local_c = *pdVar6;
        OldObject = (Object *)pdVar6[1];
        local_54 = pdVar6[2];
        local_14 = (Object *)
                   (int)ROUND(SQRT((float10)(int)(((uint)((longlong)(int)OldObject *
                                                         (longlong)(int)OldObject) >> 0x10 |
                                                  (int)((ulonglong)
                                                        ((longlong)(int)OldObject *
                                                        (longlong)(int)OldObject) >> 0x20) << 0x10)
                                                 + ((uint)((longlong)(int)local_c *
                                                          (longlong)(int)local_c) >> 0x10 |
                                                   (int)((ulonglong)
                                                         ((longlong)(int)local_c *
                                                         (longlong)(int)local_c) >> 0x20) << 0x10)))
                             );
        *(undefined4 *)&ObjectPointer->field_0x1c = 0;
        *(undefined4 *)&ObjectPointer->field_0x7c = 0;
        *(undefined4 *)&ObjectPointer->field_0x78 = 0;
      }
      if (((iVar11 < 1) || (iVar11 < 3)) || (iVar5 = 7, iVar11 != 4)) goto LAB_004d0bd6;
    }
    ObjectPointer->XPos = local_50;
    ObjectPointer->YPos = local_4c;
    ObjectPointer->ZPos = local_48;
    *(Object **)&ObjectPointer->field_0x13c = pOVar8;
    ObjectPointer->field_0x125 = ObjectPointer->field_0x125 + '\x01';
  }
  x = ObjectPointer->XPos;
  y = ObjectPointer->YPos;
  z = ObjectPointer->ZPos;
  local_40 = 0;
  local_28 = ObjectPointer;
  local_2c = (((char)ObjectPointer->field_0x125 < '\x06') - 1 & 0x80000000) + 0x80000400;
  local_3c = pOVar8;
  if (iVar5 != 0) {
    InitializeEntity(local_18,iVar5 + 4,&local_40);
  }
LAB_004d0c66:
  dVar10 = ObjectPointer->ZPos;
  dVar7 = local_1c;
  dVar12 = local_24;
  if (((int)dVar10 < (int)local_1c) && ((int)ObjectPointer->OriginZ < 0)) {
    ObjectPointer->ZPos = local_1c;
    *(undefined4 *)&ObjectPointer->field_0x13c = 0;
  }
  else if ('\x05' < (char)ObjectPointer->field_0x125) {
    ObjectPointer->OriginZ = 0;
    ObjectPointer->OriginX = 0;
    ObjectPointer->OriginY = 0;
    dVar7 = dVar10;
    dVar12 = dVar10;
  }
  dVar10 = ObjectPointer->ZPos;
  if (((-1 < (int)(dVar10 - dVar7)) && ((int)(dVar10 - dVar7) < 0x100)) &&
     ((dVar12 == dVar7 ||
      ((BasePointer == (DynamicObject *)0x0 || ((int)ObjectPointer->OriginZ < 1)))))) {
    if (*(int *)&ObjectPointer->field_0x13c == 0) {
      if (dVar10 == dVar12) {
        ObjectPointer->ZPos = dVar10 + 0x1000;
        *(undefined4 *)&ObjectPointer->FunctionPointer3 = 0;
      }
    }
    else {
      *(code **)&ObjectPointer->FunctionPointer3 = FUN_0046d010;
    }
    *(undefined4 *)&ObjectPointer->field_0x7c = 0;
    *(undefined4 *)&ObjectPointer->field_0x78 = 0;
    ObjectPointer->OriginZ = 0;
    *(undefined4 *)&ObjectPointer->field_0x18 = 0;
    if (*(int *)&ObjectPointer->field_0x174 != 0) {
      if (Net_Host == 0) {
        *(uint *)&ObjectPointer->field_0xd8 = *(uint *)&ObjectPointer->field_0xd8 & 0xfffeffff;
        ObjectPointer->Timer = 0x7c;
      }
      else {
        pdVar6 = (dword *)SpawnDynamicObject(ObjectPointer);
        *(undefined4 *)&ObjectPointer->field_0x174 = 0;
        if (ObjectPointer->MetadataPtr != 0) {
          pDVar13 = ObjectPointer;
          pdVar14 = pdVar6;
          for (iVar5 = 0xa7; iVar5 != 0; iVar5 = iVar5 + -1) {
            *pdVar14 = pDVar13->TypeID;
            pDVar13 = (DynamicObject *)&pDVar13->Unknown;
            pdVar14 = pdVar14 + 1;
          }
          FUN_00415740(pdVar6);
        }
        ObjectPointer->Timer = 1;
        *(uint *)&ObjectPointer->field_0xd8 = *(uint *)&ObjectPointer->field_0xd8 & 0xfffeffff;
        if (pdVar6[0x4f] == 0) {
          pdVar6[0x89] = 0;
        }
        else {
          pdVar6[0x89] = (dword)FUN_0046d010;
        }
        uVar9 = (short)((ulonglong)(uint)((int)pdVar6 - (int)OrganicBase) * 0x621b97c3 >> 0x28) + 1;
        local_44 = *(undefined2 *)pdVar6;
        local_40 = CONCAT22(*(undefined2 *)(local_18 + 0x14),*(undefined2 *)(local_18 + 0x10));
        local_42 = (short)((ulonglong)(pdVar6[0x4d] - (int)OrganicBase) * 0x621b97c3 >> 0x28) + 1;
        x = ObjectPointer->XPos;
        y = ObjectPointer->YPos;
        z = ObjectPointer->ZPos;
        local_2c = CONCAT22((short)((uint)*(undefined4 *)&ObjectPointer->field_0x18 >> 0x10),
                            (short)((uint)*(undefined4 *)&ObjectPointer->field_0x14 >> 0x10));
        local_28 = (DynamicObject *)
                   CONCAT22(local_28._2_2_,
                            (short)((uint)*(undefined4 *)&ObjectPointer->field_0x1c >> 0x10));
        if (pdVar6[0x4f] == 0) {
          local_3c = (Object *)(uint)uVar9;
        }
        else {
          local_3c = (Object *)
                     CONCAT22((short)((ulonglong)(pdVar6[0x4f] - (int)OrganicBase) * 0x621b97c3 >>
                                     0x28) + 1,uVar9);
        }
        TickBitMask = 0xc;
        Net_HostQueueOutgoingPacket(0x56,1,0xffffffff,&local_44,0x20);
        local_20 = *(int *)(ObjectPointer->MetadataPtr + 0x30);
        iVar5 = 0;
        BasePointer = (DynamicObject *)0x0;
        OldObject = (Object *)0x0;
        if (0 < Engine_MapObjects) {
          local_18 = Engine_MapObjects;
          pOVar8 = ObjectBase;
          do {
            if (((*(int *)pOVar8 != 0) && (pOVar8->OwnerPtr == ObjectPointer->OwnerPtr)) &&
               (*(int *)(pOVar8->MetadataPtr + 0x30) == local_20)) {
              BasePointer = (DynamicObject *)((int)BasePointer + 1);
              if (*(int *)&pOVar8->field_0x218 < iVar5) {
                iVar5 = *(int *)&pOVar8->field_0x218;
                OldObject = pOVar8;
              }
            }
            pOVar8 = pOVar8 + 1;
            local_18 = local_18 + -1;
          } while (local_18 != 0);
                    /* Limit the amount of claymores */
          if (2 < (int)BasePointer) {
            ObjectDespawner(OldObject);
          }
        }
        ObjectPointer->OriginX = 0;
        ObjectPointer->OriginY = 0;
        ObjectPointer->OriginZ = 0;
        pdVar6[0x1b] = 0;
        pdVar6[0x1c] = 0;
        pdVar6[0x1d] = 0;
      }
    }
  }
  if (*(int *)&ObjectPointer->field_0x12c == 0) {
    if ((ObjectPointer->MetadataPtr != 0) &&
       (iVar5 = *(int *)(ObjectPointer->MetadataPtr + 0x184), iVar5 != 0)) {
      VectorTransformer(&ObjectPointer->field_0x88,&ObjectPointer->XPos,iVar5);
      pdVar6 = &ObjectPointer->XPos;
      pdVar14 = (dword *)&ObjectPointer->field_0x54;
      for (iVar5 = 6; iVar5 != 0; iVar5 = iVar5 + -1) {
        *pdVar14 = *pdVar6;
        pdVar6 = pdVar6 + 1;
        pdVar14 = pdVar14 + 1;
      }
      return;
    }
    TransformGeometry(&ObjectPointer->field_0x88,&ObjectPointer->XPos);
    pdVar6 = &ObjectPointer->XPos;
    pdVar14 = (dword *)&ObjectPointer->field_0x54;
    for (iVar5 = 6; iVar5 != 0; iVar5 = iVar5 + -1) {
      *pdVar14 = *pdVar6;
      pdVar6 = pdVar6 + 1;
      pdVar14 = pdVar14 + 1;
    }
    return;
  }
  VectorTransformer(&ObjectPointer->field_0x88,&ObjectPointer->XPos,
                    *(int *)&ObjectPointer->field_0x12c);
  pdVar6 = &ObjectPointer->XPos;
  pdVar14 = (dword *)&ObjectPointer->field_0x54;
  for (iVar5 = 6; iVar5 != 0; iVar5 = iVar5 + -1) {
    *pdVar14 = *pdVar6;
    pdVar6 = pdVar6 + 1;
    pdVar14 = pdVar14 + 1;
  }
  return;
}

