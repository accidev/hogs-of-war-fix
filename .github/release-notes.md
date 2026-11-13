For the Steam version of Hogs of War on Windows 10 and 11.

**New in v1.1.0**

- Computer pigs walk on their turn. In the original PC game they stayed where they were.
- Smooth battles: as many frames per second as your monitor shows instead of 25, at the original game speed, and no more 100 % load on one CPU core (`[Battle] FrameRate`).
- Wide-screen battles: resolutions in the shape of your monitor (16:9, 16:10, 21:9) show more to the sides. Menus and videos stay 4:3 with black bars. A new player starts in a wide resolution; if you update, hold Shift while the game starts to pick one (`[Display] Widescreen`).
- Xbox (XInput) controllers work without setup, with the PlayStation button layout (table in the README). This is new and tested only with a simulated controller, so feedback is welcome.
- Menus run at their original speed, and the game no longer freezes after its window was minimized.
- The game starts even when its Windows registry entry is missing, for example from a game folder copied from another PC.
- New setting `[Render] FogDistance`: how far the fog is.
- Updating: `patch.cmd` adds every new setting to your `hogs.ini` and keeps your values.

**Install**

1. Download **HogsFix.zip** below.
2. Open the game folder: in Steam, right-click Hogs of War → Manage → Browse local files.
3. Extract the zip there. A `HogsFix` folder appears next to `warhogs_.exe`.
4. Double-click `HogsFix\patch.cmd` and wait for "Done".
5. Start the game from Steam.

Video of the whole install: https://youtu.be/oBo27HB4x_4

Update: extract the new zip over the old `HogsFix` folder and run `patch.cmd` again; your `hogs.ini` settings are kept. Uninstall: `HogsFix\uninstall.cmd`. Settings, controls and help: [README](https://github.com/accidev/hogs-of-war-fix#readme).

**Что нового в v1.1.0**: свиньи компьютера ходят на своём ходу (в оригинальной PC-версии они стояли на месте); плавный бой, столько кадров в секунду, сколько показывает монитор, вместо 25, с прежней скоростью игры и без загрузки ядра процессора на 100 %; бои на широком экране (разрешения формы вашего монитора показывают больше по бокам; меню и ролики остаются 4:3 с полосами; новый игрок сразу в широком режиме, после обновления зажмите Shift при запуске игры, чтобы выбрать его); геймпады Xbox работают без настройки с раскладкой PlayStation (новинка, проверена только на имитации геймпада, отзывы приветствуются); меню на родной скорости и без зависания после сворачивания окна; игра запускается и без своей записи в реестре Windows; новая настройка `[Render] FogDistance`; `patch.cmd` при обновлении дописывает новые настройки в ваш `hogs.ini` и сохраняет ваши значения.

**Установка**: скачайте **HogsFix.zip**, распакуйте в папку игры (Steam → правой кнопкой по игре → Управление → Просмотреть локальные файлы) и запустите `HogsFix\patch.cmd`. Видео установки: https://youtu.be/oBo27HB4x_4. Удаление: `HogsFix\uninstall.cmd`. Подробнее: [README на русском](https://github.com/accidev/hogs-of-war-fix/blob/main/README.ru.md).
