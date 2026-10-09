# Hogs of War на Windows 10/11

Фанатское исправление Steam-версии Hogs of War (2000, v1.2, appid 389900). С ним игра запускается и играется на Windows 10/11: в окне с рамкой, со своим рендером на Direct3D 11 и с музыкой. Большая цель на будущее — переписать игру под x64.

Подробный технический журнал — [docs/WORKLOG.md](docs/WORKLOG.md), разбор рендера игры — [docs/renderer/](docs/renderer/).

## Почему оригинал не работает

1. **Зависание при запуске.** Обёртка CD-музыки `winmm.dll` (ogg-winmm 2014 года) вешает загрузчик DLL Windows 10/11, окно так и не появляется.
2. **Блокировка рабочего стола.** Игра выключает все окна системы (`EnableWindow(FALSE)`) и включает их только при выходе через меню. После Alt+Tab или падения рабочий стол «мёртв».
3. **Защита LaserLock** (`wh32lib.dll`) прячет 325 вызовов API за зашифрованной таблицей.
4. **Графика.** DirectDraw 7 и Direct3D 7, только 16 бит, полный экран со сменой видеорежима. На современной системе это падения при Alt+Tab, а в окне — перекошенные маски видимости земли (пол пропадает полосами).

## Из чего состоит исправление

| файл в папке игры | что делает |
|---|---|
| `warhogs_.exe` | патчер переписывает 325 вызовов LaserLock на прямые вызовы API, и вместо `wh32lib.dll` загружается `hogs.dll` |
| `hogs.dll` | исправления во время работы: окна системы не трогаются; оконный режим: окно сразу большое и держит размер, по желанию без рамки; запуск без окна выбора разрешения и без заставок; разрешения до 1920×1440; отдаление камеры; мёртвая зона стика 15 %; громкость музыки идёт в ogg-winmm, а не в микшер Windows; исправления `_d3d.dll` (смещение кадра, маски видимости земли, объекты за камерой). Журнал `hogs.log` |
| `ddraw.dll` | свой рендер: DirectDraw 7 / Direct3D 7 игры на Direct3D 11, внутреннее разрешение кратно больше игрового и не меньше экрана. Журнал `hogsdraw.log` |
| `hogs.ini` | настройки: окно, запуск, геймпад, масштаб рендера, VSync |
| `winmm.dll`, `winmm.ini` | ogg-winmm (ayuanx) v2025.01.16: музыка из `MUSIC\TrackNN.ogg` |

Установка: распаковать `HogsFix` в папку игры и запустить `patch.cmd`. Подробная инструкция для игроков будет позже.

- Игра должна быть закрыта. Всё, что может помешать, патчер проверяет до первого изменения.
- Обновление — снова `patch.cmd`. В старый `hogs.ini` добавляется секция `[Render]`, значения игрока не меняются.
- dgVoodoo2 больше не нужен. Если его поставила старая версия патчера, он удаляется. Если ставили вручную, его файлы переименовываются в `*.orig`.
- Откат: `patch.cmd -Restore`. Он возвращает оригинальные `warhogs_.exe` и `winmm.dll`, а также файлы `*.orig`. Удаляет `hogs.dll`, `ddraw.dll`, журналы и `hogs.ini` вместе с настройками.

## Структура репозитория

- `src/hogs/` — `hogs.dll` и шаблон `hogs.ini`.
- `src/ddraw/` — `ddraw.dll` (hogsdraw). `stubs.h` генерирует `tools/gen_com_stubs.py`.
- `src/d3dtrace/` — трассировщик вызовов `_d3d.dll`, только для разработки (`--target d3dtrace`).
- `patcher/` — патчер для игроков: `patch.cmd`, `patch.ps1`, описание патча `warhogs_v12.json`.
- `tools/`:
  - `dump.ps1`, `unmap_dump.py`, `ll_table.py`, `ll_unwrap.py`, `make_patch.py` — снятие LaserLock и описание патча;
  - `make_release.ps1` — пакет для игроков `dist/HogsFix.zip`;
  - `test_patcher.ps1` (+ `test_patcher_lib.ps1`) — проверка патчера на собранном `dist`: 98 проверок в песочнице во временной папке (`pwsh tools\test_patcher.ps1`);
  - `crash_catch.ps1` — запуск игры под мини-отладчиком: падения со стеком, видеорежим, Alt+Tab, снимки окна;
  - `gen_com_stubs.py`, `gen_d3dtrace.py` — генераторы кода;
  - `peimp.py`, `pesym.py` — разбор PE;
  - `enable-windows.ps1` — разблокировка окон.
