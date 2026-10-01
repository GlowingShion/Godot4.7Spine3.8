# spine-godot 4.3 iOS 构建（含 Spine 3.8 支持）

iOS 必须用苹果的 Xcode 工具链编译（只能在 macOS 上），所以提供了两条路。

## 路线一：GitHub Actions 云构建（推荐，无需 Mac）

1. 注册/登录 GitHub，新建一个**空仓库**（Public 免费且不限时；Private 也能跑但消耗免费额度较快）。
2. 把本目录里的 `spine-godot/` 文件夹和 `.github/` 文件夹**原样上传到仓库根目录**（保持目录结构）。
   - 本地有 git 的话最方便：
     ```
     cd spine-godot-ios-ci
     git init
     git add .
     git commit -m "spine-godot 4.3 + spine38 iOS build"
     git branch -M main
     git remote add origin https://github.com/<你的用户名>/<仓库名>.git
     git push -u origin main      # 首次 push 会弹浏览器登录
     ```
3. 打开仓库页面 → **Actions** 标签 → 等 "Build iOS" 工作流跑完（约 15~25 分钟）。
4. 在该次运行页面底部的 **Artifacts** 下载 `spine-godot-ios`，解压得到两个 fat framework：
   - `libspine_godot.ios.template_debug.framework`
   - `libspine_godot.ios.template_release.framework`
5. 把这两个 framework 放进你 Godot 项目的 `bin/ios/` 文件夹（与 `.gdextension` 里 `ios.debug` / `ios.release` 指向的路径一致），Godot 导出 iOS 时会自动链接。

framework 同时包含 **arm64（真机）+ x86_64（模拟器）** 两个架构切片。

## 路线二：有任何一台 Mac

```
# 终端里执行（需要装好 Xcode + python3 + scons）
git clone --depth 1 https://github.com/godotengine/godot-cpp.git          # 放到 spine-godot/godot-cpp
# 然后把本包里的 spine-godot 文件夹整个拷到 Mac 上，进入该目录：
cd spine-godot
scons platform=ios arch=arm64 target=template_debug api_version=4.7 ios_simulator=no
scons platform=ios arch=arm64 target=template_release api_version=4.7 ios_simulator=no
```

产物在 `bin/ios/`。只要真机版就到此为止；要模拟器版再跑：

```
# 先把 bin/ios 里的 framework 挪走，再：
scons platform=ios arch=x86_64 target=template_debug api_version=4.7 ios_simulator=yes
scons platform=ios arch=x86_64 target=template_release api_version=4.7 ios_simulator=yes
# 之后用 lipo 合并（参考 .github/workflows/build-ios.yml 里的 Merge 步骤）
```

## 注意事项

- `godot-cpp` 已固定在 `507ed9d8`（与本机 Windows/Android/Web 构建完全同源，避免 API 版本错位）。
- 首次在 Xcode 里运行导出的 iOS 工程时，如果提示 GDExtension 未签名/被排除，确认 Godot 导出预设里 iOS 的 `Static Framework` 相关选项为默认，并把 framework 拖进 Xcode 工程的 Frameworks 组（Godot 4.7 生成工程时通常会自动带上 `bin/ios` 下的 framework）。
- iOS 导出还需要 Godot 的 iOS 导出模板（已随 4.7.2 模板安装）和一台 Mac 上的 Xcode 做最后的打包签名——Godot 的 iOS 导出本身就是"导出 Xcode 工程"，真正的编译签名必须发生在 macOS 上。
