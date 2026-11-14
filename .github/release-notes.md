For the Steam version of Hogs of War on Windows 10 and 11.

**New in v1.2.2**

- Fixed: water ran too fast on monitors above 25 Hz, up to 14 times the original speed on 360 Hz. It moves at the original speed now.
- Fixed: thin see-through lines between the pieces of the HUD (the timer, the weapon window).
- Fixed: the names over the pigs sat on their heads in high resolutions.
- Fixed: with `[Display] Widescreen=1`, a game that had a 4:3 resolution saved stayed 4:3. On a wide monitor battles are wide now.
- The game starts in the largest resolution for your monitor. The HUD is drawn in game pixels: for a larger HUD hold Shift while the game starts and choose fewer lines, the picture stays as sharp.

**v1.2.1 and v1.2.0**

- `[Display] Windowed` is removed: with `Windowed=0` the game ran unseen. For full screen set `[Display] Borderless=1`.
- `hogs.dll` is gone: its fixes are in `ddraw.dll` now, with the renderer. `patch.cmd` updates a game that has v1.0–v1.1 of the fix by itself.
- The fixed game needs the `ddraw.dll` of this fix. With dgVoodoo2 or another `ddraw.dll` put over it the game does not start; run `patch.cmd` again to repair it.
- Uninstall with the `uninstall.cmd` of this version, not with one kept from v1.0–v1.1.

All changes of every version: [CHANGELOG.md](https://github.com/accidev/hogs-of-war-fix/blob/main/CHANGELOG.md). v1.1 added walking computer pigs, smooth battles, wide screen and Xbox controllers.

**Install**

1. Download **HogsFix.zip** below.
2. Open the game folder: in Steam, right-click Hogs of War → Manage → Browse local files.
3. Extract the zip there. A `HogsFix` folder appears next to `warhogs_.exe`.
4. Double-click `HogsFix\patch.cmd` and wait for "Done".
5. Start the game from Steam.

Video of the whole install: https://youtu.be/oBo27HB4x_4

Update: extract the new zip over the old `HogsFix` folder and run `patch.cmd` again; your `hogs.ini` settings are kept. Uninstall: `HogsFix\uninstall.cmd`. Settings, controls and help: [README](https://github.com/accidev/hogs-of-war-fix#readme).

**Что нового в v1.2.2**: вода больше не течёт слишком быстро на мониторах с высокой частотой (на 360 Гц она шла в 14 раз быстрее оригинала). Убраны тонкие просвечивающие полоски между частями интерфейса (часы, окно оружия). Имена над свиньями больше не съезжают на головы в больших разрешениях. С `[Display] Widescreen=1` на широком мониторе бои теперь широкие и тогда, когда раньше было сохранено разрешение 4:3. Игра запускается в самом большом разрешении для вашего монитора. Интерфейс рисуется в пикселях игры: чтобы он был крупнее, зажмите Shift при запуске и выберите меньше строк, чёткость картинки та же.

**v1.2.1**: настройка `[Display] Windowed` убрана, с `Windowed=0` игра работала невидимо. Полный экран включает `[Display] Borderless=1`.

**v1.2.0**: в папке игры на одну DLL меньше, в самой игре ничего не меняется. `hogs.dll` больше нет: её исправления теперь в `ddraw.dll` вместе с рендером. `patch.cmd` сам обновляет игру с версиями 1.0–1.1 и удаляет `hogs.dll`. Исправленной игре нужен `ddraw.dll` из этого исправления: с dgVoodoo2 или другим `ddraw.dll` поверх него игра не запустится, чинит повторный запуск `patch.cmd`. Если установка или удаление прервётся на любом шаге, игра останется запускаемой. Удаляйте исправление файлом `uninstall.cmd` этой версии, а не оставшимся от 1.0–1.1. Все изменения по версиям: [CHANGELOG.md](https://github.com/accidev/hogs-of-war-fix/blob/main/CHANGELOG.md).

**Установка**: скачайте **HogsFix.zip**, распакуйте в папку игры (Steam → правой кнопкой по игре → Управление → Просмотреть локальные файлы) и запустите `HogsFix\patch.cmd`. Видео установки: https://youtu.be/oBo27HB4x_4. Удаление: `HogsFix\uninstall.cmd`. Подробнее: [README на русском](https://github.com/accidev/hogs-of-war-fix/blob/main/README.ru.md).
