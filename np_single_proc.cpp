#include<iostream>
#include<vector>
#include<string>
#include<sstream>
#include<cstring>
#include<map>
#include<unordered_map>
#include<algorithm>
#include<fstream> // test read file
#include<unistd.h> // STD pipe
#include<sys/wait.h> // waitpid
#include<fcntl.h> // open
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/types.h> 
#include<sys/socket.h>
using namespace std;

#define MAX_CLIENT_USER 30
#define MAX_CLIENT_INPUTSIZE 15000

/* struct of each client's user pipe */
struct user_pipe{
    int fdin;
    int fdout;
    int sourceID;
    int targetID;
    bool usedornot;
};

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

/* client's information structure */
struct client_information{
    string client_name;
    string client_ip;
    int client_id;
    int client_port;
    int client_fd;
    map<int,vector<int>> _pipe;
    vector<struct user_pipe> _user_pipe;
    unordered_map<string,string> client_env;
    client_information(){
        client_name = "";
        client_ip = "";
        client_id = -1;
        client_port = -1;
        client_fd = -1;
        _pipe.clear();
        _user_pipe.clear();
        client_env.clear();
    }
};

////////////////////     global variable     ////////////////////

/* server global variable */
int msock;
int nfds; // max process counts
fd_set afds;
fd_set rfds; // used to select
vector<int> client_id_table(MAX_CLIENT_USER, 0); // store current total client id
unordered_map<int, client_information> client_info_table; // store current total client information

/* current client information */
struct client_information* current_client;

////////////////////     shell function     ////////////////////

/* pipe's function */
vector<string> split_inputCmds(string);
vector<string> split_inputPath(string);
int get_pipe_num(string);
int make_pipe_in(int); // get pipe read's file descriptor
int make_pipe_out(int); // get pipe write's file descriptor
int shellMain(int, string); // shell's main function
int part_cmds(vector<string>);
int make_pipe(cmds_allinfo&);
int exec_cmds(cmds_allinfo);
void str2char(vector<string>, char**);
void close_decrease_pipe(bool); // close 0 and decrease others after number pipe and increase after ordinary pipe
void killzombieprocess(int);
bool check_command(string);

////////////////////     server function      ////////////////////

int setServerTCP(int);
int getClientID();
/* get client information by fd */
int getClientInfoInMapWithfd(int);
void welcomemsg(int);
/* delete logout client information */
void dellogoutclient(int);
/* broadcast(structure of current client information, type, message, broadcast_id) */
void broadcast(struct client_information, string, string, int);

////////////////////     user pipe function     ////////////////////



////////////////////     built-in function     ////////////////////

void _setenv(string,string);
void _printenv(string);
void _who(void);
void _tell(vector<string>);
void _yell(vector<string>);
void _name(vector<string>);

////////////////////     main function     ////////////////////

