#include <stdio.h>
#include <stdlib.h>       
#include "ftpclient.h"   
 
char g_fileName[256];     // 保存服务器发送过来的文件名
char* g_fileBuf;          // 接受存储文件内容
char g_recvBuf[1024];     // 接受消息缓冲区
int g_fileSize;           // 文件总大小
 
int main(void)
{
    initSocket();
 
    connectToHost();
 
    closeSocket();
 
    return 0;
}
 
// 初始化socket库
bool initSocket()
{
    WSADATA wsadata;
 
    if (0 != WSAStartup(MAKEWORD(2, 2), &wsadata))        // 启动协议,成功返回0
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
void connectToHost()
{
    // 创建server socket套接字 地址、端口号,AF_INET是IPV4
    SOCKET serfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
 
    if (INVALID_SOCKET == serfd)
    {
        printf("socket faild:%d", WSAGetLastError());
        return;
    }
 
    // 给socket绑定IP地址和端口号
    struct sockaddr_in serAddr;
    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(SPORT);                       // htons把本地字节序转为网络字节序
    serAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1"); // 服务器的IP地址
 
    // 连接到服务器
    if (0 != connect(serfd, (struct sockaddr*)&serAddr, sizeof(serAddr)))
    {
        printf("connect faild:%d", WSAGetLastError());
        return;
    }
    printf("连接成功！\n");
    if(!login(serfd)) {
        printf("尝试次数达到上限，与服务器断开连接");
        closesocket(serfd);
        return;
    }


    printf("登录成功！\n");
    printf("***************************************\n");
    printf("1.传输文件给服务端\n");
    printf("2.从服务端取文件\n");
    printf("3.退出程序\n");
    printf("4.pwd\n");
    printf("***************************************\n");
    while (1)
    {    
        int flag;
        do {
            printf("ftp >>");
            scanf("%d", &flag);
        } while (!(flag == 1 || flag == 2 || flag == 3 || flag == 4 || flag == 5));

        if (flag == 1)
        {
            printf("现在开始向服务端传输文件");
            clientReadySend(serfd);
            while(processMag(serfd))
            {}
        }
        else if(flag == 2)
        {
            printf("现在客户端开始接收文件\n");
            downloadFileName(serfd);// 开始处理消息,100为发送消息间隔
            while (processMag(serfd))
            {}
        }
        else if(flag == 4) {
            requestPwd(serfd);
            while (processMag(serfd))
            {}
        }
        else if(flag == 5) {
            requestLs(serfd);
            while (processMag(serfd))
            {}
        }
        else
        {
            printf("系统要退出了...\n");
            closesocket(serfd);
            return;
        }
//        printf("\nPress Any Key To Continue:\n");
//        getchar();
    }
}
 
// 处理消息
bool processMag(SOCKET serfd)
{
 
    recv(serfd, g_recvBuf, 1024, 0);                     // 收到消息   
    struct MsgHeader* msg = (struct MsgHeader*)g_recvBuf;
 
    /*
    *MSG_FILENAME       = 1,       // 文件名称                服务器使用
    *MSG_FILESIZE       = 2,       // 文件大小                客户端使用
    *MSG_READY_READ     = 3,       // 准备接受                客户端使用
    *MSG_SENDFILE       = 4,       // 发送                    服务器使用
    *MSG_SUCCESSED      = 5,       // 传输完成                两者都使用
    *MSG_OPENFILE_FAILD = 6        // 告诉客户端文件找不到    客户端使用
    */
 
    switch (msg->msgID)
    {
    case MSG_OPENFILE_FAILD:         // 6
        downloadFileName(serfd);
        break;
    case MSG_FILESIZE:               // 2  第一次接收
        readyread(serfd, msg);
        break;
    case MSG_READY_READ:             // 3
        writeFile(serfd, msg);
        break;
    case MSG_SUCCESSED:              // 5
        printf("传输完成！\n");
        return false;
        break;
    case MSG_SERVERREAD:
        printf("准备传输完成");
        sendFile(serfd, msg);
        break;

    case MSG_RECV:                  //added by yxy
        readMessage(msg);
        return false;
    }

    return true;
}
bool login(SOCKET serfd) {
    char username[30]; //TODO 长度检验
    char password[30];
    struct MsgHeader send_msg;
    struct MsgHeader* rec_msg;
    send_msg.msgID = MSG_LOGIN;

    while(true) {
        //发送用户和密码
        printf("username >>");
        scanf("%s", username);
        printf("password >>");
        scanf("%s", password);
        strcpy(send_msg.myUnion.fileInfo.fileName, username);
        strcat(send_msg.myUnion.fileInfo.fileName, " ");
        strcat(send_msg.myUnion.fileInfo.fileName, password);
        send(serfd, (char*)&send_msg, sizeof(struct MsgHeader), 0);
        //接收检查结果

        recv(serfd, g_recvBuf, 1024, 0);
        rec_msg = (struct MsgHeader*)g_recvBuf;
//        printf("%s\n",rec_msg->myUnion.fileInfo.fileName);
        if(!strcmp(rec_msg->myUnion.fileInfo.fileName, "Success")) return true;
        else if(!strcmp(rec_msg->myUnion.fileInfo.fileName, "ReachMax")) return false;
        else {
            printf("账号或密码错误，请重试\n");
        }
    }
}
void readMessage(struct MsgHeader* pmsg) {

    char *message = pmsg->myUnion.fileInfo.fileName;
    printf("%s\n", message);

}
void requestPwd(SOCKET serfd) {
    struct MsgHeader msg;
    msg.msgID = MSG_PWD;
    send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0);
}
void requestLs(SOCKET serfd) {
    struct MsgHeader msg;
    msg.msgID = MSG_LS;
    send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0);
}
void downloadFileName(SOCKET serfd)
{
    char fileName[1024];
    struct MsgHeader file;
 
    printf("输入下载的文件名：");
 
    scanf("%s", fileName);                              // 输入文件路径               
    file.msgID = MSG_FILENAME;                               // MSG_FILENAME = 1
    strcpy(file.myUnion.fileInfo.fileName, fileName);
    send(serfd, (char*)&file, sizeof(struct MsgHeader), 0);  // 发送、IP地址、内容、长度    第一次发送给服务器
}
 
