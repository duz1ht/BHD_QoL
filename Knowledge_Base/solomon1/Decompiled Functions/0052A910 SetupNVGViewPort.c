
void SetupNVGViewPort(void)

{
  undefined Flag [80];
  int Flag2;
  
  FUN_005a9210(Flag);
  NVGFlag = Flag2;
  if (Flag2 != 0) {
    NVG_Width = 512;
    NVG_Height = 512;
    FUN_004cce30();
    return;
  }
  NVG_Width = 512;
  NVG_Height = 256;
  FUN_004cce30();
  return;
}

