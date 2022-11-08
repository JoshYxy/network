#include <stdio.h>
#include <stdlib.h>
#include "ftpserver.h"

char g_recvBuf[1024] = { 0 };      // recv client msg
int g_fileSize;                    // file size
char* g_fileBuf;                   // file cache, store file which is ready to send to client
char g_fileName[256];

int main(void)
{
    initSocket();

//    while(1){
    listenToClient();
//    }

    closeSocket();

    return 0;
}

#ifndef WIN32
static void _split_whole_name(const char *whole_name, char *fname, char *ext) {
    char *p_ext;

    p_ext = rindex(whole_name, '.');
    if (NULL != p_ext) {
        if(ext) strcpy(ext, p_ext);
        snprintf(fname, p_ext - whole_name + 1, "%s", whole_name);
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

        snprintf(dir, p_whole_name - path, "%s", path);
    }
    else
    {
        _split_whole_name(path, fname, ext);
        if(dir) dir[0] = '\0';
    }
}
#endif

// init socket lib
bool initSocket()
{
#ifdef _WIN32
    WSADATA wsadata;

        if (0 != WSAStartup(MAKEWORD(2, 2), &wsadata))
        {
            printf("WSAStartup failed: %d\n", WSAGetLastError());
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
            printf("WSACleanup failed: %d\n", WSAGetLastError());
            return false;
        }

        return true;
#else
    return 0;
#endif
}

// listen cilent
void listenToClient()
{

    // create server socket (addr, port, AF_INET is IPV4)
    SOCKET serfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (serfd == INVALID_SOCKET)
    {
        printf("Socket creation failed:%d\n", GET_ERROR);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    // bind socket with IP addr and port
    struct sockaddr_in serAddr;

    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(SPORT);             // htons set local byte sequence to network sequence
#ifdef _WIN32
    serAddr.sin_addr.S_un.S_addr = ADDR_ANY;     // listen to all network card of the pc
#else
    serAddr.sin_addr.s_addr = htonl(INADDR_ANY);
#endif

    if (0 != bind(serfd, (struct sockaddr*)&serAddr, sizeof(serAddr)))
    {
        printf("Bind socket with IP addr and port failed:%d\n", GET_ERROR);
        return;
    }

    // listen to client connection
    if (0 != listen(serfd, 10))                  // 10 is the maximum of the connection queue
    {
        printf("Listen failed:%d\n", GET_ERROR);
        return;
    }

    // client connection, recv it
    struct sockaddr_in cliAddr;
    int len = sizeof(cliAddr);

    SOCKET clifd = accept(serfd, (struct sockaddr*)&cliAddr, &len);

    if (INVALID_SOCKET == clifd)
    {
        printf("Accept failed:%d\n", GET_ERROR);
        return;
    }

    printf("Receiving client connection succeed!\n");

    // user auth
    if(!auth(clifd)) {
        printf("User auth reach maximum retry or failed connection, disconnected.\n");
#ifdef _WIN32
        Sleep(5000);
#else
        sleep(5);
#endif
        return;
    }

    // processing msg
    while (processMag(clifd)) {
#ifdef _WIN32
        Sleep(200);
#else
        sleep(1);
#endif
    }

}

// processing msg
bool processMag(SOCKET clifd)
{
    // if recv succeed, return the bytes of the msg, else return 0
    int nRes = recv(clifd, g_recvBuf, 1024, 0);

    if (nRes <= 0)
    {
        printf("Client leaving...%d\n", GET_ERROR);
        return false;
    }

    // get the recved msg
    struct MsgHeader* msg = (struct MsgHeader*)g_recvBuf;
    struct MsgHeader exitmsg;

    char inf[505]; //get the stored local information
    memset(inf, 0, sizeof(inf));
    switch (msg->msgID)
    {
        case MSG_FILENAME:          // 1  first recv
            printf("%s\n", msg->myUnion.fileInfo.fileName);
            readFile(clifd, msg);
            break;
        case MSG_SENDFILE:          // 4
            sendFile(clifd, msg);
            break;
        case MSG_SUCCESSED:         // 5

            exitmsg.msgID = MSG_SUCCESSED;

            if (SOCKET_ERROR == send(clifd, (char*)&exitmsg, sizeof(struct MsgHeader), 0))   //send failed
            {
                printf("send failed: %d\n", GET_ERROR);
                return false;
            }
            printf("Session Finished!\n");
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

bool auth(SOCKET clifd) {
    struct MsgHeader send_msg;
    struct MsgHeader* rec_msg;
    int try = 0;
    char rec_string[80];
    char *username;
    char *password;
    send_msg.msgID = MSG_LOGIN;

    while(try < MAXTRY) {
        try++;
        int nRes = recv(clifd, g_recvBuf, 1024, 0);
        if (nRes <= 0)
        {
            printf("Client leaving...%d\n", GET_ERROR);
            return false;
        }

        rec_msg = (struct MsgHeader*)g_recvBuf;
        strcpy(rec_string, rec_msg->myUnion.fileInfo.fileName);
        username = strtok(rec_string, " ");
        password = strtok(NULL, " ");

//        printf("%s\n%s\n", username, password);

        if(!strcmp(username, USER) && !strcmp(password, PASS)) {  //compare username and pswd from client
            strcpy(send_msg.myUnion.fileInfo.fileName, "Success");
            if (SOCKET_ERROR == send(clifd, (const char *)&send_msg, sizeof(struct MsgHeader), 0))
            {
                printf("Message send error: %d\n", GET_ERROR);
                return false;
            }
            return true;
        }
        else if(try < MAXTRY){
            strcpy(send_msg.myUnion.fileInfo.fileName, "Failure");
            if (SOCKET_ERROR == send(clifd, (const char *)&send_msg, sizeof(struct MsgHeader), 0))
            {
                printf("Message send error: %d\n", GET_ERROR);
                return false;
            }
        }
    }
    strcpy(send_msg.myUnion.fileInfo.fileName, "ReachMax");
    if (SOCKET_ERROR == send(clifd, (const char *)&send_msg, sizeof(struct MsgHeader), 0))
    {
        printf("Message send error: %d\n", GET_ERROR);
        return false;
    }
    return false;
}

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
/*
*1.The Client wants to download: send the filename to server.
*2.Server recv the client sent filename: find the file according to the filename, send the file size to client.
*3.Client recv the file size:  perpare to recv file, alloc memory, when ready, tell the server to send.
*4.Server recv ready instruction: send file.
*5.Client start to recv and store data. When complete, tell the server complete.
*6.Close connection.
*/
bool readFile(SOCKET clifd, struct MsgHeader* pmsg)
{
    // open the file with binary mode
    FILE* pread = fopen(pmsg->myUnion.fileInfo.fileName, "rb");

    if (pread == NULL)
    {
        printf("Can't find file: [%s] ...\n", pmsg->myUnion.fileInfo.fileName);

        struct MsgHeader msg;
        msg.msgID = MSG_OPENFILE_FAILD;                                             // MSG_OPENFILE_FAILD = 6

        if (SOCKET_ERROR == send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0))   // send failed
        {
            printf("Send failed: %d\n", GET_ERROR);
        }

        return false;
    }

    // get the file size
    fseek(pread, 0, SEEK_END);
    g_fileSize = ftell(pread);
    fseek(pread, 0, SEEK_SET);

    // send the file size to client
    char text[100];
    char tfname[200] = { 0 };
    struct MsgHeader msg;

    msg.msgID = MSG_FILESIZE;                                       // MSG_FILESIZE = 2
    msg.myUnion.fileInfo.fileSize = g_fileSize;

    // get the filename and suffix from the client msg header
    _splitpath(pmsg->myUnion.fileInfo.fileName, NULL, NULL, tfname, text);  //only add suffix to the last name

    // send filename and file size to client
    strcat(tfname, text);
    strcpy(msg.myUnion.fileInfo.fileName, tfname);
    send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0);            // send filename, suffix and file size to client, first send

    // alloc mem
    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));

    if (g_fileBuf == NULL)
    {
        printf("No memory, please retry\n");
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
        printf("message send error: %d\n", GET_ERROR);
        return;
    }
}
bool sendFile(SOCKET clifd, struct MsgHeader* pms)
{
    struct MsgHeader msg;                                                     // tell the client ready to recv file
    msg.msgID = MSG_READY_READ;

    // if the total size of the whole file is larger than a packet size, dispatch
    for (size_t i = 0; i < g_fileSize; i += PACKET_SIZE)                       // PACKET_SIZE = 1012
    {
        msg.myUnion.packet.nStart = i;

        // the reset size of the sending file is smaller than a packet size
        if (i + PACKET_SIZE + 1 > g_fileSize)
        {
            msg.myUnion.packet.nsize = g_fileSize - i;
        }
        // the reset size of the sending file is still larger than a packet size
        else{
            msg.myUnion.packet.nsize = PACKET_SIZE;
        }

        // copy data from local cache buffer to packet
        memcpy(msg.myUnion.packet.buf, g_fileBuf + msg.myUnion.packet.nStart, msg.myUnion.packet.nsize);

        if (SOCKET_ERROR == send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0))  // send packet to client
        {
            printf("Send file failed: %d\n", GET_ERROR);
        }
    }

    return true;
}

