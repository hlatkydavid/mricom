/* common.c
 * 
 * Common function definitions include varoius parsers and
 * mricom specific process management
 * 
 */
#include "common.h"
#define VERBOSE_PROCESSCTRL 1
#define LLENGTH 64 // mpid file line length
/* Function: fill_mpid
 * -------------------
 * fill mpid struct with pid, ppid, name, num from /proc
 */

void fill_mpid(struct mpid *mp){

    FILE *fp;
    //struct timeval tv;
    char path[32];
    char pidstr[8];
    char *buf = NULL;
    size_t len = 0;
    int i;
    gettimeofday(&(mp->tv), NULL);
    // get pid and parent pid
    mp->pid = getpid();
    mp->ppid = getppid();
    // get names by rading /proc/[pid]/comm
    pid_t pid;
    for(i=0;i<2;i++){
        if(i==0)
            pid = mp->pid;
        else
            pid = mp->ppid;
        sprintf(pidstr, "%d",pid);
        strcpy(path, "/proc/");
        strcat(path, pidstr);
        strcat(path, "/comm");
        //printf("path here : %s\n",path);
        fp = fopen(path,"r");
        if(fp == NULL){
            perror("fill_mpid");
            exit(1);
        }
        getline(&buf, &len, fp);
        //remove newline
        buf[strlen(buf)-1]='\0';
        if(i==0)
            strcpy(mp->name, buf);
        else
            strcpy(mp->pname, buf);
        fclose(fp);
        //TODO why segfault with this???
        //free(buf);
    }
}

/*
 * Function: processctrl_add
 * -------------------------
 * add process id to local pid file, return 0 on success
 *
 */
int processctrl_add(char *path, struct mpid *mp, char *status){
    
    int ret;
    char buf[64];
    char line[128];
    char *d = DELIMITER;
    struct timeval tv;
    int fd;
    FILE *fp;
    gettimeofday(&tv, NULL);
    gethrtime(buf, tv);
    if(strcmp(status,"START") == 0 || strcmp(status,"STOP") == 0 ||
            strcmp(status, "INTRPT") == 0){
        ;
    } else{
        printf("processctrl_add: wrong status input\n");
        exit(1);
    }
    fd = open(path, O_RDWR | O_APPEND);
    if(fd < 0){
        perror("processctrl_add");
        exit(1);
    }
    fp = fdopen(fd, "a+");
    // make line 
    sprintf(line,"%s%s%d%s%d%s%s%s%s%s%s",
            status,d,mp->pid,d,mp->ppid,d,mp->name,d,mp->pname,d,buf);
    // lock file access
    ret = flock(fd,LOCK_SH);
    if(fd < 0){
        perror("processctrl_add");
        exit(1);
    }
    // open file stream
    fprintf(fp, "%s\n",line);
    fclose(fp);
    // unlock file access
    ret = flock(fd,LOCK_UN);
    if(fd < 0){
        perror("processctrl_add");
        exit(1);
    }
    ret = close(fd);
    if(fd < 0){
        perror("processctrl_add");
        exit(1);
    }
    return 0;
}

/*
 * Function processctrl_get
 * ------------------------
 *  Parse process log file mproc.log and fill running process struct
 *  
 */
#define N_MAX 128
#define LLENGTH 64
int processctrl_get(char *path, struct processes *p){

    FILE *fp;
    char filebuf[N_MAX][LLENGTH];
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int i,j,k, n, col;
    int n_remove;
    int remove_list[N_MAX];

    int start_list[N_MAX], stop_list[N_MAX], intrpt_list[N_MAX];
    int running_list[N_MAX];

    char lstatus[N_MAX][8];
    char *d = DELIMITER;
    char *token;
    int tmp_pid;

    // start_list, stop_list, intrpt_list elements are either 0 or 1 so init 0
    memset(start_list, 0, N_MAX * sizeof(int));
    memset(stop_list, 0, N_MAX * sizeof(int));
    memset(intrpt_list, 0, N_MAX * sizeof(int));
    memset(running_list, 0, N_MAX * sizeof(int));
    fp = fopen(path,"r");
    if(fp == NULL){
        printf("could not open %s\n",path);
        return 1;
    }
    i = 0;
    // read into buffer
    while (read = getline(&line, &len, fp) != -1){
        if(strncmp(line, "#",1) == 0){
            continue;
        } else {
            //replace EOL with nullbyte
            line[strlen(line)-1] = '\0';
            strcpy(filebuf[i], line);
            // sort lines starting with START, STOP, INTRPT
            if(strncmp(line, "START",5)==0){
                start_list[i] = 1;    
            } else if(strncmp(line, "STOP",4)==0){
                stop_list[i] = 1;    
            } else if(strncmp(line, "INTRPT",6)==0){
                intrpt_list[i] = 1;    
            } else {
                fprintf(stderr, "processctrl_get: wrong line\n");
            }

        }
        i++;

    }
    // init running_list as start_list, remove elements later
    for(i=0;i<N_MAX;i++){
        running_list[i] = start_list[i];
    }
    // for 'START' lines check if there is a 'STOP' or 'INTRPT'
    for(i=0; i < N_MAX; i++){
        if(start_list[i] == 1){
            // check pid
            strcpy(line, filebuf[i]);
            token = strtok(line,d);
            tmp_pid = atoi(strtok(NULL,d));
            // check if same pid was stopped or interrupted
            for(j=0;j<N_MAX;j++){
                if((stop_list[j] == 1) || (intrpt_list[j] == 1)){
                    strcpy(line, filebuf[j]);
                    token = strtok(line,d);
                    if(atoi(strtok(NULL,d)) == tmp_pid){
                        running_list[i] = 0;
                    }
                    
                }
            }
        }
    }
    // tokenize line where 'running_list' is 1
    i=0;
    p->nproc = 0;
    for(j=0; j<N_MAX; j++){
        if(running_list[j] == 1){
            strcpy(line, filebuf[j]);
            if(strncmp(line,"START",5)==0){
                token = strtok(line,d);
                col = 0;
                while(token != NULL){
                    switch(col){
                        case 0 :
                            strcpy(lstatus[i],token);
                            break;
                        case 1 :
                            p->pid[i]=atoi(token);
                            break;
                        case 2 :
                            p->ppid[i]=atoi(token);
                            break;
                        case 3 :
                            strcpy(p->name[i],token);
                            break;
                        case 4 :
                            strcpy(p->pname[i],token);
                            break;
                        case 5 :
                            strcpy(p->timestamp[i],token);
                            break;
                    }
                    token = strtok(NULL, d);
                    col++;
                }
            }
            i++;
            p->nproc++;
        }

    }
    return 0;
}
/*
 * Function: sighandler
 * --------------------
 *  Catch interrupts (eg.: Ctrl-c) and finish log file before termination
 */