int main(int argc, char* argv[]){
    if(argc != 2){
        cerr<<"input error: ./[program name] [port]\n";
        exit(1);
    }
    /* create TCP server */
    int s_port = atoi(argv[1]);
    msock = setServerTCP(s_port);

    // /* initialize client id table */
    // for(int i = 0; i < MAX_CLIENT_USER; i++){
    //     client_id_table[i] = 0;
    // }
    
    nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(msock, &afds);
    /* client socket address */
    struct sockaddr_in _cin;
    while(1){
        memcpy(&rfds, &afds, sizeof(rfds));

        /* server listen whether client need to connect */
        if(select(nfds, &rfds, NULL, NULL, NULL) < 0){
            if(errno == EINTR){
                continue;
            }
            exit(1);
        }

        /* server detect new client */
        if(FD_ISSET(msock, &rfds)){
            int ssock;
            socklen_t _cinlen = sizeof(_cin);
            if((ssock = accept(msock, (struct sockaddr*) &_cin, &_cinlen)) < 0){
                cerr<<"Accept Client FAIL !\n";
            }
            /* new client information */
            else{
                struct client_information cinfo;
                cinfo.client_fd = ssock;
                cinfo.client_ip = inet_ntoa(_cin.sin_addr);
                cinfo.client_port = ntohs(_cin.sin_port);
                cinfo.client_name = "(no name)";
                /* store client's default evironment */
                cinfo.client_env["PATH"] = "bin:.";
                /* get useful client id */
                int _client_id = getClientID();
                cinfo.client_id = _client_id;
                if(_client_id == -1){
                    cerr<<"Client's connection over 30 !\n";
                    continue;
                }
                /* store client information to client table by using id */
                client_info_table[_client_id] = cinfo;

                FD_SET(ssock, &afds);
                welcomemsg(ssock);
                /* new client login broadcast */
                broadcast(cinfo, "log-in", "", -1); // -1 means no target, everyone!
                
                /* write % to client to start service */
                string _bash = "% ";
                if(send(cinfo.client_fd, _bash.c_str(), _bash.size(), 0) < 0){
                    cerr<<"write '%' to client FAIL !\n";
                }
            }
        }

        /* check exist clients' message */
        for(int fd = 0; fd < nfds; fd++){
            if(FD_ISSET(fd, &rfds) && msock != fd){
                int _map_id = getClientInfoInMapWithfd(fd);
                /* input buffer & initialize */
                char _input[MAX_CLIENT_INPUTSIZE];
                memset(&_input, '\0', sizeof(_input));
                int n; // record input size

                /* client log out */
                if((n = recv(fd, _input, sizeof(_input), 0)) <= 0){
                    if(n < 0){
                        cerr<<"recv FAIL !\n";
                    }
                    broadcast(client_info_table[_map_id], "log-out", "", -1);
                    /* let another client can use this id */
                    client_id_table[client_info_table[_map_id].client_id-1] = 0;
                    /* delete client who logout ! */
                    dellogoutclient(client_info_table[_map_id].client_id);
                    /* close erased client's fd !!! */
                    close(fd);
                    close(STDOUT_FILENO);
                    close(STDERR_FILENO);
                    dup2(STDIN_FILENO, STDOUT_FILENO);
                    dup2(STDIN_FILENO, STDERR_FILENO);

                    FD_CLR(fd, &afds);
                }
                /* client's message exist */
                else{
                    string client_cmds(_input);
                    int execback;
                    execback = shellMain(_map_id, client_cmds);
                    /* client tap "exit" !, leave ! */
                    if(execback == -1){
                        broadcast(client_info_table[_map_id], "log-out", "", -1);
                        /* let another client can use this id */
                        client_id_table[client_info_table[_map_id].client_id-1] = 0;
                        /* delete client who logout ! */
                        dellogoutclient(client_info_table[_map_id].client_id);

                        /* need to dup stdout & stderr back to stdin and close erased client's fd !!! */
                        close(fd);
                        close(STDOUT_FILENO);
                        close(STDERR_FILENO);
                        dup2(STDIN_FILENO, STDOUT_FILENO);
                        dup2(STDIN_FILENO, STDERR_FILENO);
                        
                        FD_CLR(fd, &afds);
                    }
                }
            }
        }
    }
}

////////////////////     user pipe function code     ////////////////////


////////////////////     server function code     ///////////////////

