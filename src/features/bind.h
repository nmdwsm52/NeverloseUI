#pragma once
namespace nl {
// ---------------------------------------------------------------------------
//  按键绑定（绑定值 = 低 16 位 VK 码 + 高 16 位模式）
//
//  为什么这么设计：配置里这些 key 字段本来就是一个 int（老的 ini 里是纯 VK 码），
//  把"模式"塞进高位可以同时兼容旧配置，也不用给每个功能加一个新字段。
//
//  模式：
//    0 = Hold（按住生效，默认）
//    1 = Toggle（按一下切换）
//    2 = Always（不需要按键，一直生效）
// ---------------------------------------------------------------------------
enum BindMode { BindMode_Hold = 0, BindMode_Toggle = 1, BindMode_Always = 2 };

int  BindVk(int binding);                 // 取出 VK 码
int  BindModeOf(int binding);             // 取出模式
int  BindMake(int vk, int mode);          // 打包
bool BindActive(int binding);             // 按模式判断当前是否激活（每帧调用）
bool BindHasKey(int binding);             // 是否绑了键（Always 也算）
const char* BindVkName(int vk);           // 显示名（含 Mouse3/4/5、Ctrl 等）
}
