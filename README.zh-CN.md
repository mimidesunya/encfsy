encfsy
======

🌐 **语言**: [English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Русский](README.ru.md) | [العربية](README.ar.md) | [Deutsch](README.de.md)

---

## 关于 encfsy
encfsy 是基于 **Dokany** 和 **Crypto++** 的 Windows 版 EncFS 实现。
**仅支持 64 位系统**。

该程序在保持目录树结构不变的情况下加密文件名和文件内容。
这使其非常适合与 Dropbox、Google Drive、rsync 或其他远程存储同步加密数据：文件保持端到端加密，存储管理员无法查看其内容。

## ⚠️ 双向云同步时请不要使用 `--paranoia`
启用 `--paranoia` 模式（外部 IV 链接）时，如果同步工具改动了文件名，即便手动把名字改回去，**文件内容仍会变成无法解密的垃圾数据**。
- 在 Dropbox/OneDrive/Google Drive 等双向同步场景中，请 **不要启用 `--paranoia`**。
- 如必须使用，请将其限制在文件名不变化的单向备份场景。

## 安全功能
encfsy 使用 **Windows 凭据管理器** 进行安全的密码管理。

- 密码使用 **DPAPI**（数据保护 API）加密，并与当前用户账户绑定
- 无需在 GUI 和 encfs.exe 之间通过标准输入传递密码，消除了拦截风险
- "记住密码"选项可保存密码，下次启动时自动输入
- 密码**按每个加密目录（rootDir）单独存储**

### 密码存储位置
保存的密码可以在控制面板 → 凭据管理器 → Windows 凭据中查看。
它们以 `EncFSy:c:\path\to\encrypted` 这样的名称显示。

## GUI 使用方法
使用 **encfsw.exe** 可以轻松地挂载和卸载卷，无需使用命令行。

1. 选择加密目录（rootDir）
2. 选择要挂载的驱动器号
3. 输入密码（勾选"Remember Password"可保存）
4. 点击"Mount"

"Show Advanced Options"可访问与命令行版本相同的详细设置。

## 从命令行使用凭据管理器
在 GUI 中勾选"Remember Password"进行挂载时，密码会保存到 Windows 凭据管理器。
之后，您可以使用 `--use-credential` 选项从命令行无需输入密码即可挂载。

```bash
# 1. 首先在 GUI 中勾选"Remember Password"进行挂载
#    → 密码保存到凭据管理器

# 2. 之后可以从命令行无需输入密码进行挂载
encfs.exe C:\Data M: --use-credential
```

## 文件名长度限制
encfsy 使用现代的*长路径* API，因此完整路径不受传统的 260 字符 **MAX_PATH** 限制。

但是 NTFS 仍然将每个路径组件（文件夹或文件名）限制在 **255 个 UTF-16 字符**。
由于加密会使名称增长约 30%，为了保持在该组件限制内并与不支持长路径的工具兼容，请将**每个文件名保持在 175 个字符以内**。

## 使用方法

```
用法: encfs.exe [选项] <rootDir> <mountPoint>

参数:
  rootDir      (例: C:\test)                要加密并挂载的目录
  mountPoint   (例: M: 或 C:\mount\dokan)    挂载位置 - 驱动器号（如 M:\）
                                             或空的 NTFS 文件夹

选项:
  -u <mountPoint>                              卸载指定的卷
  -l                                           列出当前挂载的 Dokan 卷
  -v                                           将调试输出发送到调试器
  -s                                           将调试输出发送到 stderr
  -i <ms>              (默认: 120000)          超时时间（毫秒），超时后操作中止并卸载卷
  --use-credential                             从 Windows 凭据管理器读取密码
                                               （密码保持存储状态）
                                               注意：必须先在 GUI 中勾选"Remember Password"
                                               保存密码
  --use-credential-once                        从 Windows 凭据管理器读取密码
                                               （读取后删除，一次性使用）
  --dokan-debug                                启用 Dokan 调试输出
  --dokan-network <UNC>                        网络卷的 UNC 路径 (例: \\host\myfs)
  --dokan-removable                            将卷显示为可移动媒体
  --dokan-write-protect                        以只读方式挂载文件系统
  --dokan-mount-manager                        向 Windows 挂载管理器注册卷
                                               （启用回收站支持等）
  --dokan-current-session                      仅在当前会话中显示卷
  --dokan-filelock-user-mode                   在用户模式下处理 LockFile/UnlockFile；
                                               否则 Dokan 自动管理
  --dokan-enable-unmount-network-drive         允许通过资源管理器卸载网络驱动器
  --dokan-dispatch-driver-logs                 将内核驱动程序日志转发到用户空间（较慢）
  --dokan-allow-ipc-batching                   为慢速文件系统（如远程存储）启用 IPC 批处理
  --public                                     在 CreateFile 中打开句柄时模拟调用用户
                                               需要管理员权限
  --allocation-unit-size <bytes>               卷报告的分配单元大小
  --sector-size <bytes>                        卷报告的扇区大小
  --volume-name <name>                         资源管理器中显示的卷名（默认: EncFS）
  --volume-serial <hex>                        十六进制卷序列号（默认: 从底层获取）
  --paranoia                                   启用 AES-256 加密、重命名 IV 和外部 IV 链
  --alt-stream                                 启用 NTFS 备用数据流
  --case-insensitive                           执行不区分大小写的文件名匹配
  --reverse                                    反向模式: 将明文 rootDir 加密显示在 mountPoint

示例:
  encfs.exe C:\Users M:                                    # 将 C:\Users 挂载为 M:\
  encfs.exe C:\Users C:\mount\dokan                        # 挂载到 NTFS 文件夹 C:\mount\dokan
  encfs.exe C:\Users M: --dokan-network \\myfs\share       # 以 UNC \\myfs\share 挂载为网络驱动器
  encfs.exe C:\Data M: --volume-name "安全驱动器"          # 使用自定义卷名挂载
  encfs.exe C:\Data M: --use-credential                    # 使用凭据管理器中存储的密码

要卸载，请在此控制台按 Ctrl+C 或运行:
  encfs.exe -u <mountPoint>
```

## 安装
1. 安装 **Dokany**（≥ 2.0）— 从[官方发布页面](https://github.com/dokan-dev/dokany/releases)下载
2. 从 [Releases 页面](https://github.com/mimidesunya/encfsy/releases)下载最新的 **encfsy 安装程序**，按照安装向导进行设置

## 许可证
[LGPL-3.0](https://www.gnu.org/licenses/lgpl-3.0.en.html)

## 作者
[Mimi](https://github.com/mimidesunya) ｜ [X @mimidesunya](https://twitter.com/mimidesunya)
