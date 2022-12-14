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

    listenToClient();

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

// init socket lib
bool initSocket()
{
#ifdef _WIN32
    WSADATA wsadata;

        if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
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
    if (WSACleanup() != 0)
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
    printf("Server running...\n");

    // create server socket (addr, port, AF_INET is IPV4)
    SOCKET serfd;
    while((serfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET){
        #ifdef _WIN32
            WSACleanup();
            Sleep(1000);
        #else
            sleep(1);
        #endif
    }

    // bind socket with IP addr and port
    struct sockaddr_in serAddr;

    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(SPORT);
#ifdef _WIN32
    serAddr.sin_addr.S_un.S_addr = ADDR_ANY;     // listen to all network card of the pc
#else
    serAddr.sin_addr.s_addr = htonl(INADDR_ANY);
#endif

    if (bind(serfd, (struct sockaddr*)&serAddr, sizeof(serAddr)) != 0)
    {
        printf("Bind socket with IP addr and port failed:%d\n", GET_ERROR);
        return;
    }

    // listen to client connection
    if (listen(serfd, 10) != 0)                  // 10 is the maximum of the connection queue
    {
        printf("Listen failed:%d\n", GET_ERROR);
        return;
    }

    // client connection, recv it
    struct sockaddr_in cliAddr;
    int len = sizeof(cliAddr);

    // always on server
    while(true){
        SOCKET conSock = accept(serfd, (struct sockaddr*)&cliAddr, &len);

        if (conSock == INVALID_SOCKET)
        {
            printf("Accept failed:%d\n", GET_ERROR);
            continue;
        }

        printf("Receiving client connection succeed!\n");

        // user auth
        if(!auth(conSock)) {
            printf("User auth reach maximum retry or failed connection, disconnected.\n");
#ifdef _WIN32
            Sleep(5000);
#else
            sleep(5);
#endif
            continue;
        }

        // processing msg
        while (processMsg(conSock)) {}
    }
}

// processing msg
bool processMsg(SOCKET conSock)
{
    // if recv succeed, return the bytes of the msg, else return 0
    int nRes = recv(conSock, g_recvBuf, 1024, 0);

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
        case MSG_FILENAME:
            readFile(conSock, msg);
            break;
        case MSG_SENDFILE:
            sendFile(conSock, msg);
            break;
        case MSG_SUCCESSED:

            exitmsg.msgID = MSG_SUCCESSED;

            if (send(conSock, (char*)&exitmsg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)   //send failed
            {
                printf("send failed: %d\n", GET_ERROR);
                return false;
            }
            printf("Session Finished!\n");
            break;
        case MSG_CLIENTREADSENT:
            serverReady(conSock, msg);
            break;
        case MSG_PWD: //added by yxy
            getMessage(MSG_PWD, inf);
            sendMessage(conSock, inf);
            break;
        case MSG_LS: //added by yxy
            getMessage(MSG_LS, inf);
            sendMessage(conSock, inf);
            break;
        case MSG_DELETE:
            deletefile(conSock,msg);
            break;
        case MSG_MKDIR:
            makeDirectory(conSock,msg);
            break;
        case MSG_CD:
            chDirectory(conSock,msg);
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
        if (nRes <= 0) {
            printf("Client leaving...%d\n", GET_ERROR);
            return false;
        }

        rec_msg = (struct MsgHeader*)g_recvBuf;
        strcpy(rec_string, rec_msg->myUnion.fileInfo.fileName);
        username = strtok(rec_string, " ");
        password = strtok(NULL, " ");

        if(username && password && !strcmp(username, USER) && !strcmp(password, PASS)) {  //compare username and pswd from client
            strcpy(send_msg.myUnion.fileInfo.fileName, "Success");
            if (send(clifd, (const char *)&send_msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
            {
                printf("Message send error: %d\n", GET_ERROR);
                return false;
            }
            return true;
        }
        else if(try < MAXTRY){
            strcpy(send_msg.myUnion.fileInfo.fileName, "Failure");
            if (send(clifd, (const char *)&send_msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
            {
                printf("Message send error: %d\n", GET_ERROR);
                return false;
            }
        }
    }
    strcpy(send_msg.myUnion.fileInfo.fileName, "ReachMax");
    if (send(clifd, (const char *)&send_msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
    {
        printf("Message send error: %d\n", GET_ERROR);
        return false;
    }
    return false;
}

void getMessage(int type, char inf[505]) {
    char path[505];
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

bool readFile(SOCKET clifd, struct MsgHeader* pmsg) {
    char text[MAXSUFFIX];
    char tfname[MAXSTRING] ;
    // get the filename and suffix from the client msg header
    _splitpath(pmsg->myUnion.fileInfo.fileName, NULL, NULL, tfname, text);  //only add suffix to the last name
    strcat(tfname, text);
    printf("%s\n", tfname);

    FILE* pread;
    // judge the file type from its suffix
    bool isText = false;
    for(int i=0; i < TEXTFILETYPES; i++){
        if(!strcmp(text,textFiles[i])){
            isText = true;
            break;
        }
    }

    if(isText){
        pread = fopen(tfname, "rt");
        printf("Sending with text mode...\n");
    }
    else{
        pread = fopen(tfname, "rb");
        printf("Sending with binary mode...\n");
    }

    if (pread == NULL)
    {
        printf("Can't find file: [%s] ...\n", tfname);

        struct MsgHeader msg;
        msg.msgID = MSG_OPENFILE_FAILD;

        if (send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)   // send failed
        {
            printf("Send failed: %d\n", GET_ERROR);
        }
        return false;
    }

    // check if it's a dir
    char path[505];
    getcwd(path,sizeof (path));
    strcpy(path,"/");
    strcpy(path,tfname);
    struct stat status;
    stat(path, &status );
    if((status.st_mode & S_IFDIR)){
        printf("Invalid file: Can't send a dir:[%s]!\n", tfname);
        struct MsgHeader msg;
        msg.msgID = MSG_OPENFILE_FAILD;

        if (send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)   // send failed
        {
            printf("Send failed: %d\n", GET_ERROR);
        }
        return false;
    }

    // get the file size
    fseek(pread, 0, SEEK_END);
    g_fileSize = ftell(pread);

    if(g_fileSize == 0) {
        printf("readFile: Can't send empty file!\n");
        fclose(pread);
        // send failed msg
        struct MsgHeader failMsg;
        failMsg.msgID = MSG_EMPTYFILE;
        if (send(clifd, (char*)&failMsg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
            printf("readFile: Send to client failed!\n");
        return false;
    }
    fseek(pread, 0, SEEK_SET);

    // send the file size to client
    struct MsgHeader msg;
    msg.msgID = MSG_FILESIZE;
    msg.myUnion.fileInfo.fileSize = g_fileSize;

    // send filename and file size to client
    strcpy(msg.myUnion.fileInfo.fileName, tfname);

    // alloc mem
    g_fileBuf = calloc(g_fileSize + 1, sizeof(char));
    if (g_fileBuf == NULL)
    {
        printf("No memory, please retry\n");
        return false;
    }

    // reset the mem
    fread(g_fileBuf, sizeof(char), g_fileSize, pread);
    g_fileBuf[g_fileSize] = '\0';
    fclose(pread);

    // send filesize to client
    if(send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) {
        printf("message send error: %d\n", GET_ERROR);
        return false;
    }             // send filename, suffix and file size to client, first send

    return true;
}

void sendMessage(SOCKET clifd, char* message) {
    struct MsgHeader msg;

    msg.msgID = MSG_RECV;
    strcpy(msg.myUnion.fileInfo.fileName, message);

    if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
    {
        printf("message send error: %d\n", GET_ERROR);
        return;
    }
}

bool sendFile(SOCKET clifd, struct MsgHeader* pms){

    // create data socket, send port back
    struct MsgHeader msg;                                                     // tell the client ready to recv file
    msg.msgID = MSG_READY_READ;
    // create data socket and listen to it
    SOCKET dataSock;
    while((dataSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET){
    #ifdef _WIN32
            WSACleanup();
                    Sleep(1000);
    #else
            sleep(1);
    #endif
        }

    // bind socket with IP addr and port
    struct sockaddr_in dataAddr;
    dataAddr.sin_family = AF_INET;
    #ifdef _WIN32
        dataAddr.sin_addr.S_un.S_addr = ADDR_ANY;     // listen to all network card of the pc
    #else
        dataAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    #endif
        dataAddr.sin_port = htons(0);             // htons set local byte sequence to network sequence

    printf("Now ADDR:%d:%d\n",dataAddr.sin_addr,dataAddr.sin_port);

    struct sockaddr_in bindAddr;
    socklen_t address_len = sizeof(bindAddr);

    if (bind(dataSock, (struct sockaddr*)&dataAddr, sizeof(dataAddr)) != 0)
    {
        printf("serverReady: Bind socket with IP addr and port failed:%d\n", GET_ERROR);
        getsockname(dataSock,(struct sockaddr*)&bindAddr,&address_len);
        printf("Binded server address = %s:%d\n", inet_ntoa(bindAddr.sin_addr), ntohs(bindAddr.sin_port));
        return false;
    }
    getsockname(dataSock,(struct sockaddr*)&bindAddr,&address_len);
    printf("Binded server address = %s:%d\n", inet_ntoa(bindAddr.sin_addr), ntohs(bindAddr.sin_port));
    msg.port = htons(bindAddr.sin_port);
    // send data port number
    if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)   // send the second time
    {
        printf("serverReady: Send to client error: %d\n", GET_ERROR);
        return false;
    }

    printf("Listening data port...\n");
    // listen to client connection
    if (listen(dataSock, 10) != 0)
    {
        printf("serverReady: Listen failed:%d\n", GET_ERROR);
        return false;
    }

    // client connect to server, start sending
    struct sockaddr_in cliAddr;
    int len = sizeof(cliAddr);

    SOCKET dataSocket = accept(dataSock, (struct sockaddr*)&cliAddr, &len);
    if (dataSocket == INVALID_SOCKET)
    {
        printf("Accept failed:%d\n", GET_ERROR);
        return false;
    }
    printf("Sending data connection succeed!\n");

    // if the total size of the whole file is larger than a packet size, dispatch

    for (size_t i = 0; i < g_fileSize; i += PACKET_SIZE)                       // PACKET_SIZE = 1012
    {
        struct MsgHeader dataMsg;
        dataMsg.myUnion.packet.nStart = i;

        // the reset size of the sending file is smaller than a packet size
        if (i + PACKET_SIZE + 1 > g_fileSize)
        {
            dataMsg.myUnion.packet.nsize = g_fileSize - i;
        }
        // the reset size of the sending file is still larger than a packet size
        else{
            dataMsg.myUnion.packet.nsize = PACKET_SIZE;
        }

        // copy data from local cache buffer to packet
        memcpy(dataMsg.myUnion.packet.buf, g_fileBuf + dataMsg.myUnion.packet.nStart, dataMsg.myUnion.packet.nsize);

        if (send(dataSocket, (char*)&dataMsg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)  // send packet to client
        {
            printf("Send file failed: %d\n", GET_ERROR);
        }
    }

    CLOSE(dataSocket);
    printf("Finished sending file.\n");
    return true;
}

void serverReady(SOCKET clifd, struct MsgHeader* pmsg){
    g_fileSize = pmsg->myUnion.fileInfo.fileSize;
    char text[MAXSUFFIX];
    char tfname[MAXSTRING] ;

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
        // create data socket and listen to it
        SOCKET dataSock;
        while((dataSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET){
        #ifdef _WIN32
                WSACleanup();
                Sleep(1000);
        #else
                sleep(1);
        #endif
        }

        // bind socket with IP addr and port
        struct sockaddr_in dataAddr;
        dataAddr.sin_family = AF_INET;
        #ifdef _WIN32
            dataAddr.sin_addr.S_un.S_addr = ADDR_ANY;     // listen to all network card of the pc
        #else
            dataAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        #endif
        dataAddr.sin_port = htons(0);             // htons set local byte sequence to network sequence

        printf("Now ADDR:%d:%d\n",dataAddr.sin_addr,dataAddr.sin_port);

        struct sockaddr_in bindAddr;
        socklen_t address_len = sizeof(bindAddr);

        if (bind(dataSock, (struct sockaddr*)&dataAddr, sizeof(dataAddr)) != 0)
        {
            printf("serverReady: Bind socket with IP addr and port failed:%d\n", GET_ERROR);
            getsockname(dataSock,(struct sockaddr*)&bindAddr,&address_len);
            printf("Binded server address = %s:%d\n", inet_ntoa(bindAddr.sin_addr), ntohs(bindAddr.sin_port));
            return;
        }
        getsockname(dataSock,(struct sockaddr*)&bindAddr,&address_len);
        printf("Binded server address = %s:%d\n", inet_ntoa(bindAddr.sin_addr), ntohs(bindAddr.sin_port));

        struct MsgHeader msg;
        msg.msgID = MSG_SERVERREAD;
        msg.port = htons(bindAddr.sin_port);
        // send data port number
        if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)   // send the second time
        {
            printf("serverReady: Send to client error: %d\n", GET_ERROR);
            return;
        }

        printf("Filename:%s\tSize:%ld\t\n", tfname, pmsg->myUnion.fileInfo.fileSize);

        printf("Listening data port...\n");
        // listen to client connection
        if (listen(dataSock, 10) != 0)
        {
            printf("serverReady: Listen failed:%d\n", GET_ERROR);
            return;
        }

        // client connection, recv it
        struct sockaddr_in cliAddr;
        int len = sizeof(cliAddr);

        SOCKET dataSocket = accept(dataSock, (struct sockaddr*)&cliAddr, &len);
        if (dataSocket == INVALID_SOCKET)
        {
            printf("Accept failed:%d\n", GET_ERROR);
            return;
        }
        printf("Receiving data connection succeed!\n");

        while(true){
            struct MsgHeader* dataMsg;
            // if recv succeed, return the bytes of the msg, else return 0
            int nRes = recv(dataSocket, g_recvBuf, 1024, 0);

            if (nRes <= 0)
            {
                printf("serverReady: recv failed...%d\n", GET_ERROR);
                struct MsgHeader failMsg;
                failMsg.msgID = MSG_RECVFAILED;
                // send back to client
                if (send(clifd, (const char *)&failMsg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR)
                    printf("serverReady: Send to client MSG_RECVFAILED error: %d\n", GET_ERROR);
                break;
            }
            // get the recved msg
            dataMsg = (struct MsgHeader*)g_recvBuf;
            if(writeFile(clifd,dataMsg)) break;
        }
        CLOSE(dataSocket);
    }
}

bool writeFile(SOCKET clifd, struct MsgHeader* pmsg){
    if (g_fileBuf == NULL)
    {
        return false;
    }

    int nStart = pmsg->myUnion.packet.nStart;
    int nsize = pmsg->myUnion.packet.nsize;

    memcpy(g_fileBuf + nStart, pmsg->myUnion.packet.buf, nsize);    // the same as strncmpy
    long currsize;
    if(nStart + nsize >= g_fileSize){
        currsize = g_fileSize;
    }
    else{
        currsize = nStart + nsize;
    }

    printf("Receiving: %.2f%%\r", ((currsize)/(double)g_fileSize)*100);
    fflush(stdout);


    if (nStart + nsize >= g_fileSize)                       // check if the data sending is complete
    {
        FILE* pwrite;
        struct MsgHeader msg;

        pwrite = fopen(g_fileName, "wb");
        msg.msgID = MSG_SUCCESSED;
        printf("Receiving: 100.00%%\n");

        if (pwrite == NULL)
        {
            printf("Write file error...\n");
            return false;
        }

        fwrite(g_fileBuf, sizeof(char), g_fileSize, pwrite);
        fclose(pwrite);
        printf("Successfully received file: %s\n",g_fileName);

        free(g_fileBuf);
        g_fileBuf = NULL;

        send(clifd, (char*)&msg, sizeof(struct MsgHeader), 0);

        return true;
    }
    return false;
}

bool deletefile(SOCKET clifd, struct MsgHeader* pmsg){

    struct MsgHeader msg;
    // get the filename to delete
    char suffix[MAXSUFFIX];
    char deleteFilename[MAXSTRING];
    _splitpath(pmsg->myUnion.fileInfo.fileName, NULL, NULL, deleteFilename, suffix);
    strcat(deleteFilename, suffix);

    // find if the file exist
    if(access(deleteFilename, F_OK) != 0){
        // file doesn't exist, send back failed msg.
        msg.msgID = MSG_NOFILE;
        // send back to client
        if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) printf("deletefile: Send to client MSG_NOFILE error: %d\n", GET_ERROR);
        else printf("deletefile: No file.\n");
        return false;
    }

    // find file, delete it
    if(remove(deleteFilename) != 0 && rmdir(deleteFilename)){
        // deletion failed
        msg.msgID = MSG_DELETIONFAILED;
        // send back to client
        if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) printf("deletefile: Send to client MSG_DELETIONFAILED error: %d\n", GET_ERROR);
        else printf("deletefile: MSG_DELETIONFAILED, %d.\n",errno);
        return false;
    }

    // deletion succeed
    msg.msgID = MSG_SUCCESSED;
    if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) printf("deletefile: Send to client MSG_SUCCESSED error: %d\n", GET_ERROR);
    else printf("deletefile: SUCCESS!\n");
    return true;
}

bool makeDirectory(SOCKET clifd, struct MsgHeader* pmsg){
    struct MsgHeader msg;
    char directoryName[256];
    strcpy(directoryName, pmsg->myUnion.directoryInfo.directoryName);
    // find if the directory exist
        if(access(directoryName, F_OK) == -1){//not exist
            int res;
            #if defined(_WIN32)
                res = _mkdir(directoryName);
            #else
                res = mkdir(directoryName, 0777); // notice that 777 is different than 0777
            #endif
            if(res == -1){
                msg.msgID =MSG_NULLNAME;
                if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) printf("mkdir: Send to client MSG_NULLNAME error: %d\n", GET_ERROR);
                else printf("mkdir: make an empty name directory!\n");
                return false;
            }
        msg.msgID = MSG_SUCCESSED;
        if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) printf("mkdir: Send to client MSG_SUCCESSED error: %d\n", GET_ERROR);
        else printf("mkdir: SUCCESS!\n");
        return true;
    }
    else{//exist
        msg.msgID =MSG_SAMEDIR;
        if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) printf("mkdir: Send to client MSG_SAMEDIR error: %d\n", GET_ERROR);
        else printf("mkdir: MSG_SAMEDIR, %d.\n",errno);
        return false;
    }
}

bool chDirectory(SOCKET clifd, struct MsgHeader* pmsg){
    struct MsgHeader msg;
    char path[256];
    strcpy(path, pmsg->myUnion.directoryInfo.directoryName);
    //
    if (!strcmp(path, "..")) {//parent directory
        chdir("..");
        msg.msgID = MSG_SUCCESSED;
        if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) printf("cd(parent): Send to client MSG_SUCCESSED error: %d\n", GET_ERROR);
        else printf("cd(parent): SUCCESS!\n");
        return true;
    }
    else {
        if(chdir(path)==0){//success
            msg.msgID = MSG_SUCCESSED;
            if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) printf("cd: Send to client MSG_SUCCESSED error: %d\n", GET_ERROR);
            else printf("cd: SUCCESS!\n");
            return true;
        }
        else{//fail
            msg.msgID =MSG_CDFAILED;
            if (send(clifd, (const char *)&msg, sizeof(struct MsgHeader), 0) == SOCKET_ERROR) printf("cd: Send to client MSG_CDFAILED error: %d\n", GET_ERROR);
            else printf("cd: MSG_CDFAILED, %d.\n",errno);
            return false;
        }
    }
}