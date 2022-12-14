#include <stdio.h>
#include <stdlib.h>
#include "ftpclient.h"

char g_fileName[256];     // save the file name which the server sent
char* g_fileBuf;          // store file content
char g_recvBuf[1024];     // msg buffer
int g_fileSize;           // total file size

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

// init the socket lib in win
bool initSocket()
{
    #ifdef _WIN32
        WSADATA wsadata;

            if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)        // succeed, return 0
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
        if (WSACleanup() != 0)
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
    SOCKET conSoc = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (conSoc == INVALID_SOCKET)
    {
        printf("socket faild:%d", GET_ERROR);
        return;
    }

    // conSoc connect with server IP addr and server port
    struct sockaddr_in serAddr;
    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(SPORT);                       // htons convert local byte sequence to network byte sequence
    #ifdef _WIN32
        serAddr.sin_addr.S_un.S_addr = inet_addr(SIP); // server IP addr
    #else
        serAddr.sin_addr.s_addr = inet_addr(SIP);
    #endif

    if (connect(conSoc, (struct sockaddr*)&serAddr, sizeof(serAddr)) != 0)
    {
        printf("connectToHost: connect faild:%d", GET_ERROR);
        return;
    }
    printf("Connection Succeed!\n");
    if(!login(conSoc)) {
        printf("Reach retry limit, disconnected with server...");
        CLOSE(conSoc);
        return;
    }
    printf("Login Succeed!\n");
    printHelp();
    char flag[105];
    while(1) {
        printf("ftp >>");
        readInput(flag, 100);
        if(memcmp(flag, "put ",4)==0) {
            if(!clientReadySend(conSoc, flag+4)) continue;
            while(processMsg(conSoc))
            {}
        }
        else if(memcmp(flag, "get ",4)==0) {
            downloadFileName(conSoc, flag+4);// starting to processing received msg, 100 is the gap of msg sending
            while (processMsg(conSoc))
            {}
        }
        else if(memcmp(flag, "delete ",7)==0) {
            deleteFile(conSoc, flag+7);
            while (processMsg(conSoc))
            {}
        }
        else if(!strcmp(flag, "pwd")) {
            requestPwd(conSoc);
            while (processMsg(conSoc))
            {}
        }
        else if(!strcmp(flag, "ls")) {
            requestLs(conSoc);
            while (processMsg(conSoc))
            {}
        }
        else if(!strcmp(flag, "help")) {
            printHelp();
        }
        else if(!strcmp(flag, "quit")) {
            printf("FTP quiting...\n");
            CLOSE(conSoc);
            return;
        }
        else if(memcmp(flag, "mkdir ",6)==0){
            requestMkdir(conSoc, flag+6);
            while (processMsg(conSoc))
            {}
        }
        else if(memcmp(flag, "cd ",3)==0){
            requestCd(conSoc, flag+3);
            while (processMsg(conSoc))
            {}
        }
        else {
            printf("Invalid ftp command\n");
        }
    }
}