void readyread(SOCKET serfd, struct MsgHeader* pmsg)
{
    // 准备内存 pmsg->fileInfo.fileSize
    g_fileSize = pmsg->myUnion.fileInfo.fileSize;
    strcpy(g_fileName, pmsg->myUnion.fileInfo.fileName);
 
    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));         // 申请空间
 
    if (g_fileBuf == NULL)
    {
        printf("申请内存失败\n");
    }
    else
    {
        struct MsgHeader msg;  // MSG_SENDFILE = 4
        msg.msgID = MSG_SENDFILE;
 
        if (SOCKET_ERROR == send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0))   // 第二次发送
        {
            printf("客户端 send error: %d\n", WSAGetLastError());
            return;
        }
    }
 
    printf("size:%d  filename:%s\n", pmsg->myUnion.fileInfo.fileSize, pmsg->myUnion.fileInfo.fileName);
}
 
bool writeFile(SOCKET serfd, struct MsgHeader* pmsg)
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
 
        send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0);
 
        return false;
    }
 
    return true;
}
 
void clientReadySend(SOCKET serfd)
{
    struct MsgHeader msg;
    msg.msgID = MSG_CLIENTREADSENT;
    char fileName[1024] = { 0 };
    printf("请输入要上传的文件名：");
    scanf("%s", fileName);
    FILE* pread = fopen(fileName, "rb");
 
    fseek(pread, 0, SEEK_END);
    g_fileSize = ftell(pread);
    fseek(pread, 0, SEEK_SET);
 
    strcpy(msg.myUnion.fileInfo.fileName, fileName);
    msg.myUnion.fileInfo.fileSize = g_fileSize;
 
    send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0);
    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));
 
    if (g_fileBuf == NULL)
    {
        printf("内存不足，重试\n");
    }
 
    fread(g_fileBuf, sizeof(char), g_fileSize, pread);
    g_fileBuf[g_fileSize] = '\0';
 
    fclose(pread);
}
 
 
bool sendFile(SOCKET serfd, struct MsgHeader* pms)
{
    struct MsgHeader msg;                                                     // 告诉客户端准备接收文件
    msg.msgID = MSG_CLIENTSENT;
 
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
 
        if (SOCKET_ERROR == send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0))  // 告诉客户端可以发送
        {
            printf("文件发送失败：%d\n", WSAGetLastError());
        }
    }
 
    return true;
}