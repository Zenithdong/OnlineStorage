#include "md5.h"
#include <cstring>

namespace {
// 四轮压缩函数各自使用的循环左移位数；每轮包含四种固定移位模式（RFC 1321 规定的常量）。
constexpr int S11 = 7;
constexpr int S12 = 12;
constexpr int S13 = 17;
constexpr int S14 = 22;
constexpr int S21 = 5;
constexpr int S22 = 9;
constexpr int S23 = 14;
constexpr int S24 = 20;
constexpr int S31 = 4;
constexpr int S32 = 11;
constexpr int S33 = 16;
constexpr int S34 = 23;
constexpr int S41 = 6;
constexpr int S42 = 10;
constexpr int S43 = 15;
constexpr int S44 = 21;

/**
 * @brief 第一轮布尔函数：F(x,y,z) = (x & y) | (~x & z)。
 */
constexpr Uint32 F(Uint32 x, Uint32 y, Uint32 z) {
    return (x & y) | (~x & z);
}

/**
 * @brief 第二轮布尔函数：G(x,y,z) = (x & z) | (y & ~z)。
 */
constexpr Uint32 G(Uint32 x, Uint32 y, Uint32 z) {
    return (x & z) | (y & ~z);
}

/**
 * @brief 第三轮布尔函数：H(x,y,z) = x ^ y ^ z。
 */
constexpr Uint32 H(Uint32 x, Uint32 y, Uint32 z) {
    return x ^ y ^ z;
}

/**
 * @brief 第四轮布尔函数：I(x,y,z) = y ^ (x | ~z)。
 */
constexpr Uint32 I(Uint32 x, Uint32 y, Uint32 z) {
    return y ^ (x | ~z);
}

/**
 * @brief 循环左移，保留被移出高位的比特。
 * @param x 待移位值。
 * @param n 移位位数。
 * @return 左移 n 位后的 32 位值。
 */
constexpr Uint32 RotateLeft(Uint32 x, int n) {
    return (x << n) | (x >> (32 - n));
}

/**
 * @brief 执行 MD5 压缩的一步：a += F(b,c,d) + x + ac; a = RotateLeft(a, s); a += b。
 * @param a 被更新的状态字（引用）。
 * @param b,c,d 其余三个状态字（只读）。
 * @param x 消息字。
 * @param s 循环左移位数。
 * @param ac 轮常量。
 */
void StepF(Uint32& a, Uint32 b, Uint32 c, Uint32 d, Uint32 x, int s, Uint32 ac) {
    a += F(b, c, d) + x + ac;
    a = RotateLeft(a, s);
    a += b;
}

/**
 * @brief 同 StepF，但使用第二轮布尔函数 G。
 */
void StepG(Uint32& a, Uint32 b, Uint32 c, Uint32 d, Uint32 x, int s, Uint32 ac) {
    a += G(b, c, d) + x + ac;
    a = RotateLeft(a, s);
    a += b;
}

/**
 * @brief 同 StepF，但使用第三轮布尔函数 H。
 */
void StepH(Uint32& a, Uint32 b, Uint32 c, Uint32 d, Uint32 x, int s, Uint32 ac) {
    a += H(b, c, d) + x + ac;
    a = RotateLeft(a, s);
    a += b;
}

/**
 * @brief 同 StepF，但使用第四轮布尔函数 I。
 */
void StepI(Uint32& a, Uint32 b, Uint32 c, Uint32 d, Uint32 x, int s, Uint32 ac) {
    a += I(b, c, d) + x + ac;
    a = RotateLeft(a, s);
    a += b;
}
} // namespace

/**
 * @brief 构造空上下文，等价于 reset 后的初始状态。
 */
MD5::MD5() {
    reset();
}

/**
 * @brief 用内存块初始化。
 * @param input 数据首地址。
 * @param length 数据字节数。
 */
MD5::MD5(const void* input, size_t length) {
    reset();
    update(input, length);
}

/**
 * @brief 用字符串初始化（按原始字节，不含结尾 '\0'）。
 * @param str 输入字符串。
 */
