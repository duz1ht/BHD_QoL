
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void UpdateThirdPersonCamera(void)

{
  int DeltaX;
  int DeltaY;
  int DeltaZ;
  int Smoother;
  
  if (MyBase2 != (Player *)0x0) {
    DeltaX = (int)MyBase->PosX - (int)MyBase->LastPosX;
    DeltaY = (int)MyBase->PosY - (int)MyBase->LastPosY;
    DeltaZ = (int)MyBase->PosZ - (int)MyBase->LastPosZ;
    Smoother = CameraXDelta - CameraXSmoothed;
    CameraXDelta = DeltaX * 0x100;
    CameraXSmoothed = CameraXSmoothed + (Smoother + DeltaX * -0x100 + 0x10 >> 5);
    DeltaX = CameraYDelta - CameraYSmoothed;
    CameraYDelta = DeltaY * 0x100;
    CameraYSmoothed = CameraYSmoothed + (DeltaX + DeltaY * -0x100 + 0x10 >> 5);
    DeltaX = CameraZDelta - CameraZSmoothed;
    CameraZDelta = DeltaZ * 0x100;
    CameraZSmoothed = CameraZSmoothed + (DeltaX + DeltaZ * -0x100 + 0x10 >> 5);
    _Render_ScreenX = MyBase2->PosYaw;
    if (((byte)CameraInputFlags & 0x10) != 0) {
      CameraX = CameraX + -0x1000000;
    }
    if (((byte)CameraInputFlags & 0x40) != 0) {
      CameraX = CameraX + 0x1000000;
    }
    Render_3rdX = Render_3rdX +
                  ((((int)MyBase->HeadPosX + (int)MyBase2->PosX) - Render_3rdX) + 4 >> 2);
    Render_3rdY = Render_3rdY +
                  ((((int)MyBase->HeadPosY + (int)MyBase2->PosY) - Render_3rdY) + 4 >> 2);
    Render_3rdZ = Render_3rdZ +
                  ((((int)MyBase->HeadPosZ + (int)MyBase2->PosZ) - Render_3rdZ) + 4 >> 2);
  }
  return;
}

