
void UpdateWorldObjects(uint LogicalTick)

{
  int iVar1;
  int EntityIndex;
  Object *BasePointer;
  int *piVar2;
  undefined4 *puVar3;
  uint IndexType;
  undefined4 *ptr;
  undefined4 *EffectPtr;
  DynamicObject *Base;
  code *CallPointer;
  Player *Entity;
  short StancePointer;
  
  if (CollisionTimer < 32) {
    CollisionTimer = CollisionTimer + 1;
  }
  else {
    EntityIndex = 0;
    CollisionTimer = 0;
    CollisionTimer = 0;
    Entity = OrganicBase;
    if (0 < Engine_MapOrganics) {
      do {
        if ((*(int *)Entity != 0) && ((*(byte *)&Entity->StatusBitfield & 2) == 0)) {
          CheckCollision(Entity);
        }
        EntityIndex = EntityIndex + 1;
        Entity = (Player *)((int)&Entity[1].PosX + 1);
      } while (EntityIndex < Engine_MapOrganics);
    }
    EntityIndex = 0;
    BasePointer = ObjectBase;
    if (0 < Engine_MapObjects) {
      do {
        if ((*(int *)BasePointer != 0) && ((*(byte *)&BasePointer->AliveState & 2) == 0)) {
          CheckCollision((Player *)BasePointer);
        }
        EntityIndex = EntityIndex + 1;
        BasePointer = BasePointer + 1;
      } while (EntityIndex < Engine_MapObjects);
    }
  }
  if ((PauseGame == 0) && (MyBase != (Player *)0x0)) {
    if (EnvironmentGate == 0) {
      EntityIndex = 0;
      Base = (DynamicObject *)ObjectBase;
      if (0 < Engine_MapObjects) {
        do {
          if (Base->TypeID != 0) {
            if ((*(int *)&Base->LifetimeCounter == 0) ||
               (iVar1 = *(int *)&Base->LifetimeCounter + -1, *(int *)&Base->LifetimeCounter = iVar1,
               iVar1 != 0)) {
              if ((int)Base->Timer < 1) {
                ComputePositionRelativeToBuilding(Base);
                if ((code *)Base->Pointer != (code *)0x0) {
                  (*(code *)Base->Pointer)(Base,0,0);
                }
              }
              CallPointer = *(code **)&Base->FunctionPointer3;
              if ((CallPointer != GenericReturn) && (CallPointer != (code *)0x0)) {
                (*CallPointer)(Base);
                if (*(int *)&Base->FunctionPointer2 != 0) {
                  RenderDynamicObject(Base);
                }
                if ((0 < Engine_ShadowQuality) && (*(short *)&Base->ShadowIndex != 0)) {
                  UpdateShadowPosition(*(short *)&Base->ShadowIndex,Base);
                }
              }
              Base->Timer = Base->Timer - 1;
            }
            else if (Net_Host != 0) {
              ObjectDespawner(Base);
            }
          }
          EntityIndex = EntityIndex + 1;
          Base = (DynamicObject *)&Base->StrideLength;
        } while (EntityIndex < Engine_MapObjects);
      }
      Handle3DSoundWrapper();
      HandleCinematicObjects();
      HandleDustEffects();
      SpawnedObjectUpdater();
      UpdateActiveProjectiles();
      IndexType = LogicalTick & 7;
      Base = (DynamicObject *)(BuildingBase + IndexType * 668);
      if ((int)IndexType < Engine_MapBuildings) {
        do {
          if (Base->TypeID != 0) {
            if ((int)Base->Timer < 1) {
              ComputePositionRelativeToBuilding(Base);
              if ((code *)Base->Pointer == (code *)0x0) {
                Base->Timer = 62;
              }
              else {
                (*(code *)Base->Pointer)(Base,0,0);
              }
            }
            else {
              Base->Timer = Base->Timer - 8;
            }
            if ((((Base->MetadataPtr != 0) &&
                 (CallPointer = *(code **)&Base->FunctionPointer3, CallPointer != GenericReturn)) &&
                (CallPointer != (code *)0x0)) &&
               ((*CallPointer)(Base), *(int *)&Base->FunctionPointer2 != 0)) {
              RenderDynamicObject(Base);
            }
          }
          IndexType = IndexType + 8;
          Base = (DynamicObject *)&Base[3].field_0x2e0;
        } while ((int)IndexType < Engine_MapBuildings);
      }
      EntityIndex = 0;
      ptr = &MarkerBase2;
      if (0 < Engine_MapMarkers) {
        do {
          Net_UpdateMapMarkersWrapper(ptr);
          EntityIndex = EntityIndex + 1;
          ptr = ptr + 6;
        } while (EntityIndex < Engine_MapMarkers);
      }
      LogicalTick = LogicalTick & 63;
      piVar2 = (int *)(MarkerBase + LogicalTick * 668);
      IndexType = LogicalTick;
      if ((int)LogicalTick < Engine_MapMarkers) {
        do {
          if (*piVar2 != 0) {
            if ((code *)piVar2[0x8b] != (code *)0x0) {
              if (piVar2[0x86] < 1) {
                (*(code *)piVar2[0x8b])(piVar2,0,0);
              }
              else {
                piVar2[0x86] = piVar2[0x86] + -0x40;
              }
            }
            if ((code *)piVar2[0x89] != (code *)0x0) {
              (*(code *)piVar2[0x89])(piVar2);
            }
          }
          IndexType = IndexType + 0x40;
          piVar2 = piVar2 + 0x29c0;
        } while ((int)IndexType < Engine_MapMarkers);
      }
      if (LogicalTick == 0) {
        LifetimeUpdater();
      }
    }
    else {
      EntityIndex = 0;
      if (0 < Engine_MapObjects) {
        ptr = (undefined4 *)&ObjectBase->field_0x54;
        do {
          puVar3 = ptr + -0x13;
          EffectPtr = ptr;
          for (iVar1 = 6; iVar1 != 0; iVar1 = iVar1 + -1) {
            *EffectPtr = *puVar3;
            puVar3 = puVar3 + 1;
            EffectPtr = EffectPtr + 1;
          }
          EntityIndex = EntityIndex + 1;
          ptr = ptr + 0xa7;
        } while (EntityIndex < Engine_MapObjects);
      }
    }
    EntityIndex = 0;
    Entity = OrganicBase;
    if (0 < Engine_MapOrganics) {
      do {
        if (((*(int *)((int)&Entity->SnapUpdateCallback + 1) == 0) ||
            (*(code **)&Entity->FunctionPointer == GenericReturn)) &&
           ((CallPointer = *(code **)&Entity->FunctionPointer, CallPointer != GenericReturn &&
            (CallPointer != (code *)0x0)))) {
          (*CallPointer)(Entity);
          if ((0 < Engine_ShadowQuality) && (*(short *)&Entity->ShadowParam != 0)) {
            UpdateShadowPosition(*(short *)&Entity->ShadowParam,Entity);
          }
          StancePointer = *(short *)((int)&Entity->PreviousStance + 1);
          if (StancePointer != 0) {
            CacheEntityPos(StancePointer,Entity);
          }
        }
        EntityIndex = EntityIndex + 1;
        Entity = (Player *)((int)&Entity[1].PosX + 1);
      } while (EntityIndex < Engine_MapOrganics);
    }
    UpdatePlayerPositionForSound();
    if (EnvironmentGate != 0) {
      EpilogueWrapper();
      return;
    }
    TickCounter = TickCounter + 1;
  }
  return;
}