MD5::MD5(const std::string& str) {
    reset();
    update(str);
}

/**
 * @brief 用输入流初始化，按块读取（适合大文件）。
 * @param in 输入流。
 */
MD5::MD5(std::istream& in) {
    reset();
    update(in);
}

/**
 * @brief 返回 16 字节二进制摘要（惰性计算）。
 * @return 指向内部摘要缓冲的只读指针。
 * @note 首次调用才执行 final；重复调用直接返回缓存结果。
 */
const byte* MD5::digest() {
    if (!finished_) {
        finished_ = true;
        final();
    }
    return digest_.data();
}

/**
 * @brief 重置累计位数并恢复 MD5 规范的四个初始状态常量。
 */
void MD5::reset() {
    finished_ = false;
    count_[0] = count_[1] = 0;
    state_[0] = 0x67452301;
    state_[1] = 0xefcdab89;
    state_[2] = 0x98badcfe;
    state_[3] = 0x10325476;
}

/**
 * @brief 公共内存入口：把无类型指针转为字节视图后交给私有重载。
 * @param input 数据首地址。
 * @param length 数据字节数。
 */
void MD5::update(const void* input, size_t length) {
    update(reinterpret_cast<const byte*>(input), length);
}

/**
 * @brief 追加字符串数据。
 * @param str 输入字符串。
 */
void MD5::update(const std::string& str) {
    update(reinterpret_cast<const byte*>(str.c_str()), str.length());
}

/**
 * @brief 追加输入流数据（按 BUFFER_SIZE 分块）。
 * @param in 输入流。
 * @note gcount 保证最后一次只处理实际读取的字节；不关闭调用者传入的流。
 */
void MD5::update(std::istream& in) {
    if (!in)
        return;

    std::streamsize length;
    std::array<char, BUFFER_SIZE> buffer;
    while (!in.eof()) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        length = in.gcount();
        if (length > 0)
            update(buffer.data(), static_cast<size_t>(length));
    }
    // 不关闭调用者传入的流，其生命周期由调用方管理。
}

/**
 * @brief 内部增量更新：补齐遗留分组、压缩完整块、缓存尾部。
 * @param input 字节视图。
 * @param length 字节数。
 */
void MD5::update(const byte* input, size_t length) {
    Uint32 i, index, partLen;

    finished_ = false;

    // 由当前累计位数换算出缓冲区已经占用的字节数。
    index = static_cast<Uint32>((count_[0] >> 3) & 0x3f);

    // 两个 32 位字共同维护模 2^64 的总位数，并显式处理低位加法溢出。
    if ((count_[0] += (static_cast<Uint32>(length) << 3)) < (static_cast<Uint32>(length) << 3))
        count_[1]++;
    count_[1] += static_cast<Uint32>(length) >> 29;

    partLen = 64 - index;

    // 输入足以补满当前分组时先压缩缓冲区，再逐个压缩后续完整分组。
    if (length >= partLen) {
        std::memcpy(&buffer_[index], input, partLen);
        transform(buffer_.data());

        for (i = partLen; i + 63 < length; i += 64)
            transform(&input[i]);
        index = 0;
    } else {
        i = 0;
    }

    // 保留尾部数据，等待下一次 update 或 final 的标准填充将其补成完整分组。
    std::memcpy(&buffer_[index], &input[i], length - i);
}

/**
 * @brief 收尾：追加填充与原始位长度，导出 128 位摘要。
 * @note 临时保存状态，使 digest/toString 的读取保持非破坏性（可继续追加数据）。
 */
void MD5::final() {
    std::array<byte, 8> bits{};
    auto oldState = state_;
    auto oldCount = count_;
    Uint32 index, padLen;

    // update(PADDING) 会改变上下文，因此先备份链式状态和输入长度。
    // 长度字段必须记录填充前的输入位数。
    encode(count_.data(), bits.data(), 8);

    // 把当前长度推进到模 64 等于 56，为末尾 8 字节长度字段预留位置。
    index = static_cast<Uint32>((count_[0] >> 3) & 0x3f);
    padLen = (index < 56) ? (56 - index) : (120 - index);
    update(PADDING.data(), padLen);

    // 追加小端格式的原始位长度，完成最后一个或两个 512 位分组。
    update(bits.data(), 8);

    // 四个状态字按小端顺序导出为 16 字节摘要。
    encode(state_.data(), digest_.data(), 16);

    // 恢复上下文，让 digest/toString 的读取保持非破坏性。
    state_ = oldState;
    count_ = oldCount;
}