void sighandler(int signum){
    // accepts no argument, so set up mpid again
    struct mpid *mp;
    char c = '\0';
    char path[LPATH] = {0};
    char metaf[LPATH] = {0};
    strcpy(path, getenv("MRICOMDIR"));
    snprintf(path, sizeof(path),"%s/%s",getenv("MRICOMDIR"),MPROC_FILE);
    mp = malloc(sizeof(struct mpid));
    // process name, mp->name
    memset(mp, 0, sizeof(struct mpid));
    fill_mpid(mp);
    // general interrupt
    if(signum == SIGINT){
        // blockstim additional interrupt handling
        if(strcmp(mp->name,"blockstim")==0){
            snprintf(metaf, sizeof(metaf),
                    "%s/%sblockstim.meta",getenv("MRICOMDIR"),DATA_DIR);
            fprintf_meta_intrpt(metaf);
        }
        // blockstim additional interrupt handling
        else if(strcmp(mp->name,"eventstim")==0){
            ;
            //TODO
        }
        // blockstim additional interrupt handling
        else if(strcmp(mp->name,"analogdaq")==0){
            snprintf(metaf, sizeof(metaf),
                    "%s/%sanalogdaq.meta",getenv("MRICOMDIR"),DATA_DIR);
            fprintf_meta_intrpt(metaf);
            
        }
        // ttlctrl
        else if(strcmp(mp->name,"ttlctrl")==0){
            snprintf(metaf, sizeof(metaf),
                    "%s/%sttlctrl.meta",getenv("MRICOMDIR"),DATA_DIR);
            fprintf_meta_intrpt(metaf);
            
        }
        else {
            ;
        }
        processctrl_add(path, mp, "INTRPT");
        fprintf(stderr,"%s exiting...\n",mp->name);
        free(mp);
        exit(1);
    }
    // mribg status control
    if(signum == SIGUSR1 && strcmp(mp->name, "mribg")==0){
        mribg_status = 1;
    }
    free(mp);
}   


/*
 * Function: processctrl_clean()
 * -----------------------------
 * Clear contents of mproc.log file, keep only currently running processes
 */
int processctrl_clean(struct gen_settings *gs, struct processes *pr){

    FILE *fp;
    int fd;
    int i, ret;
    char line[64];
    char *d = DELIMITER;
    //fd = open(gs->mpid_file);
    fd = open(gs->mpid_file, O_RDWR);
    if(fd < 0){
        perror("processctrl_clean");
        exit(1);
    }
    fp = fopen(gs->mpid_file,"w");
    ret = flock(fd,LOCK_SH);
    if(fd < 0){
        perror("processctrl_clean");
        exit(1);
    }
    for(i=0;i<pr->nproc;i++){
        snprintf(line, 64, "START%s%d%s%d%s%s%s%s%s%s\n", d, pr->pid[i], d, 
            pr->ppid[i], d, pr->name[i], d, pr->pname[i], d, pr->timestamp[i]);
        fprintf(fp,"%s",line);
    }
    fclose(fp);
    // unlock file access
    ret = flock(fd,LOCK_UN);
    if(fd < 0){
        perror("processctrl_clean");
        exit(1);
    }
    close(fd);
    return 0;
}

/*
 * Function: processctrl_archive
 * -----------------------------
 * Copy mproc.log contents with timestamp  into archive file
 */
//TODO
int processctrl_archive(char *path, char *archive){

    
    return 0;
}

/*
 * Function: parse_procpar
 * -----------------------
 */

int parse_procpar(){

}

/*
 * Function: search_procpar
 * -------------------------
 * search procpar file for specified parameter and finds its value
 * works only with string valued parameters eg.: comment, mricomcmd 
 *
 * input: 
 *      char *parameter_name 
 *      char *command 
 */
int search_procpar(char *parameter_name, char *command){

    // TODO get these from settings file
    char procpar[] = "procpar"; // procpar path
    char *parname = "comment "; // procpar parameter to search for
    char line[128]; // max procpar line length
    char cmd[64]; // string value of procpar parameter 'mricomcmd'
    int i,j,k,n = 0;

    int bool_found = 0;
    FILE *fp;

    fp = fopen(procpar,"r");
    if(fp == NULL){
        printf("cannot access file");
        exit(EXIT_FAILURE);
    }   
    // read only while parameter is not found
    while(fgets(line, 128, fp)){
        if (i > n && n != 0){
            break;
        }   
        if(strncmp(line, parname, strlen(parname)) == 0){
            n = i + 1;
        }   
        // this is the value line
        if(i == n && i != 0){
            while(line[j] != '\n'){
                if(j > 1 && line[j] != '"'){
                    cmd[k] = line[j];
                    k++;
                }
                j++;
            }   
        }   
        i++;
    }
    printf("comment value: %s\n",cmd);
    return 0;

}
/*
 * Function: parse_gen_settings
 * ------------------------
 * reads settings file and fills gen_settings struct
 */
