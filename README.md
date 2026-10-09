# Hogs of War на Windows 11

Цель: запустить Steam-версию Hogs of War (2000, v1.2, appid 389900) на Windows 11. В перспективе — переписать игру под x64 с современным DirectX.

## Почему игра не работала

1. **Зависание при запуске.** Обёртка CD-музыки `winmm.dll` (ogg-winmm 2014 года) вешала загрузчик DLL в Windows 11. Процесс не доходил до кода игры и грузил около двух ядер. Окно так и не появлялось.
2. **Блокировка рабочего стола.** После создания главного окна игра перебирает все окна системы (`EnumWindows`) и выключает каждое (`EnableWindow(FALSE)`). Включает их обратно только при выходе через меню. После Alt+Tab, падения или снятия процесса все программы остаются выключенными, и любой клик даёт звук ошибки. Вдобавок игра вызывает `SystemParametersInfoA(SPI_SCREENSAVERRUNNING)` — трюк Win9x для блокировки Alt+Tab.
3. **Графика.** Игра использует DirectDraw 7 и Direct3D времён DX6, работает только в 16 битах и до 1024×768.

## Что сделано

Файлы в папке игры (`..\`):

| файл | что это |
|---|---|
| `warhogs_.exe` | пропатченный exe, см. ниже; оригинал лежит в `backup\warhogs_.exe.orig` |
| `DDraw.dll`, `D3DImm.dll`, `dgVoodoo.conf`, `dgVoodooCpl.exe` | dgVoodoo2 2.87.5: DirectDraw и D3D через Direct3D 11/12, фальшивый полноэкранный режим без смены видеорежима, без водяного знака |
| `winmm.dll`, `winmm.ini` | ogg-winmm (ayuanx) v2025.01.16: музыка из `MUSIC\TrackNN.ogg`; старая обёртка лежит в `backup\` |

### Патч `warhogs_.exe`

1. **Снята защита LaserLock.** `wh32LIB.DLL` подменяла 325 вызовов API одним `call [CallDLL]`. Нужную функцию она находила по адресу возврата в зашифрованной таблице: запись 9 байт по адресу `0x10012118`, ключ меняется по кругу с периодом 9. Ключи восстановлены из вызовов, которые LaserLock уже сам переписал в памяти. Все 325 вызовов переписаны на прямые `call/jmp [IAT]`, импорт `wh32lib.dll` убран. Код самой игры защита не шифровала.
2. **Игра больше не трогает чужие окна.** В функции обратного вызова `0x44CE80` команда `jz` заменена на `jmp`.
3. **Убраны оба вызова `SPI_SCREENSAVERRUNNING`** (`0x44CFC1`, `0x47EA97`).

Если Steam проверит целостность файлов, он вернёт оригинальный exe. Тогда пересоберите патч, см. ниже.

## Структура

- `unlock-windows.cmd` — аварийный разблокировщик окон. Нужен только для непропатченной игры. Запускается через Win+R или диспетчер задач, один раз спрашивает права администратора.
- `tools\` — скрипты:
  - `ll_table.py`, `ll_unwrap.py` — снятие LaserLock;
  - `patch_exe.py` — патчи совместимости;
  - `dump.ps1`, `unmap_dump.py` — дамп распакованных модулей из памяти;
  - `sample.ps1`, `test_run.ps1` — профилирование потоков и прогон игры с подсчётом выключенных окон;
  - `peimp.py`, `pesym.py`, `peexp_bytes.py` — разбор PE.
- `ghidra\HogsOfWar.gpr` — проект Ghidra 12.1.4 с программами `warhogs_.exe`, `wh32LIB.DLL`, `wh32lib_unpacked.dll` и `warhogs_unwrapped.exe`. Последний — основной для изучения: в нём видны настоящие имена API.
- `downloads\` — архивы dgVoodoo2 и ogg-winmm.
- `backup\` — оригинальные файлы игры.
- `work\` — промежуточные файлы: дампы, таблица LaserLock, собранные exe.

## Пересборка патча

PowerShell 7. Скрипты `.ps1` с пометкой «32-bit» запускаются через `C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe`.

```powershell
$p = "D:\SteamLibrary\steamapps\common\HogsofWar\HoG x64"
$g = "D:\SteamLibrary\steamapps\common\HogsofWar"
# 1. Дамп. Игра должна быть оригинальной: LaserLock расшифровывает себя в памяти.
& C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe -File "$p\tools\dump.ps1" -Exe "$g\warhogs_.exe" -OutDir "$p\work\dumps"
# 2. Таблица и снятие защиты.
python -I "$p\tools\ll_table.py" "$p\work\dumps\wh32lib.dll_10000000.bin" "$p\work\dumps\warhogs_.exe_00400000.bin" "$p\work\ll_table.json"
python -I "$p\tools\ll_unwrap.py" "$p\backup\warhogs_.exe.orig" "$p\work\ll_table.json" "$p\work\warhogs_unwrapped.exe" "$p\work\ll_sites.json"
# 3. Патчи совместимости.
python -I "$p\tools\patch_exe.py" "$p\work\warhogs_unwrapped.exe" "$p\work\warhogs_patched.exe"
```

## Что дальше

- Проверить Alt+Tab в фальшивом полноэкранном режиме dgVoodoo.
- Поднять внутреннее разрешение (`Resolution` в `dgVoodoo.conf`) и снять ограничение 1024×768 в окне настроек.
- Большая цель: декомпиляция в C и сборка под x64 с D3D11 или SDL3.
