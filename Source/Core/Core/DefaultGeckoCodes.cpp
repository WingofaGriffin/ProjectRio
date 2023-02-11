#include "Core/DefaultGeckoCodes.h"
#include "NetPlayProto.h"
#include "Config/NetplaySettings.h"
#include <VideoCommon/VideoConfig.h>

void DefaultGeckoCodes::RunCodeInject(bool bNetplayEventCode, bool bIsRanked, bool bIsNight, u32 uGameMode, bool bDisableReplays)
{
  NetplayEventCode = bNetplayEventCode;
  IsRanked = bIsRanked;
  IsNight = bIsNight;
  GameMode = uGameMode; //1 == stars off, 2 == stars on, 0 == anything else
  DisableReplays = bDisableReplays;

  aWriteAddr = 0x802ED200;  // starting asm write addr

  PowerPC::HostWrite_U8(0x1, aControllerRumble);  // enable rumble

  // this is a very bad and last-minute "fix" to the pitcher stamina bug. i can't find the true source of the bug so i'm
  // manually fixing it with this line here. will remove soon once bug is truly squashed
  if (PowerPC::HostRead_U8(0x8069BBDD) == 0xC5)
    PowerPC::HostWrite_U8(05, 0x8069BBDD);

  // handle asm writes for required code
  for (DefaultGeckoCode geckocode : sRequiredCodes)
    WriteAsm(geckocode);

  if (NetplayEventCode || IsRanked)
    InjectNetplayEventCode();

  if (IsRanked)
    AddRankedCodes();

  if (g_ActiveConfig.bTrainingModeOverlay && !IsRanked)
    WriteAsm(sEasyBattingZ);

  // Netplay Config Codes
  if (NetPlay::IsNetPlayRunning())
  {
    if (IsNight)
      WriteAsm(sNightStadium);

    if (DisableReplays)
    {
      if (PowerPC::HostRead_U32(aDisableReplays) == 0x38000001)
        PowerPC::HostWrite_U32(0x38000000, aDisableReplays);
    }

    if (Config::Get(Config::NETPLAY_DISABLE_MUSIC))
    {
      PowerPC::HostWrite_U32(0x38000000, aDisableMusic_1);
      PowerPC::HostWrite_U32(0x38000000, aDisableMusic_2);
    }

    if (Config::Get(Config::NETPLAY_HIGHLIGHT_BALL_SHADOW))
    {
      WriteAsm(sHighlightBallShadow);
    }

    //if (Config::Get(Config::NETPLAY_NEVER_CULL))
    //{
    //  PowerPC::HostWrite_U32(0x38000007, aNeverCull_1);
    //  PowerPC::HostWrite_U32(0x38000001, aNeverCull_2);
    //  PowerPC::HostWrite_U32(0x38000001, aNeverCull_3);
    //  if (PowerPC::HostRead_U32(aNeverCull_4) == 0x881a0093)
    //    PowerPC::HostWrite_U32(0x38000003, aNeverCull_4);
    //}
  }
}

void DefaultGeckoCodes::InjectNetplayEventCode()
{
  if (PowerPC::HostRead_U32(aBootToMainMenu) == 0x38600001) // Boot to Main Menu
    PowerPC::HostWrite_U32(0x38600005, aBootToMainMenu);
  PowerPC::HostWrite_U8(0x1, aSkipMemCardCheck);               // Skip Mem Card Check
  PowerPC::HostWrite_U32(0x380400f6, aUnlimitedExtraInnings);  // Unlimited Extra Innings

  // Unlock Everything
  PowerPC::HostWrite_U8(0x2, aUnlockEverything_1);

  for (int i = 0; i <= 0x5; i++)
    PowerPC::HostWrite_U8(0x3, aUnlockEverything_2 + i);

  for (int i = 0; i <= 0x5; i++)
    PowerPC::HostWrite_U8(0x1, aUnlockEverything_3 + i);

  for (int i = 0; i <= 0x29; i++)
    PowerPC::HostWrite_U8(0x1, aUnlockEverything_4 + i);

  PowerPC::HostWrite_U8(0x1, aUnlockEverything_5);

  if (!(GameMode == 1 && IsRanked))  // if stars off ranked, don't unlock superstars
  {
    for (int i = 0; i <= 0x35; i++)
      PowerPC::HostWrite_U8(0x1, aUnlockEverything_6 + i);
  }

  for (int i = 0; i <= 0x3; i++)
    PowerPC::HostWrite_U8(0x1, aUnlockEverything_7 + i);

  PowerPC::HostWrite_U16(0x0101, aUnlockEverything_8);
  //PowerPC::HostWrite_U8(0x1, aUnlockEverything_8 + 1);

  // handle asm writes for netplay codes
  for (DefaultGeckoCode geckocode : sNetplayCodes)
    WriteAsm(geckocode);
}


// Adds codes specific to ranked, like the Pitch Clock
void DefaultGeckoCodes::AddRankedCodes()
{
  PowerPC::HostWrite_U32(0x60000000, aPitchClock_1);
  PowerPC::HostWrite_U32(0x60000000, aPitchClock_2);
  PowerPC::HostWrite_U32(0x60000000, aPitchClock_3);
  PowerPC::HostWrite_U32(0x386001bb, aBatSound);

  WriteAsm(sPitchClock);
  WriteAsm(sRestrictBatterPausing);
  WriteAsm(sHazardless);
  WriteAsm(sHazardless_1);
}


// calls this each time you want to write a code
void DefaultGeckoCodes::WriteAsm(DefaultGeckoCode CodeBlock)
{
  // METHODOLOGY:
  // use aWriteAddr as starting asm write addr
  // we compute a value, branchAmount, which tells how far we have to branch to get from the injection to aWriteAddr
  // do fancy bit wise math to formulate the hex value of the desired branch instruction
  // writes in first instruction in code block to aWriteAddr, increment aWriteAddr by 4, repeat for all code lines
  // once code block is finished, compute another branch instruction back to injection addr and write it in
  // repeat for all codes
  u32 branchToCode = 0x48000000;
  u32 baseAddr = aWriteAddr & 0x03ffffff;
  u32 codeAddr = CodeBlock.addr & 0x03ffffff;
  u32 branchAmount = (baseAddr - codeAddr) & 0x03ffffff;

  branchToCode += branchAmount;

  // write asm to free memory
  for (int i = 0; i < CodeBlock.codeLines.size(); i++)
  {
    PowerPC::HostWrite_U32(CodeBlock.codeLines[i], aWriteAddr);
    aWriteAddr += 4;
  }

  // write branches
  u32 branchFromCode = 0x48000000;
  baseAddr = aWriteAddr & 0x03ffffff;
  codeAddr = CodeBlock.addr & 0x03ffffff;
  branchAmount = (codeAddr - baseAddr) & 0x03ffffff;

  // branch at the end of the gecko code
  branchFromCode += branchAmount + 4;
  PowerPC::HostWrite_U32(branchFromCode, aWriteAddr);
  aWriteAddr += 4;

  if (CodeBlock.conditionalVal != 0 && PowerPC::HostRead_U32(CodeBlock.addr) != CodeBlock.conditionalVal)
    return;
  // branch at injection location
  PowerPC::HostWrite_U32(branchToCode, CodeBlock.addr);
}

// end