int parse_gen_settings(struct gen_settings *settings){

    char settings_file[128] = {0} ;
    char mricomdir[128];
    FILE *fp;
    char line[128];
    char buf[128];
    char tmpbuf[128] = {0};
    char *token;
    int len;
    int i = 0; int j = 0;
    int nchan = NCHAN; // for comparing number of channel to channel names

    //check if MRICOMDIR exists as env TODO
    strcpy(mricomdir,getenv("MRICOMDIR"));
    strcat(settings_file, mricomdir);
    strcat(settings_file, "/");
    strcat(settings_file, SETTINGS_FILE);

    //set memory to zero
    memset(settings, 0, sizeof(*settings));
    fp = fopen(settings_file, "r");
    if(fp == NULL){
        printf("\nerror: could not open file 'settings'\n");
        printf("quitting ...\n");
        exit(EXIT_FAILURE);
    }
    while(fgets(line, 128, fp)){
        // ignore whitespace and comments
        if(line[0] == '\n' || line[0] == '\t'
           || line[0] == '#' || line[0] == ' '){
            continue;
        }
        //remove whitespace
        remove_spaces(line);
        //remove newline
        len = strlen(line);
        if(line[len-1] == '\n')
            line[len-1] = '\0';
        /* general settings */
        
        token = strtok(line,"=");

        if(strcmp(token,"DEVICE") == 0){
            token = strtok(NULL,"=");
            strcpy(settings->device, token);
            continue;
        }
        if(strcmp(token,"WORKDIR") == 0){
            token = strtok(NULL,"=");
            strcpy(settings->workdir, token);
            continue;
        }
        if(strcmp(token,"STUDIES_DIR") == 0){
            token = strtok(NULL,"=");
            strcpy(settings->studies_dir, token);
            continue;
        }
        if(strcmp(token, "PID_FILE") == 0){
            token = strtok(NULL,"=");
            strcpy(tmpbuf, token);
            continue;
        }
        if(strcmp(token, "KST_FILE") == 0){
            token = strtok(NULL,"=");
            strcpy(settings->kst_file, token);
            continue;
        }
        if(strcmp(token, "KST_SETTINGS") == 0){
            token = strtok(NULL,"=");
            strcpy(settings->kst_settings, token);
            continue;
        }
        if(strcmp(token, "PRECISION") == 0){
            token = strtok(NULL,"=");
            settings->precision = atoi(token);
            continue;
        }
        if(strcmp(line, "PROCPAR") == 0){
            token = strtok(NULL,"=");
            strcpy(settings->procpar, token);
            continue;
        }
        if(strcmp(line, "EVENT_DIR") == 0){
            token = strtok(NULL,"=");
            strcpy(settings->event_dir, token);
            continue;
        }
        if(strcmp(line, "RAMDISK") == 0){
            token = strtok(NULL,"=");
            strcpy(settings->ramdisk, token);
            continue;
        }
        if(strcmp(line, "MRIBG_INIT_STATUS") == 0){
            token = strtok(NULL,"=");
            settings->mribg_init_status = atoi(token);
            continue;
        }
    }
    // additional formatting
    strcat(settings->mpid_file,settings->workdir);
    strcat(settings->mpid_file,"/");
    strcat(settings->mpid_file, tmpbuf);
    return 0;
}
int parse_dev_settings(struct dev_settings *ds){

    char settings_file[128] = {0};
    char mricomdir[128];
    FILE *fp;
    char line[128];
    char buf[128];
    char *token;
    int len;
    int i = 0; int j = 0;
    int naichan = NAICHAN; // for comparing number of channel to channel names

    strcpy(mricomdir,getenv("MRICOMDIR"));
    strcat(settings_file, mricomdir);
    strcat(settings_file, "/");
    strcat(settings_file, SETTINGS_FILE);

    fp = fopen(settings_file, "r");
    if(fp == NULL){
        printf("\nerror: could not open file 'settings'\n");
        printf("quitting ...\n");
        exit(EXIT_FAILURE);
    }
    while(fgets(line, 128, fp)){
        // ignore whitespace and comments
        if(line[0] == '\n' || line[0] == '\t'
           || line[0] == '#' || line[0] == ' '){
            continue;
        }
        //remove whitespace
        remove_spaces(line);
        //remove newline
        len = strlen(line);
        if(line[len-1] == '\n')
            line[len-1] = '\0';
        
        token = strtok(line,"=");

        if(strcmp(token,"DEVPATH") == 0){
            token = strtok(NULL,"=");
            strcpy(ds->devpath, token);
            continue;
        }
        // analog input
        // -------------------------------------------
        if(strcmp(line, "IS_ANALOG_DIFFERENTIAL") == 0){
            token = strtok(NULL,"=");
            ds->is_analog_differential = atoi(token);
            continue;
        }
        if(strcmp(line, "ANALOG_SAMPLING_RATE") == 0){
            token = strtok(NULL,"=");
            ds->analog_sampling_rate = atoi(token);
            continue;
        }
        if(strcmp(line, "ANALOG_IN_SUBDEV") == 0){
            token = strtok(NULL,"=");
            ds->analog_in_subdev = atoi(token);
            continue;
        }
        if(strcmp(line, "ANALOG_CH_NAMES") == 0){
            i = 0;
            token = strtok(NULL,"=");
            strcpy(buf, token);
            token = strtok(buf, ",");
            while(token != NULL){
                strcpy(ds->analog_ch_names[i], token);
                i++;
                token = strtok(NULL,",");
            }
            i = 0; // reset to 0
            continue;
        }
        if(strcmp(line, "ANALOG_IN_CHAN") == 0){
            i=0;
            token = strtok(NULL,"=");
            strcpy(buf, token);
            token = strtok(buf, ",");
            while(token != NULL){
                ds->analog_in_chan[i] = (int)atoi(token);
                i++;
                token = strtok(NULL,",");
            }
            //TODO check if number is same as NAICHAN
            i = 0; // set 0 again, just to be sure
        }
        // digital stim
        // -----------------------------------------
        if(strcmp(line, "STIM_SUBDEV") == 0){
            token = strtok(NULL,"=");
            ds->stim_subdev = atoi(token);
            continue;
        }
        if(strcmp(line, "STIM_TRIG_CHAN") == 0){
            token = strtok(NULL,"=");
            ds->stim_trig_chan = atoi(token);
            continue;
        }
        if(strcmp(line, "STIM_TTL_CHAN") == 0){
            token = strtok(NULL,"=");
            ds->stim_ttl_chan = atoi(token);
            continue;
        }
        // ttlctrl
        //----------------------------------------------
        if(strcmp(line, "TTLCTRL_SUBDEV") == 0){
            token = strtok(NULL,"=");
            ds->ttlctrl_subdev = atoi(token);
            continue;
        }
        if(strcmp(line, "TTLCTRL_CONSOLE_IN_CHAN") == 0){
            token = strtok(NULL,"=");
            ds->ttlctrl_console_in_chan = atoi(token);
            continue;
        }
        if(strcmp(line, "TTLCTRL_CONSOLE_OUT_CHAN") == 0){
            token = strtok(NULL,"=");
            ds->ttlctrl_console_out_chan = atoi(token);
            continue;
        }
        if(strcmp(line, "TTLCTRL_OUT_CHAN") == 0){
            token = strtok(NULL,"=");
            ds->ttlctrl_out_chan = atoi(token);
            continue;
        }
        if(strcmp(line, "TTLCTRL_USR_CHAN") == 0){
            i=0;
            token = strtok(NULL,"=");
            strcpy(buf, token);
            token = strtok(buf, ",");
            while(token != NULL){
                ds->ttlctrl_usr_chan[i] = (int)atoi(token);
                i++;
                token = strtok(NULL,",");
            }
            i = 0; // set 0 again, just to be sure
        }
        // test_console
        // ------------------------------------------
        if(strcmp(line, "TEST_CONSOLE_SUBDEV") == 0){
            token = strtok(NULL,"=");
            ds->test_console_subdev = atoi(token);
            continue;
        }
        if(strcmp(line, "TEST_CONSOLE_OUT_CHAN") == 0){
            token = strtok(NULL,"=");
            ds->test_console_out_chan = atoi(token);
            continue;
        }
        if(strcmp(line, "TEST_CONSOLE_IN_CHAN") == 0){
            token = strtok(NULL,"=");
            ds->test_console_in_chan = atoi(token);
            continue;
        }
    }
    return 0;
}

/*
 * Function: fprint_common_header
 * ------------------------------
 * write timing, version, etc info into file common to tsv and meta
 */
