#include<iostream>
#include<vector>
#include<string>
#include<sstream> // istringstream
#include<cstring>
#include<map>
#include<unordered_map>
#include<algorithm>
#include<fstream> // test read file
#include<stdlib.h>
#include<unistd.h> // STD pipe
#include<sys/wait.h> // waitpid
#include<fcntl.h> // open
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/types.h> 
#include<sys/socket.h>
#include<sys/stat.h>
#include<sys/ipc.h> // mkfifo
#include<sys/shm.h>
using namespace std;

#define MAX_CLIENT_USER 30
#define MAX_CLIENT_INPUTSIZE 15000
#define MAX_CLIENT_MESSAGE 1025
#define MAX_CLIENT_NAME 30
#define MAX_FILE_LENGTH 30

/* using struct to record current cmds, fdin and fdout */
struct cmds_allinfo{
    vector<string> cmds;
    int fdin;
    int fdout;
    int fderr;
    bool dopipe; // used to check current command is pipe or not
    bool endofcmds; // if true and dopipe is false, it need to wait to output entirely
    bool isordpipe; // check the pipe is ordinary or not
    cmds_allinfo(){
        cmds.clear();
        fdin = STDIN_FILENO;
        fdout = STDOUT_FILENO;
        fderr = STDERR_FILENO;
        dopipe = false;
        endofcmds = false;
        isordpipe = false;
    }
};

/* FIFO */
struct Fifo{
    char file_name[MAX_FILE_LENGTH];
    int file_in;
    int file_out;
    bool file_exist;
    Fifo(){
        memset(file_name, '\0', MAX_FILE_LENGTH);
        file_in = -1;
        file_out = -1;
        file_exist = false;
    }
};

/* client's information structure */
struct client_information{
    int client_id;
    int client_pid;
    int client_port;
    bool client_exist;
    char client_name[MAX_CLIENT_NAME];
    char client_ip[INET_ADDRSTRLEN];
    Fifo client_fifo[MAX_CLIENT_USER];
    client_information(){
        client_id = -1;
        client_pid = -1;
        client_port = -1;
        client_exist = false;
        memset(client_name, '\0', MAX_CLIENT_NAME);
        memset(client_ip, '\0', INET_ADDRSTRLEN);
    }
};

////////////////////     global variable     ////////////////////

/* server global variable */
int shm_clientInfo_global;
int shm_clientMsg_global;
/* record client's ID */
int client_id_global;

/* store pipe's file descriptor */
map<int,vector<int>> _pipe;

////////////////////     shell function     ////////////////////

/* pipe's function */
int get_pipe_num(string);
int make_pipe_in(int); // get pipe read's file descriptor
int make_pipe_out(int); // get pipe write's file descriptor
void close_decrease_pipe(bool); // close 0 and decrease others after number pipe and increase after ordinary pipe
void part_cmds(vector<string>);
void make_pipe(cmds_allinfo&);
void exec_cmds(cmds_allinfo);
void str2char(vector<string>, char**);
vector<string> split_inputCmds(string);

////////////////////     server function      ////////////////////

int setServerTCP(int);
int getClientID();
/* shell main function */
void shellMain(int);
void setClientInfo(int, int, struct sockaddr_in);
void eraselogoutfifo(int);
void setShareMM();
void server_signal_handler(int);
/* broadcast(structure of current client information, type, message, broadcast_id) */
void broadcast(int, string, string, int);

////////////////////     user pipe function     ////////////////////

////////////////////     built-in function     ////////////////////
void _setenv(string,string);
void _printenv(string);
// void _who(void);
// void _tell(vector<string>);
// void _yell(vector<string>);
void _name(vector<string>);
void welcomemsg();

////////////////////     main function     ////////////////////

