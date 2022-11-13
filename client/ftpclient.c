#include <stdio.h>
#include <stdlib.h>
#include "ftpclient.h"

char g_fileName[256];     // save the file name which the server sent
char* g_fileBuf;          // store file content
char g_recvBuf[1024];     // msg buffer
int g_fileSize;           // total file size

void readInput(char* buffer, int size)
{
    char* nl = NULL;
    memset(buffer, 0, size);

    if(fgets(buffer, size, stdin) != NULL)
    {
        nl = strchr(buffer, '\n');
        if(nl != 0)
        {
            *nl = '\0';
        }
    }

    fflush(stdin);
}

int main(void)
{
    initSocket();

    connectToHost();

    closeSocket();

    return 0;
}

#ifndef WIN32
static void _split_whole_name(const char *whole_name, char *fname, char *ext) {
    char *p_ext;

    p_ext = rindex(whole_name, '.');
    if (NULL != p_ext) {
        if(ext) strcpy(ext, p_ext);
        if(fname) snprintf(fname, p_ext - whole_name + 1, "%s", whole_name);
    } else {
        if(ext) ext[0] = '\0';
        if(fname) strcpy(fname, whole_name);
    }
}

void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext) {
    char *p_whole_name;

    if(drive) drive[0] = '\0';
    if (NULL == path)
    {
        if(dir) dir[0] = '\0';
        if(fname) fname[0] = '\0';
        if(ext) ext[0] = '\0';
        return;
    }

    if ('/' == path[strlen(path)])
    {
        if(dir) strcpy(dir, path);
        if(fname) fname[0] = '\0';
        if(ext) ext[0] = '\0';
        return;
    }

    p_whole_name = rindex(path, '/');
    if (NULL != p_whole_name)
    {
        p_whole_name++;
        _split_whole_name(p_whole_name, fname, ext);

        if(dir) snprintf(dir, p_whole_name - path, "%s", path);
    }
    else
    {
        _split_whole_name(path, fname, ext);
        if(dir) dir[0] = '\0';
    }
}
#endif

// init the socket lib in win
bool initSocket()
{
#ifdef _WIN32
    WSADATA wsadata;

        if (0 != WSAStartup(MAKEWORD(2, 2), &wsadata))        // succeed, return 0
        {
            printf("WSAStartup faild: %d\n", WSAGetLastError());
            return false;
        }

        return true;
#else
    return 0;
#endif

}

// close socket lib
bool closeSocket()
{
#ifdef _WIN32
    if (0 != WSACleanup())
        {
            printf("WSACleanup faild: %d\n", WSAGetLastError());
            return false;
        }

        return true;
#else
    return 0;
#endif
}

// listen the client connection
void connectToHost()
{
    // create server socket (addr, port, the AF_INET is IPV4)
    SOCKET serfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (INVALID_SOCKET == serfd)
    {
        printf("socket faild:%d", GET_ERROR);
        return;
    }

    // bind socket with IP addr and port
    struct sockaddr_in serAddr;
    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(SPORT);                       // htons convert local byte sequence to network byte sequence
#ifdef _WIN32
    serAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1"); // server IP addr
#else
    serAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
#endif


    // connect to server
    if (0 != connect(serfd, (struct sockaddr*)&serAddr, sizeof(serAddr)))
    {
        printf("connect faild:%d", GET_ERROR);
        return;
    }
    printf("Connection Succeed!\n");
    if(!login(serfd)) {
        printf("Reach retry limit, disconnected with server...");
        CLOSE(serfd);
        return;
    }
    printf("Login Succeed!\n");
    printHelp();
    char flag[105];
    while(1) {
        printf("ftp >>");
        readInput(flag, 100);
        if(memcmp(flag, "put ",4)==0) {
            if(!clientReadySend(serfd, flag+4)) continue;
            while(processMsg(serfd))
            {}
        }
        else if(memcmp(flag, "get ",4)==0) {
            downloadFileName(serfd, flag+4);// starting to processing received msg, 100 is the gap of msg sending
            while (processMsg(serfd))
            {}
        }
        else if(memcmp(flag, "delete ",7)==0) {
            deleteFile(serfd, flag+7);
            while (processMsg(serfd))
            {}
        }
        else if(!strcmp(flag, "pwd")) {
            requestPwd(serfd);
            while (processMsg(serfd))
            {}
        }
        else if(!strcmp(flag, "ls")) {
            requestLs(serfd);
            while (processMsg(serfd))
            {}
        }
        else if(!strcmp(flag, "help")) {
            printHelp();
        }
        else if(!strcmp(flag, "quit")) {
            printf("FTP quiting...\n");
            CLOSE(serfd);
            return;
        }
        else if(memcmp(flag, "mkdir ",6)==0){
            requestMkdir(serfd, flag+6);
            while (processMsg(serfd))
            {}
        }
        else if(memcmp(flag, "cd ",3)==0){
            requestCd(serfd, flag+3);
            while (processMsg(serfd))
            {}
        }
        else {
            printf("Invalid ftp command\n");
        }
    }

//        printf("\nPress Any Key To Continue:\n");
//        getchar();

}