int fprintf_common_header(FILE *fp, struct header *h, int argc, char **args){

    char line[64];
    char buf[64] = {0};
    int vmaj = VERSION_MAJOR;
    int vmin = VERSION_MINOR;
    int i;
    // check header for null, fill it with defaults
    if(fp == NULL){
        printf("fprint_header_common: file not open\n");
        exit(1);
    }
    if(strcmp(h->proc, "")==0){
        char name[16];
        int pid = getpid();
        char *tok;
        getname(name, pid);
        tok = strtok(name, "\n");
        strcpy(h->proc,tok);
    }
    fprintf(fp,"# cmd=%s args=",h->proc);

    if(is_memzero(&(h->timestamp), sizeof(struct timeval))){
        struct timeval tv;
        gettimeofday(&tv, NULL);
        gethrtime(buf, tv);
    } else {
        gethrtime(buf, h->timestamp);
    }
    if(argc==1){
        fprintf(fp,"NULL");
    } else {
        for(i=1;i<argc;i++){
            if(i==1)
                fprintf(fp,"%s",args[i]);
            else
            fprintf(fp,",%s",args[i]);
        }
    }
    fprintf(fp," Mricom v%d.%d\n",vmaj,vmin);
    fprintf(fp,"# timestamp=%s\n", buf);
    return 0;

}


/*
 * Function fprintf_times_meta
 * ---------------------------
 * log times struct in a common format in metadata files
 */

void fprintf_times_meta(FILE *fp, struct times *t){

    char *buf;
    buf = malloc(sizeof(char)*64); // for human readable time

    fprintf(fp, "\n%% TIMING\n");
    gethrtime(buf, t->start);
    fprintf(fp, "start=%s\n",buf);
    gethrtime(buf, t->action);
    fprintf(fp, "action=%s\n",buf);
    gethrtime(buf, t->stop);
    fprintf(fp, "stop=%s\n",buf);
    free(buf);

}
/*
 * Function fprintf_times_meta
 * ---------------------------
 * Log times struct in a common format in metadata files.
 *
 * Element represents one element of the times struct
 */


void fprintf_meta_times(char *p, struct times *t, char *element){

    char *buf;
    FILE *fp;
    fp = fopen(p,"a");
    buf = malloc(sizeof(char)*64); // for human readable time

    if(strcmp(element, "start")==0){
        fprintf(fp, "\n%% TIMING\n");
        gethrtime(buf, t->start);
        fprintf(fp, "start=%s\n",buf);
        free(buf);
    } else if(strcmp(element, "action")==0){
        gethrtime(buf, t->action);
        fprintf(fp, "action=%s\n",buf);
        free(buf);
    } else if(strcmp(element,"stop")==0){
        gethrtime(buf, t->stop);
        fprintf(fp, "stop=%s\n",buf);
        free(buf);
    } else {
        fprintf(stderr, "fprintf_meta_times: wrong input\n");
        free(buf);
    }
    fclose(fp);

}

void fprintf_meta_intrpt(char *p){

    char *buf;
    FILE *fp;
    struct timeval tv; // TODO replace with timespec
    struct timespec ts;
    int ret;
    fp = fopen(p,"a");
    buf = malloc(sizeof(char)*64); // for human readable time
    gettimeofday(&tv,NULL);
    //clock_gettime(CLOKC_REALTIME, &ts);
    gethrtime(buf, tv);
    //printf("buf: %s\n",buf);
    ret = fprintf(fp, "intrpt=%s\n",buf);
    //printf("ret: %d\n",ret);
    free(buf);
    fclose(fp);
}

/*
 * Function: fprintf_times
 * -----------------------
 *  Print elements of time struct to open filestream in human readable format.
 */
void fprintf_times(FILE *fp, struct times *t){

    char buf[64] = {0};
    if(fp == NULL){
        return;
    }
    gethrtime(buf, t->start);
    fprintf(fp, "start: %s\n",buf);
    gethrtime(buf, t->action);
    fprintf(fp, "action: %s\n",buf);
    gethrtime(buf, t->stop);
    fprintf(fp, "stop: %s\n",buf);
}

/*
 * Function: compare_common_header
 * --------------------------------
 * Return 0 if the 2-line headers are the same in the 2 files (.tsv and .meta)
 * -1 othervise
 * 
 */
int compare_common_header(char *file1, char *file2){

    FILE *fp;
    char buf1[2][64];
    char buf2[2][64];
    int count, ret;
    count = 0;
    // read first two lines into buffers
    fp = fopen(file1,"r");
    fgets(buf1[0], 64, fp);
    fgets(buf1[1], 64, fp);
    fclose(fp);
    fp = fopen(file2,"r");
    fgets(buf2[0], 64, fp);
    fgets(buf2[1], 64, fp);
    fclose(fp);
    // check if equal
    if(strcmp(buf1[0], buf2[0]) != 0)
        return -1;
    if(strcmp(buf1[1], buf2[1]) != 0)
        return -1;

    return 0;

}
/*
 * Function: getname
 * -----------------
 * get process name from process pid
 */
void getname(char *name, int pid){

    FILE *fp;
    char *procname = NULL;
    char path[32];
    char pidstr[8];
    size_t len = 0; 
    sprintf(pidstr, "%d",pid);
    strcpy(path, "/proc/");
    strcat(path, pidstr);
    strcat(path, "/comm");
    fp = fopen(path,"r");
    if(fp == NULL){
        fprintf(stderr, "getname: cannot open file %s\n",path);
        exit(1);
    }
    getline(&procname, &len, fp);
    strcpy(name, procname);
    free(procname);
    fclose(fp);
}

/* Function: getppname
 * -------------------------
 * Find parent process name and put into string pointer input
 */
void getppname(char *name){

    pid_t pid; 
    FILE *fp;
    char path[32];
    char pidstr[8];
    char *pname = NULL;
    size_t len = 0;
    pid = getppid();
    //itoa(pid,pidstr,10);
    sprintf(pidstr, "%d",pid);
    strcpy(path, "/proc/");
    strcat(path, pidstr);
    strcat(path, "/comm");
    //printf("path here : %s\n",path);
    fp = fopen(path,"r");
    if(fp == NULL){
        perror("getppname");
        exit(1);
    }
    getline(&pname, &len, fp);
    //TODO strip newline from end
    strcpy(name, pname);
    free(pname);
    fclose(fp);
}
/* Function: getcmdline
 * -------------------------
 * Find process invoking command and put into string pointer input
 */
 //TODO dont use this
