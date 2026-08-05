
void ClipCursorToViewPort(void)

{
  HWND hWnd;
  tagRECT *lpRect;
  tagRECT Coordinate;
  
  lpRect = &Coordinate;
  hWnd = GetDesktopWindow();
  GetWindowRect(hWnd,lpRect);
  Coordinate.left = 0;
  Coordinate.top = 0;
  Coordinate.right = 639;
  Coordinate.bottom = 479;
  ClipCursor(&Coordinate);
  return;
}

