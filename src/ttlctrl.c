/*
 * ttlctrl.c
 *
 * Mricom subprogram to control timing between stimulus and sequence start.
 * 
 * OPTION 1:
 * ttlctrl does a handshake with the console. This consists of the following:
 * 1.
 * ttlctrl waits TTL high from console. This is accomplished by spX_on in
 * pulse sequence.
 * 2. after the TTL high is sent by the console, it waits for a TTL high from
 * ttlctrl. This is accomplished by xgate(1) in the pulse sequence.
 * 3. Console 
 *                ____       ___
 * ttlctrl:  ____|    |_____|   |____
 *               _____
 * console:  ___|
 * 
 * TODO not implemented!
 * OPTION 2:
 * TTL handshake: ttlctrl waits for TTL input from console, if received
 * sends TTL back. Pulse sequence is started when TTL response is received
 *
 * TODO use the 3bit usr input for error handling?
 * Input is a single integer between 0-7, which is challenged against the
 * 3 bit input coming from the console.
 *
 * Timing information is saved in ttlctrl.meta
 *  start time is program launch time (almost)
 *  action time is the time of sequence start TTL signal
 *  stop time is sequence finish time
 *
 */


#include "ttlctrl.h"

#define TESTING 0
#define VERBOSE 0

#define OPTION 1

//TODO as well
#define MPIDLOG 0 // save instance in mpid.log
#define LOGGING 1 // save TTl actions with timestamps to a logfile

