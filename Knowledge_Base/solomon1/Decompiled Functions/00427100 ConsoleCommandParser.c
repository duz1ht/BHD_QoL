
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined4 ConsoleCommandParser(char *CommandString)

{
  char cVar1;
  int StringCheck;
  int Pointer;
  undefined4 uVar2;
  undefined PacketType;
  undefined Argument [2048];
  undefined StringBuffer [512];
  undefined4 InputCMD;
  undefined4 Character;
  char *cmd;
  
  cmd = CommandString;
  if (CommandString == (char *)0x0) {
    return 0;
  }
  Pointer = CommandSanityCheck(CommandString,Argument,&InputCMD);
  if (Pointer < 1) {
    return 0;
  }
  StringCheck = CompareInputWithString(InputCMD,STR_WhoIs);
  if (StringCheck == 0) {
    StringCheck = CompareInputWithString(InputCMD,Str_ClientDebugMSGList);
    if (StringCheck != 0) {
      ClientDebugMsgList = (uint)(ClientDebugMsgList == 0);
      if (ClientDebugMsgList != 0) {
        PushStatusMessage(STR_ClientDebugMsgListEnabled,HUD_ConsoleTextColor,0x3a2);
        return 1;
      }
      PushStatusMessage(STR_ClientDebugMsgListDisabled,HUD_ConsoleTextColor,0x3a2);
      return 1;
    }
    StringCheck = CompareInputWithString(InputCMD,s_UNITHELP_006249f0);
    if ((((((StringCheck != 0) ||
           (StringCheck = CompareInputWithString(InputCMD,s_SHOWUNIT_006249e4), StringCheck != 0))
          || (StringCheck = CompareInputWithString(InputCMD,s_SHOWUNITS_006249d8), StringCheck != 0)
          ) || ((StringCheck = CompareInputWithString(InputCMD,s_CREATEUNIT_006249cc),
                StringCheck != 0 ||
                (StringCheck = CompareInputWithString(InputCMD,s_JOINUNIT_006249c0),
                StringCheck != 0)))) ||
        ((StringCheck = CompareInputWithString(InputCMD,s_LEAVEUNIT_006249b4), StringCheck != 0 ||
         ((StringCheck = CompareInputWithString(InputCMD,s_GRANTUNITENTRY_006249a4),
          StringCheck != 0 ||
          (StringCheck = CompareInputWithString(InputCMD,s_DISBANUNIT_00624998), StringCheck != 0)))
         ))) || (StringCheck = CompareInputWithString(InputCMD,s_UNITCHAT_0062498c),
                StringCheck != 0)) goto NoMatch;
    StringCheck = CompareInputWithString(InputCMD,s_DIRTYUPSTREAM_0062497c);
    if (StringCheck != 0) {
      if (Pointer < 2) {
        StringConverter(StringBuffer,STR_DirtyUpstreamHelp);
        PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,930);
        return 1;
      }
      Pointer = CompareInputWithString(Character,&Premode);
      if (Pointer != 0) {
        StringConverter(StringBuffer,STR_DirtyUpstreamPreMode);
        PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,930);
        Net_DirtyUpstreamPost = 0;
        return 1;
      }
      Pointer = CompareInputWithString(Character,&PostMode);
      if (Pointer != 0) {
        StringConverter(StringBuffer,STR_DirtyUpstreamPostMode);
        PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,930);
        Net_DirtyUpstreamPost = 1;
        return 1;
      }
      Net_DirtyUpstream = NumberParser(Character);
      if (Net_DirtyUpstream < 0) {
        Net_DirtyUpstream = 0;
LAB_00427446:
        StringConverter(StringBuffer,STR_DirtyUpstreamToggledOff);
      }
      else {
        if (Net_DirtyUpstream < 6) {
          if (Net_DirtyUpstream == 0) goto LAB_00427446;
        }
        else {
          Net_DirtyUpstream = 5;
        }
        StringConverter(StringBuffer,STR_DirtyUpstreamSetTo,Net_DirtyUpstream);
      }
      PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,930);
      return 1;
    }
    StringCheck = CompareInputWithString(InputCMD,&DAT_006248c4);
    if (StringCheck != 0) {
      if ((Net_MPGame == 0) || (Net_ServiceType != 1)) {
        StringConverter(StringBuffer,STR_BANWorksNWOnly);
        PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
        return 1;
      }
      if (Net_Host == 0) {
        if (Pointer < 2) {
          StringConverter(StringBuffer,STR_BANHelp);
          PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
          return 1;
        }
        uVar2 = NumberParser(Character);
        Pointer = PlayerLookup(Net_PlayerLimitPointer,uVar2);
        if ((Pointer == 0) || (*(char *)(Pointer + 0x10) == '\0')) {
          cmd = STR_PlayerNameNotFound;
        }
        else {
          if (**(char **)(Pointer + 0x1c) != '\0') {
            StringCheck = FUN_00486340(DAT_009ed5d4,*(char **)(Pointer + 0x1c));
            if (StringCheck != 0) {
              StringConverter(StringBuffer,s_Player___ld___s__already_in_ban_l_0062485c,uVar2,
                              *(undefined4 *)(Pointer + 0x14));
              PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
              return 1;
            }
            StringCheck = FUN_004861c0(DAT_009ed5d4,*(undefined4 *)(Pointer + 0x1c),
                                       *(undefined4 *)(Pointer + 0x14));
            if (StringCheck != 0) {
              FindBanListTxt(s_banlist_txt_00624850);
            }
            StringConverter(StringBuffer,s_Added_player___ld___s__to_ban_li_0062482c,uVar2,
                            *(undefined4 *)(Pointer + 0x14));
            PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
            return 1;
          }
          cmd = s_Player___ld_s_PCID_is_not_known__00624884;
        }
        StringConverter(StringBuffer,cmd,uVar2);
        PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
        return 1;
      }
      goto NoMatch;
    }
    StringCheck = CompareInputWithString(InputCMD,s_UNBAN_006247dc);
    if (StringCheck != 0) {
      if ((Net_MPGame == 0) || (Net_ServiceType != 1)) {
        StringConverter(StringBuffer,s_UNBAN_only_works_in_novaworld_mu_00624734);
        PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
        return 1;
      }
      if (Net_Host == 0) {
        if (Pointer < 2) {
          StringConverter(StringBuffer,s_Usage__UNBAN_<player_number>_006247bc);
          PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
          return 1;
        }
        uVar2 = NumberParser(Character);
        Pointer = PlayerLookup(Net_PlayerLimitPointer,uVar2);
        if ((Pointer == 0) || (*(char *)(Pointer + 0x10) == '\0')) {
          StringConverter(StringBuffer,STR_PlayerNameNotFound,uVar2);
          PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
          return 1;
        }
        if (**(char **)(Pointer + 0x1c) == '\0') {
          StringConverter(StringBuffer,s_Player___ld_s_PCID_is_not_known__00624884,uVar2);
          PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
          return 1;
        }
        CommandString = (char *)FUN_00486340(DAT_009ed5d4,*(char **)(Pointer + 0x1c));
        if (CommandString != (char *)0x0) {
          if (DAT_009ed5d4 != 0) {
            FUN_004862e0(&CommandString);
            FindBanListTxt(s_banlist_txt_00624850);
          }
          StringConverter(StringBuffer,s_Removed_player___ld___s__from_ba_00624794,uVar2,
                          *(undefined4 *)(Pointer + 0x14));
          PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
          return 1;
        }
        StringConverter(StringBuffer,s_Player___ld___s__is_not_in_the_b_00624768,uVar2,
                        *(undefined4 *)(Pointer + 0x14));
        PushStatusMessage(StringBuffer,HUD_ConsoleTextColor,0x3a2);
        return 1;
      }
      goto NoMatch;
    }
    Pointer = CompareInputWithString(InputCMD,s_SERVER_0062472c);
    if (Pointer == 0) {
      Pointer = CompareInputWithString(InputCMD,s_GETPLAYERLIST_0062471c);
      if (Pointer == 0) {
        return 0;
      }
      HostSessionInfo._2_1_ = 0;
      Net_PayLoadData = 0;
      SecondaryWPN = 55;
      _PayLoadSize = 2;
      Net_PrimaryQueueOutgoingPacket(&Net_PayLoadQueue,0x22,1,0xffffffff,&Net_PayLoadData,2);
      _PayLoadSize = 0;
      PacketType = 0x23;
      goto LAB_004279c6;
    }
    Pointer = FUN_005c32ca((int)*cmd);
    for (; (Pointer != 0 && (*cmd != '\0')); cmd = cmd + 1) {
      Pointer = FUN_005c32ca((int)cmd[1]);
    }
    Pointer = FUN_005c32ca((int)*cmd);
    for (; (Pointer == 0 && (*cmd != '\0')); cmd = cmd + 1) {
      Pointer = FUN_005c32ca((int)cmd[1]);
    }
    Pointer = FUN_005c32ca((int)*cmd);
    for (; (Pointer != 0 && (*cmd != '\0')); cmd = cmd + 1) {
      Pointer = FUN_005c32ca((int)cmd[1]);
    }
    if (cmd != (char *)0x0) goto NoMatch;
    Net_PayLoadData = 0;
  }
  else {
NoMatch:
    CopyCString(&Net_PayLoadData,cmd,0x400);
  }
  _PayLoadSize = 0xffffffff;
  cmd = &Net_PayLoadData;
  do {
    if (_PayLoadSize == 0) break;
    _PayLoadSize = _PayLoadSize - 1;
    cVar1 = *cmd;
    cmd = cmd + 1;
  } while (cVar1 != '\0');
  _PayLoadSize = ~_PayLoadSize;
  PacketType = 0x24;
LAB_004279c6:
  Net_PrimaryQueueOutgoingPacket
            (&Net_PayLoadQueue,PacketType,1,0xffffffff,&Net_PayLoadData,_PayLoadSize);
  return 1;
}

