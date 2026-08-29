🌐 [English](README.md) | 🇨🇳 [中文](README.zh-CN.md)

# 用于 GoldSource 和 Xash3D 的 Half-Life SDK [![Build Status](https://github.com/FWGS/hlsdk-portable/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/FWGS/hlsdk-portable/actions/workflows/build.yml) [![Windows Build Status](https://ci.appveyor.com/api/projects/status/github/FWGS/hlsdk-portable?svg=true)](https://ci.appveyor.com/project/a1batross/hlsdk-portable)

用于 GoldSource 和 Xash3D 的 Half-Life SDK，包含一些 bug 修复。

<details><summary>更新日志</summary>
<p>

- 修复了猎头蟹偶尔卡住无法做任何事情的 bug。技术细节：现在在调用 `SetYawSpeed` 之前设置怪物的 `Activity`。[补丁](https://github.com/FWGS/hlsdk-portable/commit/467899b99aa225a95d90222137f18c141c929c86)
- 怪物现在按代码预期播放待机声音。技术细节：问题在于检查了错误的变量。[补丁](https://github.com/FWGS/hlsdk-portable/commit/9fc712da019a1ca646171e912209a993e7c43976)
- 修复了导致对话怪物（科学家和警卫）在脚本序列中有时面向错误方向的 bug。[补丁](https://github.com/FWGS/hlsdk-portable/commit/3e2808de62e479e83068c075cb88b4f177f9acc7)
- 修复了小队成员移除。此 bug 影响了猎头蟹的攻击，因为它们的攻击取决于感知到的小队成员数量。[补丁](https://github.com/FWGS/hlsdk-portable/commit/b4502f71336a08f3f2c72b7b061b2838a149a11b)
- 科学家现在对气味做出反应。[补丁](https://github.com/FWGS/hlsdk-portable/commit/2de4e7ab003d5b1674d12525f5aefb1e57a49fa3)
- Tau-cannon (gauss) 播放待机动画。
- Tau-cannon (gauss) 光束颜色取决于充能，就像在 Half-Life 中引入预测代码之前一样。[补丁](https://github.com/FWGS/hlsdk-portable/commit/0a29ec49c8183ebb8da22a6d2ef395eae9c3dffe)
- 恢复了单人游戏中的 gluon 闪光。[补丁](https://github.com/FWGS/hlsdk-portable/commit/9d7ab6acf46a8b71ef119d9c252767865522d21d)
- 手榴弹在收起后不再保持待发状态，防止切换武器后爆炸。[补丁](https://github.com/FWGS/hlsdk-portable/commit/6e1059026faa90c5bfe5e3b3f4f58fde398d4524)
- 修复了恢复后手电筒电池显示为耗尽的问题。
- 修复了读取 sentences.txt 时可能的溢出。[补丁](https://github.com/FWGS/hlsdk-portable/commit/cb51d2aa179f1eb622e08c1c07b053ccd49e40a5)
- 修复了恢复后光束附件失效（导致视觉 bug）。[补丁](https://github.com/FWGS/hlsdk-portable/commit/74b5543c83c5cdcb88e9254bacab08bc63c4c896)
- 修复了外星控制器在非战斗状态下面向错误方向。[补丁](https://github.com/FWGS/hlsdk-portable/commit/e51878c45b618f9b3920b46357545cbb47befeda)
- 修复了快速切换武器时武器部署动画有时不播放的 bug。[补丁](https://github.com/FWGS/hlsdk-portable/commit/ed676a5413c2d26b2982e5b014e0731f0eda6a0d) [补丁2](https://github.com/FWGS/hlsdk-portable/commit/4053dca7a9cf999391cbd77224144da207e4540b)
- 修复了绊线地雷拾取时身体错误的问题[补丁](https://github.com/FWGS/hlsdk-portable/commit/abf08e4520e3b6cd12a40f269f4a256cf8496227)

编译时可启用的 bug 修复相关宏：

- **CROWBAR_DELAY_FIX** 修复了撬棍在第一次攻击后延迟更长的 bug。
- **CROWBAR_FIX_RAPID_CROWBAR** 修复了攻击已杀死怪物尸体时的"快速撬棍"bug。
- **GAUSS_OVERCHARGE_FIX** 修复了 tau-cannon (gauss) 过充后充能声音不停止的问题。
- **CROWBAR_IDLE_ANIM** 使撬棍播放待机动画。
- **TRIPMINE_BEAM_DUPLICATION_FIX** 修复了绊线地雷光束在关卡切换时重复的问题。
- **HANDGRENADE_DEPLOY_FIX** 使手榴弹在投掷完成后播放取出动画。
- **WEAPONS_ANIMATION_TIMES_FIX** 修复了某些武器的部署和待机动画时间。

HL25 相关宏，编译时可启用：

- **SATCHEL_OLD_BEHAVIOUR** 旧的 HL 25 周年前的沙包行为。
- **SPEAKABLE_TARGETS** 使 cycler 和 func_button 可说话（会破坏 amxmodx 插件）。

Bug 修复相关服务器 cvar：

- **satchelfix**：如果设置为 1，门不会被沙包阻挡。修复了 `crossfire` 地图上的著名漏洞。
- **explosionfix**：如果设置为 1，爆炸伤害不会穿透薄墙。
- **selfgauss**：如果设置为 0，玩家在射击厚墙时不会被副攻击伤害。

*注意*：这些宏和 cvar 在 [hlfixed](https://github.com/FWGS/hlsdk-portable/tree/hlfixed) 分支中进行了调整（更多信息请阅读[这里](https://github.com/FWGS/hlsdk-portable/wiki/HL-Fixed)）。bug 修复宏在 `master` 分支中保持关闭状态，以维持与原版服务器和客户端的兼容性。

其他服务器 cvar：

- **mp_bhopcap**：如果设置为 0，禁用连跳限制。
- **chargerfix**：如果设置为 1，墙壁上的生命和弹药充电器在玩家满血或满甲时会播放拒绝声音。
- **corpsephysics**：如果设置为 1，已杀死怪物的尸体会因冲击而飞起一点。这是 Half-Life 中被剪切的功能。

</p>
</details>

<details><summary>Mod 支持</summary>
<p>

此仓库包含一些 mod 的（重新）实现，作为从 `master` 派生的单独分支。支持的 mod 列表可在[这里](https://github.com/FWGS/hlsdk-portable/wiki/Mods)找到。注意某些分支不稳定且不完整。

要本地获取 mod 分支，请运行以下 git 命令：

```
git fetch origin asheep:asheep
```

这假设你已将 **FWGS/hlsdk-portable** 设置为 `origin` 远程并想获取 `asheep` 分支。

</p>
</details>

# 获取源代码

通过 [git](https://git-scm.com/downloads) 克隆仓库或通过 GitHub 上的 **Code** 按钮下载 ZIP。第一种方法更可取，因为它还允许你搜索仓库历史、切换分支和克隆 vgui 子模块。

要在 Git Bash（Windows 上）或终端（类 Unix 操作系统上）中使用 git 克隆仓库：

```
git clone --recursive https://github.com/FWGS/hlsdk-portable
```

# CI 构建

最新的构建始终可在[构建产物](https://github.com/FWGS/hlsdk-portable/actions)中获取，适用于大多数常见平台，如果你登录 GitHub 就可以下载。

所有分支和更多平台的构建最终会更新到 [hlsdk-mega-build](https://github.com/FWGS/hlsdk-mega-build/releases/tag/continuous) 仓库。

# 构建说明

## Windows x86

### 前提条件

安装并运行 [Visual Studio Installer](https://visualstudio.microsoft.com/downloads/)。安装程序允许你选择特定组件。选择 `Desktop development with C++`。你可以在安装详细信息中取消勾选不需要的内容，但必须保持 `MSVC` 和相应的 Windows SDK（例如 Windows 10 SDK 或 Windows 11 SDK）勾选。你也可以保持 `C++ CMake tools for Windows` 勾选，因为你需要 **cmake**。或者你可以从 [cmake.org](https://cmake.org/download/) 安装 **cmake**，在安装过程中勾选 *Add to the PATH...*。

### 打开命令提示符

如果 **cmake** 是通过 Visual Studio Installer 安装的，你需要通过 Windows `Start` 菜单运行 `Developer command prompt for VS`。如果 **cmake** 是通过 cmake 安装程序安装的，你可以运行常规的 Windows `cmd`。

在提示符中使用 `cd` 命令导航到 hlsdk 目录，例如：
```
cd C:\Users\username\projects\hlsdk-portable
```

注意：如果 hlsdk-portable 解压在另一个磁盘上，先导航到那里：
```
D:
cd projects\hlsdk-portable
```

### 构建

配置项目：
```
cmake -A Win32 -B build
```
注意如果你修改了 `CMakeLists.txt` 文件或想用不同参数重新配置项目，必须重复配置步骤。

下一步是编译库：
```
cmake --build build --config Release
```
`hl.dll` 和 `client.dll` 将出现在 `build/dlls/Release` 和 `build/cl_dll/Release` 目录中。

如果你有 mod 并想自动将库安装到 mod 目录，将 **GAMEDIR** 变量设置为目录名称，将 **CMAKE_INSTALL_PREFIX** 设置为你的 Half-Life 或 Xash3D 安装路径：
```
cmake -A Win32 -B build -DGAMEDIR=mod -DCMAKE_INSTALL_PREFIX="C:\Program Files (x86)\Steam\steamapps\common\Half-Life"
```
然后使用 `--target install` 参数调用 `cmake`：
```
cmake --build build --config Release --target install
```

#### 选择 Visual Studio 版本

你可以在配置步骤中通过指定 cmake 生成器来显式选择 Visual Studio 版本：
```
cmake -G "Visual Studio 16 2019" -A Win32 -B build
```

### Windows on ARM (ARM64)

要为 Windows on ARM（例如 Snapdragon X Elite）构建，省略 `-A Win32` — 当从 ARM64 开发者命令提示符或在 ARM64 原生系统上运行时，CMake 将自动目标为 ARM64：
```
cmake -B build
cmake --build build --config Release
```
输出 DLL 将命名为 `hl_arm64.dll` 和 `client_arm64.dll`。

### 在 Visual Studio 中编辑代码

配置步骤后，`HLSDK-PORTABLE.sln` 应出现在 `build` 目录中。你可以在 Visual Studio 中打开此解决方案并继续开发。

## Linux x86。使用 Steam Runtime 在 chroot 中进行便携式 Steam 兼容构建

### 前提条件

为 Linux 构建 Steam 兼容游戏的官方方式是通过 steam-runtime。

*注意*：对于基于 RHEL 的发行版，你可能需要使用系统 chroot 或 docker。

安装 schroot。在 Ubuntu 或 Debian 上：

```
sudo apt install schroot
```

克隆 https://github.com/ValveSoftware/steam-runtime 并按照说明操作：[下载](https://github.com/ValveSoftware/steam-runtime/blob/e014a74f60b45a861d38a867b1c81efe8484f77a/README.md#downloading-a-steam-runtime)和[设置](https://github.com/ValveSoftware/steam-runtime/blob/e014a74f60b45a861d38a867b1c81efe8484f77a/README.md#using-schroot) chroot。

```
sudo ./setup_chroot.sh --i386 --tarball ./com.valvesoftware.SteamRuntime.Sdk-i386-scout-sysroot.tar.gz
```

### 构建

现在你可以通过在命令前加上 `schroot --chroot steamrt_scout_i386 --` 来使用 cmake 和 make：
```
schroot --chroot steamrt_scout_i386 -- cmake -DCMAKE_BUILD_TYPE=Release -B build-in-steamrt -S .
schroot --chroot steamrt_scout_i386 -- cmake --build build-in-steamrt
```

## Linux x86。不使用 Steam Runtime 的便携式 Steam 兼容构建

### 前提条件

安装 C++ 编译器、cmake 和 C、C++ 以及 SDL2 的 x86 开发库。

#### Ubuntu/Debian：
```
sudo apt install cmake build-essential gcc-multilib g++-multilib libsdl2-dev:i386
```

#### RedHat/Fedora/CentOS：
```
sudo dnf install cmake gcc gcc-c++ glibc-devel.i686 SDL2-devel.i686
```

### 构建

```
cmake -DCMAKE_BUILD_TYPE=Release -B build -S .
cmake --build build
```

注意以此方式构建的库可能与 Steam Half-Life 不兼容。如果你遇到此问题，可以配置为使用 c++ 和 gcc 库静态构建：
```
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc" -B build -S .
cmake --build build
```

或者，你可以使用小型 libsupc++ 库和优化构建标志来避免 libstdc++/libgcc_s 链接（实际上只需设置 Release 构建类型并将 C 编译器设置为 C++ 编译器）：
```
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cc -B build -S .
cmake --build build
```
为了确保可移植性，最好使用 Steam Runtime 或其他旧发行版的 chroot 进行构建。

## Linux x86。在自己的 chroot 中进行便携式 Steam 兼容构建

### 前提条件

使用最适合你的方式来创建旧发行版的 32 位 chroot。例如在 Ubuntu/Debian 上，你可以使用 debootstrap。

```
sudo apt install debootstrap schroot
sudo mkdir -p /var/choots
sudo debootstrap --arch=i386 jessie /var/chroots/jessie-i386 # 在 Ubuntu 上输入 trusty 代替 jessie
sudo chroot /var/chroots/jessie-i386
```

```
# 在 chroot 内
apt install cmake build-essential gcc-multilib g++-multilib libsdl2-dev
exit
```

在 /etc/schroot/chroot.d/jessie.conf 中创建并调整以下配置（你可以选择不同的名称）：

```
[jessie]
type=directory
description=Debian jessie i386
directory=/var/chroots/jessie-i386/
users=yourusername
groups=adm
root-groups=root
preserve-environment=true
personality=linux32
```

将你的实际用户名替换 `yourusername`。

### 构建

在任何 make 或 cmake 调用前加上 `schroot -c jessie --`：
```
schroot --chroot jessie -- cmake -DCMAKE_BUILD_TYPE=Release -B build-in-chroot -S .
schroot --chroot jessie -- cmake --build build-in-chroot
```

## Android
1. 设置 [Android Studio/Android SDK](https://developer.android.com/studio)。

### Android Studio
打开位于 `android` 文件夹中的项目并构建。

### 命令行
```
cd android
./gradlew assembleRelease
```

### 自定义构建
settings.gradle：
* **rootProject.name** - Android Studio 中显示的项目名称（可选）。

app/build.gradle：
* **android->namespace** 和 **android->defaultConfig->applicationId** - 将两者都设置为所需的包名。
* **getBuildNum** 函数 - 将 **releaseDate** 变量设置为所需值。

app/java/su/xash/hlsdk/MainActivity.java：
* **.putExtra("gamedir", ...)** - 设置所需的游戏目录。

src/main/AndroidManifest.xml：
* **application->android:label** - 设置所需的应用程序名称。
* **su.xash.engine.gamedir** 值 - 设置为与上面相同。

## Nintendo Switch

### 前提条件

1. 设置 [`dkp-pacman`](https://devkitpro.org/wiki/devkitPro_pacman)。
2. 安装依赖包：
```
sudo dkp-pacman -S switch-dev dkp-toolchain-vars switch-mesa switch-libdrm_nouveau switch-sdl2
```
3. 确保 `DEVKITPRO` 环境变量设置为 devkitPro SDK 根目录：
```
export DEVKITPRO=/opt/devkitpro
```
4. 安装 libsolder：
```
source $DEVKITPRO/switchvars.sh
git clone https://github.com/fgsfdsfgs/libsolder.git
make -C libsolder install
```

### 使用 CMake 构建
```
mkdir build && cd build
aarch64-none-elf-cmake -G"Unix Makefiles" -DCMAKE_PROJECT_HLSDK-PORTABLE_INCLUDE="$DEVKITPRO/portlibs/switch/share/SolderShim.cmake" ..
make -j
```

### 使用 waf 构建
```
./waf configure -T release --nswitch
./waf build
```

## PlayStation Vita

### 前提条件

1. 设置 [VitaSDK](https://vitasdk.org/)。
2. 安装 [vita-rtld](https://github.com/fgsfdsfgs/vita-rtld)：
   ```
   git clone https://github.com/fgsfdsfgs/vita-rtld.git && cd vita-rtld
   mkdir build && cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make -j2 install
   ```

### 使用 waf 构建：
```
./waf configure -T release --psvita
./waf build
```

### 使用 CMake 构建：
```
mkdir build && cd build
cmake -G"Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" -DCMAKE_PROJECT_HLSDK-PORTABLE_INCLUDE="$VITASDK/share/vrtld_shim.cmake" ..
make -j
```

## 其他平台

支持在其他架构（例如 x86_64 或 arm）和符合 POSIX 的操作系统（例如 FreeBSD）上构建。

### 前提条件

安装 C 和 C++ 编译器（如 gcc 或 clang）、cmake 和 make。

### 构建

```
cmake -DCMAKE_BUILD_TYPE=Release -B build -S .
cmake --build build
```

强制 64 位构建：
```
cmake -DCMAKE_BUILD_TYPE=Release -D64BIT=1 -B build -S .
cmake --build build
```

### 使用 waf 构建

要使用 waf，你需要安装 python（最低 2.7）

```
./waf configure -T release
./waf
```

强制 64 位构建：
```
./waf configure -T release -8
./waf
```

## 构建选项

一些在 cmake 步骤中可以设置的有用构建选项。

* **GOLDSOURCE_SUPPORT** - 允许开启/关闭 GoldSource 输入支持。在 x86 Windows 和 x86 Linux 上默认为 **ON**，在其他平台上为 **OFF**。
* **64BIT** - 允许开启/关闭 64 位构建。在 x86_64 Windows、x86_64 Linux 和 32 位平台上默认为 **OFF**，在其他 64 位平台上为 **ON**。
* **USE_VGUI** - 是否使用 VGUI 库。默认为 **OFF**。你需要初始化 `vgui_support` 子模块才能使用 VGUI 构建。

此列表不完整。查看 `mod_options.txt` 以查看所有可用选项及其默认值。

传递给 cmake 时在选项名称前加 `-D`。布尔选项可以取值 **OFF** 和 **ON**。示例：

```
cmake .. -DUSE_VGUI=ON -DGOLDSOURCE_SUPPORT=ON -DCROWBAR_IDLE_ANIM=ON -DCROWBAR_FIX_RAPID_CROWBAR=ON
```

要为你的 mod 添加新的构建选项，可以在 `mod_options.txt` 文件中添加以下格式：
```
<definition name>=<definition value> # <description>
```
如果 `definition value` 设置为 `OFF` 或 `ON`，它将被视为布尔值。否则将是字符串。`definition name` 和 `definition value` 都不能包含空白字符。
