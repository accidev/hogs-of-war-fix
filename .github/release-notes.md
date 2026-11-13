For the Steam version of Hogs of War on Windows 10 and 11.

**New in v1.1.1**

A review of the whole code after v1.1.0. No new settings: update as usual.

- The installer is safer when a file cannot be written: the game is never left with a patched exe but without `hogs.dll`, a `hogs.ini` that cannot be updated no longer stops the install, and the music library is replaced only after its new copy is complete.
- The renderer is guarded against several rare failures: a window resize that failed half way left the window black for good, a mirrored picture at the edge of the screen showed the wrong part of itself, and a lost graphics device now leaves a line in `hogsdraw.log`.
- `[Battle] FrameRate` is described as it works: 25 or less is the original rate, and with `VSync=1` there are never more frames than the monitor shows.
- Checked again in the game: leaving a battle, the results screen, "Retry" and a second battle with the new battle loop; pause, menus and results with a simulated controller.

All changes of every version: [CHANGELOG.md](https://github.com/accidev/hogs-of-war-fix/blob/main/CHANGELOG.md). v1.1.0 added walking computer pigs, smooth battles, wide screen and Xbox controllers.

**Install**

1. Download **HogsFix.zip** below.
2. Open the game folder: in Steam, right-click Hogs of War → Manage → Browse local files.
3. Extract the zip there. A `HogsFix` folder appears next to `warhogs_.exe`.
4. Double-click `HogsFix\patch.cmd` and wait for "Done".
5. Start the game from Steam.

Video of the whole install: https://youtu.be/oBo27HB4x_4

Update: extract the new zip over the old `HogsFix` folder and run `patch.cmd` again; your `hogs.ini` settings are kept. Uninstall: `HogsFix\uninstall.cmd`. Settings, controls and help: [README](https://github.com/accidev/hogs-of-war-fix#readme).

**Что нового в v1.1.1**: проверка всего кода после v1.1.0, новых настроек нет. Установщик надёжнее, когда файл не удаётся записать: игра не остаётся с изменённым exe без `hogs.dll`, а `hogs.ini`, который нельзя обновить, больше не прерывает установку. Рендер защищён от нескольких редких сбоев (окно оставалось чёрным после неудачного изменения размера, зеркальная картинка у края экрана показывала не ту часть). Описание `[Battle] FrameRate` приведено в соответствие с работой. Заново проверены в игре выход из боя, экран итогов и повторный бой. Все изменения по версиям: [CHANGELOG.md](https://github.com/accidev/hogs-of-war-fix/blob/main/CHANGELOG.md).

**Установка**: скачайте **HogsFix.zip**, распакуйте в папку игры (Steam → правой кнопкой по игре → Управление → Просмотреть локальные файлы) и запустите `HogsFix\patch.cmd`. Видео установки: https://youtu.be/oBo27HB4x_4. Удаление: `HogsFix\uninstall.cmd`. Подробнее: [README на русском](https://github.com/accidev/hogs-of-war-fix/blob/main/README.ru.md).