int main(int argc, char **argv){

    struct gen_settings *gs;
    struct dev_settings *ds;
    struct mpid *mp;

    comedi_t *dev;
    char mricomdir[LPATH];
    int subdev, outchan, outchan2, inchan;
    int usrchan[N_USER_BITS];
    int i, ret;
    int waitbits; // console start confir signal int between 0-2**N_USER_BITS
    int ttl_w = 30; // width of TTL signal
    int wait_for_handshake;

    // metafile,saving timings and whatnot
    char metafile[LPATH] = {0};
    char metaf_name[] = "ttlctrl.meta";
    FILE *fp;
    struct times *t;
    struct timeval tv; // use gettimeofday
    // struct timespec tvs;
    struct header *h;

    // check env
    if(getenv("MRICOMDIR") == NULL){
        fprintf(stderr, "ttlctrl: no MRICOMDIR in environment\n");
        exit(1);
    }
    signal(SIGINT, sighandler);

    if(argc == 1){
        wait_for_handshake = 1;

    } else if(argc == 2) {
        // /this is not implemented/
        wait_for_handshake = 0;
        waitbits = atoi(argv[1]);
        if(waitbits < 0 || waitbits > (int)pow(2,N_USER_BITS)){
            fprintf(stderr,"ttlctrl: incorrect input\n");
            exit(1);
        }
    } else {
        // allow wrong aruments only for testing purposes
        if(TESTING != 1){
            fprintf(stderr,"ttlctrl: incorrect input\n");
            exit(1);
        }
    }

    strcpy(mricomdir, getenv("MRICOMDIR"));

    gs = malloc(sizeof(struct gen_settings));
    ds = malloc(sizeof(struct dev_settings));
    mp = malloc(sizeof(struct mpid));
    t = malloc(sizeof(struct times));
    h = malloc(sizeof(struct header));
    memset(gs, 0, sizeof(struct gen_settings));
    memset(ds, 0, sizeof(struct dev_settings));
    memset(mp, 0, sizeof(struct mpid));
    memset(t, 0, sizeof(struct times));
    memset(h, 0, sizeof(struct header));

    parse_gen_settings(gs);
    parse_dev_settings(ds);
    fill_mpid(mp);
    processctrl_add(gs->mpid_file, mp, "START");

    subdev = ds->ttlctrl_subdev;
    inchan  = ds->ttlctrl_console_in_chan;
    outchan = ds->ttlctrl_out_chan; 
    outchan2 = ds->ttlctrl_console_out_chan; 
    //outchan2 = ds->ttlctrl_usr_chan[0];

    // start timestamp
    gettimeofday(&tv, NULL);
    t->start = tv;

    // prepare header
    snprintf(metafile, sizeof(metafile),"%s/%s%s",
            mricomdir,DATA_DIR,metaf_name);
    h->timestamp = tv;
    strcpy(h->proc, argv[0]);
    fp = fopen(metafile, "w");
    if(fp == NULL){
        fprintf(stderr, "ttlctrl: cannot open %s\n",metafile);
        exit(1);
    }
    fprintf_common_header(fp, h, argc, argv);
    fclose(fp);
    fprintf_meta_times(metafile, t, "start");
    /*
    for(i=0;i<N_USER_BITS;i++){
        usrchan[i] = ds->ttlctrl_usr_chan[i];
    }
    */
    // print settings
    //
    if(VERBOSE > 0){
        fprintf(stderr, "subdev=%d\n",subdev);
        fprintf(stderr, "inchan=%d\n",inchan);
        fprintf(stderr, "outchan=%d\n",outchan);
        fprintf(stderr, "outchan2=%d\n",outchan2);
        for(i=0; i < N_USER_BITS; i++){
            fprintf(stderr, "usrchan[%d]=%d\n",i,usrchan[i]);
        }

    }

    // channel setup
    dev = comedi_open(ds->devpath);
    if(dev == NULL){
        comedi_perror("comedi_open");
        exit(1);
    }

    //init channels
    comedi_dio_config(dev, subdev, outchan, COMEDI_OUTPUT);
    comedi_dio_config(dev, subdev, outchan2, COMEDI_OUTPUT);
    comedi_dio_config(dev, subdev, inchan, COMEDI_INPUT);
    comedi_dio_write(dev, subdev, outchan , 0);
    comedi_dio_write(dev, subdev, outchan2 , 0);

    //----------------------------------------------
    // in testing case only ask for user getchar
    if(TESTING == 1){
        printf("TESTING=1; press ENTER\n");
        getchar();
        comedi_dio_write(dev, subdev, outchan , 1);
        usleep(ttl_w);
        comedi_dio_write(dev, subdev, outchan , 0);
        free(gs);
        free(ds);
        return 0;
    }
    //----------------------------------------------

    // wait for confirmation bits from console
    // wait either for user bits or ttl signal
    //ret = wait_user_bits(dev, subdev, usrchan, waitbits);
    //ret = wait_console_ttl(dev, subdev, inchan);
    if(OPTION == 1)
        ret = wait_console_handshake(dev, subdev, inchan, outchan2);

    if(ret == 0){
        // send TTL signal both to console and blockstim
        // These go on 2 different DIO channels
        if(VERBOSE > 0)
            fprintf(stderr, "sending launch TTL\n");
        // wait for console TTL on
        ret = wait_console_ready_signal(dev, subdev, inchan);
        comedi_dio_write(dev, subdev, outchan , 1);
        comedi_dio_write(dev, subdev, outchan2 , 1);
        usleep(ttl_w);
        comedi_dio_write(dev, subdev, outchan , 0);
        comedi_dio_write(dev, subdev, outchan2 , 0);
        gettimeofday(&tv,NULL);
        t->action = tv;
        fprintf_meta_times(metafile, t, "action");

    } else if(ret == -1){
        fprintf(stderr, "ttlctrl: console timeout error\n");
    } else {
        //TODO
        fprintf(stderr, "unknown err\n");
        exit(1);
    }
    
    // wait pulsesequence finish signal input from console
    // TODO maybe use a different input??
    ret = wait_console_end_signal(dev, subdev, inchan);
    ret = send_console_end_signal(dev, subdev, outchan);
    
    // finish up metafile
    gettimeofday(&tv, NULL);
    t->stop = tv;
    fprintf_meta_times(metafile, t, "stop");
    // mribg starts handling sequence specific data files
    // send message to mribg
    ret = send_mribg("ttlctrl,stop");
    if(ret < 0){
        fprintf(stderr, "'ttlctrl,stop' rejected by mribg\n");
    }
    processctrl_add(gs->mpid_file, mp, "STOP");
    free(gs);
    free(ds);
    free(mp);
    return 0;
}

/*
 * Function: wait_user_bits
 * ------------------------
 *  Check 3 bit input from console, return 0 on success, -1 on timeout.
 */
