
void Net_ClientMessageDispatcher(undefined4 NetPlayer,char *MessageString)

{
  int Count;
  DWORD TickPointer;
  char MessageType;
  
  MessageType = *MessageString;
  MessageString = MessageString + 1;
  ChatStringPreProcessor(MessageString);
                    /* White Status Message */
  if (MessageType == '\0') {
    PushStatusMessage(MessageString,HUD_ConsoleTextColor,0x3a2);
    return;
  }
                    /* Team Message */
  if (MessageType != '\x01') {
    if (MessageType == '\x02') {
      PushChatMessage(MessageString,HUD_TextColor,0x3a2);
      return;
    }
                    /* Yellow Admin Message */
    if (MessageType == '\x03') {
      PushChatMessage(MessageString,HUD_YellowColor,0x3a2);
      return;
    }
                    /* Team Message */
    if (MessageType != '\x04') {
      if (MessageType == '\x05') {
        PushChatMessage(MessageString,HUD_TeamColor,0x3a2);
        return;
      }
                    /* Unknown */
      if (MessageType == '\a') {
        PushStatusMessage(MessageString,HUD_ConsoleTextColor,0x3a2);
        return;
      }
                    /* Unit Message */
      if (MessageType == '\b') {
        PushStatusMessage(MessageString,HUD_OrangeColor,0x3a2);
        return;
      }
      Count = Counter;
                    /* Special Numbered Message */
      if (MessageType != '\t') {
        PushStatusMessage(MessageString,0xff,0x3a2);
        return;
      }
      do {
        do {
          if (Count < 0x10) {
LAB_0041c16a:
            Counter = Count + 1;
            CopyCString(&DAT_0071e920 + Count * 0x88,MessageString,0x80);
            TickPointer = GetTickCount();
            (&DAT_0071e918)[Count * 0x22] = TickPointer;
            DAT_0071f1a0 = DAT_0071f1a0 + 1;
            (&DAT_0071e91c)[Count * 0x22] = DAT_0071f1a0;
            return;
          }
        } while (Count == 0);
        if (Count == 1) {
          Count = 0;
          goto LAB_0041c16a;
        }
        FUN_005c2d00(&DAT_0071e918,&DAT_0071e9a0,Count * 0x88 + -0x88);
        Count = Counter + -1;
        Counter = Count;
      } while( true );
    }
  }
                    /* Global Chat Message */
  PushChatMessage(MessageString,HUD_TeamColor,0x3a2);
  return;
}