int main(int argc, char* argv[]){
    int msock, ssock;
    if(argc != 2){
        cerr<<"input error: ./[program name] [port]"<<endl;
        exit(1);
    }

    /* server signal */
    signal(SIGCHLD, server_signal_handler);
    signal(SIGINT, server_signal_handler);

    /* create TCP server */
    int s_port = atoi(argv[1]);
    msock = setServerTCP(s_port);

    setenv("PATH", "bin:.", 1);
    setShareMM();

    /* client socket address */
    struct sockaddr_in _cin;
    while(1){

        /* server listen whether client need to connect */
        socklen_t _cinlen = sizeof(_cin);
        ssock = accept(msock, (struct sockaddr*) &_cin, &_cinlen);
        if(ssock < 0){
            cerr<<"accept Client Fail ! (main)"<<endl;
            continue;
        }

        /* fork to another process */
        pid_t _pid;
        _pid = fork();
        if(_pid == 0){
            dup2(ssock, STDIN_FILENO);
            dup2(ssock, STDOUT_FILENO);
            dup2(ssock, STDERR_FILENO);
            close(msock);

            int _client_id;
            if((_client_id = getClientID()) == -1){
                continue;
            }

            // cout<<_client_id<<endl;
            setClientInfo(ssock, _client_id, _cin);
            shellMain(_client_id);
            
            /* client exit */
            eraselogoutfifo(_client_id);
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            exit(0);
        }
        else{
            close(ssock);
        }
    }
    close(msock);
    return 0;
}

////////////////////     server function code     ///////////////////