/* set TCP server */
int setServerTCP(int port){
    int msock = 0;

    /* SOCK_STREAM -> TCP */
    if((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        cerr<<"Create TCP Server FAIL !\n";
        return 0;
    }

    /* set socker -> setsocketopt, allow different ip to use same port */
    const int opt = 1;
    if(setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        cerr<<"Set Socket With setsockopt FAIL !\n";
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
        cerr<<"Bind Server Socket FAIL !\n";
        return 0;
    }

    /* listen */
    listen(msock, 0);
    return msock;
}

/* delete logout client */
void dellogoutclient(int _id){
    /* delete user pipe who want to send message to target */
    for(auto it:client_info_table){
        // vector<struct user_pipe> tmp_userpipe = it.second._user_pipe;
        // vector<struct user_pipe>::iterator iter = tmp_userpipe.begin();
        vector<struct user_pipe>::iterator iter = it.second._user_pipe.begin();
        while(iter != it.second._user_pipe.end()){
            /* someone who want send message to target client */
            if((*iter).targetID == _id){
                close((*iter).fdin);
                close((*iter).fdout);
                it.second._user_pipe.erase(iter);
                continue;
            }
            iter++;
        }
    }
    /* delete target in client_info_table */
    client_info_table.erase(_id);
    return;
}

/* broadcast */
void broadcast(struct client_information cInfo, string func, string msg, int tarId){
    string broadcast_msg = "";
    if(func == "log-in"){
        broadcast_msg += ("*** User '" + cInfo.client_name + "' entered from " + cInfo.client_ip + ":" + to_string(cInfo.client_port) + ". ***\n");
    }
    else if(func == "log-out"){
        broadcast_msg += ("*** User '" + cInfo.client_name + "' left. ***\n");
    }
    else if(func == "yell"){
        broadcast_msg += msg;
    }
    else if(func == "name"){
        broadcast_msg += msg;
    }
    //cout<<msg<<endl;
    /* write message to all clients without server */
    for(int fd = 0; fd < nfds; fd++){
        if(fd == msock){
            continue;
        }
        if(FD_ISSET(fd, &afds)){
            if(send(fd, broadcast_msg.c_str(), broadcast_msg.size(), 0) < 0){
                cerr<<"Broadcast to "<<fd<<" Fail !\n";
            }
        }
    }
    return;
}

/* find client information with fd to broadcast */
int getClientInfoInMapWithfd(int _fd){
    for(auto it:client_info_table){
        if(it.second.client_fd == _fd){
            return it.first;
        }
    }
    return 0;
}

/* check & get client's id */
int getClientID(){
    for(int i=0;i<MAX_CLIENT_USER;i++){
        if(client_id_table[i] == 0){
            client_id_table[i] = 1;
            return i+1;
        }
    }
    return -1;
}

/* welcome message */
void welcomemsg(int _ssock){
    string res = "";
    res += "****************************************\n";
    res += "** Welcome to the information server. **\n";
    res += "****************************************\n";
    if(send(_ssock, res.c_str(), res.size(), 0) < 0){
        perror("welcome message write FAIL !\n");
    }
    return;
}

////////////////////     shell function code     ////////////////////

/* shell's main function */
int shellMain(int _id, string _input_cmd){
    /* record this time exec result */
    int res_exec;

    /*
        This part is the most important, 
        PLEASE USE "&" !!!
        Otherwise, you will not find where is wrong !!!
    */
    current_client = &client_info_table[_id];

    /* dup stdout & stderr to client's fd */
    dup2(current_client->client_fd, STDOUT_FILENO);
    dup2(current_client->client_fd, STDERR_FILENO);

    /* transform environment to current client's environment */
    clearenv();
    for(auto _env:current_client->client_env){
        _setenv(_env.first, _env.second);
    }
    
    /* process the client's input */
    if(_input_cmd.size() == 0){
        return 0; // not logout
    }
    vector<string> cmds = split_inputCmds(_input_cmd);
    res_exec = part_cmds(cmds);

    /*
        bug: if no use fflush, the output data will
        reamin in the output buffer and every time it
        will output all data.
    */
    fflush(stdout);
    string _bash = "% ";
    if(send(current_client->client_fd, _bash.c_str(), _bash.size(), 0) == -1){
        cerr<<"write '%' to client FAIL !\n";
    }
    return res_exec;
}

/* part of the commands to exec */
int part_cmds(vector<string> cmds){
    /* record this time exec result */
    int res_exec;

    int _size = cmds.size();
    /* current index of all cmds */
    int _cur = 0; 
    /* struct cmds_allinfo to store part cmds's informations */
    cmds_allinfo cmds_info;
    cmds_info.fdin = current_client->client_fd;
    cmds_info.fdout = current_client->client_fd;
    cmds_info.fderr = current_client->client_fd;
    // cout<<cmds_info.cmds.size()<<" "<<cmds_info.fdin<<" "<<cmds_info.fdout<<endl;
    while(_cur < _size){
        if(cmds_info.cmds.size() == 0){
            cmds_info.cmds.push_back(cmds[_cur++]);
            /* check tell and yell message because if the message is ! or |, it will be wrong */
            if(cmds_info.cmds[0] == "tell" || cmds_info.cmds[0] == "yell" || cmds_info.cmds[0] == "name"){
                while(_cur < _size){
                    cmds_info.cmds.push_back(cmds[_cur++]);
                }
            }
            /* check the last cmds, need to make pipe */
            if(_cur >= _size){
                cmds_info.endofcmds = true;
                cmds_info.dopipe = false;
                cmds_info.isordpipe = false;
                res_exec = make_pipe(cmds_info);
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
            res_exec = make_pipe(cmds_info);
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
            res_exec = make_pipe(cmds_info);
        }
        else{
            cmds_info.cmds.push_back(cmds[_cur++]);
            /* check the last cmds, need to make pipe */
            if(_cur >= _size){
                cmds_info.endofcmds = true;
                cmds_info.dopipe = false;
                cmds_info.isordpipe = false;
                res_exec = make_pipe(cmds_info);
            }
        }
    }
    return res_exec;
}

int make_pipe(cmds_allinfo &ccmds_info){
    /* record this time exec's result */
    int res_exec;

    ccmds_info.fdin = make_pipe_in(ccmds_info.fdin);
    res_exec = exec_cmds(ccmds_info);
    /* decrease , increase and close pipe number after exec each time (different between ordinary and number pipe) */
    close_decrease_pipe(ccmds_info.isordpipe);
    // if(ccmds_info.isordpipe == false){
    //     close_decrease_pipe();
    // }
    /* reset the struct of part cmds */
    ccmds_info.cmds.clear();
    ccmds_info.fdin = current_client->client_fd;
    ccmds_info.fdout = current_client->client_fd;
    ccmds_info.fderr = current_client->client_fd;
    ccmds_info.dopipe = false;
    ccmds_info.endofcmds = false;
    ccmds_info.isordpipe = false;
    return res_exec;
}

int exec_cmds(cmds_allinfo ccmds_info){

    /* check built-in commands - setenv, printenv & exit */
    if(ccmds_info.cmds[0] == "exit" || ccmds_info.cmds[0] == "EOF"|| ccmds_info.cmds[0] == "setenv" || ccmds_info.cmds[0] == "printenv"){
        if(ccmds_info.cmds[0] == "exit" || ccmds_info.cmds[0] == "EOF"){
            return -1;
        }
        else if(ccmds_info.cmds[0] == "setenv"){
            if(current_client->client_env.find(ccmds_info.cmds[1]) != current_client->client_env.end()){
                current_client->client_env[ccmds_info.cmds[1]] = ccmds_info.cmds[2];
            }
            else{
                current_client->client_env[ccmds_info.cmds[1]] = ccmds_info.cmds[2];
            }
            _setenv(ccmds_info.cmds[1], ccmds_info.cmds[2]);
            return 0;
        }
        else if(ccmds_info.cmds[0] == "printenv"){
            _printenv(ccmds_info.cmds[1]);
            return 0;
        }
    }
    /* check built-in commands - who, tell, yell and name */
    if(ccmds_info.cmds[0] == "who" || ccmds_info.cmds[0] == "tell" || ccmds_info.cmds[0] == "yell" || ccmds_info.cmds[0] == "name" ){
        if(ccmds_info.cmds[0] == "who"){
            _who();
            return 0;
        }
        else if(ccmds_info.cmds[0] == "tell"){
            _tell(ccmds_info.cmds);
            return 0;
        }
        else if(ccmds_info.cmds[0] == "yell"){
            _yell(ccmds_info.cmds);
            return 0;
        }
        else{
            _name(ccmds_info.cmds);
            return 0;
        }
    }

    char* args[ccmds_info.cmds.size()+1];
    str2char(ccmds_info.cmds,args);

    /* use signal to prevent zombie process */
    signal(SIGCHLD, killzombieprocess);

    //cout<<ccmds_info.fdin<<" "<<ccmds_info.fdout<<" "<<ccmds_info.fderr<<endl;
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

        /* Need to dup stdin to another pipe if ccmds_info.fdin has changed, otherwise, remain the same. */
        if(ccmds_info.fdin != current_client->client_fd){
            dup2(ccmds_info.fdin,STDIN_FILENO);
        }
        // close(ccmds_info.fdin);
        dup2(ccmds_info.fdout,STDOUT_FILENO);
        // close(ccmds_info.fdout);
        dup2(ccmds_info.fderr,STDERR_FILENO);
        // close(ccmds_info.fderr);
        for(auto it: current_client->_pipe){
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
    return 0;
}

// bool check_command(string filename){
//     string filepath;
//     for(auto it:_path){
//         filepath = it + "/" + filename;
//         /* 
//             originally use open but something wrong, because it will open file.
//             So, google say "access" ! nice !
//         */
//         if((access(filepath.c_str(), F_OK)) != -1){
//             return true;
//         }
//     }
//     return false;
// }

/* signal handler */
void killzombieprocess(int sig){
    int status;
    while(waitpid(-1,&status,WNOHANG) > 0){
        // cout<<"!\n";
        /* 
            wait for exist exiting child process
            (-1 means every child process) until 
            no zombie child process.
            if receive a zombie child process,
            it will return pid number, else return -1 
        */
    };
}

////////////////////     built-in function code     ////////////////////

void _who(){
    string who_msg = "<ID>  <nickname>  <IP:Port>   <indicate me>\n";
    cout<<who_msg;
    for(int i=0;i<MAX_CLIENT_USER;i++){
        if(client_id_table[i] == 1){
            client_information who_client = client_info_table[i+1]; // id is id-table + 1
            who_msg = (to_string(i+1) + "   " + who_client.client_name + "  " + who_client.client_ip + ":" + to_string(who_client.client_port));
            if(current_client->client_id == (i+1)){
                who_msg += "    <-me\n";
            }
            else{
                who_msg += "\n";
            }
            cout<<who_msg;
        }
    }
    return;
}

void _tell(vector<string> _cmds){
    int recv_id = stoi(_cmds[1]);
    string tell_msg = "";
    for(int i=2; i<_cmds.size(); i++){
        tell_msg += _cmds[i];
        if(i != _cmds.size()-1){
            tell_msg += " ";
        }
        else{
            tell_msg += "\n";
        }
    }
    /* receiver exist */
    if(client_id_table[recv_id-1] == 1){
        int recv_fd = client_info_table[recv_id].client_fd;
        string _tell_msg = ("*** " + current_client->client_name + " told you ***: " + tell_msg); 
        if(send(recv_fd, _tell_msg.c_str(), _tell_msg.size(), 0) < 0){
            cerr<<"send _tell message FAIL !\n";
        }
    }
    else{
        string _tell_msg = ("*** Error: user #" + _cmds[1] + " does not exist yet. ***\n");
        cout<<_tell_msg;
    }
    return;
}

void _yell(vector<string> _cmds){
    string yell_msg = "";
    for(int i=1; i<_cmds.size(); i++){
        yell_msg += _cmds[i];
        if(i != _cmds.size()-1){
            yell_msg += " ";
        }
        else{
            yell_msg += "\n";
        }
    }
    string _yell_msg = ("*** " + current_client->client_name + " yelled ***: " + yell_msg);
    broadcast((*current_client), "yell", _yell_msg, -1);
    return;
}


void _name(vector<string> _cmds){
    string name = "";
    for(int i=1; i<_cmds.size(); i++){
        name += _cmds[i];
        if(i != _cmds.size()-1){
            name += " ";
        }
    }
    string name_msg;
    for(auto it:client_info_table){
        if(it.second.client_name == name){
            name_msg = ("*** User '" + name + "' already exists. ***\n");
            cout<<name_msg;
            return;
        }
    }
    /* record the current client's name */
    current_client->client_name = name;
    name_msg = ("*** User from " + current_client->client_ip + ":" + to_string(current_client->client_port) + " is named '" + name + "'. ***\n");
    broadcast((*current_client), "name", name_msg, -1);
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

/*
    bug: ordinary pipe and number pipe are different,
    if the pipe is number pipe, it should be seen as a
    line of command. As for ordinary pipe, it is only a pipe
    and not need to be seen as a line of command.
    Hence, I use pipe num = -1 to represent ordinary pipe.
    and pipe which number > 0 represent number pipe.
    It will call "close_decrease_pipe" after exec, if it is
    number pipe, all pipe;s number minus 1, otherwise, make pipe
    -1 to 0 to be the latter command's input and others pipe number
    remain the same.
    [ bug fixed at commit d155c3911b6f7bd2263c1cd495dc0b694e04d341 ]
*/

void close_decrease_pipe(bool ordpipe){
    if(current_client->_pipe.find(0) != current_client->_pipe.end()){
        vector<int> fd = current_client->_pipe[0];
        close(fd[0]);
        close(fd[1]);
        current_client->_pipe.erase(0);
    }
    if(!ordpipe){
        map<int,vector<int>> _pipe_tmp;
        for(auto it:current_client->_pipe){
            _pipe_tmp[it.first-1] = it.second;
        }
        current_client->_pipe = _pipe_tmp;
        // for(auto it:current_client->_pipe){
        //     cout<<it.first<<" "<<it.second[0]<<" "<<it.second[1]<<endl;
        // }
        return;
    }
    current_client->_pipe[0] = current_client->_pipe[-1];
    current_client->_pipe.erase(-1);
    // map<int,vector<int>> _pipe_tmp;
    // for(auto it:_pipe){
    //     _pipe_tmp[it.first-1] = it.second;
    // }
    // _pipe = _pipe_tmp;
}

int make_pipe_in(int pipein){
    if(current_client->_pipe.find(0) != current_client->_pipe.end()){
        /* close write end to get EOF */
        close(current_client->_pipe[0][1]);
        return current_client->_pipe[0][0];
    }
    return pipein;
}

int make_pipe_out(int pipenum){
    if(current_client->_pipe.find(pipenum) == current_client->_pipe.end()){
        int fd[2];
        if(pipe(fd) < 0){
            perror("create pipe fault !");
            exit(1);
        }
        current_client->_pipe[pipenum] = {fd[0],fd[1]};
        return fd[1];
    }
    return current_client->_pipe[pipenum][1];
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

vector<string> split_inputPath(string sin){
    vector<string> res = {};
    size_t start = 0;
    size_t end = 0;
    start = sin.find_first_not_of(':', end);
    while(start != string::npos){
        end = sin.find_first_of(':', start);
        res.push_back(sin.substr(start, end-start));
        start = sin.find_first_not_of(':', end);
    }
    return res;
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