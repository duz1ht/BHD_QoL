
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void Net_ClientReceiveNetDelay(undefined4 Base,undefined4 *Delay)

{
  if (Net_Host == 0) {
    _Net_TickDelay = *Delay;
  }
  return;
}

