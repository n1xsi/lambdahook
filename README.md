<h1 align="center">
  <img src="https://i.imgur.com/KfSK09T.jpeg" width="20%">
  
  lambdahook

  [![C++](https://img.shields.io/badge/c++-1a1a1a?style=for-the-badge&logo=c%2B%2B&logoColor=00599c)](#)
  [![CMake](https://img.shields.io/badge/CMake-1a1a1a?style=for-the-badge&logo=cmake&logoColor=white)](#)
  [![Platform](https://custom-icon-badges.demolab.com/badge/Windows-1a1a1a?logo=windows&style=for-the-badge)](#)
</h1>

Windows cheat for GoldSRC-games after 25th anniversary update.

Based on [8dcc](https://github.com/8dcc/hl-cheat) and [UnkwUsr](https://github.com/UnkwUsr/hlhax) sources. They didn't want to update the sources, so I did it myself and made some improvements.

> [!WARNING]
> Cheat is under development.
> I’m trying to update the old hooks and functions to match the new update for all goldsrc games.

## Available features

<h3> only in dod <img src="https://shared.fastly.steamstatic.com/community_assets/images/apps/30/aadc0ce51ff6ba2042d633f8ec033b0de62091d0.jpg" align="top" width="3%">: </h3>

* **Aimbot** (with fov setting);
* **Bhop**;
* **Chams** (for players/hands/all);
* **ESP** (with "enemy_only" setting).

<h3> in other goldsrc games (e.g. <img src="https://shared.fastly.steamstatic.com/community_assets/images/apps/10/6b0312cda02f5f777efa2f3318c307ff9acafbb5.jpg" align="top" width="3%">, <img src="https://shared.fastly.steamstatic.com/community_assets/images/apps/70/95be6d131fc61f145797317ca437c9765f24b41c.jpg" align="top" width="3%">, <img src="https://shared.fastly.steamstatic.com/community_assets/images/apps/40/c525f76c8bc7353db4fd74b128c4ae2028426c2a.jpg" align="top" width="3%">, <img src="https://shared.fastly.steamstatic.com/community_assets/images/apps/60/98c69e04cd59b838e05cb6980c12c05874c6419e.jpg" align="top" width="3%">): </h3>

* **ESP**.

> [!NOTE]
> If your ESP works incorrectly (e.g. in DoD) - just press button "M" to toggle map and fix ESP with boxes and names.

### Also cheat has its own **injector**:
compile [injector.c](injector/injector.c) ⟶ add built cheat dll to the same dir ⟶ start game ⟶ start injector.exe

## Logs
Logs about inject, hooks and some features are written here: `C:\dod-cheat-log.txt`

## How to build

In windows 10 I use `mingw32` and `ninja`:
```bash
export PATH="/c/msys64/mingw32/bin:$PATH" && cd "C:\path\to\lambdahook\build" && /c/msys64/mingw32/bin/ninja.exe 2>&1
```

Result will be here: `build/bin/liblambdahook.dll`
