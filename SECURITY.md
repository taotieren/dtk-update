# Security Policy

## 提权模型

dtk-update 仅通过 `pkexec`（polkit）执行包管理写操作，不在进程内持久持有 root 权限。

## 安全公告

更新前的安全提示从 deepin 安全中心 D-Bus 接口获取，接口不可用时降级为无提示，不阻塞流程。

## 报告漏洞

请通过 GitHub Security Advisories 私报，勿在公开 issue 披露细节。