void serverReady(SOCKET clifd, struct MsgHeader* pmsg)
{
    g_fileSize = pmsg->myUnion.fileInfo.fileSize;
    char text[100];
    char tfname[200] = { 0 };

    _splitpath(pmsg->myUnion.fileInfo.fileName, NULL, NULL, tfname, text);  //only add suffix to the last name

    strcat(tfname, text);
    strcpy(g_fileName, tfname);
    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));         // alloc mem

    if (g_fileBuf == NULL)
    {
        printf("Alloc falied!\n");
    }
    else
    {
        struct MsgHeader msg;
        msg.msgID = MSG_SERVERREAD;

        if (SOCKET_ERROR == send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0))   // send the second time
        {
            printf("Client send error: %d\n", GET_ERROR);
            return;
        }
    }

    printf("Filename:%s\tSize:%d\t\n", pmsg->myUnion.fileInfo.fileName, pmsg->myUnion.fileInfo.fileSize);
}

bool writeFile(SOCKET clifd, struct MsgHeader* pmsg)
{
    if (g_fileBuf == NULL)
    {
        return false;
    }

    int nStart = pmsg->myUnion.packet.nStart;
    int nsize = pmsg->myUnion.packet.nsize;

    memcpy(g_fileBuf + nStart, pmsg->myUnion.packet.buf, nsize);    // the same as strncmpy
    printf("Packet size:%d %d\n", nStart + nsize, g_fileSize);

    if (nStart + nsize >= g_fileSize)                       // check if the data sending is complete
    {
        FILE* pwrite;
        struct MsgHeader msg;

        pwrite = fopen(g_fileName, "wb");
        msg.msgID = MSG_SUCCESSED;

        if (pwrite == NULL)
        {
            printf("Write file error...\n");
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