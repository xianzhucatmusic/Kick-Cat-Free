#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace kf
{

//==============================================================================
/** 认证加密（AEAD）：XChaCha20-Poly1305。
 *
 *  用途：需要对内存块做认证加密时使用。
 *
 *  为什么用 XChaCha20-Poly1305 而不是 AES-256-GCM：
 *   - 纯软件实现，任何 CPU 都能用（不依赖 AES-NI）
 *   - 24 字节随机 nonce，随机生成也不会碰撞（GCM 只有 12 字节，
 *     大量消息下需要小心 nonce 重用）
 *
 *  安全性不低于 AES-256-GCM，且在"必须跨平台可用"的场景下更合适。
 */
class AesGcm
{
public:
    static constexpr int kKeyBytes = 32;
    static constexpr int kNonceBytes = 24;      // XChaCha20 用 192 bit nonce
    static constexpr int kTagBytes = 16;

    using Key = std::array<unsigned char, kKeyBytes>;

    /** 加密。
     *  输出格式：nonce(12) || ciphertext || tag(16)
     *  @param aad  附加认证数据（不加密但参与认证，用于绑定上下文） */
    static bool encrypt (const void* plaintext, size_t plainBytes,
                         const Key& key,
                         const void* aad, size_t aadBytes,
                         juce::MemoryBlock& out);

    /** 解密。认证失败（被篡改/密钥错误）返回 false。 */
    static bool decrypt (const juce::MemoryBlock& in,
                         const Key& key,
                         const void* aad, size_t aadBytes,
                         juce::MemoryBlock& plainOut);

    /** 生成随机密钥。 */
    static bool randomKey (Key& keyOut);

    /** 用密码派生密钥（Argon2id）。 */
    static bool deriveKeyFromPassword (const juce::String& password,
                                       const juce::MemoryBlock& salt,
                                       Key& keyOut);

    /** 生成随机盐。 */
    static juce::MemoryBlock randomSalt (int bytes = 16);

private:
    JUCE_DECLARE_NON_COPYABLE (AesGcm)
};

} // namespace kf
