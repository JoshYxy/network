#include <stdio.h>
#include "ftpserver.h"
 
char g_recvBuf[1024] = { 0 };      // 用来接收客户端消息
int g_fileSize;                    // 文件大小
char* g_fileBuf;                   // 储存文件
char g_fileName[256];
int main(void)
{
    initSocket();
 
    listenToClient();
 
    closeSocket();
 
    return 0;
}
 
// 初始化socket库
bool initSocket()
{
    WSADATA wsadata;
 
    if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)  // 启动协议,成功返回0
    {
        printf("WSAStartup faild: %d\n", WSAGetLastError());
        return false;
    }
 
    return true;
}
 
// 关闭socket库
bool closeSocket()
{
    if (0 != WSACleanup())
    {
        printf("WSACleanup faild: %d\n", WSAGetLastError());
        return false;
    }
 
    return true;
}
 
// 监听客户端连接
void listenToClient()
{

    // 创建server socket套接字 地址、端口号,AF_INET是IPV4
    SOCKET serfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
 
    if (serfd == INVALID_SOCKET)
    {
        printf("socket faild:%d", WSAGetLastError());
        WSACleanup();
        return;
    }
 
    // 给socket绑定IP地址和端口号
    struct sockaddr_in serAddr;
 
    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(SPORT);             // htons把本地字节序转为网络字节序
    serAddr.sin_addr.S_un.S_addr = ADDR_ANY;     // 监听本机所有网卡
 
    if (0 != bind(serfd, (struct sockaddr*)&serAddr, sizeof(serAddr)))
    {
        printf("bind faild:%d", WSAGetLastError());
        return;
    }
 
    // 监听客户端连接
    if (0 != listen(serfd, 10))                  // 10为队列最大数
    {
        printf("listen faild:%d", WSAGetLastError());
        return;
    }
 
    // 有客户端连接，接受连接
    struct sockaddr_in cliAddr;
    int len = sizeof(cliAddr);
 
    SOCKET clifd = accept(serfd, (struct sockaddr*)&cliAddr, &len);
 
    if (INVALID_SOCKET == clifd)
    {
        printf("accept faild:%d", WSAGetLastError());
        return;
    }
 
    printf("接受成功！\n");
 
        // 开始处理消息
 
 
        while (processMag(clifd))
        {
            Sleep(200);
        }
 
}
 
// 处理消息
bool processMag(SOCKET clifd)
{
    // 成功接收消息返回收到的字节数，否则返回0
    int nRes = recv(clifd, g_recvBuf, 1024, 0);         // 接收

    if (nRes <= 0)
    {
        printf("客户端下线...%d", WSAGetLastError());
        return false;
    }
 
    // 获取接受的的消息
    struct MsgHeader* msg = (struct MsgHeader*)g_recvBuf;
    struct MsgHeader exitmsg;
 
    /*
    *MSG_FILENAME       = 1,    // 文件名称                服务器使用
    *MSG_FILESIZE       = 2,    // 文件大小                客户端使用
    *MSG_READY_READ     = 3,    // 准备接受                客户端使用
    *MSG_SENDFILE       = 4,    // 发送                    服务器使用
    *MSG_SUCCESSED      = 5,    // 传输完成                两者都使用
    *MSG_OPENFILE_FAILD = 6     // 告诉客户端文件找不到    客户端使用
     *
    */
    char inf[505]; //存放获取的本机信息
    memset(inf, 0, sizeof(inf));
    switch (msg->msgID)
    {
        case MSG_FILENAME:          // 1  第一次接收
            printf("%s\n", msg->myUnion.fileInfo.fileName);
            readFile(clifd, msg);
            break;
        case MSG_SENDFILE:          // 4
            sendFile(clifd, msg);
            break;
        case MSG_SUCCESSED:         // 5

            exitmsg.msgID = MSG_SUCCESSED;

            if (SOCKET_ERROR == send(clifd, (char*)&exitmsg, sizeof(struct MsgHeader), 0))   //失败发送给客户端
            {
                printf("send faild: %d\n", WSAGetLastError());
                return false;
            }
            printf("完成！\n");
            break;
        case MSG_CLIENTREADSENT: //7
            serverReady(clifd, msg);
            break;
        case MSG_CLIENTSENT:
            writeFile(clifd, msg);
            break;
        case MSG_PWD: //added by yxy
            getMessage(MSG_PWD, inf);
            sendMessage(clifd, inf);
            break;
        case MSG_LS: //added by yxy
            getMessage(MSG_LS, inf);
            sendMessage(clifd, inf);
            break;

    }
    return true;
}
 