/* set TCP server */
int setServerTCP(int port){
    int msock = 0;

    /* SOCK_STREAM -> TCP */
    if((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        cerr<<"Create TCP Server fault ! (setServerTCP)"<<endl;
        return 0;
    }

    /* set socker -> setsocketopt, allow different ip to use same port */
    const int opt = 1;
    if(setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        cerr<<"Set Socket With setsockopt fault ! (setServerTCP)"<<endl;
        return 0;
    }

    /* initialize sockaddr_in */
    struct sockaddr_in _sin;
    bzero(&_sin, sizeof(_sin));
    _sin.sin_family = AF_INET;
    _sin.sin_port = htons(port);
    _sin.sin_addr.s_addr = htonl(INADDR_ANY);

    /* bind socket */
    if(bind(msock, (sockaddr*) &_sin, sizeof(_sin)) == -1){
        cerr<<"Bind Server Socket fault ! (setServerTCP)"<<endl;
        return 0;
    }

    /* listen */
    listen(msock, 0);
    return msock;
}

/* get feasible client ID */
int getClientID(){
    client_information* shm_clientInfo_tmp;
    if((shm_clientInfo_tmp = (client_information*)shmat(shm_clientInfo_global, NULL, 0)) == (client_information*)-1){
        cerr<<"get client ID fail ! (getClientID)"<<endl;
        exit(1);
    }
    for(int i=0; i<MAX_CLIENT_USER; i++){
        if(shm_clientInfo_tmp[i].client_exist == false){
            shm_clientInfo_tmp[i].client_exist = true;
            shmdt(shm_clientInfo_tmp);
            return i+1;
        }
    }
    shmdt(shm_clientInfo_tmp);
    return -1;
}

/* close log out fifo fd */
void eraselogoutfifo(int __client_id){
    client_information* shm_clientInfo_tmp;
    if((shm_clientInfo_tmp = (client_information*)shmat(shm_clientInfo_global, NULL, 0)) == (client_information*)-1){
        cerr<<"set client's information fail ! (eraselogoutfifo)"<<endl;
        return;
    }
    shm_clientInfo_tmp[__client_id-1].client_exist = false;
    shm_clientInfo_tmp[__client_id-1].client_id = -1;
    shm_clientInfo_tmp[__client_id-1].client_pid = -1;
    shm_clientInfo_tmp[__client_id-1].client_port = -1;
    memset(shm_clientInfo_tmp[__client_id-1].client_name, '\0', MAX_CLIENT_NAME);
    memset(shm_clientInfo_tmp[__client_id-1].client_ip, '\0', INET_ADDRSTRLEN);
    for(int i=0; i<MAX_CLIENT_USER; i++){
        shm_clientInfo_tmp[__client_id].client_fifo[i].file_exist = false;
    }
    for(int i=0; i<MAX_CLIENT_USER; i++){
        if(shm_clientInfo_tmp[i].client_fifo[__client_id-1].file_exist == true){
            /* need to read all file out */
            char _buf[MAX_CLIENT_MESSAGE];
            while(recv(shm_clientInfo_tmp[i].client_fifo[__client_id-1].file_in, &_buf, sizeof(_buf), 0) > 0){
                /* this is the most important part to clear data in share memory. */
            }
            shm_clientInfo_tmp[i].client_fifo[__client_id-1].file_exist = false;
            shm_clientInfo_tmp[i].client_fifo[__client_id-1].file_in = -1;
            shm_clientInfo_tmp[i].client_fifo[__client_id-1].file_out = -1;
            remove(shm_clientInfo_tmp[i].client_fifo[__client_id-1].file_name);
            memset(shm_clientInfo_tmp[i].client_fifo[__client_id-1].file_name, '\0', MAX_FILE_LENGTH);
        }
    }
    shmdt(shm_clientInfo_tmp);
    return;
}

/* set new client's information */
void setClientInfo(int _ssock, int __client_id, struct sockaddr_in __cin){
    client_information* shm_clientInfo_tmp;
    if((shm_clientInfo_tmp = (client_information*)shmat(shm_clientInfo_global, NULL, 0)) == (client_information*)-1){
        cerr<<"set client's information fail ! (setClientInfo)"<<endl;
        return;
    }
    shm_clientInfo_tmp[__client_id-1].client_exist = true;
    shm_clientInfo_tmp[__client_id-1].client_id = __client_id;
    shm_clientInfo_tmp[__client_id-1].client_pid = getpid();
    shm_clientInfo_tmp[__client_id-1].client_port = ntohs(__cin.sin_port);
    strncpy(shm_clientInfo_tmp[__client_id-1].client_ip, inet_ntoa(__cin.sin_addr), INET_ADDRSTRLEN);
    string name = "(no name)";
    strncpy(shm_clientInfo_tmp[__client_id-1].client_name, name.c_str(), MAX_CLIENT_NAME);
    // cout<<shm_clientInfo_tmp[__client_id-1].client_name<<" "<<strlen(shm_clientInfo_tmp[__client_id-1].client_name)<<endl;
    // cout<<shm_clientInfo_tmp[__client_id-1].client_id<<endl;
    // cout<<shm_clientInfo_tmp[__client_id-1].client_pid<<endl;
    // cout<<shm_clientInfo_tmp[__client_id-1].client_exist<<endl;
    // cout<<shm_clientInfo_tmp[__client_id-1].client_ip<<endl;
    // cout<<shm_clientInfo_tmp[__client_id-1].client_port<<endl;
    shmdt(shm_clientInfo_tmp);
    return;
}

/* initialize the share memory */
void setShareMM(){
    int shm_clientInfo;
    int shm_clientMsg;
    key_t key_clientInfo = 10000;
    key_t key_clientMsg = 10001;;
    /* construct share memory */
    if((shm_clientInfo = shmget(key_clientInfo, sizeof(client_information) * MAX_CLIENT_USER, IPC_CREAT | 0666)) < 0){
        cerr<<"construct client_information's share memory fail ! (setShareMM)"<<endl;
        return;
    }
    if((shm_clientMsg = shmget(key_clientMsg, sizeof(char) * MAX_CLIENT_MESSAGE, IPC_CREAT | 0666)) < 0){
        cerr<<"construct client_message's share memory fail ! (setShareMM)"<<endl;
        return;
    }
    /* record the share memory's id */
    shm_clientInfo_global = shm_clientInfo;
    shm_clientMsg_global = shm_clientMsg;
    // cout<<shm_clientInfo_global<<endl;
    // cout<<shm_clientMsg_global<<endl;

    /* INIT */
    client_information *shm_clientInfo_tmp;
    if((shm_clientInfo_tmp = (client_information*)shmat(shm_clientInfo_global, NULL, 0)) == (client_information*)-1){
        cerr<<"match client_information's share memory start fail ! (setShareMM)"<<endl;
        return;
    }
    for(int i=0; i<MAX_CLIENT_USER; i++){
        shm_clientInfo_tmp[i].client_exist = false;
        for(int j=0; j<MAX_CLIENT_USER; j++){
            shm_clientInfo_tmp[i].client_fifo[j].file_exist = false;
        }
    }
    shmdt(shm_clientInfo_tmp);
    return;
}

/* broadcast */
void broadcast(int _client_id, string _type, string _msg, int _target_id){
    string broadcast_msg = "";
    client_information* shm_clientInfo_tmp;
    if((shm_clientInfo_tmp = (client_information*)shmat(shm_clientInfo_global, NULL, 0)) == (client_information*)-1){
        cerr<<"match client_information's share memory start fail ! (broadcast)"<<endl;
        return;
    }
    char* shm_clientmsg_tmp;
    if((shm_clientmsg_tmp = (char*)shmat(shm_clientMsg_global, NULL, 0)) == (char*)-1){
        cerr<<"match client_message's share memory start fail ! (broadcast)"<<endl;
        return;
    }

    if(_type == "log-in"){
        broadcast_msg = ("*** User '" + string(shm_clientInfo_tmp[_client_id-1].client_name) + "' entered from " + string(shm_clientInfo_tmp[_client_id-1].client_ip) + ":" + to_string(shm_clientInfo_tmp[_client_id-1].client_port) + ". ***");
        // cout<<broadcast_msg<<endl;
    }
    if(_type == "name"){
        broadcast_msg = _msg;
    }

    memset(shm_clientmsg_tmp, '\0', MAX_CLIENT_MESSAGE);
    strncpy(shm_clientmsg_tmp, broadcast_msg.c_str(), MAX_CLIENT_MESSAGE);
    for(int i=0; i<MAX_CLIENT_USER; i++){
        if(shm_clientInfo_tmp[i].client_exist == true){
            kill(shm_clientInfo_tmp[i].client_pid, SIGUSR1);
        }
    }

    shmdt(shm_clientInfo_tmp);
    shmdt(shm_clientmsg_tmp);
    return;
}

/* server signal handler */
void server_signal_handler(int sig){
    if(sig == SIGCHLD){
        int status;
        while(waitpid(-1, &status, WNOHANG) > 0){
            /* 
                wait for exist exiting child process
                (-1 means every child process) until 
                no zombie child process.
                if receive a zombie child process,
                it will return pid number, else return -1 
            */
        };
    }
    else if(sig == SIGINT){
        /* server exit, remove the share memory */
        shmctl(shm_clientInfo_global, IPC_RMID, NULL);
        shmctl(shm_clientMsg_global, IPC_RMID, NULL);
        cout<<endl;
        exit(0);
    }
    else if(sig == SIGUSR1){
        char* shm_clientMsg_tmp;
        if((shm_clientMsg_tmp = (char*)shmat(shm_clientMsg_global, NULL, 0)) == (char*)-1){
            cerr<<"match client_message's share memory start fail ! (SIGUSR!)"<<endl;
            return;
        }
        cout<<shm_clientMsg_tmp<<endl;
        shmdt(shm_clientMsg_tmp);
    }
    return;
}

////////////////////     user pipe function code     ////////////////////

////////////////////     built-in function code     ////////////////////

/* welcome message */
void welcomemsg(){
    string res = "";
    res += "****************************************\n";
    res += "** Welcome to the information server. **\n";
    res += "****************************************";
    cout<<res<<endl;
    return;
}

void _setenv(string name, string value){
    /* setenv(char*, char*, int) */
    setenv(name.c_str(), value.c_str(), 1);
    /* renew the total path */
    // _path.clear();
    // _path = split_inputPath(value);
    // for(auto it:_path){
    //     cout<<it<<" ";
    // }
    // cout<<endl;
}

void _printenv(string name){
    char* _name;
    if((_name = getenv(name.c_str())) == NULL){
        return;
    }
    cout<<_name<<endl;
}

void _name(vector<string> cmds){
    string name = "";
    string name_msg = "";
    for(int i=1; i<cmds.size(); i++){
        name += cmds[i];
        if(i != cmds.size()-1){
            name += " ";
        }
    }
    client_information* shm_clientInfo_tmp;
    if((shm_clientInfo_tmp = (client_information*)shmat(shm_clientInfo_global, NULL, 0)) == (client_information*)-1){
        cerr<<"match client_information's share memory start fail ! (_name)"<<endl;
        exit(1);
        return;
    }
    for(int i=0; i<MAX_CLIENT_USER; i++){
        if(shm_clientInfo_tmp[i].client_name == name){
            name_msg = ("*** User '" + name + "' already exists. ***");
            cout<<name_msg<<endl;
            return;
        }
    }
    strncpy(shm_clientInfo_tmp[client_id_global-1].client_name, name.c_str(), MAX_CLIENT_NAME);
    name_msg = ("*** User from " + string(shm_clientInfo_tmp[client_id_global-1].client_ip) + ":" + to_string(shm_clientInfo_tmp[client_id_global-1].client_port) + " is named '" + shm_clientInfo_tmp[client_id_global-1].client_name + "'. ***");
    shmdt(shm_clientInfo_tmp);
    broadcast(client_id_global, "name", name_msg, -1);
    return;
}

////////////////////     shell function code     ////////////////////

/* shell's main function */
void shellMain(int _id){
    
    /* call client signals */
    /* share memory of MESSAGE */
    signal(SIGUSR1, server_signal_handler);

    welcomemsg();
    /* broadcast log in function */
    /*
        bug: cannot use broadcast before signal, it will cause 
        kernel cannot catch the proper handler, then kill the process.
        [ bug fixxed ]
    */
    broadcast(_id, "log-in", "", -1);

    /* initialize some global variables */
    clearenv();
    setenv("PATH", "bin:.", 1);
    _pipe.clear();
    /* record client's ID */
    client_id_global = _id;
    string input_cmd;

    while(1){
        cout<<"% ";
        getline(cin,input_cmd);
        if(cin.eof()){
            cout<<endl;
            break;
        }
        if(input_cmd.size() == 0){
            continue;
        }
        vector<string> cmds = split_inputCmds(input_cmd);
        if(cmds[0] == "exit"){
            return;
        }
        part_cmds(cmds);
    }
    return;
}

/* part of the commands to exec */
void part_cmds(vector<string> cmds){
    int _size = cmds.size();
    /* current index of all cmds */
    int _cur = 0; 
    /* struct cmds_allinfo to store part cmds's informations */
    cmds_allinfo cmds_info;
    // cout<<cmds_info.cmds.size()<<" "<<cmds_info.fdin<<" "<<cmds_info.fdout<<endl;
    while(_cur < _size){
        if(cmds_info.cmds.size() == 0){
            cmds_info.cmds.push_back(cmds[_cur++]);
            /* check the last cmds, need to make pipe */
            if(_cur >= _size){
                cmds_info.endofcmds = true;
                cmds_info.dopipe = false;
                cmds_info.isordpipe = false;
                make_pipe(cmds_info);
            }
            continue;
        }
        if(cmds[_cur][0] == '|' || cmds[_cur][0] == '!'){
            int pipe_num;
            pipe_num = get_pipe_num(cmds[_cur]);
            // cout<<pipe_num<<endl;
            /* construct stdout goal pipe (write) */
            cmds_info.fdout = make_pipe_out(pipe_num);
            /* connect cmds_info.fderr to cmds_info.fdout */
            cmds_info.fderr = cmds[_cur][0] == '!' ? cmds_info.fdout : cmds_info.fderr;
            cmds_info.isordpipe = pipe_num == -1 ? true : false;
            cmds_info.dopipe = true;
            _cur++;
            if(_cur >= _size){
                cmds_info.endofcmds = true;
            }
            make_pipe(cmds_info);
        }
        else if(cmds[_cur] == ">"){
            string file_name = cmds[++_cur];
            _cur++;
            int pipe_out_to_file;
            if((pipe_out_to_file = open(file_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)) < 0){
                perror("open or create file fault !");
                exit(1);
            }
            cmds_info.fdout = pipe_out_to_file;
            cmds_info.dopipe = false;
            cmds_info.isordpipe = false;
            if(_cur >= _size){
                cmds_info.endofcmds = true;
            }
            make_pipe(cmds_info);
        }
        else{
            cmds_info.cmds.push_back(cmds[_cur++]);
            /* check the last cmds, need to make pipe */
            if(_cur >= _size){
                cmds_info.endofcmds = true;
                cmds_info.dopipe = false;
                cmds_info.isordpipe = false;
                make_pipe(cmds_info);
            }
        }
    }
}

void make_pipe(cmds_allinfo &ccmds_info){
    ccmds_info.fdin = make_pipe_in(ccmds_info.fdin);
    exec_cmds(ccmds_info);
    /* decrease , increase and close pipe number after exec each time (different between ordinary and number pipe) */
    close_decrease_pipe(ccmds_info.isordpipe);
    // if(ccmds_info.isordpipe == false){
    //     close_decrease_pipe();
    // }
    /* reset the struct of part cmds */
    ccmds_info.cmds.clear();
    ccmds_info.fdin = STDIN_FILENO;
    ccmds_info.fdout = STDOUT_FILENO;
    ccmds_info.fderr = STDERR_FILENO;
    ccmds_info.dopipe = false;
    ccmds_info.endofcmds = false;
    ccmds_info.isordpipe = false;
}

void exec_cmds(cmds_allinfo ccmds_info){

    /* check built-in commands - setenv, printenv & exit */
    if(ccmds_info.cmds[0] == "exit" || ccmds_info.cmds[0] == "EOF"|| ccmds_info.cmds[0] == "setenv" || ccmds_info.cmds[0] == "printenv"){
        if(ccmds_info.cmds[0] == "exit" || ccmds_info.cmds[0] == "EOF"){
            exit(0);
        }
        else if(ccmds_info.cmds[0] == "setenv"){
            // if(ccmds_info.cmds.size() > 3){
            //     cerr<<"setenv fault : setenv's parameters too much !\n";
            //     return;
            // }
            _setenv(ccmds_info.cmds[1], ccmds_info.cmds[2]);
        }
        else{
            _printenv(ccmds_info.cmds[1]);
        }
        return;
    }

    /* check built-in commands - who, tell, yell and name */
    if(ccmds_info.cmds[0] == "who" || ccmds_info.cmds[0] == "tell" || ccmds_info.cmds[0] == "yell" || ccmds_info.cmds[0] == "name" ){
        if(ccmds_info.cmds[0] == "name"){
            _name(ccmds_info.cmds);
            return;
        }
        // if(ccmds_info.cmds[0] == "who"){
        //     _who();
        //     return;
        // }
        // else if(ccmds_info.cmds[0] == "tell"){
        //     _tell(ccmds_info.cmds);
        //     return;
        // }
        // else if(ccmds_info.cmds[0] == "yell"){
        //     _yell(ccmds_info.cmds);
        //     return;
        // }
        // else{
        //     _name(ccmds_info.cmds);
        //     return;
        // }
    }

    char* args[ccmds_info.cmds.size()+1];
    str2char(ccmds_info.cmds,args);

    /* use signal to prevent zombie process */
    signal(SIGCHLD, server_signal_handler);

    // cout<<ccmds_info.fdin<<" "<<ccmds_info.fdout<<" "<<ccmds_info.fderr<<endl;
    pid_t _pid;
    _pid = fork();
    /* child process */
    if(_pid == 0){
        // cout<<ccmds_info.fdin<<" "<<ccmds_info.fdout<<" "<<ccmds_info.fderr<<endl;
        /* 
            redirect stdin & stdout & stderr to pipe in child process to prevent 
            the original STDIN & STDOUT & STDERR pointer been changed in parent process

            check STDIN_FILENO and STDOUT_FILENO and STDERR_FILENO != fd, 
            redirect to fd's file table.
            Due to prevent the amounts of fd over the limit of system,
            close fd-in and fd-out and fd-err if !=, since if ==,
            it will close the STDIN_FILENO and STDIN_FILENO
            and make error !

            bug : it cannot close after dup2 immediately, since when i implement 
            "!", ccmds_info.fdout ie equal to ccmds_info.fderr, if close ccmds_info.fdout,
            same as close the ccmds_info.fderr. Hence, it needs to dup2 all fd and then close.
            [ bug fixed at commit 4e1b42d627f181565196c8e492eea6faf48ab875 ]
        */
        dup2(ccmds_info.fdin,STDIN_FILENO);
        // close(ccmds_info.fdin);
        dup2(ccmds_info.fdout,STDOUT_FILENO);
        // close(ccmds_info.fdout);
        dup2(ccmds_info.fderr,STDERR_FILENO);
        // close(ccmds_info.fderr);
        for(auto it:_pipe){
            vector<int> fd = it.second;
            close(fd[0]);
            close(fd[1]);
        }

        /* check command is exist or not */
        // if(check_command(ccmds_info.cmds[0]) == false){
        //     cerr<<"Unknown command: ["<<ccmds_info.cmds[0]<<"].\n";
        //     exit(1);
        // }

        if(execvp(args[0],args) < 0){
            cerr<<"Unknown command: ["<<ccmds_info.cmds[0]<<"].\n";
            exit(1);
        }
        /* test execvp execute success by checking this output appear or not */
        // perror("execvp success ???\n");
    }
    /* parent process */
    else{
        /*
            bug: Since i wait child process when the current commands at the end
            and it is not pipe operation, the parent process may run end befofe 
            child process. that is, it will back to the main function and output "%"
            then output the child process's message.
            Hence, I use sleep in parent process to make it wait a minute to run and 
            hope it can solve this problem.
            [ bug fixed at commit ff38e0536d4fc9dd8bd803c754212bb5874de753 ]
        */
        /* UNSURE & NEED TO CHECK !!! */
        sleep(1);
        // int status;
        // waitpid(_pid, &status, 0);

        /* UNSURE & NEED TO CHECK !!! */
        if((ccmds_info.endofcmds == true) && (ccmds_info.dopipe == false)){
            int status;
            waitpid(_pid, &status, 0);
        }
    }
}

void close_decrease_pipe(bool ordpipe){
    if(_pipe.find(0) != _pipe.end()){
        vector<int> fd = _pipe[0];
        close(fd[0]);
        close(fd[1]);
        _pipe.erase(0);
    }
    if(!ordpipe){
        map<int,vector<int>> _pipe_tmp;
        for(auto it:_pipe){
            _pipe_tmp[it.first-1] = it.second;
        }
        _pipe = _pipe_tmp;
        return;
    }
    _pipe[0] = _pipe[-1];
    _pipe.erase(-1);
    // map<int,vector<int>> _pipe_tmp;
    // for(auto it:_pipe){
    //     _pipe_tmp[it.first-1] = it.second;
    // }
    // _pipe = _pipe_tmp;
}

int make_pipe_in(int pipein){
    if(_pipe.find(0) != _pipe.end()){
        /* close write end to get EOF */
        close(_pipe[0][1]);
        return _pipe[0][0];
    }
    return pipein;
}

int make_pipe_out(int pipenum){
    if(_pipe.find(pipenum) == _pipe.end()){
        int fd[2];
        if(pipe(fd) < 0){
            perror("create pipe fault !");
            exit(1);
        }
        _pipe[pipenum] = {fd[0],fd[1]};
        return fd[1];
    }
    return _pipe[pipenum][1];
}

int get_pipe_num(string cmd){
    if(cmd.size() == 1){
        return -1;
    }
    string _pipe_num = cmd.substr(1);
    return stoi(_pipe_num);
}

void str2char(vector<string> cmds, char** args){
    args[cmds.size()] = NULL;
    for(int i=0;i<cmds.size();i++){
        args[i] = new char(cmds[i].size()+1);
        strcpy(args[i],cmds[i].c_str());
    }
}

vector<string> split_inputCmds(string sin){
    vector<string> res = {};
    istringstream ss(sin);
    string tmp;
    while(ss >> tmp){
        res.push_back(tmp);
    }
    /*
        Here originally has bug, if not use "telnet", it will be fine,
        but in telnet, it will has bug.
        Though bug fixxed, i have no idea what's wrong of below.
    */
    // size_t start = 0;
    // size_t end = 0;
    // start = sin.find_first_not_of(' ', end);
    // while(start != string::npos){
    //     end = sin.find_first_of(' ', start);
    //     res.push_back(sin.substr(start, end-start));
    //     start = sin.find_first_not_of(' ', end);
    // }
    return res;
}