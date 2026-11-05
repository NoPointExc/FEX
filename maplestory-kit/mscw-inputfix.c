/* mscw-inputfix: keep MapleStory input working under Proton in DroidDeck.
 *
 * Proton drops all X keyboard/mouse events while the session-wide named event
 * "__wine_steamclient_GameOverlayActivated" is set. Steam sets it through the Nexon
 * launcher's lsteamclient when the launcher closes (focus falls back to Steam), and
 * never sends the matching "deactivated", so the game starts with input disabled.
 * This waits for MapleStory.exe, then clears the event whenever it is set, until the
 * game exits.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

static HANDLE find_game(void) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  PROCESSENTRY32 pe = {sizeof pe};
  HANDLE h = NULL;
  if (snap == INVALID_HANDLE_VALUE) return NULL;
  for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe)) {
    if (!lstrcmpiA(pe.szExeFile, "MapleStory.exe")) {
      h = OpenProcess(SYNCHRONIZE, FALSE, pe.th32ProcessID);
      if (h) break;
    }
  }
  CloseHandle(snap);
  return h;
}

int main(void) {
  HANDLE ev = CreateEventA(NULL, TRUE, FALSE, "__wine_steamclient_GameOverlayActivated");
  HANDLE game = NULL;
  int resets = 0;
  if (!ev) return 1;

  /* Wait up to 10 minutes for the game to start. */
  for (int i = 0; i < 2400 && !(game = find_game()); i++) Sleep(250);
  if (!game) return 0;

  while (WaitForSingleObject(game, 250) == WAIT_TIMEOUT) {
    if (WaitForSingleObject(ev, 0) == WAIT_OBJECT_0) {
      ResetEvent(ev);
      printf("mscw-inputfix: cleared stuck Steam overlay flag (%d)\n", ++resets);
      fflush(stdout);
    }
  }
  return 0;
}