void getcmdline(char *cmdl){

    pid_t pid; 
    //FILE *fp;
    int fd; // file descriptor
    char path[32];
    char pidstr[8];
    //char *buf = NULL;
    char *buf;
    buf = malloc(sizeof(char)*64);
    size_t len = 0;
    pid = getpid();
    //itoa(pid,pidstr,10);
    sprintf(pidstr, "%d",pid);
    strcpy(path, "/proc/");
    strcat(path, pidstr);
    strcat(path, "/cmdline");
    //printf("path here : %s\n",path);
    fd = open(path,O_RDONLY);
    if(fd == -1){
        perror("getcmdline");
        exit(1);
    }
    //getline(&buf, &len, fp);

    read(fd, buf, 64);
    strcpy(cmdl, buf);
    free(buf);
    close(fd);
}

/*
 * Function gethrtime
 * ------------------
 * Copy timeval into human readable string buffer
 * example: 2020-04-28 20:07:34.992715
 */
void gethrtime(char *outbuf, struct timeval tv){

    time_t nowtime;
    struct tm *nowtm;
    char tmbuf[64];
    char buf[64];

    memset(outbuf, 0, sizeof((*outbuf)));
    nowtime = tv.tv_sec;
    nowtm = localtime(&nowtime);
    strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
    snprintf(buf, sizeof buf, "%s.%06ld", tmbuf, tv.tv_usec);
    strcpy(outbuf, buf);

}
/*
 * Function clockhrtime
 * ------------------
 * Same as above, but with clock_gettime struct
 * Copy timeval into human readable string buffer
 * example: 2020-04-28 20:07:34.992715
 */
void getclockhrtime(char *outbuf, struct timespec tv){

    time_t nowtime;
    struct tm *nowtm;
    char tmbuf[64];
    char buf[64];

    memset(outbuf, 0, sizeof((*outbuf)));
    nowtime = tv.tv_sec;
    nowtm = localtime(&nowtime);
    strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
    snprintf(buf, sizeof buf, "%s.%09ld", tmbuf, tv.tv_nsec);
    strcpy(outbuf, buf);

}
/*
 * Function: getusecdelay
 * ----------------------
 * Calculate current difference in microsec from input time
 */
int getusecdelay(struct timeval tv1){

    struct timeval tv2;
    int time;
    int mic;
    double mega = 1000000;
    gettimeofday(&tv2,NULL);
    time = (tv2.tv_sec - tv1.tv_sec) * mega;
    mic = (tv2.tv_usec - tv1.tv_usec);
    mic += time;
    return mic;
}
/*
 * Function: clockusecdelay
 * ----------------------
 * Calculate current difference in microsec from input time
 */
int clockusecdelay(struct timespec tv1){

    struct timespec tv2;
    int time;
    int mic;
    double giga = 1000000000;
    clock_gettime(CLOCK_MONOTONIC,&tv2);
    time = (tv2.tv_sec - tv1.tv_sec) * giga;
    mic = (tv2.tv_nsec - tv1.tv_nsec);
    mic += time;
    return mic;
}
/*
 * Function: getsecdiff
 * ----------------------
 * Calculate difference in seconds (double) between two timepoints.
 */
double getsecdiff(struct timeval tv1, struct timeval tv2){

    double diff;
    diff = tv2.tv_sec - tv1.tv_sec;
    diff += (double) (tv2.tv_usec - tv1.tv_usec) / 1000000.0;
    return diff;
}

/*
 * Function: getusecdiff
 * ----------------------
 * Calculate difference in microseconds between two timepoints.
 */
long int getusecdiff(struct timeval tv1, struct timeval tv2){

    //TODO
    double diff;
    diff = tv2.tv_sec - tv1.tv_sec;
    diff += (double) (tv2.tv_usec - tv1.tv_usec) / 1000000.0;
    return diff;
}

/*
 * Function: count_lines
 * ---------------------
 * Return the number of lines in file by counting the newline character. 
 */
#define COUNT_BUFSIZE 1024
long int count_lines(char *path){

  int newlines = 0;
  char buf[COUNT_BUFSIZE];
  FILE* file;

  file = fopen(path, "r");
  while (fgets(buf, COUNT_BUFSIZE, file))
  {
    if (!(strlen(buf) == COUNT_BUFSIZE-1 && buf[COUNT_BUFSIZE-2] != '\n'))
      newlines++;
  }
  fclose(file);
  return newlines;
}

/* Function: remove_spaces
 * -----------------------
 * remove whitespace from a line in config file
 */
void remove_spaces(char* s) {
    const char* d = s;
    do {
        while (*d == ' ') {
            ++d;
        }
    } while (*s++ = *d++);
}

/*
 * Function: is_number
 * -------------------
 *  Return True if character array represents a number
 */
bool is_number(char number[]){
    int i = 0;

    //checking for negative numbers
    if (number[0] == '-')
        i = 1;
    for (; number[i] != 0; i++)
    {
        //if (number[i] > '9' || number[i] < '0')
        if (!isdigit(number[i]))
            return false;
    }
    return true;
}

/*
 * Function: is_number
 * -------------------
 *  Return True if character array represents a positive double
 */
bool is_posdouble(char number[]){

    char c;
    int i;
    int count=0;
    bool ret;
    // check if contains decimal
    for(i=0; i<strlen(number);i++){
        if(count > 1)
            return false;
        // only one decinal dot allowed
        if(number[i] == '.'){
            count++;
            continue;
        }
        if(!isdigit(number[i]))
            return false;
    }
    return true;
}

/*
 * Function: is_memzero
 * --------------------
 *  Return true if input pointer memory space is zero.
 */
bool is_memzero(void *ptr, size_t n){

    void *p;
    p = malloc(n);
    if(memcmp(p, ptr, n) == 0)
        return true;
    return false;
}

/*
 * Function: count_chars
 * ---------------------
 *  Return the number of occurrences of char c in string.
 */
int count_chars(char *str, char c){

    int count = 0;
    int i;
    for(i=0; i<strlen(str); i++){
        if(str[i] == c)
            count++;
    }
    return count;
}

/*
 * Function: count_precision
 * -------------------------
 *  Return the number of digits after decimal separator. Return -1 on error.
 */
int count_precision(char *str){

    char *tok;
    if(is_posdouble(str) == false){
        fprintf(stderr, "Count_precision: not a double!\n");
        return -1;
    }
    tok = strtok(str, ".");
    tok = strtok(NULL,".");
    return strlen(tok);
}
/*
 * Function: mkpath
 * ----------------
 *  Recursively create directories. Return -1 on error, 0 on success.
 */
int mkpath(char *file_path, mode_t mode){

    assert(file_path && *file_path);
    for (char* p = strchr(file_path + 1, '/'); p; p = strchr(p + 1, '/')) {
        *p = '\0';
        if (mkdir(file_path, mode) == -1) {
            if (errno != EEXIST) {
                *p = '/';
                return -1;
            }
        }
        *p = '/';
    }
    return 0;
}

/*
 * Function: fcpy
 * --------------
 *  Copy the contents of source file to dest file. Return copied char count.
 */
