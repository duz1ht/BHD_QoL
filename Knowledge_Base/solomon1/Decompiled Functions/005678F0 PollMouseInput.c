
void PollMouseInput(void)

{
  int ReturnValue;
  tagPOINT Cursor;
  tagMSG Coordinate;
  
  Cursor.x = 0;
  Cursor.y = 0;
  ReturnValue = PeekMessageA(&Coordinate,WindowHandle,512,521,1);
  while (ReturnValue != 0) {
    ProcessMouseInput(Coordinate.wParam,Coordinate.lParam,Coordinate.message,1);
    ReturnValue = PeekMessageA(&Coordinate,WindowHandle,512,521,1);
  }
  ClientToScreen(WindowHandle,&Cursor);
  SetCursorPosition(Cursor.x + 320,Cursor.y + 240);
  RawMouseX = CursorX + -320;
  RawMouseY = CursorY + -240;
  CursorX = 320;
  CursorY = 240;
  return;
}

