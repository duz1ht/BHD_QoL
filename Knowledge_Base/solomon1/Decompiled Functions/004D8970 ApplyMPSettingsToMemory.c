
bool ApplyMPSettingsToMemory(int Base)

{
  int Pointer;
  
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_red_password_00647668);
  if (Pointer == 0) {
    _strncpy(&MP_RedPassword,*(char **)(Base + 0xc),0x10);
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_blue_password_00647658);
  if (Pointer == 0) {
    _strncpy(&MP_BluePassword,*(char **)(Base + 0xc),0x10);
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_yellow_password_00647648);
  if (Pointer == 0) {
    _strncpy(&MP_YellowPassword,*(char **)(Base + 0xc),0x10);
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_violet_password_00647638);
  if (Pointer == 0) {
    _strncpy(&MP_VioletPassword,*(char **)(Base + 0xc),0x10);
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_mp_verbose_0064762c);
  if (Pointer == 0) {
    SYS_Verbose = NumberParserWrapper(*(undefined4 *)(Base + 0xc));
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_mp_NoCharAbilities_00647618);
  if (Pointer == 0) {
    MP_NoCharacterAbilities = NumberParserWrapper(*(undefined4 *)(Base + 0xc));
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_mp_NoCrossHairSpread_00647600);
  if (Pointer == 0) {
    MP_NoCrosshairSpread = NumberParserWrapper(*(undefined4 *)(Base + 0xc));
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_mp_NoScopeDrift_006475f0);
  if (Pointer == 0) {
    MP_NoScopeDrift = NumberParserWrapper(*(undefined4 *)(Base + 0xc));
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_mp_NoWeaponRecoil_006475dc);
  if (Pointer == 0) {
    MP_NoRecoil = NumberParserWrapper(*(undefined4 *)(Base + 0xc));
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_mp_gpsicons_006475d0);
  if (Pointer == 0) {
    MP_GPSIcons = NumberParserWrapper(*(undefined4 *)(Base + 0xc));
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_mp_wind_006475c8);
  if (Pointer == 0) {
    MP_EnvironmentWind = NumberParserWrapper(*(undefined4 *)(Base + 0xc));
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_mp_DroppedWeaponDisappear_006475ac);
  if (Pointer == 0) {
    MP_DroppedWPNDisappear = NumberParserWrapper(*(undefined4 *)(Base + 0xc));
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_mp_NoDropWeapons_00647598);
  if (Pointer == 0) {
    MP_NoDropWeapons = NumberParserWrapper(*(undefined4 *)(Base + 0xc));
    return true;
  }
  Pointer = CompareString(*(undefined4 *)(Base + 4),s_mp_NoRespawnWithPrimary_00647580);
  if (Pointer == 0) {
    MP_NoRespawnPrimary = NumberParserWrapper(*(undefined4 *)(Base + 0xc));
  }
  return Pointer == 0;
}

