For the Steam version of Hogs of War on Windows 10 and 11.

**New in v1.4.0**

- Network games over the internet. In the game choose Multi-player, Network, then "- INTERNET -": you see the games of other players of the fix, and the last line makes your own. Nobody needs an open port, and players do not see each other's IP addresses. Two to four players, as in the game's own network play.
- The list of games shows the players of each game and your ping: `BRAVO'S GAME 2-4 125MS`. Your name and a password for games with friends are in the new `[Network]` section of `hogs.ini`.
- Watching another player's turn in a network battle is smooth now. It was shown at about 8 frames a second.
- All players need v1.4.0. This is new and was tested with two copies of the game on one PC only: please report how it works between real PCs. To get the game's own network play back (local network, IP address), empty `Server=` in `[Network]`.

**v1.3.2**

- Fixed a crash with the Russian translation: the game closed on the screen where you name your team or a pig when you pressed D, S or one of 13 other letter keys. If the translation is installed, run `russian.cmd` again after `patch.cmd`: it installs release 1.1.1 of the translation.
- On that screen the keyboard now types the letters of the current keyboard layout: Russian letters in the Russian layout (with the translation), Latin letters in the English one.

**v1.3.1**

- `russian.cmd` installs a newer Russian translation: the whole Russian alphabet and the Latin letters in every font, corrected text, readable English names and credits.

**v1.3.0**

- The Russian translation of the game (text, fonts, videos and voices) can be installed from the fix. Double-click `HogsFix\russian.cmd`: it downloads the translation (about 330 MB) from [hogs-of-war-ru](https://github.com/accidev/hogs-of-war-ru), checks it and puts it into the game folder. On a PC whose display language is Russian, `patch.cmd` asks whether to do this.
- `HogsFix\russian-remove.cmd` puts the English files back; `uninstall.cmd` does it too.
- Nothing in the game itself changed since v1.2.2.

**v1.2.2**

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

**Что нового в v1.4.0**: сетевая игра через интернет. В игре выберите «Много игроков», «Сеть», затем «- INTERNET -»: откроется список игр других игроков с исправлением, последняя строка создаёт свою. Открывать порты не нужно, игроки не видят IP-адреса друг друга. В списке видно число игроков каждой игры и ваш пинг: `BRAVO'S GAME 2-4 125MS`. Имя и пароль для игры с друзьями задаются в новом разделе `[Network]` файла `hogs.ini`. Чужой ход в сетевом бою теперь идёт плавно, раньше он показывался примерно с 8 кадрами в секунду. У всех игроков должна быть версия 1.4.0. Возможность новая и проверена только на двух копиях игры на одном компьютере: напишите, как она работает у вас. Чтобы вернуть обычную сетевую игру (локальная сеть, IP-адрес), сотрите адрес в `Server=` раздела `[Network]`.

**v1.3.2**: исправлен вылет с русским переводом. Игра закрывалась на экране, где называют команду или свинью, при нажатии D, S и ещё 13 клавиш с буквами. Если перевод уже стоит, после `patch.cmd` запустите `russian.cmd` ещё раз: он поставит версию 1.1.1 перевода. На этом экране клавиатура теперь печатает буквы включённой раскладки: русские при русской, латинские при английской.

**v1.3.1**: `russian.cmd` ставит новую версию русского перевода. В ней весь русский алфавит и вся латиница во всех шрифтах, исправленный текст («НАСТРОЙКИ», «ЭКСПЕРТ», «МАТЧ» вместо «НАСТРОИКИ», «ЕКСПЕРТ», «МАТ4»), читаются английские имена и титры.

**v1.3.0**: русский перевод игры (текст, шрифты, ролики и озвучка) теперь ставится из исправления. Дважды щёлкните `HogsFix\russian.cmd`: он скачает перевод (около 330 МБ) из репозитория [hogs-of-war-ru](https://github.com/accidev/hogs-of-war-ru), проверит его и положит в папку игры. На компьютере с русским языком Windows `patch.cmd` сам спросит, ставить ли перевод. Английский возвращает `HogsFix\russian-remove.cmd`, а также `uninstall.cmd`. В самой игре с v1.2.2 ничего не изменилось.

**v1.2.2**: вода больше не течёт слишком быстро на мониторах с высокой частотой (на 360 Гц она шла в 14 раз быстрее оригинала). Убраны тонкие просвечивающие полоски между частями интерфейса (часы, окно оружия). Имена над свиньями больше не съезжают на головы в больших разрешениях. С `[Display] Widescreen=1` на широком мониторе бои теперь широкие и тогда, когда раньше было сохранено разрешение 4:3. Игра запускается в самом большом разрешении для вашего монитора. Интерфейс рисуется в пикселях игры: чтобы он был крупнее, зажмите Shift при запуске и выберите меньше строк, чёткость картинки та же.

**v1.2.1**: настройка `[Display] Windowed` убрана, с `Windowed=0` игра работала невидимо. Полный экран включает `[Display] Borderless=1`.

**v1.2.0**: в папке игры на одну DLL меньше, в самой игре ничего не меняется. `hogs.dll` больше нет: её исправления теперь в `ddraw.dll` вместе с рендером. `patch.cmd` сам обновляет игру с версиями 1.0–1.1 и удаляет `hogs.dll`. Исправленной игре нужен `ddraw.dll` из этого исправления: с dgVoodoo2 или другим `ddraw.dll` поверх него игра не запустится, чинит повторный запуск `patch.cmd`. Если установка или удаление прервётся на любом шаге, игра останется запускаемой. Удаляйте исправление файлом `uninstall.cmd` этой версии, а не оставшимся от 1.0–1.1. Все изменения по версиям: [CHANGELOG.md](https://github.com/accidev/hogs-of-war-fix/blob/main/CHANGELOG.md).

**Установка**: скачайте **HogsFix.zip**, распакуйте в папку игры (Steam → правой кнопкой по игре → Управление → Просмотреть локальные файлы) и запустите `HogsFix\patch.cmd`. Видео установки: https://youtu.be/oBo27HB4x_4. Удаление: `HogsFix\uninstall.cmd`. Подробнее: [README на русском](https://github.com/accidev/hogs-of-war-fix/blob/main/README.ru.md).
