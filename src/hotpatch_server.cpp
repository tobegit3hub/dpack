
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h> 
#include <string>
#include <sys/types.h>
#include <iostream>
#include <thread>
#include <gflags/gflags.h>
#include <string>
#include <dlfcn.h>
#include <memory>
#include <cassert>
#include <glog/logging.h>

#include "hotpatch_server.h"

using google::INFO;

namespace hotpatch {

// The global object
HotpatchServer* global_hotpatch_server_ = NULL;

HotpatchServer* GetHotpatchServer() {
  // TODO: Use lock to avoid conflict
  if (!global_hotpatch_server_) {
    global_hotpatch_server_ = new HotpatchServer;
  }
  return global_hotpatch_server_;
}

void InitHotpatchServer() {
  GetHotpatchServer()->Init();
} 

void ShutdownHotpatchServer() {
    delete global_hotpatch_server_;
    global_hotpatch_server_ = NULL;
}

void RegisterVariable(std::string key, void *p_value) {
    auto hp = GetHotpatchServer();
    hp->RegisterVariable(key, p_value);
}


void RegisterFunction(std::string name, void* p_func) {
    auto hp = GetHotpatchServer();
    hp->RegisterFunction(name, p_func);
}

// Constructor
HotpatchServer::HotpatchServer() {
    hotpatch_command = std::make_shared<HotpatchCommand>(registered_variables, registered_libraries, registered_dl_handlers, registered_functions);
}

HotpatchServer::~HotpatchServer() {
    // Release resources
    Close();
}

void start_socket_server(HotpatchServer* p_hotpatch_server) {

    const int buffer_size = 128;
    const int client_size = 8;

    // Get pid to set socket file
    pid_t pid = getpid();
    std::string socket_path = "/tmp/" + std::to_string(pid) + ".socket";
    std::cout << "Unix socket path: " << socket_path << std::endl;

    struct sockaddr_un serun;
    int listenfd, size;

    if ((listenfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        exit(1);
    }
 
    memset(&serun, 0, sizeof(serun));
    serun.sun_family = AF_UNIX;
    strcpy(serun.sun_path, socket_path.c_str());
    size = offsetof(struct sockaddr_un, sun_path) + strlen(serun.sun_path);
    unlink(socket_path.c_str());

    if (::bind(listenfd, (struct sockaddr *)&serun, size) < 0) {
        perror("bind error");
        exit(1);
    }

    // Start unix socket server    
    if (listen(listenfd, client_size) < 0) {
        perror("listen error");
        exit(1);        
    }

    struct sockaddr_un cliun;
    socklen_t cliun_len = sizeof(cliun);
    int connfd;
    while(p_hotpatch_server->GetShouldStop() == false) {
        // TODO: Use async API to close the socket
        // Accept socket client
        if ((connfd = accept(listenfd, (struct sockaddr *)&cliun, &cliun_len)) < 0){
            perror("accept error");
            continue;
        }

        // TODO: Pass the flag to stop waiting clients
        while(1) {
            char read_buf[buffer_size];
            int readSize = read(connfd, read_buf, sizeof(read_buf));
            if (readSize < 0) {
                perror("read error");
                break;
            } else if(readSize == 0) {
                printf("EOF\n");
                break;
            }

            // TODO: Create new buffer to load actual command
            char new_read_buf[readSize];
            memcpy(new_read_buf, read_buf, readSize);

            // Handle command
            p_hotpatch_server->GetHotpathCommand()->ParseCommand(std::string(new_read_buf));

            // Output result
            std::string sendResult = "Success to run";
            write(connfd, sendResult.c_str(), sendResult.size());
        }
        close(connfd);
    }
    close(listenfd);
}

void HotpatchServer::Init() {
    socket_server_thread = std::thread(&start_socket_server, this);
}

void HotpatchServer::Close() {
    cout << "Close the Hotpatch server" << endl;

    // Close the opened dynamic libraries handlers
    for(std::map<std::string, void*>::iterator it = registered_dl_handlers.begin(); it != registered_dl_handlers.end(); ++it) {
        cout << "Close dynamic library: " << it->first << endl;
        if (it->second != NULL) {
            dlclose(it->second);
        }
    }

    SetShouldStop(true);

    socket_server_thread.detach();
    //socket_server_thread.join();
}

void HotpatchServer::RegisterVariable(std::string key, void *p_value) {
    // TODO: Make share to release smart pointers
    registered_variables[key] = p_value;
}

void HotpatchServer::RegisterFunction(std::string func_name, void* p_func) {
    registered_functions[func_name] = p_func;
}

bool HotpatchServer::GetShouldStop() {
    return should_stop;
}

void HotpatchServer::SetShouldStop(bool stop) {
    should_stop = stop;
}

std::shared_ptr<HotpatchCommand> HotpatchServer::GetHotpathCommand() {
    return hotpatch_command;
}

} // End of namespace