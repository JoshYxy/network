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
};
int main(void)
{
    initSocket();

    connectToHost();

    closeSocket();

    return 0;
}

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
    printf("***************************************\n");
    printf("1.Send file to server\n");
    printf("2.Get file from server\n");
    printf("3.Quit\n");
    printf("4.pwd\n");
    printf("5.ls\n");
    printf("***************************************\n");
    char flag[105];
    while(1) {
        printf("ftp >>");
        readInput(flag, 100);

        if(!strcmp(flag, "put")) {
            printf("Now start sending file to server:");
            clientReadySend(serfd);
            while(processMag(serfd))
            {}
        }
        else if(!strcmp(flag, "get")) {
            printf("Now start recving file from server:\n");
            downloadFileName(serfd);// starting to processing received msg, 100 is the gap of msg sending
            while (processMag(serfd))
            {}
        }
        else if(!strcmp(flag, "pwd")) {
            requestPwd(serfd);
            while (processMag(serfd))
            {}
        }
        else if(!strcmp(flag, "ls")) {
            requestLs(serfd);
            while (processMag(serfd))
            {}
        }
        else if(!strcmp(flag, "quit")) {
            printf("FTP quiting...\n");
            CLOSE(serfd);
            return;
        }
        else {
            printf("Invalid ftp command\n");
        }
    }

//        printf("\nPress Any Key To Continue:\n");
//        getchar();

}

// handle msg
bool processMag(SOCKET serfd)
{

    recv(serfd, g_recvBuf, 1024, 0);                     // recv msg
    struct MsgHeader* msg = (struct MsgHeader*)g_recvBuf;

    /*
    *MSG_FILENAME       = 1,       // filename               server use
    *MSG_FILESIZE       = 2,       // filesize               client use
    *MSG_READY_READ     = 3,       // ready to recv          client use
    *MSG_SENDFILE       = 4,       // send                   server use
    *MSG_SUCCESSED      = 5,       // send complete          both use
    *MSG_OPENFILE_FAILD = 6        // tell the client that can't find file    client use
    */

    switch (msg->msgID)
    {
        case MSG_OPENFILE_FAILD:         // 6
            downloadFileName(serfd);
            break;
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
            printf("Ready to Send!");
            sendFile(serfd, msg);
            break;

        case MSG_RECV:                  //added by yxy
            readMessage(msg);
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
//        scanf("%s", username);
        readInput(username, MAXLOGIN);
        printf("password >>");
        readInput(password, MAXLOGIN);
//        scanf("%s", password);
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
void downloadFileName(SOCKET serfd)
{
    char fileName[1024];
    struct MsgHeader file;

    printf("Enter download filename:");

    scanf("%s", fileName);                              // file path
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
    if (g_fileBuf == NULL)
    {
        printf("Didn't prepare memory for file! ERROR!\n");
        return false;
    }

    int nStart = pmsg->myUnion.packet.nStart;
    int nsize = pmsg->myUnion.packet.nsize;

    memcpy(g_fileBuf + nStart, pmsg->myUnion.packet.buf, nsize);    // same as strncmpy
    printf("packet size:%d %d\n", nStart + nsize, g_fileSize);

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

        return false;
    }

    return true;
}

void clientReadySend(SOCKET serfd)
{
    struct MsgHeader msg;
    msg.msgID = MSG_CLIENTREADSENT;
    char fileName[1024] = { 0 };
    printf("Enter the filename to upload:");
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
        printf("Out of memory, please try again\n");
    }

    fread(g_fileBuf, sizeof(char), g_fileSize, pread);
    g_fileBuf[g_fileSize] = '\0';

    fclose(pread);
}


bool sendFile(SOCKET serfd, struct MsgHeader* pms)
{
    struct MsgHeader msg;                                                     // tell the client ready to recv file
    msg.msgID = MSG_CLIENTSENT;

    // if the total size of the whole file is larger than a packet size, dispatch
    for (size_t i = 0; i < g_fileSize; i += PACKET_SIZE)                       // PACKET_SIZE = 1012
    {
        msg.myUnion.packet.nStart = i;

        // the total size of the whole file is smaller than a packet size
        if (i + PACKET_SIZE + 1 > g_fileSize)
        {
            msg.myUnion.packet.nsize = g_fileSize - i;
        }
        else
        {
            msg.myUnion.packet.nsize = PACKET_SIZE;
        }

        memcpy(msg.myUnion.packet.buf, g_fileBuf + msg.myUnion.packet.nStart, msg.myUnion.packet.nsize);

        if (SOCKET_ERROR == send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0))  // tell the client that can send
        {
            printf("Sending file failed: %d\n", GET_ERROR);
        }
    }

    return true;
}