// handle msg
bool processMsg(SOCKET conSoc){

    recv(conSoc, g_recvBuf, 1024, 0);
    struct MsgHeader* msg = (struct MsgHeader*)g_recvBuf;

    switch (msg->msgID)
    {
        case MSG_OPENFILE_FAILD:
            printf("File doesn't exist on server or is invalid!\n");
            return false;
        case MSG_FILESIZE:
            readyread(conSoc, msg);
            break;
        case MSG_READY_READ:
            writeFile(conSoc, msg);
            break;
        case MSG_SUCCESSED:
            printf("Session Complete!\n");
            return false;
        case MSG_SERVERREAD:
            printf("Ready to Send!\n");
            sendFile(msg);
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
        case MSG_NULLNAME:
            printf("Mkdir failed! Try to create a directory with no name!\n");
            return false;
        case MSG_RECVFAILED:
            printf("Server recv failed!\n");
            return false;
        case MSG_EMPTYFILE:
            printf("Can't get a empty file from server!\n");
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
        if (send(serfd, (char*)&send_msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
        {
            printf("login: Client send error: %d\n", GET_ERROR);
            return false;
        }

        //receive authentication response
        recv(serfd, g_recvBuf, 1024, 0);
        rec_msg = (struct MsgHeader*)g_recvBuf;
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
    if (send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
    {
        printf("requestPwd: Client send error: %d\n", GET_ERROR);
        return;
    }
}

void requestLs(SOCKET serfd) {
    struct MsgHeader msg;
    msg.msgID = MSG_LS;
    if (send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
    {
        printf("requestLs: Client send error: %d\n", GET_ERROR);
        return;
    }
}

void downloadFileName(SOCKET serfd, char* cmd) {
    printf("Now start recving file from server:");
    char fileName[1024];
    struct MsgHeader file;

    strcpy(fileName, cmd);
    printf("%s\n",fileName);

    file.msgID = MSG_FILENAME;
    strcpy(file.myUnion.fileInfo.fileName, fileName);
    if (send(serfd, (char*)&file, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
    {
        printf("downloadFileName: Client send error: %d\n", GET_ERROR);
        return;
    }
}

void readyread(SOCKET serfd, struct MsgHeader* pmsg) {
    // prepare memory: pmsg->fileInfo.fileSize
    g_fileSize = pmsg->myUnion.fileInfo.fileSize;
    strcpy(g_fileName, pmsg->myUnion.fileInfo.fileName);

    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));         // apply for memory
    if (g_fileBuf == NULL)
    {
        printf("Alloc Failed!\n");
    }
    else{
        struct MsgHeader msg;
        msg.msgID = MSG_SENDFILE;

        if (send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
        {
            printf("readyread: Client send error: %d\n", GET_ERROR);
            return;
        }
    }
    printf("Ready to recv file,filename:%s\tsize:%d\n",pmsg->myUnion.fileInfo.fileName, pmsg->myUnion.fileInfo.fileSize);
}

bool writeFile(SOCKET serfd, struct MsgHeader* pmsg) {
    if (g_fileBuf == NULL){
        printf("Didn't prepare memory for file! ERROR!\n");
        return false;
    }

    // get MSG_READY_READ, connect to server
    // init dataSocket
    SOCKET dataSoc = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (dataSoc == INVALID_SOCKET)
    {
        printf("sendFile: socket faild:%d", GET_ERROR);
        return false;
    }

    // get the server data port, connect it with dataSoc
    struct sockaddr_in dataAddr;
    dataAddr.sin_family = AF_INET;
    dataAddr.sin_port = ntohs(pmsg->port);
    #ifdef _WIN32
        dataAddr.sin_addr.S_un.S_addr = inet_addr(SIP); // server IP addr
    #else
        dataAddr.sin_addr.s_addr = inet_addr(SIP);
    #endif

    printf("Connecting to port:%d\n", ntohs(dataAddr.sin_port));
    if (connect(dataSoc, (struct sockaddr*)&dataAddr, sizeof(dataAddr)) != 0)
    {
        printf("sendFile: connect faild:%d\n", GET_ERROR);
        return false;
    }
    printf("Data Connection Succeed!\n");

    // recv data circularly
    while(true){

        int nRes = recv(dataSoc, g_recvBuf, 1024, 0);

        if(nRes <= 0){
            printf("Server leaving...%d\n",GET_ERROR);
            return false;
        }
        struct MsgHeader* dataMsg = (struct MsgHeader*)g_recvBuf;
        int nStart = dataMsg->myUnion.packet.nStart;
        int nsize = dataMsg->myUnion.packet.nsize;

        memcpy(g_fileBuf + nStart, dataMsg->myUnion.packet.buf, nsize);    // same as strncmpy
        long currsize;
        if(nStart + nsize >= g_fileSize){
            currsize = g_fileSize;
        }
        else{
            currsize = nStart + nsize;
        }

        printf("Receiving: %.2f%%\r", ((currsize)/(double)g_fileSize)*100);
        fflush(stdout);

        if (nStart + nsize >= g_fileSize) {                       // check if the file is sent completely
            CLOSE(dataSoc);
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

            if (send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
            {
                printf("writeFile: Client send error: %d\n", GET_ERROR);
                return false;
            }
            printf("Receiving: 100.00%%\n");
            return true;
        }
    }
}

bool clientReadySend(SOCKET conSoc, char* cmd){
    printf("Now start sending file to server: ");

    // check if file exist
    char fileName[1024] = { 0 };
    char suffix[MAXSUFFIX];
    strcpy(fileName, cmd);
    printf("%s\n",fileName);

    // judge the file type from its suffix
    _splitpath(fileName,NULL,NULL,NULL,suffix);
    FILE* pread;

    // check its type
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
        printf("File open failed: %s, Error code: %d\n",fileName,errno);
        return false;
    }

    // check if it's a dir
    char path[505];
    getcwd(path,sizeof (path));
    strcpy(path,"/");
    strcpy(path,fileName);
    struct stat status;
    stat( path, &status );
    if((status.st_mode & S_IFDIR)){
        printf("Invalid file: Can't send a dir:[%s]!\n",fileName);
        return false;
    }

    // get filesize
    fseek(pread, 0, SEEK_END);
    g_fileSize = ftell(pread);

    if(g_fileSize == 0) {
        printf("clientReadySend: Can't send empty file!\n");
        fclose(pread);
        return false;
    }

    fseek(pread, 0, SEEK_SET);

    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));

    if (g_fileBuf == NULL)
    {
        printf("Out of memory, please try again\n");
    }

    fread(g_fileBuf, sizeof(char), g_fileSize, pread);
    g_fileBuf[g_fileSize] = '\0';
    fclose(pread);

    // send put signal to server
    struct MsgHeader msg;
    msg.msgID = MSG_CLIENTREADSENT;
    strcpy(msg.myUnion.fileInfo.fileName, fileName);
    msg.myUnion.fileInfo.fileSize = g_fileSize;
    if (send(conSoc, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
    {
        printf("clientReadySend: Client send error: %d\n", GET_ERROR);
        return false;
    }
    return true;
}


bool sendFile(struct MsgHeader* pms) {
    // init dataSocket
    SOCKET dataSoc = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (dataSoc == INVALID_SOCKET)
    {
        printf("sendFile: socket faild:%d", GET_ERROR);
        return false;
    }

    // get the server data port, connect it with dataSoc
    struct sockaddr_in dataAddr;
    dataAddr.sin_family = AF_INET;
    dataAddr.sin_port = ntohs(pms->port);
    #ifdef _WIN32
        dataAddr.sin_addr.S_un.S_addr = inet_addr(SIP); // server IP addr
    #else
        dataAddr.sin_addr.s_addr = inet_addr(SIP);
    #endif

    printf("Connecting to port:%d\n", ntohs(dataAddr.sin_port));
    if (connect(dataSoc, (struct sockaddr*)&dataAddr, sizeof(dataAddr)) != 0)
    {
        printf("sendFile: connect faild:%d\n", GET_ERROR);
        return false;
    }
    printf("Data Connection Succeed!\n");

    // send file
    struct MsgHeader msg;                                                     // tell the client ready to recv file
    msg.msgID = MSG_CLIENTSENT;

    // if the total size of the whole file is larger than a packet size, dispatch
    for (size_t i = 0; i < g_fileSize; i += PACKET_SIZE)
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

        if (send(dataSoc, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)  // send to server
        {
            printf("sendFile: Sending file failed: %d\n", GET_ERROR);
        }

        printf("Sending: %.2f%%\r", ((currsize)/(double)g_fileSize)*100);
        fflush(stdout);
    }

    printf("Sending: 100.00%%\n");
    CLOSE(dataSoc);
    return true;
}

void deleteFile(SOCKET serfd, char*cmd){
    printf("Now start deleting file on server:");
    struct MsgHeader msg;
    msg.msgID = MSG_DELETE;
    strcpy(msg.myUnion.fileInfo.fileName,cmd);
    printf("%s\n",msg.myUnion.fileInfo.fileName);
    if (send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) printf("deleteFile: Message send error: %d\n", GET_ERROR);
}

void requestMkdir(SOCKET serfd, char* cmd){
    struct MsgHeader msg;
    char directoryName[256];
    strcpy(directoryName, cmd);
    msg.msgID = MSG_MKDIR;
    strcpy(msg.myUnion.directoryInfo.directoryName,directoryName);
    if (send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
    {
        printf("requestMkdir: Sending file failed: %d\n", GET_ERROR);
    }
}

void requestCd(SOCKET serfd, char* cmd){
    struct MsgHeader msg;
    char path[256];
    strcpy(path, cmd);
    msg.msgID =MSG_CD;
    strcpy(msg.myUnion.directoryInfo.directoryName,path);
    if (send(serfd, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
    {
        printf("requestCd: Sending file failed: %d\n", GET_ERROR);
    }
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