For the Steam version of Hogs of War on Windows 10 and 11.

**New in v1.5.4**

- The mouse does more in the menus. On the team screen of a multi-player game click the arrows under a pig's name, or turn the wheel over the pig, to change its class. When a pig is promoted, click the picture of its career path. A name is typed by clicking the letters. On the Load and Save screens point at a saved game and click it.
- On the team screen of a multi-player game the Left key on a button moves the cursor to the pigs.

**v1.5.3**

- Fixed: holes in the ground in the sniper sight on a wide screen, with the sky showing through. The renderer ran out of room for the ground there; now it leaves out what is outside the picture.
- The keys set for the four directions in Options → Controls (for example W A S D) also move the cursor in the menus. The arrow keys work as before.
- The mouse works on the team screen: point at a pig or a button, click a pig to open its menu (promote, swap position, rename).

**v1.5.2**

- The sniper sight fits the screen. The game drew it at one size, made for 640×480 to 1024×768: on a larger or a wide screen the black frame ended before the edges and the lens was small. Now the black reaches every edge, the lens takes nine tenths of the height and has a soft shadow at the rim, and thin cross-hairs run across it.
- Fixed: the message banner at the top of a battle lost letters. With the Russian translation 17 letters were missing, so the message about a crate could show a few letters or none.
- Fixed: a long message in the banner went blank before its end.

**v1.5.1**

- Options → Controls shows every action on one screen: each of the twelve slots holds one action, the name over the key. The game showed six lines and scrolled.
- Mouse buttons can be keys: choose an action and press a mouse button. The screen shows it as MOUSE 1 to MOUSE 5, and in a battle the button does that action.
- Delete on that screen puts the default keys back.
- This screen was tested with keys and mouse messages sent to the game window, not yet by hand with a real key or mouse button: please report what does not work.

**v1.5.0**

- The mouse works in the menus: point at a line to select it, click to enter, the right button goes back, the wheel moves up and down.
- Network games are steadier. The menu no longer freezes while the game connects. A lost connection is noticed within 15 seconds, and a battle goes on for the players who stay; before, the player who lost the connection got a frozen game. A new game shows in the list within a second or two, not ten.
- **All players of a network game need v1.5.0**: this version and v1.4.0 do not see each other's games.
- The games online are also a web page: open the address from `[Network] Server` of `hogs.ini` in a browser, with `https://` in place of `wss://`.
- The fix also installs on a copy of the game that is not from Steam: version 1.2 with a `warhogs.exe` without copy protection.
- The game starts with the software renderer of Windows when the graphics card cannot be used.

**v1.4.0**

- Network games over the internet. In the game choose Multi-player, Network, then "- INTERNET -": you see the games of other players of the fix, and the last line makes your own. Nobody needs an open port, and players do not see each other's IP addresses. Two to four players, as in the game's own network play.
- The list of games shows the players of each game and your ping: `BRAVO'S GAME 2-4 125MS`. Your name and a password for games with friends are in the `[Network]` section of `hogs.ini`.
- Watching another player's turn in a network battle is smooth. It was shown at about 8 frames a second.
- Tested with two copies of the game on one PC only: please report how it works between real PCs. To get the game's own network play back (local network, IP address), empty `Server=` in `[Network]`.

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

**Что нового в v1.5.4**: мышь в меню умеет больше. На экране команды в игре на несколько игроков класс свиньи меняется щелчком по стрелкам под её именем или колесом над свиньёй. При повышении свиньи путь карьеры выбирается щелчком по его картинке. Имя набирается щелчками по буквам: стрелка справа стирает, черта ставит пробел, OK принимает имя. На экранах загрузки и сохранения наведите указатель на сохранённую игру и щёлкните. Клавиша «влево» на кнопке экрана команды в игре на несколько игроков переводит курсор на свиней.

**v1.5.3**: исправлены дыры в земле в оптическом прицеле на широком экране: куски земли пропадали, и сквозь них было видно небо. Рендеру там не хватало места под вершины земли; теперь он не берёт то, что за краем картинки. Клавиши, назначенные на четыре направления в «Настройки → Управление» (например W A S D), двигают курсор и в меню; стрелки работают как раньше. На экране команды работает мышь: наведите указатель на свинью или кнопку, щелчок по свинье открывает её меню (повышение, смена места, имя). Игра хранит клавиши вместе с командой: после перезапуска меню слушается назначенных клавиш, когда загружена сохранённая команда.

**v1.5.2**: оптический прицел рисуется под разрешение боя. Игра рисовала его одного размера, рассчитанного на 640×480–1024×768: на большом или широком экране чернота не доходила до краёв, а линза была маленькой. Теперь чернота доходит до всех краёв, линза занимает девять десятых высоты, у её края мягкая тень, через линзу идёт тонкое перекрестие. Исправлен баннер сообщений вверху экрана в бою: он терял буквы. С русским переводом пропадали 17 букв (Б Г Д Ё Ж З И Й Л П У Ф Ц Ч Ш Щ Ъ), и сообщение о подобранном ящике могло показать несколько букв или ни одной. Длинное сообщение больше не гаснет перед концом.

