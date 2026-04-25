# KamyshTracker

KamyshTracker - нативный плагин для OBS Studio под Windows. Он читает текущий трек через Windows System Media Transport Controls (SMTC) и отвечает в Twitch-чат на команду с названием текущего трека.

Старое tray-приложение и локальный HTTP API удалены. Плагин больше не открывает `127.0.0.1:5050`, не предоставляет `/json` или `/widget` и не требует Browser Source в OBS.

## Возможности

- Читает текущую медиа-сессию через SMTC, с приоритетом Spotify и Yandex Music.
- Фильтрует не-музыкальные источники: браузеры, YouTube, Twitch, Netflix, Prime Video, Facebook, Instagram и похожие.
- Добавляет окно настроек `Tools -> KamyshTracker` внутри OBS.
- Авторизует Twitch отдельно через OAuth Authorization Code + PKCE.
- Проверяет, что OAuth-аккаунт совпадает с Twitch-аккаунтом, выбранным в OBS.
- Слушает чат через Twitch EventSub WebSocket.
- Отправляет ответы через Twitch Helix Send Chat Message API.

## Требования

- Windows 10/11 x64.
- OBS Studio 30+.
- Для установки готового release не нужны Visual Studio, CMake, Qt и OBS development files.
- Для сборки из исходников:
- Visual Studio 2022 Build Tools с MSVC.
- CMake 3.24+.
- Qt 6 с модулями Widgets, Network и WebSockets.
- OBS/libobs development files, доступные для CMake.
- Twitch application Client ID. Логин использует Twitch Device Code Flow, поэтому client secret не нужен.

## Установка из готового release

1. Скачайте `kamyshtracker-obs-plugin.zip` из последнего GitHub Release: <https://github.com/weazzylee/kamysh-tracker/releases/latest>.
2. Закройте OBS Studio.
3. Распакуйте архив в папку установки OBS Studio, обычно это `C:\Program Files\obs-studio`.
4. Проверьте, что файлы попали в эти папки:
   - `obs-plugins\64bit\kamyshtracker.dll`
   - `data\obs-plugins\kamyshtracker\locale\`
   - `bin\64bit\Qt6WebSockets.dll`
   - `bin\64bit\tls\`
5. Запустите OBS Studio и откройте `Tools -> KamyshTracker`.

## Сборка

Укажите пути к установленным Qt и OBS:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:\Path\To\Qt;C:\Path\To\OBS"
cmake --build build --config Release
cmake --install build --config Release --prefix "C:\Program Files\obs-studio"
```

Модуль плагина: `kamyshtracker.dll`.

## Настройка в OBS

1. Выберите и авторизуйте Twitch как текущий streaming service в OBS.
2. Откройте `Tools -> KamyshTracker`.
3. Оставьте Twitch Client ID по умолчанию или замените его на ID своего приложения.
4. Нажмите `Login`; браузер откроет Twitch activation, затем авторизуйте тот же Twitch-аккаунт, который используется в OBS.
5. Настройте команду и шаблоны ответа.

Значения по умолчанию:

- Команды: `!трек !track !shazam !шазам`
- Если играет музыка: `Сейчас играет: {artist} - {title}`
- Если ничего не играет: `Сейчас ничего не играет`

Плейсхолдеры:

- `{artist}`
- `{title}`
- `{track}`
- `{source}`

Если OBS не настроен на Twitch, KamyshTracker не будет отвечать на команды. Если OBS отдает Twitch login, плагин требует совпадения с OAuth-аккаунтом. Если OBS настроен через stream key и скрывает login, плагин использует OAuth-аккаунт и показывает это в окне настроек.

## Примечания

OBS не предоставляет поддерживаемый публичный API, через который сторонний плагин мог бы безопасно использовать Twitch OAuth-токен OBS. Поэтому KamyshTracker делает отдельную OAuth-авторизацию и использует совпадение аккаунта OBS как обязательную проверку безопасности.

Токены хранятся в конфиге текущего OBS-профиля `kamyshtracker.ini`. Access и refresh token не показываются в UI, их нельзя публиковать или передавать другим людям.
