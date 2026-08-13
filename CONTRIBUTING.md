# Contributing

## 架构约束

- `src/core` 为 UI 无关业务逻辑，**禁止**直接 include Dock/Tray/UI 头文件。
- 包管理操作必须经由 `PackageBackend` 抽象接口，新增后端在 `core/package` 添加实现。
- 所有提权写操作经 `pkexec`（polkit），不在进程内直接 `system("sudo ...")`。
- 配置项通过 `AppConfig`（DConfig）暴露，保持透明可配。

## 开发流程

1. 安装依赖：`sudo apt build-dep .`
2. 构建：`mkdir build && cd build && cmake .. && make`
3. 测试：`ctest --output-on-failure`
4. 提交前：`clang-format` 与 `clang-tidy` 通过。

## 分支

- `main` 为保护分支，PR 需通过 CI。

## 翻译

界面文案请使用 `tr()`/`Dtk::Widget::DTranslator`，不要硬编码字符串。支持简体中文、
英语、西班牙语、法语、德语五种语言，源文件位于 `translations/`。修改文案后刷新模板：

```bash
lupdate ../src -ts dtk-update_en_US.ts dtk-update_zh_CN.ts \
    dtk-update_es.ts dtk-update_fr.ts dtk-update_de.ts -source-language en_US
```

新语种请同步修改 `translations/CMakeLists.txt` 的 `TRANSLATION_FILES`。
