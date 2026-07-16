# BauPlayerMusic DDNet 听歌服

BauPlayerMusic 是基于 DDNet 的 Windows 服务端分支，在 DDRace 玩法上加入了点歌、地图内音频、QQ 群联动、汉字展示、彩色名字、管理员工具以及多种休闲小游戏。

本仓库是可重新编译的干净源码目录。它包含运行所需的 `a.map`、原始地图副本、中文笔画库、字体、海龟汤题库和谁是卧底词库；不包含编译产物、参考项目、已下载歌曲、Cookie、SQLite 数据库、日志或任何真实密钥。

## 主要功能

- 游戏内搜索、点歌、队列、切歌投票、猜歌和点歌排行。
- Python 后端下载音频、歌词和封面，并通过 `music_map_patcher` 生成候选地图。
- NapCat / OneBot 11 QQ 群队列查询、喊话和 QQ 身份绑定。
- 海龟汤、谁是卧底、今日人品、掷骰子和随机选择。
- 原地单挑：支持霰弹枪、榴弹枪、激光枪组合以及独立原版 tune。
- 五子棋、四子棋、飞行棋和羽毛球。
- FDDR 风格彩色名字、`mod_alert`、汉字 `chid`、Chris 管理和队列管理命令。
- 热重载状态恢复与小游戏实体清理保护。

默认地图为 `a`：

- 服务端地图：`data/maps/a.map`
- 音乐缝图原图：`data/originmaps/a.map`

两个文件在发布时应保持相同；后端只修改候选副本，不直接覆盖原图。

## 仓库结构

```text
.
├─ src/                         DDNet 与 BauPlayerMusic C++ 源码
├─ datasrc/                     协议与内容生成源
├─ cmake/ scripts/ other/       构建、打包和平台文件
├─ ddnet-libs/                  官方依赖 Git 子模块
├─ data/
│  ├─ maps/a.map                默认运行地图
│  ├─ originmaps/a.map          地图缝音频使用的干净原图
│  ├─ fonts/                    DDNet 字体与中文字体
│  ├─ chinese_strokes.txt       chid 汉字笔画库
│  └─ musico/
│     ├─ turtle_soup_cases.json 海龟汤题库
│     └─ undercover_words.txt   谁是卧底词库
├─ mds.py                       Python 主后端
├─ admin_panel.py               本机 Web 管理面板后端
├─ admin_web/                   无外部 CDN 的现代化 WebUI
├─ qqbot_bridge.py              NapCat / OneBot 11 桥接
├─ turtle_soup.py               海龟汤逻辑
├─ undercover_game.py           谁是卧底逻辑
├─ .env.example                 带中文注释的后端配置模板
└─ myServerconfig.example.cfg   服务端安全配置模板
```

运行时生成的歌曲、封面、候选地图、Web 地图、SQLite、日志和 Cookie 均已加入 `.gitignore`。

## Windows Server 2022 环境要求

建议安装：

1. Git for Windows。
2. CMake 3.20 或更新版本。
3. Visual Studio 2022 Build Tools 或更新版本，勾选“使用 C++ 的桌面开发”和 Windows SDK。
4. Python 3.11 或 3.12 x64，并勾选 `Add Python to PATH`。
5. FFmpeg x64，将 `ffmpeg.exe` 所在目录加入系统 `PATH`。

检查命令：

```powershell
git --version
cmake --version
python --version
ffmpeg -version
```

## 克隆源码

必须同时拉取 `ddnet-libs` 子模块：

```powershell
git clone --recurse-submodules <你的仓库地址> BauPlayerMusic
cd BauPlayerMusic
```

如果已经普通克隆：

```powershell
git submodule update --init --recursive
```

## 编译服务端

在“Developer PowerShell for VS”中执行。以下配置只构建 Windows x64 服务端，关闭客户端、MySQL 和 WebSocket 客户端依赖：

```powershell
cmake -S . -B build-server -A x64 `
  -DCLIENT=OFF `
  -DWEBSOCKETS=OFF `
  -DMYSQL=OFF `
  -DPREFER_BUNDLED_LIBS=ON

cmake --build build-server --config Release --target game-server -- /m
```