#define FCPY_VERBOSE 0
int fcpy(char *sourcefile, char *destfile){

    FILE *sourceFile;
    FILE *destFile;
    int  count = 0;
    char ch;
    if(access(destfile, F_OK) != -1){
        if(FCPY_VERBOSE > 0)
            fprintf(stderr, "fcpy: warning: %s already exists.\n",destfile);
    }
    sourceFile = fopen(sourcefile,"r");
    if(sourceFile == NULL){
        fprintf(stderr, "cannot open source file\n");
        return -1;
    }
    destFile = fopen(destfile,"w");
    if(destFile == NULL){
        fprintf(stderr, "cannot open dest file\n");
        return -1;
    }

    /* Copy file contents character by character. */
    while ((ch = fgetc(sourceFile)) != EOF)
    {
        fputc(ch, destFile);
        /* Increment character copied count */
        count++;
    }
    fclose(sourceFile);
    fclose(destFile);
    return count;
}

/*
 * Function: update_curstudy
 * -------------------------
 *  Update study_id in curstudy file
 */
int update_curstudy(struct gen_settings *gs, struct study *st){

    FILE *fp;
    char path[LPATH*2] = {0};
    snprintf(path, sizeof(path),"%s/%s%s",gs->workdir,DATA_DIR,CURSTUDY);
    fp = fopen(path, "w+");
    if(fp == NULL){
        perror("fopen");
        exit(1);
    }
    fprintf(fp,"# current study id\n");
    fprintf(fp, "%s\n",st->id);

    fclose(fp);
    return 0;
}

/*
 * Function: update_curpar
 * -----------------------
 *  Update sequence parameters in curpar file
 */
int update_curpar(struct gen_settings *gs, struct study *st){

    FILE *fp;
    char path[LPATH*2] = {0};
    int n = st->seqnum;
    snprintf(path, sizeof(path),"%s/%s%s",gs->workdir,DATA_DIR,CURPAR);
    fp = fopen(path, "w+");
    if(fp == NULL){
        perror("fopen");
        exit(1);
    }
    fprintf(fp,"# Study params: seqnum, sequence, events\n");
    fprintf(fp, "%d\n%s\n%s\n",n, st->sequence[n], st->event[n]);

    fclose(fp);
    return 0;
}

/*
 * Function: read_curpar
 * ---------------------
 *  Read the curpar file and fill the input args with the contents.
 *  Return 0 on success, -1 otherwise
 */
 
int read_curpar(struct gen_settings *gs, int *num, char *seq, char *event){

    FILE *fp;
    int count = 0;
    char path[LPATH * 2] = {0};
    size_t len = 0;
    ssize_t read;
    char *line = NULL;
    snprintf(path, sizeof(path), "%s/%s%s",gs->workdir, DATA_DIR, CURPAR);
    fp = fopen(path, "r");
    if(fp == NULL){
        fprintf(stderr, "fcpy: cannot open file %s\n",path);
        return -1;
    }
    while((read = getline(&line, &len, fp)) != -1){
        if(line[0] == '#'){
            // leave out the comments
            continue;
        } else {
            strtok(line, "\n");
            switch(count){
                case 0:
                    (*num) = atoi(line);
                    break;
                case 1:
                    strcpy(seq, line);
                    break;
                case 2:
                    strcpy(event, line);
                    break;

            }
            count++;
        }
    }
    return 0;
}

/*
 * Function: read_curstudy
 * ---------------------
 *  Read the curstudy file for the ID of the running study
 *  Return 0 on success, -1 otherwise
 */
int read_curstudy(struct gen_settings *gs, char *id){

    FILE *fp;
    char path[LPATH*2] = {0};
    size_t len = 0;
    ssize_t read;
    char *line = NULL;
    snprintf(path, sizeof(path), "%s/%s%s",gs->workdir, DATA_DIR, CURSTUDY);
    fp = fopen(path, "r");
    if(fp == NULL){
        fprintf(stderr, "fcpy: cannot open file %s\n",path);
        return -1;
    }
    while((read = getline(&line, &len, fp)) != -1){
        if(line[0] == '#'){
            // leave out the commnets
            continue;
        } else {
            strtok(line, "\n"); // remove newline
            strcpy(id, line);
        }
    }

    fclose(fp);
    return 0;
}

/*
 * Function: datahandler
 * ---------------------
 * Copies recorded data and logs to approprate directories.
 * Return -1 on error, 0 on success
 *
 * Files to manage, then clean on sequence end
 *  - blockstim.meta
 *  - blockstim.tsv
 *  - ttlctrl.meta
 *  - eventstim.meta
 *  - eventstim.tsv
 *  - curpar
 *
 * File to manage then clean on study end
 *  - analogdaq.meta
 *  - analogdaq.tsv
 *  - curstudy
 *  
 */