**v1.5.1**: экран «Настройки → Управление» показывает все действия сразу. В каждом из двенадцати гнёзд доски одно действие: название и под ним клавиша; раньше игра показывала шесть строк и прокручивала их. Кнопки мыши назначаются как клавиши: выберите действие и нажмите кнопку мыши, на экране она называется MOUSE 1 – MOUSE 5 (с русским переводом — МЫШЬ), в бою кнопка делает это действие. Delete на этом экране возвращает исходные клавиши. Экран проверен клавишами и сообщениями мыши, посланными в окно игры, но ещё не руками с настоящей клавишей и кнопкой мыши: напишите, если что-то не работает.

**v1.5.0**: в меню работает мышь. Наведение выбирает строку, щелчок входит, правая кнопка возвращает назад, колесо двигает выбор. Сетевая игра стала устойчивее: меню не замирает при подключении, обрыв связи замечается за 15 секунд, и бой продолжается для оставшихся (раньше у игрока с оборванной связью игра зависала); новая игра появляется в списке за секунду-две, а не за десять. **У всех игроков сетевой игры должна быть версия 1.5.0**: она и 1.4.0 не видят игры друг друга. Игры онлайн видны и на веб-странице: откройте в браузере адрес из `[Network] Server` файла `hogs.ini`, заменив `wss://` на `https://`. Исправление ставится и на копию игры не из Steam: версию 1.2 с `warhogs.exe` без защиты от копирования. Если видеокарту использовать нельзя, игра запускается на программном рендере Windows.

**v1.4.0**: сетевая игра через интернет. В игре выберите «Много игроков», «Сеть», затем «- INTERNET -»: откроется список игр других игроков с исправлением, последняя строка создаёт свою. Открывать порты не нужно, игроки не видят IP-адреса друг друга. В списке видно число игроков каждой игры и ваш пинг: `BRAVO'S GAME 2-4 125MS`. Имя и пароль для игры с друзьями задаются в разделе `[Network]` файла `hogs.ini`. Чужой ход в сетевом бою идёт плавно, раньше он показывался примерно с 8 кадрами в секунду. Проверено только на двух копиях игры на одном компьютере: напишите, как она работает у вас. Чтобы вернуть обычную сетевую игру (локальная сеть, IP-адрес), сотрите адрес в `Server=` раздела `[Network]`.

**v1.3.2**: исправлен вылет с русским переводом. Игра закрывалась на экране, где называют команду или свинью, при нажатии D, S и ещё 13 клавиш с буквами. Если перевод уже стоит, после `patch.cmd` запустите `russian.cmd` ещё раз: он поставит версию 1.1.1 перевода. На этом экране клавиатура теперь печатает буквы включённой раскладки: русские при русской, латинские при английской.

**v1.3.1**: `russian.cmd` ставит новую версию русского перевода. В ней весь русский алфавит и вся латиница во всех шрифтах, исправленный текст («НАСТРОЙКИ», «ЭКСПЕРТ», «МАТЧ» вместо «НАСТРОИКИ», «ЕКСПЕРТ», «МАТ4»), читаются английские имена и титры.

**v1.3.0**: русский перевод игры (текст, шрифты, ролики и озвучка) теперь ставится из исправления. Дважды щёлкните `HogsFix\russian.cmd`: он скачает перевод (около 330 МБ) из репозитория [hogs-of-war-ru](https://github.com/accidev/hogs-of-war-ru), проверит его и положит в папку игры. На компьютере с русским языком Windows `patch.cmd` сам спросит, ставить ли перевод. Английский возвращает `HogsFix\russian-remove.cmd`, а также `uninstall.cmd`. В самой игре с v1.2.2 ничего не изменилось.

**v1.2.2**: вода больше не течёт слишком быстро на мониторах с высокой частотой (на 360 Гц она шла в 14 раз быстрее оригинала). Убраны тонкие просвечивающие полоски между частями интерфейса (часы, окно оружия). Имена над свиньями больше не съезжают на головы в больших разрешениях. С `[Display] Widescreen=1` на широком мониторе бои теперь широкие и тогда, когда раньше было сохранено разрешение 4:3. Игра запускается в самом большом разрешении для вашего монитора. Интерфейс рисуется в пикселях игры: чтобы он был крупнее, зажмите Shift при запуске и выберите меньше строк, чёткость картинки та же.

**v1.2.1**: настройка `[Display] Windowed` убрана, с `Windowed=0` игра работала невидимо. Полный экран включает `[Display] Borderless=1`.

**v1.2.0**: в папке игры на одну DLL меньше, в самой игре ничего не меняется. `hogs.dll` больше нет: её исправления теперь в `ddraw.dll` вместе с рендером. `patch.cmd` сам обновляет игру с версиями 1.0–1.1 и удаляет `hogs.dll`. Исправленной игре нужен `ddraw.dll` из этого исправления: с dgVoodoo2 или другим `ddraw.dll` поверх него игра не запустится, чинит повторный запуск `patch.cmd`. Если установка или удаление прервётся на любом шаге, игра останется запускаемой. Удаляйте исправление файлом `uninstall.cmd` этой версии, а не оставшимся от 1.0–1.1. Все изменения по версиям: [CHANGELOG.md](https://github.com/accidev/hogs-of-war-fix/blob/main/CHANGELOG.md).

**Установка**: скачайте **HogsFix.zip**, распакуйте в папку игры (Steam → правой кнопкой по игре → Управление → Просмотреть локальные файлы) и запустите `HogsFix\patch.cmd`. Видео установки: https://youtu.be/oBo27HB4x_4. Удаление: `HogsFix\uninstall.cmd`. Подробнее: [README на русском](https://github.com/accidev/hogs-of-war-fix/blob/main/README.ru.md).