若机器安装的是 Visual Studio 18 2026，可显式指定：

```powershell
cmake -S . -B build-server -G "Visual Studio 18 2026" -A x64 `
  -DCLIENT=OFF -DWEBSOCKETS=OFF -DMYSQL=OFF -DPREFER_BUNDLED_LIBS=ON
```

主要产物：

```text
build-server/Release/DDNet-Server.exe
build-server/Release/music_map_patcher.exe
```

构建后 CMake 会把 Python 后端、管理面板、requirements、启动脚本、`.env.example`、`myServerconfig.example.cfg`、中文笔画库和小游戏题库复制到服务端产物附近。DDNet 的数据目标也会准备运行所需的 `data` 文件。

## 首次部署

推荐把运行文件放在独立目录，例如 `C:\BauPlayerMusic`，不要直接在源码目录长期运行。

最低目录结构：

```text
C:\BauPlayerMusic\
├─ DDNet-Server.exe
├─ music_map_patcher.exe
├─ mds.py
├─ admin_panel.py
├─ admin_web\
├─ mds_aliyun.py
├─ qqbot_bridge.py
├─ music_stats.py
├─ turtle_soup.py
├─ undercover_game.py
├─ requirements.txt
├─ start_bpmusic_windows.ps1
├─ start_bpmusic_windows.cmd
├─ start_admin_panel.ps1
├─ start_admin_panel.cmd
├─ .env
├─ myServerconfig.cfg
└─ data\
   ├─ maps\a.map
   ├─ originmaps\a.map
   ├─ fonts\...
   ├─ chinese_strokes.txt
   └─ musico\
      ├─ turtle_soup_cases.json
      └─ undercover_words.txt
```

复制配置模板：

```powershell
Copy-Item .env.example .env
Copy-Item myServerconfig.example.cfg myServerconfig.cfg
```

然后：

- 编辑 `.env`，每个字段已有中文注释。
- 修改 `myServerconfig.cfg` 的服务器名和 RCON 密码。
- 保持 `sv_map "a"`。
- 首次测试时保持 `sv_register 0`，不要立刻公开。

### 安装 Python 依赖

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -r requirements.txt -i https://pypi.tuna.tsinghua.edu.cn/simple
```

若 PowerShell 禁止激活脚本，可以不激活，直接执行：

```powershell
.\.venv\Scripts\python.exe -m pip install -r requirements.txt -i https://pypi.tuna.tsinghua.edu.cn/simple
```

## 启动

### 一键检查和启动

```powershell
.\start_bpmusic_windows.ps1
```

或双击：

```text
start_bpmusic_windows.cmd
```

启动器会检查服务端、Python、依赖、FFmpeg、后端端口以及可选的 NapCat WebSocket。

### Web 管理面板

双击：

```text
start_admin_panel.cmd
```

或在 PowerShell 中运行：

```powershell
.\start_admin_panel.ps1
```

浏览器会打开 <http://127.0.0.1:8787/>。面板提供：

- DDNet 服务端和 Python 后端的状态、启动、停止、重启与日志查看。
- 带字段说明的 `.env` 和 `myServerconfig.cfg` 图形化编辑。
- QQBot / NapCat WebSocket 连通性与桥接状态检查。
- 海龟汤题库解析、题目数量和 DeepSeek 配置状态检查。
- S3/雨云对象存储端点、密钥、桶权限测试。
- FFmpeg、地图缝合器、默认地图、字体和运行数据完整度检查。

安全规则：

- 默认仅监听 `127.0.0.1`，适合在 Windows Server 远程桌面里打开。
- 密钥不会由 API 回传到浏览器；页面只显示“已配置”，留空保存会保留原值。
- 面板保存配置时会保留 `.env` 中的中文注释，并采用临时文件原子替换。
- 面板只允许停止自己启动的进程，不会误杀手动启动或由其他服务管理器启动的进程。
- 若确实要监听非本机地址，必须先设置高强度 `BPMUSIC_PANEL_TOKEN`；仍建议通过反向代理、TLS 和防火墙访问。

