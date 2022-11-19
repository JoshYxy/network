#pragma once

#ifdef _WIN32
/* See http://stackoverflow.com/questions/12765743/getaddrinfo-on-win32 */
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0501  /* Windows XP. */
    #endif
    #include <winsock2.h>
    #include <Ws2tcpip.h>
    #define CLOSE closesocket
    #define GET_ERROR WSAGetLastError()
#else
/* Assume that any non-Windows platform uses POSIX-style sockets instead. */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>  /* Needed for getaddrinfo() and freeaddrinfo() */
#include <unistd.h> /* Needed for close() */
#include <string.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define SOCKET int
#define GET_ERROR geterror()
#define CLOSE close
#endif

#pragma comment(lib,"ws2_32.lib")  // 加载静态库
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>       // for dir check

#define SPORT 8888                 // 服务器端口号
#define SIP "127.0.0.1"             //server IP
#define PACKET_SIZE (1024 - sizeof(int) * 4)
#define TEXTFILETYPES 7
const int MAXLOGIN = 100;
const int MAXSUFFIX = 100;

extern int errno;
int geterror(){return errno;}

char textFiles[][TEXTFILETYPES] = {".txt",".c",".h",".css",".js",".php",".html"};

// 定义标记
enum MSGTAG
{
    MSG_LOGIN = 0,             //登录                 两者都使用
    MSG_FILENAME = 1,         // 文件名称              服务器使用
    MSG_FILESIZE = 2,         // 文件大小              客户端使用
    MSG_READY_READ = 3,         // 准备接受              客户端使用
    MSG_SENDFILE = 4,         // 发送                  服务器使用
    MSG_SUCCESSED = 5,         // 传输完成              两者都使用
    MSG_OPENFILE_FAILD = 6,          // 告诉客户端文件找不到  客户端使用
    MSG_CLIENTREADSENT = 7,        // client ask to put, send filename and size to server, get server data port
    MSG_SERVERREAD = 8,        //服务端申请空间
    MSG_CLIENTSENT = 9,            //客户端传输
    MSG_PWD = 10,
    MSG_RECV = 11,
    MSG_LS = 12,
    MSG_DELETE = 13,        //delete file on server, SERVER
    MSG_NOFILE = 14,        //delete file doesn't exist, CLIENT
    MSG_DELETIONFAILED = 15,    //deletion failed, CLIENT
    MSG_MKDIR = 16,         //mkdir a directory, SERVER
    MSG_SAMEDIR =17,       //same dir name,   CLIENT
    MSG_CD =18,         //cd to a directory , SERVER
    MSG_CDFAILED =19,   //cd failed,   CLIENT
    MSG_NULLNAME =20,  //NAME=null,   CLIENT
    MSG_RECVFAILED = 21,     // recv failed, CLIENT
    MSG_EMPTYFILE = 22      // try to send empty file, CLIENT
};

#pragma pack(1)                     // 设置结构体1字节对齐**************

struct MsgHeader                    // 封装消息头
{
    enum MSGTAG msgID;              // 当前消息标记   4
    int port;
    union MyUnion
    {
        struct Mystruct
        {
            long fileSize;           // 文件大小  4
            char fileName[256];     // 文件名    256
        }fileInfo;
        struct
        {
            int nStart;             // 包的编号
            int nsize;              // 该包的数据大小
            char buf[PACKET_SIZE];
        }packet;
        struct {
            char directoryName[256];//目录名/目录路径  256
        }directoryInfo;
    }myUnion;

};

#pragma pack()

// 初始化socket库
bool initSocket();

// 关闭socket库
bool closeSocket();

// 监听客户端连接
void connectToHost();

// 处理消息
bool processMsg(SOCKET);

// 获取文件名
void downloadFileName(SOCKET serfd, char* cmd);

// 文件内容读进内存
void readyread(SOCKET, struct MsgHeader*);

// 写入文件内容
bool writeFile(SOCKET, struct MsgHeader*);

//服务端发送文件路径和大小 然后在自己的缓冲区将文件缓存下来
bool clientReadySend(SOCKET, char* cmd);

//准备开始发送文件
bool sendFile(struct MsgHeader*);

//yxy新加的三个函数
void readMessage(struct MsgHeader*);

void requestPwd(SOCKET);

void requestLs(SOCKET);
// 登录
bool login(SOCKET);

void readInput(char *, int);

void deleteFile(SOCKET serfd, char* cmd);
//gza新加
void requestMkdir(SOCKET serfd, char* cmd);

void requestCd(SOCKET serfd, char* cmd);

void printHelp();