// handle msg
bool processMsg(SOCKET serfd)
{

    recv(serfd, g_recvBuf, 1024, 0);                     // recv msg
    struct MsgHeader* msg = (struct MsgHeader*)g_recvBuf;

    /*
    MSG_LOGIN = 0,             //登录                 两者都使用
    MSG_FILENAME = 1,         // 文件名称              服务器使用
    MSG_FILESIZE = 2,         // 文件大小              客户端使用
    MSG_READY_READ = 3,         // 准备接受              客户端使用
    MSG_SENDFILE = 4,         // 发送                  服务器使用
    MSG_SUCCESSED = 5,         // 传输完成              两者都使用
    MSG_OPENFILE_FAILD = 6,          // 告诉客户端文件找不到  客户端使用
    MSG_CLIENTREADSENT = 7,        //客户端发送路径和文件大小
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
    */

    switch (msg->msgID)
    {
        case MSG_OPENFILE_FAILD:         // 6
            printf("File doesn't exist on server!\n");
            return false;
        case MSG_FILESIZE:               // 2  recv for the first time
            readyread(serfd, msg);
            break;
        case MSG_READY_READ:             // 3
            writeFile(serfd, msg);
            break;
        case MSG_SUCCESSED:              // 5
            printf("Session Complete!\n");
            return false;
        case MSG_SERVERREAD:
            printf("Ready to Send!\n");
            sendFile(serfd, msg);
            break;
        case MSG_RECV:                  //added by yxy
            readMessage(msg);
            return false;
        case MSG_NOFILE:
            printf("File doesn't exist on server!\n");
            return false;
        case MSG_DELETIONFAILED:
            printf("Deletion Failed!\n");
            return false;
        case  MSG_SAMEDIR:
            printf("Same name directory has existed!\n");
            return false;
        case  MSG_CDFAILED:
            printf("Cd failed!\n");
            return false;
    }

    return true;
}
bool login(SOCKET serfd) {
    char username[105];
    char password[105];
    struct MsgHeader send_msg;
    struct MsgHeader* rec_msg;
    send_msg.msgID = MSG_LOGIN;

    while(true) {
        //send user and pswd
        printf("username >>");
        readInput(username, MAXLOGIN);
        printf("password >>");
        readInput(password, MAXLOGIN);
        strcpy(send_msg.myUnion.fileInfo.fileName, username);
        strcat(send_msg.myUnion.fileInfo.fileName, " ");
        strcat(send_msg.myUnion.fileInfo.fileName, password);
        send(serfd, (char*)&send_msg, sizeof(struct MsgHeader), 0);
        //receive authentication response

        recv(serfd, g_recvBuf, 1024, 0);
        rec_msg = (struct MsgHeader*)g_recvBuf;
//        printf("%s\n",rec_msg->myUnion.fileInfo.fileName);
        if(!strcmp(rec_msg->myUnion.fileInfo.fileName, "Success")) return true;
        else if(!strcmp(rec_msg->myUnion.fileInfo.fileName, "ReachMax")) return false;
        else {
            printf("Invalid username or wrong pswd, please try again:\n");
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
void downloadFileName(SOCKET serfd, char* cmd)
{
    printf("Now start recving file from server:");
    char fileName[1024];
    struct MsgHeader file;

    strcpy(fileName, cmd);
    printf("%s\n",fileName);

    file.msgID = MSG_FILENAME;                               // MSG_FILENAME = 1
    strcpy(file.myUnion.fileInfo.fileName, fileName);
    send(serfd, (char*)&file, sizeof(struct MsgHeader), 0);  // send to server for the first time
}

void readyread(SOCKET serfd, struct MsgHeader* pmsg)
{
    // prepare memory: pmsg->fileInfo.fileSize
    g_fileSize = pmsg->myUnion.fileInfo.fileSize;
    strcpy(g_fileName, pmsg->myUnion.fileInfo.fileName);

    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));         // apply for memory

    if (g_fileBuf == NULL)
    {
        printf("Alloc Failed!\n");
    }
    else{
        struct MsgHeader msg;  // MSG_SENDFILE = 4
        msg.msgID = MSG_SENDFILE;

        if (SOCKET_ERROR == send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0))   // Send the second time
        {
            printf("Client send error: %d\n", GET_ERROR);
            return;
        }
    }

    printf("Ready to recv file,filename:%s\tsize:%d\n",pmsg->myUnion.fileInfo.fileName, pmsg->myUnion.fileInfo.fileSize);
}