/**
 * @brief 压缩：把 64 字节解码为 16 个小端字，执行四轮运算并累加回链式状态。
 * @param block 64 字节分组。
 */
void MD5::transform(const byte block[64]) {
    Uint32 a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::array<Uint32, 16> x{};

    decode(block, x.data(), 64);

    // 第一轮按原顺序访问 16 个消息字，并使用 F 函数建立初始非线性混合。
    StepF(a, b, c, d, x[0], S11, 0xd76aa478);
    StepF(d, a, b, c, x[1], S12, 0xe8c7b756);
    StepF(c, d, a, b, x[2], S13, 0x242070db);
    StepF(b, c, d, a, x[3], S14, 0xc1bdceee);
    StepF(a, b, c, d, x[4], S11, 0xf57c0faf);
    StepF(d, a, b, c, x[5], S12, 0x4787c62a);
    StepF(c, d, a, b, x[6], S13, 0xa8304613);
    StepF(b, c, d, a, x[7], S14, 0xfd469501);
    StepF(a, b, c, d, x[8], S11, 0x698098d8);
    StepF(d, a, b, c, x[9], S12, 0x8b44f7af);
    StepF(c, d, a, b, x[10], S13, 0xffff5bb1);
    StepF(b, c, d, a, x[11], S14, 0x895cd7be);
    StepF(a, b, c, d, x[12], S11, 0x6b901122);
    StepF(d, a, b, c, x[13], S12, 0xfd987193);
    StepF(c, d, a, b, x[14], S13, 0xa679438e);
    StepF(b, c, d, a, x[15], S14, 0x49b40821);

    // 第二轮改变消息字访问顺序并使用 G 函数，使局部输入变化继续扩散到全部状态字。
    StepG(a, b, c, d, x[1], S21, 0xf61e2562);
    StepG(d, a, b, c, x[6], S22, 0xc040b340);
    StepG(c, d, a, b, x[11], S23, 0x265e5a51);
    StepG(b, c, d, a, x[0], S24, 0xe9b6c7aa);
    StepG(a, b, c, d, x[5], S21, 0xd62f105d);
    StepG(d, a, b, c, x[10], S22, 0x2441453);
    StepG(c, d, a, b, x[15], S23, 0xd8a1e681);
    StepG(b, c, d, a, x[4], S24, 0xe7d3fbc8);
    StepG(a, b, c, d, x[9], S21, 0x21e1cde6);
    StepG(d, a, b, c, x[14], S22, 0xc33707d6);
    StepG(c, d, a, b, x[3], S23, 0xf4d50d87);
    StepG(b, c, d, a, x[8], S24, 0x455a14ed);
    StepG(a, b, c, d, x[13], S21, 0xa9e3e905);
    StepG(d, a, b, c, x[2], S22, 0xfcefa3f8);
    StepG(c, d, a, b, x[7], S23, 0x676f02d9);
    StepG(b, c, d, a, x[12], S24, 0x8d2a4c8a);

    // 第三轮使用异或型 H 函数和新的访问序列，进一步消除输入字之间的直接对应关系。
    StepH(a, b, c, d, x[5], S31, 0xfffa3942);
    StepH(d, a, b, c, x[8], S32, 0x8771f681);
    StepH(c, d, a, b, x[11], S33, 0x6d9d6122);
    StepH(b, c, d, a, x[14], S34, 0xfde5380c);
    StepH(a, b, c, d, x[1], S31, 0xa4beea44);
    StepH(d, a, b, c, x[4], S32, 0x4bdecfa9);
    StepH(c, d, a, b, x[7], S33, 0xf6bb4b60);
    StepH(b, c, d, a, x[10], S34, 0xbebfbc70);
    StepH(a, b, c, d, x[13], S31, 0x289b7ec6);
    StepH(d, a, b, c, x[0], S32, 0xeaa127fa);
    StepH(c, d, a, b, x[3], S33, 0xd4ef3085);
    StepH(b, c, d, a, x[6], S34, 0x4881d05);
    StepH(a, b, c, d, x[9], S31, 0xd9d4d039);
    StepH(d, a, b, c, x[12], S32, 0xe6db99e5);
    StepH(c, d, a, b, x[15], S33, 0x1fa27cf8);
    StepH(b, c, d, a, x[2], S34, 0xc4ac5665);

    // 第四轮使用 I 函数完成最后混合，四个工作变量随后累加回跨分组状态。
    StepI(a, b, c, d, x[0], S41, 0xf4292244);
    StepI(d, a, b, c, x[7], S42, 0x432aff97);
    StepI(c, d, a, b, x[14], S43, 0xab9423a7);
    StepI(b, c, d, a, x[5], S44, 0xfc93a039);
    StepI(a, b, c, d, x[12], S41, 0x655b59c3);
    StepI(d, a, b, c, x[3], S42, 0x8f0ccc92);
    StepI(c, d, a, b, x[10], S43, 0xffeff47d);
    StepI(b, c, d, a, x[1], S44, 0x85845dd1);
    StepI(a, b, c, d, x[8], S41, 0x6fa87e4f);
    StepI(d, a, b, c, x[15], S42, 0xfe2ce6e0);
    StepI(c, d, a, b, x[6], S43, 0xa3014314);
    StepI(b, c, d, a, x[13], S44, 0x4e0811a1);
    StepI(a, b, c, d, x[4], S41, 0xf7537e82);
    StepI(d, a, b, c, x[11], S42, 0xbd3af235);
    StepI(c, d, a, b, x[2], S43, 0x2ad7d2bb);
    StepI(b, c, d, a, x[9], S44, 0xeb86d391);

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
}

