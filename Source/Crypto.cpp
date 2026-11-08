//==============================================================================
// Crypto.cpp —— 无外部加密库依赖的实现
//
// 本项目不链接 libsodium，这里提供一个轻量实现，好处：
//   · Windows / Linux 交叉编译不需要准备 libsodium
//   · 二进制更小
//
// ⚠️ AesGcm::encrypt / decrypt 在本实现里返回 false（不做加密）。
//    调用方需自行处理失败情况。
//==============================================================================

#include "Crypto.h"

namespace kf
{

//==============================================================================
bool AesGcm::encrypt (const void*, size_t, const Key&,
                      const void*, size_t, juce::MemoryBlock& out)
{
    out.reset();
    return false;
}

bool AesGcm::decrypt (const juce::MemoryBlock&, const Key&,
                      const void*, size_t, juce::MemoryBlock& plainOut)
{
    plainOut.reset();
    return false;
}

bool AesGcm::randomKey (Key& keyOut)
{
    juce::Random r;
    for (auto& b : keyOut)
        b = (unsigned char) r.nextInt (256);
    return true;
}

bool AesGcm::deriveKeyFromPassword (const juce::String&, const juce::MemoryBlock&, Key&)
{
    return false;
}

juce::MemoryBlock AesGcm::randomSalt (int bytes)
{
    juce::MemoryBlock mb ((size_t) juce::jmax (1, bytes), true);
    juce::Random r;
    for (size_t i = 0; i < mb.getSize(); ++i)
        ((unsigned char*) mb.getData())[i] = (unsigned char) r.nextInt (256);
    return mb;
}

} // namespace kf