/*
*1.客户端请求下载文件 —把文件名发送给服务器
*2.服务器接收客户端发送的文件名 —根据文件名找到文件，把文件大小发送给客户端
*3.客户端接收到文件大小—准备开始接受，开辟内存  准备完成要告诉服务器可以发送了
*4.服务器接受的开始发送的指令开始发送
*5.开始接收数据，存起来     接受完成，告诉服务器接收完成
*6.关闭连接
*/
void getMessage(int type, char inf[505]) {
    char path[105];
    DIR *dp;
    struct dirent *entry;

    switch(type)
    {
        case MSG_PWD:
            getcwd(path, sizeof(path));
            strcpy(inf, path);
            return;
        case MSG_LS:
            getcwd(path, sizeof(path));
            dp = opendir(path);
            while((entry = readdir(dp)) != NULL)
            {
                strcat(inf, entry->d_name);
                strcat(inf, "  ");
            }
            return;
    }
}
bool readFile(SOCKET clifd, struct MsgHeader* pmsg)
{
    FILE* pread = fopen(pmsg->myUnion.fileInfo.fileName, "rb");
 
    if (pread == NULL)
    {
        printf("找不到[%s]文件...\n", pmsg->myUnion.fileInfo.fileName);
 
        struct MsgHeader msg;
        msg.msgID = MSG_OPENFILE_FAILD;                                             // MSG_OPENFILE_FAILD = 6
 
        if (SOCKET_ERROR == send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0))   // 失败发送给客户端
        {
            printf("send faild: %d\n", WSAGetLastError());
        }
 
        return false;
    }
 
    // 获取文件大小
    fseek(pread, 0, SEEK_END);
    g_fileSize = ftell(pread);
    fseek(pread, 0, SEEK_SET);
 
    // 把文件大小发给客户端
    char text[100];
    char tfname[200] = { 0 };
    struct MsgHeader msg;
 
    msg.msgID = MSG_FILESIZE;                                       // MSG_FILESIZE = 2
    msg.myUnion.fileInfo.fileSize = g_fileSize;
 
    _splitpath(pmsg->myUnion.fileInfo.fileName, NULL, NULL, tfname, text);  //只需要最后的名字加后缀
 
    strcat(tfname, text);
    strcpy(msg.myUnion.fileInfo.fileName, tfname);
 
    send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0);            // 文件名和后缀、文件大小发回客户端  第一次发送给客户端
 
    //读写文件内容
    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));
 
    if (g_fileBuf == NULL)
    {
        printf("内存不足，重试\n");
        return false;
    }
 
    fread(g_fileBuf, sizeof(char), g_fileSize, pread);
    g_fileBuf[g_fileSize] = '\0';
 
    fclose(pread);
    return true;
}

void sendMessage(SOCKET clifd, char* message) {
    struct MsgHeader msg;

    msg.msgID = MSG_RECV;
    strcpy(msg.myUnion.fileInfo.fileName, message);

    if (SOCKET_ERROR == send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0))
    {
        printf("message send error: %d\n", WSAGetLastError());
        return;
    }
}
bool sendFile(SOCKET clifd, struct MsgHeader* pms)
{
    struct MsgHeader msg;                                                     // 告诉客户端准备接收文件
    msg.msgID = MSG_READY_READ;
 
    // 如果文件的长度大于每个数据包能传送的大小（1012），那么久分块
    for (size_t i = 0; i < g_fileSize; i += PACKET_SIZE)                       // PACKET_SIZE = 1012
    {
        msg.myUnion.packet.nStart = i;
 
        // 包的大小大于总数据的大小
        if (i + PACKET_SIZE + 1 > g_fileSize)
        {
            msg.myUnion.packet.nsize = g_fileSize - i;
        }
        else
        {
            msg.myUnion.packet.nsize = PACKET_SIZE;
        }
 
        memcpy(msg.myUnion.packet.buf, g_fileBuf + msg.myUnion.packet.nStart, msg.myUnion.packet.nsize);
 
        if (SOCKET_ERROR == send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0))  // 告诉客户端可以发送
        {
            printf("文件发送失败：%d\n", WSAGetLastError());
        }
    }
 
    return true;
}
 
void serverReady(SOCKET clifd, struct MsgHeader* pmsg)
{
    g_fileSize = pmsg->myUnion.fileInfo.fileSize;
    char text[100];
    char tfname[200] = { 0 };
 
    _splitpath(pmsg->myUnion.fileInfo.fileName, NULL, NULL, tfname, text);  //只需要最后的名字加后缀
 
    strcat(tfname, text);
    strcpy(g_fileName, tfname);
    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));         // 申请空间
 
    if (g_fileBuf == NULL)
    {
        printf("申请内存失败\n");
    }
    else
    {
        struct MsgHeader msg;  
        msg.msgID = MSG_SERVERREAD;
 
        if (SOCKET_ERROR == send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0))   // 第二次发送
        {
            printf("客户端 send error: %d\n", WSAGetLastError());
            return;
        }
    }
 
    printf("filename:%s    size:%d  \n", pmsg->myUnion.fileInfo.fileName, pmsg->myUnion.fileInfo.fileSize);
}
 
bool writeFile(SOCKET clifd, struct MsgHeader* pmsg)
{
    if (g_fileBuf == NULL)
    {
        return false;
    }
 
    int nStart = pmsg->myUnion.packet.nStart;
    int nsize = pmsg->myUnion.packet.nsize;
 
    memcpy(g_fileBuf + nStart, pmsg->myUnion.packet.buf, nsize);    // strncmpy一样
    printf("packet size:%d %d\n", nStart + nsize, g_fileSize);
 
    if (nStart + nsize >= g_fileSize)                       // 判断数据是否发完数据
    {
        FILE* pwrite;
        struct MsgHeader msg;
 
        pwrite = fopen(g_fileName, "wb");
        msg.msgID = MSG_SUCCESSED;
 
        if (pwrite == NULL)
        {
            printf("write file error...\n");
            return false;
        }
 
        fwrite(g_fileBuf, sizeof(char), g_fileSize, pwrite);
        fclose(pwrite);
 
        free(g_fileBuf);
        g_fileBuf = NULL;
 
        send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0);
 
        return false;
    }
 
    return true;
}