### 手动启动

先启动后端：

```powershell
python .\mds.py
```

确认健康检查：

```powershell
Invoke-RestMethod http://127.0.0.1:5000/health
```

再启动服务端：

```powershell
.\DDNet-Server.exe
```

不要把 `BPMUSIC_HOST` 改为 `0.0.0.0`，除非你明确需要远程访问并已增加反向代理、认证和防火墙规则。正常部署只需 DDNet 服务端访问 `127.0.0.1:5000`。

## `.env` 配置要点

完整解释见 [`.env.example`](.env.example)。最容易混淆的是：

- `BPMUSIC_QQBOT_ACCESS_TOKEN`：NapCat 网络配置生成的 Token。
- `BPMUSIC_QQBOT_SERVER_TOKEN`：DDNet 服务端与 Python 后端之间自建的至少 64 位随机密钥。
- `sv_music_qq_token`：必须与 `BPMUSIC_QQBOT_SERVER_TOKEN` 相同。
- `BPMUSIC_S3_ENDPOINT_URL`：后端上传使用的 API/内网端点。
- `BPMUSIC_S3_PUBLIC_DOMAIN`：玩家下载地图使用的公网域名，不能写仅服务器可达的内网地址。

生成随机共享密钥：

```powershell
-join ((1..96) | ForEach-Object { 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'[(Get-Random -Maximum 62)] })
```

不要把输出提交到 Git。

## FFmpeg

FFmpeg 不放进源码仓库。安装后确保下面命令可用：

```powershell
ffmpeg -version
where.exe ffmpeg
```

也可以把 `ffmpeg.exe` 放到运行目录并在启动服务前把该目录加入 `PATH`。

## NapCat / QQBot（可选）

1. 安装并登录 NapCat。
2. 在网络配置中创建 OneBot 11 WebSocket 服务端，例如 `ws://127.0.0.1:3001`。
3. 生成 Access Token，填入 `.env` 的 `BPMUSIC_QQBOT_ACCESS_TOKEN`。
4. 填写允许的 `BPMUSIC_QQBOT_GROUP_IDS`。
5. 生成独立随机密钥，同时填写：
   - `.env`：`BPMUSIC_QQBOT_SERVER_TOKEN`
   - `myServerconfig.cfg`：`sv_music_qq_token`
6. 设置 `BPMUSIC_QQBOT_ENABLED=1` 和 `sv_music_qq_relay 1`。
7. 重启 Python 后端与 DDNet 服务端。

NapCat WebUI Token、NapCat Access Token、DDNet/后端共享 Token 是不同用途，不要混用。

## 对象存储与内网上传（可选）

本地播放不需要对象存储。需要让公网玩家通过 HTTPS 获取候选地图时再配置 S3/OSS。

雨云同地域内网上传示例：

```dotenv
BPMUSIC_S3_ENDPOINT_URL=https://cn-nb1.internal.rains3.com
BPMUSIC_S3_ACCESS_KEY=在服务器本地填写
BPMUSIC_S3_SECRET_KEY=在服务器本地填写
BPMUSIC_S3_BUCKET_NAME=你的桶名
BPMUSIC_S3_PUBLIC_DOMAIN=https://你的公网访问域名
```

如果 `*.internal.rains3.com:443` 不通，先检查云服务器是否有同地域私网 IP。Ping 被禁并不能单独证明服务异常，应使用：

```powershell
Test-NetConnection cn-nb1.internal.rains3.com -Port 443
```

## 防火墙与公网前检查

- DDNet 默认需要放行服务端实际监听端口的 UDP 入站，例如 UDP 8303。
- Python 5000、NapCat 3001、RCON 和数据库端口不应直接暴露公网。
- 设置高强度 `sv_rcon_password`。
- `.env`、Cookie、对象存储 Key、NapCat Token 不得提交 Git。
- 先使用 `sv_register 0` 完成本机和内网测试，再决定是否设置为 `1`。
- 建议限制 Windows Defender 防火墙来源，并定期备份地图和运行数据库。