int datahandler(struct gen_settings *gs, char *action){

    //int n = study->seqnum;
    int i, ret, num;
    size_t l;
    struct stat s = {0};
    char studydir[LPATH*2] = {0};
    char seqdir[LPATH*2] = {0};
    char datadir[LPATH*2] = {0};
    char src[LPATH*2] = {0};
    char dst[LPATH*2] = {0};
    char seq[64];
    char id[64];
    char event[64];
    // sequence specific files
    char *filetocpy[] = {"blockstim.meta", "blockstim.tsv", "eventstim.tsv",
                            "ttlctrl.meta", "curpar", "eventstim.meta"};
    // study specific files, copy these to study dir as well
    char *sfiletocpy[] = {"analogdaq.meta", "analogdaq.tsv", "curstudy",
                            "study.tsv", "anesth.log"};
    // names for analog data slicing 
    char adaqtsvrel[] = "analogdaq.tsv";
    char adaqmetarel[] = "analogdaq.meta";
    char ttlctrlmrel[] = "ttlctrl.meta";
    char phystsvrel[] = "phys.tsv";
    // for slicing analog acquisition data
    char ttlctrlm[LPATH*2] = {0};
    char adaqtsv[LPATH*2] = {0};
    char adaqmeta[LPATH*2] = {0};
    char phystsv[LPATH*2] = {0};

    ret = read_curstudy(gs, id);
    if(ret < 0){
        return -1;
    }
    ret = read_curpar(gs, &num ,seq, event);
    if(ret < 0){
        return -1;
    }

    snprintf(studydir, sizeof(studydir),"%s/%s",gs->studies_dir, id);
    snprintf(seqdir, sizeof(seqdir),"%s/%s/%s",gs->studies_dir, id, seq);
    snprintf(datadir, sizeof(datadir),"%s/%s",gs->workdir, DATA_DIR);
    datadir[strlen(datadir)-1] = '\0';  // correction for consistency
    // these files are not ready, need to copy here first
    snprintf(adaqtsv, sizeof(adaqtsv),"%s/%s",studydir, adaqtsvrel);
    snprintf(adaqmeta, sizeof(adaqmeta),"%s/%s",studydir, adaqmetarel);
    snprintf(ttlctrlm, sizeof(ttlctrlm),"%s/%s",seqdir, ttlctrlmrel);
    snprintf(phystsv, sizeof(phystsv),"%s/%s",seqdir, phystsvrel);
    
    /*
    fprintf(stderr, "\n");
    fprintf(stderr, "study, %s seq %s\n",id, seq);
    fprintf(stderr, "%s\n",studydir);
    fprintf(stderr, "%s\n",datadir);
    fprintf(stderr, "%s\n",adaqtsv);
    fprintf(stderr, "%s\n",ttlctrlm);
    fprintf(stderr, "%s\n",phystsv);
    */

    //--------------------------------------------------------------
    // data management on mribg 'stop' request, usually from ttlctrl
    //--------------------------------------------------------------
    if(strcmp(action, "sequence_stop")==0){
        // make seqdir, to be sure
        
        memset(&s, 0, sizeof(s)); 
        if(stat(studydir, &s) == -1){
            mkdir(studydir, 0755);
        }
        memset(&s, 0, sizeof(s)); 
        if(stat(seqdir, &s) == -1){
            mkdir(seqdir, 0755);
        }
        
        //mkpath(seqdir, 0755);
        // copy blockstim, ttlctrl data and meta files
        for(l=0; l< sizeof(filetocpy) / sizeof(filetocpy[0]); l++){
            snprintf(src, sizeof(src), "%s/%s",datadir,filetocpy[l]);
            snprintf(dst, sizeof(dst), "%s/%s", seqdir, filetocpy[l]);
            if(access(src, F_OK) != -1){
                fcpy(src, dst);
            }
        }
        // copy study specific stuff
        for(l = 0; l < sizeof(sfiletocpy) / sizeof(sfiletocpy[0]); l++){
            snprintf(src, sizeof(src), "%s/%s",datadir,sfiletocpy[l]);
            snprintf(dst, sizeof(dst), "%s/%s", studydir, sfiletocpy[l]);
            if(access(src, F_OK) != -1){
                fcpy(src, dst);
            }
        }
        // create sequence specific analog data
        extract_analogdaq(adaqtsv, adaqmeta, ttlctrlm, phystsv);
        combine_all();

        // clean data dir, delete copied instances
        for(l=0; l< sizeof(filetocpy) / sizeof(filetocpy[0]); l++){
            snprintf(src, sizeof(src), "%s/%s",datadir,filetocpy[l]);
            //fprintf(stderr, "src %s\n",src);
            if(access(src, F_OK) != -1){
                remove(src);
            }
        }
        
        return 0;

    }
    //TODO
    //--------------------------------------------------------------
    // data management on mribg study 'stop' request
    //--------------------------------------------------------------
    else if(strcmp(action, "study_stop") == 0){
        ;
    } else {
        fprintf(stderr, "datahandler: unknown action\n");
        return -1;
    }

    return 0;
}

/*
 * Function: extract_analogdaq
 * ---------------------------
 *  Extract sequence specfic data from full study data of the analog daq.
 *  Make new file with similar layout to analogdaq.tsv, but only keep relevant
 *  data. The appropriate times are taken from ttlctrl.meta and analogdaq.meta.
 *  Return 0 on success, -1 on error.
 *  
 *  dest timastamp is the same as the ttlctrl timestamp
 *
 */
#define ZERO_INIT_TIME 1 // set 1 to make TIME start from zero in dest file
#define MAX_L_VAL 16
#define MAX_N_VAL 16
int extract_analogdaq(char *adaq,char *adaqmeta,char *ttlctrlmeta,char *dest){

    // for data extraction
    struct times *tt;
    struct times *at;
    double start_time, stop_time, diff_time;
    long int n_lines;
    double timestep;
    int count = -1;
    int first_count;
    int max_count;
    // dest file
    struct header *h;
    FILE *fp_dest;
    struct timeval tv;
    // for readline
    size_t len = 0;
    ssize_t read;
    char *line = NULL;
    char *tok;
    FILE *fp_adaq;

    // for zero_init_time
    char valstr[MAX_N_VAL][MAX_L_VAL] = {0}; 
    char *ltok;     // tokenizing
    double tval = 0;// time
    int n_val;      // number of columns
    int i;
    int precision; // decimal digits
    char linebuf[128] = {0};

    tt = malloc(sizeof(struct times));
    at = malloc(sizeof(struct times));
    h = malloc(sizeof(struct header));
    memset(tt, 0, sizeof(struct times));
    memset(at, 0, sizeof(struct times));
    memset(h, 0, sizeof(struct header));
    read_meta_times(tt, ttlctrlmeta);
    read_meta_times(at, adaqmeta);
    /*
    fprintf(stderr, "ttlctrl times\n");
    fprintf_times(stderr, tt);
    fprintf(stderr, "adaq times\n");
    fprintf_times(stderr, at);
    */
    n_lines = count_lines(adaq);
    start_time = getsecdiff(at->action, tt->action);
    stop_time = getsecdiff(at->action, tt->stop);
    //diff_time = stop_time - start_time;
    diff_time = getsecdiff(tt->action, tt->stop);
    /*
    fprintf(stderr, "start_time: %lf\n",start_time);
    fprintf(stderr, "stop_time: %lf\n",stop_time);
    fprintf(stderr, "lines: %ld\n",n_lines);
    */

    // prepare destination file
    fp_dest = fopen(dest, "w");
    if(fp_dest == NULL){
        fprintf(stderr, "extract_analogdaq: cannot open dest file: %s\n",dest);
        return -1;
    }
    extract_header_time(ttlctrlmeta, &tv);
    h->timestamp = tv;
    fprintf_common_header(fp_dest, h, 1, 0);
    // extract from analogdaq.tsv
    fp_adaq = fopen(adaq, "r");
    if(fp_adaq == NULL){
        fprintf(stderr, "Cannot open analogdaq.tsv file on path: %s\n",adaq);
        return -1;
    }
    int found_col_names=0; // set to 1 when the line TIME RESP etc is found
    int trycount=0;
    while((read = getline(&line, &len, fp_adaq)) != -1){
        // ignore comments and blank lines
        if(line[0] == '#' || line[0] == '\n' || line[0]==' ' || line[0]=='\t')
            continue;
        if(count_chars(line, '\n') == 0) // skip incomplete last line
            continue; 
        if(found_col_names == 0){
            if((strstr(line,"TIME")!=NULL) && strstr(line,"RESP")!=NULL){
                fprintf(fp_dest, "%s",line);
                // count number of columns: tab delimiters+1
                n_val = count_chars(line, '\t') + 1;
                found_col_names = 1;
                count = 0;
            } else if (trycount > 20){
                fprintf(stderr, "Cannot find column names\n");
                return -1;
            } else {
                trycount++;
            }

        } else {
            // found column names
            // TODO make this dynamic maybe to accomodate different schemes?
            // first instance of TIME data is the same as the timestep
            if(strncmp(line, "0.", 2) == 0 && count == 0){
                strcpy(linebuf, line);
                tok = strtok(linebuf, "\t");
                if(is_posdouble(tok)){
                    sscanf(tok, "%lf",&timestep);
                    precision = count_precision(tok);
                    first_count = (int) (start_time / timestep);
                    //max_count = (int) (stop_time / timestep);
                    max_count = (int) ((start_time + diff_time) / timestep);
                    count = 0;
                } else {
                    fprintf(stderr, "extract_analogdaq: cannot find timestep\n");
                    return -1;
                }
            }
            if(count < first_count && count != -1){
                count++;
            }
            // thesea re the lines to extract
            if(count >= first_count && count < max_count){
                if(ZERO_INIT_TIME == 1){
                    ltok = strtok(line, "\t");
                    strcpy(valstr[0], ltok); // this is the time, ignore
                    for(i=1; i<n_val; i++){
                        ltok = strtok(NULL, "\t");
                        strcpy(valstr[i], ltok);
                        // last one has \n at the end
                    }
                    //calc new time based on timestep and line count
                    tval = (double)(count-first_count) * timestep; 
                    fprintf(fp_dest, "%.*lf",precision,tval);
                    for(i=1; i<n_val; i++){
                        fprintf(fp_dest, "\t%s",valstr[i]);
                    }

                } else if(ZERO_INIT_TIME == 0){
                    fprintf(fp_dest, "%s", line);
                }
                count++;
            }
            if(count > max_count){
                break;
            }
        }
    }


    fclose(fp_adaq);
    fclose(fp_dest);
    free(tt);
    free(at);
    return 0;
} 

