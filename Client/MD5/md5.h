#pragma once
#ifndef MD5_H
#define MD5_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

// 算法按字节和 32 位字处理；uint32_t 定宽，避免 unsigned long 在 LP64 平台变为 64 位导致计算错误。
using byte = std::uint8_t;   ///< 字节定宽别名。
using Uint32 = std::uint32_t; ///< 32 位字定宽别名（替代平台相关的 unsigned long）。

/**
 * @brief 增量式 MD5 计算器。
 *
 * 所属模块：客户端 MD5 模块（文件哈希）。
 * 用途：上传前计算文件内容摘要，供服务端判重、秒传和断点续传协商。
 *
 * 设计：update() 可多次追加数据，因此大文件无需整体载入内存；digest()/final()
 * 负责 RFC 1321 风格的填充与收尾，toString() 输出网盘协议使用的 32 位小写十六进制。
 *
 * 定宽：内部统一使用 std::uint8_t / std::uint32_t，避免 unsigned long 在 LP64
 * 平台变为 64 位导致摘要错误。
 *
 * 不可复制：拷贝构造/赋值被删除，防止误拷贝内部状态造成摘要污染。
 */
class MD5 {
public:
    /// @brief 构造空上下文（等价于 reset 后的初始状态）。
    MD5();

    /**
     * @brief 用内存块初始化。
     * @param input 数据首地址。
     * @param length 数据字节数。
     */
    MD5(const void* input, size_t length);

    /**
     * @brief 用字符串初始化（按原始字节，不含结尾 '\0'）。
     * @param str 输入字符串。
     */
    MD5(const std::string& str);

    /**
     * @brief 用输入流初始化，按块读取（适合大文件）。
     * @param in 输入流。
     */
    MD5(std::istream& in);

    /**
     * @brief 追加内存块数据。
     * @param input 数据首地址。
     * @param length 数据字节数。
     */
    void update(const void* input, size_t length);

    /**
     * @brief 追加字符串数据。
     * @param str 输入字符串。
     */
    void update(const std::string& str);

    /**
     * @brief 追加输入流数据（按 BUFFER_SIZE 分块）。
     * @param in 输入流。
     * @note 不关闭调用者传入的流，其生命周期由调用方管理。
     */
    void update(std::istream& in);

    /**
     * @brief 返回 16 字节二进制摘要（惰性计算，首次调用才执行 final）。
     * @return 指向内部摘要缓冲的只读指针。
     */
    const byte* digest();

    /**
     * @brief 返回 32 位小写十六进制文本摘要。
     * @return 32 字符十六进制串。
     */
    std::string toString();

    /**
     * @brief 重置累计长度与状态字，使同一对象可重新计算另一份内容。
     */
    void reset();

private:
    /**
     * @brief 内部更新：维护 64 字节分组缓冲，把完整分组交给 transform 压缩。
     * @param input 字节视图。
     * @param length 字节数。
     */
    void update(const byte* input, size_t length);

    /**
     * @brief 收尾：追加 0x80、零填充和原始位长度，再导出 128 位摘要。
     */
    void final();

    /**
     * @brief 压缩：对一个 512 位分组执行四轮共 64 步非线性运算。
     * @param block 64 字节分组。
     */
    void transform(const byte block[64]);

    /**
     * @brief 小端编码：32 位字数组展开为字节数组。
     * @param input 字数组。
     * @param output 字节输出（length 必须为 4 的倍数）。
     * @param length 字节数。
     */
    void encode(const Uint32* input, byte* output, size_t length);

    /**
     * @brief 小端解码：字节数组还原为 32 位字数组。
     * @param input 字节数组。
     * @param output 字输出。
     * @param length 字节数。
     */
    void decode(const byte* input, Uint32* output, size_t length);

    /**
     * @brief 字节数组转小写十六进制文本。
     * @param input 字节数组。
     * @param length 字节数。
     * @return 2×length 长度的十六进制串。
     */
    std::string bytesToHexString(const byte* input, size_t length);

    // 禁止复制内部计算状态，避免误拷贝造成摘要污染。
    MD5(const MD5&) = delete;
    MD5& operator=(const MD5&) = delete;

private:
    std::array<Uint32, 4> state_; ///< 四个 32 位链式状态字 A、B、C、D。
    std::array<Uint32, 2> count_; ///< 累计输入位数，低位字在前的 64 位计数器。
    std::array<byte, 64> buffer_; ///< 尚不足一个 512 位分组的输入缓存。
    std::array<byte, 16> digest_; ///< final 生成的 128 位二进制摘要。
    bool finished_ = false;       ///< 标记摘要是否完成，避免重复执行填充。

    inline static constexpr std::array<byte, 64> PADDING{0x80}; ///< 首字节 0x80、其余为 0 的标准填充模板。
    inline static constexpr char HEX[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'}; ///< 半字节查表。
    static constexpr size_t BUFFER_SIZE = 1024; ///< 从输入流增量读取的临时块大小。
};

#endif
