#include<iostream>
#include<vector>
#include<string>
#include<cstring>
#include<map>
#include<algorithm>
#include<fstream> // test read file
#include<unistd.h> // STD pipe
#include<sys/wait.h> // waitpid
#include<fcntl.h> // open
using namespace std;


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

//////////     global variable     //////////

/* store pipe's file descriptor */
map<int,vector<int>> _pipe;
/* store path information */
vector<string> _path;

//////////     help function     //////////

vector<string> split_inputCmds(string);
vector<string> split_inputPath(string);
int get_pipe_num(string);
int make_pipe_in(int); // get pipe read's file descriptor
int make_pipe_out(int); // get pipe write's file descriptor
void str2char(vector<string>, char**);
void part_cmds(vector<string>);
void exec_cmds(cmds_allinfo);
void make_pipe(cmds_allinfo&);
void close_decrease_pipe(bool); // close 0 and decrease others after number pipe and increase after ordinary pipe
void killzombieprocess(int);
bool check_command(string);

//////////     built-in function     //////////

void _setenv(string,string);
void _printenv(string);

//////////     main function     //////////

int main(){
    _setenv("PATH", "bin:.");
    string input_cmd;
    // ifstream file("test.txt",ios::in);
    cout<<"% ";
    while(1){
        // getline(file,input_cmd);
        getline(cin,input_cmd);
        if(cin.eof()){
        // if(cin.eof() || file.eof()){
            cout<<"\n";
            break;
        }
        if(input_cmd.size() == 0){
            cout<<"% ";
            continue;
        }
        // vector<string> cmds = split_cmds(input_cmd);
        vector<string> cmds = split_inputCmds(input_cmd);
        part_cmds(cmds);
        cout<<"% ";
    }
}

//////////     help function code     //////////

/* part of the commands to exec */
void part_cmds(vector<string> cmds){
    int _size = cmds.size();
    /* current index of all cmds */
    int _cur = 0; 
    /* struct cmds_allinfo to store part cmds's informations */
    struct cmds_allinfo cmds_info;
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

    char* args[ccmds_info.cmds.size()+1];
    str2char(ccmds_info.cmds,args);

    /* use signal to prevent zombie process */
    signal(SIGCHLD, killzombieprocess);

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

bool check_command(string filename){
    string filepath;
    for(auto it:_path){
        filepath = it + "/" + filename;
        /* 
            originally use open but something wrong, because it will open file.
            So, google say "access" ! nice !
        */
        if((access(filepath.c_str(), F_OK)) != -1){
            return true;
        }
    }
    return false;
}

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
    size_t start = 0;
    size_t end = 0;
    start = sin.find_first_not_of(' ', end);
    while(start != string::npos){
        end = sin.find_first_of(' ', start);
        res.push_back(sin.substr(start, end-start));
        start = sin.find_first_not_of(' ', end);
    }
    return res;
}