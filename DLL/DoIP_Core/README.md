# DoIP_Core DLL

DoIP 协议栈 CAPL DLL，基于 WinSock2 实现 ISO 13400-2 诊断通信。

## 依赖

- Windows SDK（ws2_32.lib, kernel32.lib）
- CMake >= 3.15
- MSVC（Visual Studio 2019+ 或 Build Tools）
- 无第三方库依赖

## 编译

```bash
cd DLL/DoIP_Core
cmake -B build -A Win32
cmake --build build --config Release
```

产物：`build/Release/DoIP_Core.dll`（x86，约 83KB）

复制到项目 `Modules/` 目录即可使用：
```bash
copy build\Release\DoIP_Core.dll ..\..\Modules\
```

## 注意事项

- 必须编译为 32 位（`-A Win32`），CANoe CAPL DLL 仅支持 x86
- 使用静态 CRT（`/MT`），无需目标机器安装 VC++ 运行时
- 取消 CMakeLists.txt 中 `DOIP_DEBUG` 注释可启用调试日志

## 源文件

| 文件 | 说明 |
|------|------|
| DoIP_Core.cpp | CAPL 导出接口（caplDllTable） |
| doip_protocol.h/cpp | ISO 13400-2 帧构建/解析 |
| doip_tcp.h/cpp | WinSock2 TCP 连接管理 |
| cdll.h | CANoe CAPL DLL 标准头文件 |
