For the Steam version of Hogs of War on Windows 10 and 11.

**New in v2.1.0**

- **Network battles stay together.** When the game of one player stood still for 15 seconds on his turn, the other copies removed him, but he was not told and played on alone, so the players ended up in different battles. Now only the lobby server says who has left, and every copy hears it at the same moment. A copy that cannot play its own turn for 15 seconds leaves the room by itself.
- While a battle waits for another player, the window is drawn and answers. A player who leaves while the battle loads no longer stops the others for a minute. When the player on turn leaves, the others go on at once.
- **Sound in network battles:** every player hears the battle at their own volumes. The game took the volumes of the host for everyone, so with the host's effects at zero the guests heard only music.
- **Local network play:** with `Server=` empty, press Enter on an empty address and the game finds the games on your local network. The address you type is shown, and the dot keys type a dot in any keyboard layout.
- Fixed: the game closed when Enter was pressed twice before the level menu of Multi-player appeared. A crashed game no longer stays in the list of processes.
- All players need 2.1.0: older versions do not see these games.

**v2.0.2**

- `hogs.log` tells more about network games: the time of every line, why a connection was lost, who is in the room, when a copy of the game stood still or waited for the player on turn, and a number for every turn that must be the same in the logs of all players.
- If a network game goes wrong, copy `hogs.log` before you start the game again: the game writes it anew at every start. Open an issue with the logs of all players and the time it happened.

**v2.0.1**

- Fixed: the keys set in Options → Controls were lost when the game was closed. The game kept them only in a saved team, so after a restart a network game, a multi-player game or a new team had the default keys. Now they are kept in the file `savekeys` next to the saved teams and work in every kind of game.
- A team that is loaded takes the keys of Options → Controls.
- Keys set before this version are in your saved teams: load one of them once, or set the keys once in Options → Controls, and from then on they stay.

**v2.0.0**

As a kid you wanted to play as a Legend. Or at least as an Ace. The game never let you: Legends stood only in the enemy team of the last mission, and no map had an Ace. Now you can.

- **Hero → Ace → Legend.** Once you have finished the campaign, every team of a campaign, a new one too, promotes a Hero to an Ace and an Ace to a Legend, for 8 promotion points each. Finished means that the game gave you the purple Team Lard: it does so after the last mission if you lost no more than two pigs. Keep a saved game of that team in the game folder, the fix knows by it.
- **Ace:** 175 health, all the weapons of a Hero, three Jetpacks, two Self Heals and an F-Rain Airstrike.
- **Legend:** 200 health, all the weapons of an Ace, two Heavy M-Guns, two Special Ops, two Airstrikes, a Super TNT in place of the TNT, and Shockwave without a count in place of the Poison Gas.
- A promotion takes nothing away, and both classes see mines as a Hero does. The team screen shows the names ACE and LEGEND and their rank stripes.
- **The Ace has its own look:** the head of a Commando, in a balaclava, on the body of a Legend, in a greatcoat.
- Fixed: the game could close at any moment in a menu or a battle. It read its lists of sounds past their end and sometimes damaged its own memory.
- To try the new classes at once: the cheat of the `[Cheats]` section of `hogs.ini`, F11 adds 10 promotion points.

**v1.5.6**

- In the view through a weapon's sight Forward raises the sight and Back lowers it. The game had them the other way round.
- Fixed: the tail of a pig seen from a distance bent and stretched with the body of the pig.
- A key or a click just before a story video no longer ends it: a video takes no key and no click in its first 1.5 seconds.

**v1.5.5**

- Fixed: the story videos of the campaign were not shown. A video ended on its first frame when the key that closed the mission results was still held. The campaign shows a video after missions 5, 10, 15, 20 and 24, and at the end.

**v1.5.4**

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

**Что нового в v2.0.1**: исправлено: клавиши из «Настройки → Управление» терялись, когда игру закрывали. Игра хранила их только в сохранённой команде, поэтому после перезапуска сетевая игра, игра на нескольких и новая команда получали исходные клавиши. Теперь клавиши лежат в файле `savekeys` рядом с сохранёнными командами и работают в любой игре. Загруженная команда берёт клавиши из «Управления». Клавиши, назначенные до этой версии, лежат в ваших сохранённых командах: один раз загрузите любую из них или один раз назначьте клавиши в «Управлении», дальше они сохранятся.

**v2.0.0**: в детстве вы мечтали сыграть за Легенду. Ну или хотя бы за Аса. Игра не давала: Легенды стояли только у врага в последней миссии, а Аса не было ни на одной карте. Теперь можно. Когда кампания пройдена, любая команда кампании, и новая тоже, повышает Героя до Аса и Аса до Легенды, по 8 очков повышения за шаг. Пройдена — значит, игра дала вам фиолетовую команду Team Lard (в переводе «Марди»): она даёт её после последней миссии, если потеряно не больше двух свиней. Держите в папке игры сохранение этой команды: по нему исправление это и узнаёт. Ас: 175 здоровья, всё оружие Героя, три джетпака, два самолечения и огненный удар. Легенда: 200 здоровья, всё оружие Аса, по два тяжёлых пулемёта, спец-опс и авиаудара, супер-ТНТ вместо ТНТ и шок без счёта вместо ядовитого газа. Повышение ничего не отнимает, оба класса видят мины, как Герой. Экран команды показывает названия классов и их нашивки: одна у Аса, две у Легенды. У Аса свой вид: голова Коммандо в балаклаве на теле Легенды в шинели. Исправлено: игра могла закрыться в любой момент в меню или в бою. Она читала списки звуков дальше их конца и иногда портила свою память. Чтобы попробовать новые классы сразу, включите чит из раздела `[Cheats]` файла `hogs.ini`: F11 даёт 10 очков повышения.

**v1.5.6**: в виде через прицел оружия «вперёд» поднимает прицел, «назад» опускает — в игре было наоборот. Исправлено: хвост свиньи вдали гнулся и растягивался вместе с туловищем. Нажатие или щелчок прямо перед сюжетным роликом больше его не закрывают: первые 1,5 секунды ролик не реагирует на клавиши и щелчки.

**v1.5.5**: исправлено: в кампании не показывались сюжетные ролики. Ролик закрывался на первом кадре, если клавиша, которой закрыли итоги миссии, была ещё зажата. Теперь клавиша или кнопка мыши закрывает ролик, только если нажата во время него. Ролики идут после побед в миссиях 5, 10, 15, 20 и 24 и в финале.

**v1.5.4**: мышь в меню умеет больше. На экране команды в игре на несколько игроков класс свиньи меняется щелчком по стрелкам под её именем или колесом над свиньёй. При повышении свиньи путь карьеры выбирается щелчком по его картинке. Имя набирается щелчками по буквам: стрелка справа стирает, черта ставит пробел, OK принимает имя. На экранах загрузки и сохранения наведите указатель на сохранённую игру и щёлкните. Клавиша «влево» на кнопке экрана команды в игре на несколько игроков переводит курсор на свиней.

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