/*
 * Function: read_meta_times
 * -------------------------
 *  Parse timing information from meta files, and fill times struct. 
 *  Return 0 on success.
 */
int read_meta_times(struct times *t, char *filename){

    FILE *fp;
    size_t len = 0;
    ssize_t read;
    char *line = NULL;
    int count = -1;
    char *tok;
    struct timeval tv;
    fp = fopen(filename ,"r");
    if(fp == NULL){
        perror("fopen");
        return -1;
    }
    while((read = getline(&line, &len, fp)) != -1){
        if(strstr(line, "TIMING") != NULL) {
            count = 0; // found it
        } else if(count > -1) {
            tok = strtok(line, "=");
            tok = strtok(NULL, "=");
            memset(&tv, 0, sizeof(struct timeval));
            hr2timeval(&tv, tok);
            switch(count){
                case 0:
                    t->start = tv;
                    break;
                case 1:
                    t->action = tv;
                    break;
                case 2:
                    t->stop = tv;
                    break;

            }
            count++;
        } else {
            continue;
        }
    }
    fclose(fp);
    
    return 0;
}

/*
 * Function: read_study_tsv
 * ------------------------
 * Parse study.tsv file in study directory, and fill study struct.
 * Return 0 on success, -1 otherwise.
 */
//TODO already similar in func.c
int read_study_tsv(struct gen_settings *gs, char *id, struct study *st){

    char path[LPATH*2];
    size_t len = 0;
    ssize_t read;
    char *line = NULL;
    int count = -1;
    char *tok;
    struct timeval tv; // ?? 
    FILE *fp;

    snprintf(path, sizeof(path), "%s/%s/study.tsv",gs->studies_dir,id);
    fp = fopen(path, "r");
    if(fp == NULL){
        fprintf(stderr, "Cannot open file  %s\n",path);
        return -1;
    }
    while((read = getline(&line, &len, fp)) != -1){
        line = strtok(line, "\n");
        if(strncmp(line, "id=", 3) == 0){
            strtok(line, "=");
            tok = strtok(NULL, "=");
            strcpy(st->id,tok);
            continue;
        }
        if(strncmp(line, "seqnum", 6) == 0){
            ;
        }
    }
     
    fclose(fp);

    return 0;
}

/*
 * Function: extract_header_time
 * -----------------------------
 *  Read common heade timestamp and convert to timeval. Return -1 on error.
 */
int extract_header_time(char *path, struct timeval *tv){

    FILE *fp;
    size_t len = 0;
    ssize_t read;
    char *line = NULL;
    int count = -1;
    char *tok;
    char buf[64];
    fp = fopen(path, "r");
    if(fp == NULL){
        fprintf(stderr, "Cannot open file: '%s'\n",path);
        return -1;
    }
    while((read = getline(&line, &len, fp)) != -1){
        if(strstr(line, "timestamp") != NULL){
            tok = strtok(line, "=");
            tok = strtok(NULL, "=");
            strcpy(buf, tok);
            hr2timeval(tv,buf);
            break;
        }
    }
    fclose(fp);
    return 0;
}

/*
 * Function: hr2timeval
 * -----------------
 *  Convert human readable timestring to timeval, return 0 on success.
 *
 *  example format of input string: 2020-06-16 20:13:28.974153
 */
int hr2timeval(struct timeval *tv, char *hrtimestr){

    struct tm tmvar;;
    time_t timevar;
    char buf[64] = {0};
    char *tok;
    char day[16];
    char date[16];
    char usec[16];

    memset(tv, 0, sizeof(struct timeval));
    //fprintf(stderr, "timestr %s\n",hrtimestr);
    strcpy(buf, hrtimestr);
    // cut date-day-usec
    tok = strtok(buf, " ");
    strcpy(date, tok);
    tok = strtok(NULL, " ");
    strcpy(buf, tok);
    tok = strtok(buf, ".");
    strcpy(day, tok);
    tok = strtok(NULL, ".");
    strcpy(usec, tok);
    // cut date
    tok = strtok(date, "-");
    tmvar.tm_year = atoi(tok);
    tmvar.tm_year -= 1900;
    tok = strtok(NULL, "-");
    tmvar.tm_mon = atoi(tok);
    tmvar.tm_mon--;
    tok = strtok(NULL, "-");
    tmvar.tm_mday = atoi(tok);
    // cut day
    tok = strtok(day, ":");
    tmvar.tm_hour = atoi(tok);
    tok = strtok(NULL, ":");
    tmvar.tm_min = atoi(tok);
    tok = strtok(NULL, ":");
    tmvar.tm_sec = atoi(tok);
    timevar = mktime(&tmvar);
    tv->tv_usec = atoi(usec);
    tv->tv_sec = timevar;
    return 0;

}


int combine_all(){
    return 0;
}