## 常用玩家命令

| 命令 | 功能 |
|---|---|
| `/song 歌名`、`/choose 编号` | 搜索并选择歌曲 |
| `/mls`、`/skip` | 查看队列、发起切歌 |
| `/musicrank`、`/myrank` | 点歌排行与个人记录 |
| `/guess`、`/guessrank` | 猜歌 |
| `/bindqq`、`/bindstatus` | QQ 身份绑定 |
| `/jrrp`、`/roll`、`/pick` | 休闲命令 |
| `/uc` | 谁是卧底 |
| `/duel 昵称 [武器...]` | 原地单挑 |
| `/gomoku 昵称` | 五子棋邀请 |
| `/connect4 昵称` | 四子棋邀请 |
| `/fly 昵称1 [昵称2 昵称3]` | 2–4 人飞行棋邀请 |
| `/ready` | 棋类游戏准备 |
| `/ball`、`/red`、`/blue`、`/start 分数` | 羽毛球 |

邀请可以通过 `/... accept`、`/... decline`，也可以使用服务端弹出的 F3/F4 投票处理。

## 常用管理员命令

以下命令在 F2 远程控制台使用：

```text
queue_list
queue_search <歌名>
queue_insert_after <队列序号> <搜索结果序号>
queue_remove <队列序号>
queue_skip
queue_clear
queue_restart
queue_status

color <客户端ID>
chid <速度> <文字>
chid_grant <客户端ID> <精确昵称>
chid_revoke <客户端ID> <精确昵称>
chris <客户端ID> <动作>
mod_alert <客户端ID> <消息>
mod_alert_all <消息>

flightchess_test
dice3d <客户端ID>
dice3d_clear
```

生产服不要把这些命令下放给普通玩家。

## 运行数据与备份

建议备份：

- `data/maps/a.map`
- `data/originmaps/a.map`
- `data/musico/*.sqlite3`
- DDNet 实际存储目录下的 `data/musico/playlist.txt`
- 服务器本地 `.env` 和 `myServerconfig.cfg`（加密保存，不上传 Git）

不必备份：

- `data/musicso/` 已下载歌曲和封面
- `data/musico/prepared_maps/`
- `data/webmaps/`
- `build-server/`

## 常见问题

### 服务端提示找不到 `a.map`

确认运行目录包含 `data/maps/a.map`，并检查 `myServerconfig.cfg`：

```cfg
sv_map "a"
```

### 点歌后找不到原图

确认 `data/originmaps/a.map` 存在，且 `.env` 中：

```dotenv
BPMUSIC_ORIGIN_MAPS_DIR=data/originmaps
```

### 海龟汤只有少量内置题目

确认存在：

```text
data/musico/turtle_soup_cases.json
```

题库在 `mds.py` 启动时加载，补文件后必须重启后端。

### 启动器找不到 FFmpeg

把 FFmpeg `bin` 目录加入系统 `PATH`，重新打开 PowerShell，再执行 `ffmpeg -version`。

### 后端正常但服务端无法点歌

确认：

- `Invoke-RestMethod http://127.0.0.1:5000/health` 成功。
- `sv_music_backend_url` 与 `.env` 监听端口一致。
- 服务端和后端对 `data` 路径的理解一致。
- Windows Defender 或安全软件没有阻止本机连接。

## 开发与提交

每次新增或删除源文件后重新运行 CMake 配置。提交前至少执行：

```powershell
cmake --build build-server --config Release --target game-server -- /m
python -m unittest discover -s tests
git status --short
```

严禁提交：

- `.env`、`myServerconfig.cfg`、Cookie 和真实 Token。
- `build*`、`deployment`、exe/dll/pdb。
- SQLite、日志、已下载歌曲、封面和候选地图。
- 参考项目、备份目录、NapCat/QQ 安装目录和临时素材。

## 许可证与上游

本项目基于 DDNet/Teeworlds 代码开发。上游许可证与贡献说明见 [license.txt](license.txt) 和 [CONTRIBUTING.md](CONTRIBUTING.md)。提交或分发时请保留相应版权与许可证文件。