/**
 * @brief 小端编码：32 位字数组展开为字节数组。
 * @param input 字数组。
 * @param output 字节输出。
 * @param length 字节数（必须为 4 的倍数，调用点分别用 8 或 16）。
 */
void MD5::encode(const Uint32* input, byte* output, size_t length) {
    for (size_t i = 0, j = 0; j < length; i++, j += 4) {
        output[j] = static_cast<byte>(input[i] & 0xff);
        output[j + 1] = static_cast<byte>((input[i] >> 8) & 0xff);
        output[j + 2] = static_cast<byte>((input[i] >> 16) & 0xff);
        output[j + 3] = static_cast<byte>((input[i] >> 24) & 0xff);
    }
}

/**
 * @brief 小端解码：字节数组还原为 32 位字数组。
 * @param input 字节数组。
 * @param output 字输出。
 * @param length 字节数（压缩函数固定解码 64 字节）。
 */
void MD5::decode(const byte* input, Uint32* output, size_t length) {
    for (size_t i = 0, j = 0; j < length; i++, j += 4) {
        output[i] = static_cast<Uint32>(input[j]) | (static_cast<Uint32>(input[j + 1]) << 8) |
                    (static_cast<Uint32>(input[j + 2]) << 16) | (static_cast<Uint32>(input[j + 3]) << 24);
    }
}

/**
 * @brief 字节数组转小写十六进制文本。
 * @param input 字节数组。
 * @param length 字节数。
 * @return 2×length 长度的十六进制串。
 * @note 每个字节拆成高、低半字节，通过 HEX 查表输出固定两位。
 */
std::string MD5::bytesToHexString(const byte* input, size_t length) {
    std::string str;
    str.reserve(length << 1);
    for (size_t i = 0; i < length; i++) {
        const unsigned t = input[i];
        str.append(1, HEX[t >> 4]);
        str.append(1, HEX[t & 0x0F]);
    }
    return str;
}

/**
 * @brief 返回 32 位小写十六进制文本摘要。
 * @return 32 字符十六进制串。
 */
std::string MD5::toString() {
    return bytesToHexString(digest(), 16);
}