bool writeFile(SOCKET serfd, struct MsgHeader* pmsg)
{
    if (g_fileBuf == NULL){
        printf("Didn't prepare memory for file! ERROR!\n");
        return false;
    }

    int nStart = pmsg->myUnion.packet.nStart;
    int nsize = pmsg->myUnion.packet.nsize;

    memcpy(g_fileBuf + nStart, pmsg->myUnion.packet.buf, nsize);    // same as strncmpy
    long currsize;
    if(nStart + nsize >= g_fileSize){
        currsize = g_fileSize;
    }
    else{
        currsize = nStart + nsize;
    }

    printf("Receiving: %.2f%%\r", ((currsize)/(double)g_fileSize)*100);
    fflush(stdout);

    if (nStart + nsize >= g_fileSize)                       // check if the file is sent completely
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
        printf("Receiving: 100.00%%\n");
        return true;
    }

    return true;
}

bool clientReadySend(SOCKET serfd, char* cmd){
    printf("Now start sending file to server: ");
    struct MsgHeader msg;
    msg.msgID = MSG_CLIENTREADSENT;
    char fileName[1024] = { 0 };
    char suffix[MAXSUFFIX];
    strcpy(fileName, cmd);
    printf("%s\n",fileName);

    // judge the file type from its suffix
    _splitpath(fileName,NULL,NULL,NULL,suffix);
    FILE* pread;
    bool isText = false;

    for(int i=0;i<TEXTFILETYPES;i++){
        if(!strcmp(suffix,textFiles[i])){
            isText = true;
            break;
        }
    }

    if(isText){
        pread = fopen(fileName, "rt");
        printf("Sending with text mode...\n");
    }
    else{
        pread = fopen(fileName, "rb");
        printf("Sending with binary mode...\n");
    }

    if(pread == NULL) {
        printf("File open failed: %s, Error code: %d\n",fileName,GET_ERROR);
        return false;
    }

    fseek(pread, 0, SEEK_END);
    g_fileSize = ftell(pread);
    fseek(pread, 0, SEEK_SET);

    strcpy(msg.myUnion.fileInfo.fileName, fileName);
    msg.myUnion.fileInfo.fileSize = g_fileSize;

    send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0);
    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));

    if (g_fileBuf == NULL)
    {
        printf("Out of memory, please try again\n");
    }

    fread(g_fileBuf, sizeof(char), g_fileSize, pread);
    g_fileBuf[g_fileSize] = '\0';

    fclose(pread);
    return true;
}


bool sendFile(SOCKET serfd, struct MsgHeader* pms)
{
    struct MsgHeader msg;                                                     // tell the client ready to recv file
    msg.msgID = MSG_CLIENTSENT;

    // if the total size of the whole file is larger than a packet size, dispatch
    for (size_t i = 0; i < g_fileSize; i += PACKET_SIZE)                       // PACKET_SIZE = 1012
    {
        long currsize;
        msg.myUnion.packet.nStart = i;

        // the total size of the whole file is smaller than a packet size
        if (i + PACKET_SIZE + 1 > g_fileSize)
        {
            msg.myUnion.packet.nsize = g_fileSize - i;
            currsize = g_fileSize;
        }
        else
        {
            msg.myUnion.packet.nsize = PACKET_SIZE;
            currsize = i + PACKET_SIZE;
        }

        memcpy(msg.myUnion.packet.buf, g_fileBuf + msg.myUnion.packet.nStart, msg.myUnion.packet.nsize);

        if (SOCKET_ERROR == send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0))  // tell the client that can send
        {
            printf("Sending file failed: %d\n", GET_ERROR);
        }

        printf("Sending: %.2f%%\r", ((currsize)/(double)g_fileSize)*100);
        fflush(stdout);
    }

    printf("Sending: 100.00%%\n");
    return true;
}

void deleteFile(SOCKET serfd, char*cmd){
    printf("Now start deleting file on server:");
    struct MsgHeader msg;
    msg.msgID = MSG_DELETE;
    strcpy(msg.myUnion.fileInfo.fileName,cmd);
    printf("%s\n",msg.myUnion.fileInfo.fileName);
    if (SOCKET_ERROR == send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0)) printf("deleteFile: Message send error: %d\n", GET_ERROR);
}
void requestMkdir(SOCKET serfd, char* cmd){
    struct MsgHeader msg;
    char directoryName[256];
    strcpy(directoryName, cmd);
    msg.msgID = MSG_MKDIR;
    strcpy(msg.myUnion.directoryInfo.directoryName,directoryName);
    send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0);
}
void requestCd(SOCKET serfd, char* cmd){
    struct MsgHeader msg;
    char path[256];
    strcpy(path, cmd);
    msg.msgID =MSG_CD;
    strcpy(msg.myUnion.directoryInfo.directoryName,path);
    send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0);
}
void printHelp(){
    printf("***************************************\n");
    printf("[ Enter valid commands below to use FTP system! ]\n");
    printf("put [filename]: Send file to server\n");
    printf("get [filename]: Get file from server\n");
    printf("delete [filename]: Delete file on server\n");
    printf("pwd: Print current working directory\n");
    printf("ls: List all files in current directory on server\n");
    printf("mkdir: Make a new directory on server\n");
    printf("cd: Change to the remote directory on the remote machine\n");
    printf("help: Re-print valid commands\n");
    printf("quit: Quit FTP system\n");
    printf("***************************************\n");
}