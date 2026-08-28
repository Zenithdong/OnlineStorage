#include "kernel.h"

/**
 * @brief 构造 Kernel 并创建网络层实现。
 * @param parent Qt 父对象。
 * @note 把 this 作为回调接口传给 tcpnet，形成“tcpnet 收完整包 -> Kernel 分发”的调用链。
 */
Kernel::Kernel(QObject* parent) : QObject{parent} {
    // 把自身作为回调接口传给网络层，形成“tcpnet 收完整包 -> Kernel 分发”的调用链。
    m_pNet = std::make_unique<tcpnet>(this);
}

/**
 * @brief 建立到服务端的 TCP 连接（转发到网络层）。
 */
bool Kernel::Connect(const char* szip, short nport) {
    return m_pNet->ConnectServer(szip, nport);
}

/**
 * @brief 断开连接并停止接收线程（转发到网络层）。
 */
void Kernel::DisConnect() {
    m_pNet->disConnectServer();
}

/**
 * @brief 发送一个完整业务包（转发到网络层）。
 */
bool Kernel::SendData(const char* szbuf, int Len) {
    return m_pNet->SendData(szbuf, Len);
}

/**
 * @brief 接收线程取得完整包后调用的分发入口。
 * @param szbuf 完整包缓冲区（接收线程临时分配，仅调用期间有效）。
 * @param len 包体长度。
 * @note 在接收线程执行。先复制缓冲区为 QByteArray（深拷贝），再按首字节路由、
 *       用 sizeof 校验固定包长，随后 emit 信号；信号经 QueuedConnection 排入主线程队列。
 */
void Kernel::DealData(const char* szbuf, int len) {
    if (!szbuf || len <= 0) {
        return;
    }

    // 先复制接收缓冲区再按首字节路由；随后用 sizeof 校验固定包长，阻止短包被强制转换后越界读取。
    QByteArray packet(szbuf, len);
    switch (*szbuf) {
    case _default_protocol_login_rs:
        if (len == sizeof(STRU_LOGIN_RS))
            emit LoginRs(packet);
        break;

    case _default_protocol_register_rs:
        if (len == sizeof(STRU_REGISTER_RS))
            emit RegisterRs(packet);
        break;

    case _default_protocol_getfilelist_rs:
        if (len == sizeof(STRU_GETFILELIST_RS))
            emit GetFileLisRs(packet);
        break;
    case _default_protocol_uploadfileinfo_rs:
        if (len == sizeof(STRU_UPLOADFILEINFO_RS))
            emit UploadFileInfoRs(packet);
        break;
    case _default_protocol_selectfile_rs:
        if (len == sizeof(STRU_SELECTFILE_RS))
            emit SelectFileRs(packet);
        break;
    case _default_protocol_sharelink_rs:
        if (len == sizeof(STRU_SHARELINK_RS))
            emit ShareLinkRs(packet);
        break;
    case _default_protocol_getlink_rs:
        if (len == sizeof(STRU_GETLINK_RS))
            emit GetLinkRs(packet);
        break;
    case _default_protocol_downloadfileinfo_rs:
        if (len == sizeof(STRU_DOWNLOADFILE_RS))
            emit DownLoadFileRs(packet);
        break;
    }
}