- `unlock-windows.cmd` — разблокировка окон после запуска непропатченной игры (Win+R или диспетчер задач).
- `third_party/ogg-winmm/` — лицензия (GPL-2.0) и описание ogg-winmm.
- В `.gitignore` и не публикуются: `ghidra/`, `backup/`, `downloads/`, `work/`, `build/`, `dist/`.

## Сборка

Нужны Visual Studio с C++ и CMake. Игра 32-битная.

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
pwsh tools\make_release.ps1          # dist\HogsFix и dist\HogsFix.zip
```

## Пересборка описания патча exe

Скрипты с пометкой «Run under 32-bit Windows PowerShell» запускаются через `C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe`.

```powershell
$p = "D:\SteamLibrary\steamapps\common\HogsofWar\HoG x64"
$g = "D:\SteamLibrary\steamapps\common\HogsofWar"
# 1. Дамп. Exe должен быть оригинальным: LaserLock расшифровывает себя в памяти.
& C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe -File "$p\tools\dump.ps1" -Exe "$g\warhogs_.exe" -OutDir "$p\work\dumps"
# 2. Таблица LaserLock и exe без неё.
python -I "$p\tools\ll_table.py" "$p\work\dumps\wh32lib.dll_10000000.bin" "$p\work\dumps\warhogs_.exe_00400000.bin" "$p\work\ll_table.json"
python -I "$p\tools\ll_unwrap.py" "$p\backup\warhogs_.exe.orig" "$p\work\ll_table.json" "$p\work\warhogs_unwrapped.exe" "$p\work\ll_sites.json"
# 3. Описание патча для патчера.
python -I "$p\tools\make_patch.py" "$p\backup\warhogs_.exe.orig" "$p\work\warhogs_unwrapped.exe" "$p\work\ll_sites.json" "$p\patcher\warhogs_v12.json"
```

## Отладка

- `hogs.log` и `hogsdraw.log` рядом с игрой.
- Дамп кадра: в `hogs.ini` добавить `[Debug]` с `FrameDump=1`. Тогда F12 записывает следующий кадр в `hogsdraw_dump\`: все вызовы отрисовки с адресом вызова в `_d3d.dll`, картинку кадра и текстуры.
- Нет звука: один раз поднять Hogs of War в микшере громкости Windows. Без исправления игра сама меняла там свою громкость вместе с громкостью музыки и могла оставить 0. С `hogs.dll` она этого больше не делает, но уже сохранённое значение остаётся.

## Управление и настройки

- Колесо мыши или Num+ / Num− — камера ближе или дальше, от 50 до 200 %, шаг 10 %.
- Shift при запуске игры — окно выбора разрешения и детализации. Без Shift игра стартует с прошлым выбором из `launch.bin`, а если файла нет, окно показывается само.
- `hogs.ini`:
  - `[Display] Borderless=1` — окно без рамки на весь монитор, как полный экран;
  - `[Startup] SkipLauncher`, `SkipIntro` — пропуск окна выбора и заставок, по умолчанию оба включены;
  - `[Gamepad] Deadzone` — мёртвая зона стика в процентах, по умолчанию 15; в игре было 50;
  - `[Render] Scale`, `VSync` — масштаб рендера и вертикальная синхронизация.

## Чит

В `hogs.ini` добавить `[Cheats]` с `PromotionPoints=1`. Тогда F11 даёт команде на экране 10 очков повышения (PP), не больше 999. Очки видны на экране отряда. По умолчанию выключено.

## Что дальше

- Проверить на своём рендере видео Bink в окне и Alt+Tab.
- README для игроков и релиз на GitHub.
- Широкий экран 16:9: две правки проекции в `_d3d.dll` и режимы 16:9 (разбор в `docs/WORKLOG.md`).
- Большая цель: декомпиляция и сборка под x64.