#define TIMEOUT_SEC 30
int wait_user_bits(comedi_t *dev, int subdev, int chan[N_USER_BITS], int num){

    int i;
    int bit[N_USER_BITS];
    int result;
    struct timeval tv1, tv2;
    gettimeofday(&tv1,NULL);
    while(1){
        result = 0;
        gettimeofday(&tv2,NULL);
        for(i=0;i<N_USER_BITS;i++){
            comedi_dio_read(dev, subdev, chan[i], &bit[i]);
            result += (int)pow(2,i) * bit[i];
        }
        if(result == num){
            break;
        }
        if((tv2.tv_sec - tv1.tv_sec) > TIMEOUT_SEC){
            return -1;
        }
        usleep(5);

    }
    return 0;
}

/*
 * Function: wait_console_ready_signal
 * ---------------------------------
 *  Wait for TTL high from console input. Does not include timeout
 */
int wait_console_ready_signal(comedi_t *dev, int subdev, int inchan){

    int bit = 0;
    while(bit != 1){
        comedi_dio_read(dev, subdev, inchan, &bit);
    }
    return 0;
}
/*
 * Function: wait_console_end_signal
 * ---------------------------------
 */
int wait_console_end_signal(comedi_t *dev, int subdev, int inchan){

    int bit = 0;
    while(bit != 1){
        comedi_dio_read(dev, subdev, inchan, &bit);
    }
    return 0;
}
/*
 * Function: send_console_end_signal
 * ---------------------------------
 */
int send_console_end_signal(comedi_t *dev, int subdev, int outchan){

    comedi_dio_write(dev, subdev, outchan, 1);
    usleep(10);
    comedi_dio_write(dev, subdev, outchan, 0);
    return 0;
}

/*
 * Function: wait_console_ttl
 * --------------------------
 *  Wait for TTL singal on one channel, return 0 on success, -1 on timeout.
 */
int wait_console_ttl(comedi_t *dev, int subdev, int chan){

    int bit;
    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);
    while(1){
        gettimeofday(&tv2, NULL);
        comedi_dio_read(dev, subdev, chan, &bit);
        if(bit == 1){
            return 0;
        }
        if((tv2.tv_sec - tv1.tv_sec) > TIMEOUT_SEC){
            return -1;
        }
    }
}

#define HANDSHAKE_TIMEOUT_SEC 5
/*
 * Function: waint_console_handshake
 * ---------------------------------
 *  Do a 3-way handshake with console, by TTL-signaling. Return 0 when done, or
 *  -1 on timeout.
 *
 * Wait for TTL from console, and send TTL back if received. On the console end
 * this is accomplished in the pulsesequence by xgate, and spX_on/spX_off
 * pulse sequence statements. A special parameter as argument for xgate should
 * be created as well.
 */

int wait_console_handshake(comedi_t *dev, int subdev, int inchan, int outchan){

    int ret, bit;
    int usec = 5; // sent TTL width in microsec
    int usec_wait = 10;
    struct timeval tv1, tv2, tv3;
    gettimeofday(&tv1, NULL);
    // wait for first TTL in
    if(VERBOSE > 1)
        fprintf(stderr, "waiting for input...\n");
    while(1){
        ret = 0;
        gettimeofday(&tv2, NULL);
        comedi_dio_read(dev, subdev, inchan, &bit);
        if(bit == 1){
            // fisrst TTL in send one back
            gettimeofday(&tv2, NULL);
            if(VERBOSE > 1)
                fprintf(stderr, "TTL input on ch %d\n",inchan);
            comedi_dio_write(dev, subdev, outchan, 1);
            usleep(usec);
            comedi_dio_write(dev, subdev, outchan, 0);
            if(VERBOSE > 1)
                fprintf(stderr, "TTL output on ch %d\n",outchan);
            // wait till console reacts
            usleep(usec_wait);
            //wait for TTL response from console
            while(1){
                gettimeofday(&tv3, NULL);
                comedi_dio_read(dev, subdev, inchan, &bit);
                if(bit == 1){
                    if(VERBOSE > 1)
                        fprintf(stderr, "TTL input on ch %d\n",inchan);
                    return 0;
                }
                if((tv3.tv_sec - tv2.tv_sec) > HANDSHAKE_TIMEOUT_SEC){
                    return -1;
                }
            }
            return 0;
        }
        if((tv2.tv_sec - tv1.tv_sec) > TIMEOUT_SEC){
            return -1;
        }
    }
}

