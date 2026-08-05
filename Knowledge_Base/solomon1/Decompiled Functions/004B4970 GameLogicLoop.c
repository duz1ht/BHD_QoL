
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* Loop handles mouse movement, keyboard input, map objects, camera orbit, client and server-sided
   networking */

undefined4 GameLogicLoop(void)

{
  int Pointer;
  
                    /* Handle Mouse Input */
  if (((GameWindowFlag == 0) || (GameInputSystem == 0)) || (GameRunning == 0)) {
    if (((InputReadyFlag != 0) && (GameInputSystem != 0)) && (GameRunning != 0)) {
      MouseDeltaX = MouseXRemainder;
      MouseDeltaY = MouseYRemainder;
    }
  }
  else {
    PeekMessageWrapper();
    Pointer = ReturnLockMouse();
    if (Pointer == 0) {
      PollMouseInput();
      MouseDeltaX = (RawMouseX * 4) / MouseSensitivity;
      MouseDeltaY = (RawMouseY * 4) / MouseSensitivity;
      MouseXRemainder = RawMouseX * 4 - (MouseSensitivity + -1) * MouseDeltaX;
      MouseYRemainder = RawMouseY * 4 - (MouseSensitivity + -1) * MouseDeltaY;
    }
    else {
      MouseDeltaX = 0;
      MouseXRemainder = 0;
      MouseDeltaY = 0;
      MouseYRemainder = 0;
    }
  }
  RawMouseX = PreMouseX + MouseDeltaX;
  RawMouseY = PreMouseY + MouseDeltaY;
  PreMouseX = MouseDeltaX;
  PreMouseY = MouseDeltaY;
  Pointer = ReturnGameLoopState();
  if (Pointer != 0) {
    return 1;
  }
  Net_ProcessDirectPlayTable();
  if (PauseGame == 0) {
    AdvanceGamePhysics(FrameID);
    LogicalTick = LogicalTick + 1;
    FrameID = 0;
    if (LastUnpauseTick == 0) {
      LastUnpauseTick = GetTickCount();
    }
  }
                    /* Update the camera death position every tick so it spins around your corpse
                       then increment the timer. */
  UpdateCameraOrbit();
  if (((Net_Host != 0) && (PauseGame == 0)) &&
     ((EnvironmentGate == 0 &&
      ((Pointer = Host_ReturnStartDelay(), Pointer == 0 && (0 < Net_TimeLeft)))))) {
    Net_TimeLeft = Net_TimeLeft + -1;
  }
  PeekMessageWrapper();
                    /* Handle user input first, then send your inputs to the server. If you're the
                       host then handle all clients and send snapshots to your peers. */
  Pointer = ReturnLockMouse();
  if (Pointer == 0) {
    PlayerInput(LogicalTick);
  }
  Net_ProcessClientState();
  ProcessPendingSounds();
  VoiceoverCooldownTimer();
  Net_InterpolateClientTimer();
  if ((Net_Host != 0) && (PauseGame == 0)) {
    Net_ProcessServerState();
  }
                    /* Get Start Delay from 00425830 and test it as well as missioncomplete so we
                       know if we need to freeze the game and all map objects and players. If we
                       aren't pausing the game then update map objects and contintue.
                        */
  Pointer = Net_GetStartDelay();
  if ((Pointer == 0) && ((Net_MPGame == 0 || (MissionComplete == 0)))) {
    UpdateWorldObjects(LogicalTick);
    MapEntityManager();
  }
  PostUpdateEntities();
                    /* Spawn FX around the player like blood, casings, etc. */
  if (MyBase != (Player *)0x0) {
    UpdateFXAroundPlayer(MyBase->PosX,MyBase->PosY,MyBase->PosZ);
  }
                    /* Prepare the next set of packets with our xyz, yaw, etc. */
  if (Net_ClientTickCounter == Net_ClientWaitTicks) {
    Net_QueueFullPosition();
  }
  else {
    Net_QueueAdditionalPosition();
  }
                    /* Lock the camera to your player as you move through 3D space */
  if (MyBase2 != (Player *)0x0) {
    UpdateThirdPersonCamera();
  }
                    /* Update Environment, Game World, Clouds, Fog */
  if (PauseGame == 0) {
    TickPlayerControlState();
    UpdateEnvironmentLighting(LogicalTick);
    MissionScriptLoop();
  }
  ChatFadeLogic();
  Pointer = FrameCounter + 1;
  if (61 < FrameCounter + 1) {
    CurrentFramerate = FrameAccumulator;
    _FrameTimeDelta = CurrentTimeTicks - LastFrameTimestamp;
    FrameCounter = FrameCounter + -61;
    FrameAccumulator = 0;
    LastFrameTimestamp = CurrentTimeTicks;
    FramerateAccumulation(&LastFrameCountSnapshot);
    Pointer = FrameCounter;
  }
  FrameCounter = Pointer;
  if (InputReadyFlag != 0) {
    UpdateAudioSourcesForFrame(LogicalTick);
    TickEnvironmentEffects(LogicalTick);
  }
  GameTick = GameTick + 1;
  if (GameState != 8) {
    if (GameState == 4) {
      if (Net_MPGame == 0) {
        HUD_SPStatistics = 0;
        GameTick = 0;
        InitializeMapResources();
        UpdateMusicStream();
        Pointer = 0;
        do {
          ClearEntitySlot(&OrganicBase->field_0x0 + Pointer);
          Pointer = Pointer + 0x29c;
        } while (Pointer < 0x29c00);
        Pointer = 0;
        do {
          ClearEntitySlot(&ObjectBase->field_0x0 + Pointer);
          Pointer = Pointer + 0x29c;
        } while (Pointer < 0xc3b40);
        Pointer = 0;
        do {
          ClearEntitySlot(Pointer + BuildingBase);
          Pointer = Pointer + 0x29c;
        } while (Pointer < 0xc3b40);
        ResetMapMemoryPools();
        FUN_0055c820();
        if (Net_Host != 0) {
          Net_HandleMapEvents();
        }
        _Engine_PNPFlag1 = Engine_PNPFlag1;
        _Engine_Resolution = Engine_ResolutionCheck;
        if ((Net_MPGame == 0) && (DAT_009f72e0 == 1)) {
          DAT_009f2468 = DAT_009f3744;
        }
        StatUpdater();
        Net_ResetGameWorld();
        LoadingScreen(1);
        return 0;
      }
      if (Net_Host == 0) goto SetGameLoop;
    }
    if (SetGameLoopFlag != 0) {
      SetCurrentGameLoop(0);
      return 1;
    }
    if (GameState == 0) {
      return 0;
    }
    SetCurrentGameLoop(s_Post_Menu_00648a78);
    return 1;
  }
SetGameLoop:
  SetCurrentGameLoop(LoopSelection);
  return 1;
}

