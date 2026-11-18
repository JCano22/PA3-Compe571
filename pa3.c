#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_FREQS 4 //number of cpu frequency settings
#define NUM_TASKS 5 //max tasks the task array can hold
static const int FREQS[NUM_FREQS] = {1188, 918, 648, 384}; //MHz for each freq index, 0 = fastest

//struct to hold info for each task
typedef struct{
    char taskName[16];
    int period; //period = deadline
    int wcet[NUM_FREQS]; //wcet at each freq index
    int freqIndex; //freq this task runs at (0-3)
    int remainingTime; //work left on current job
    int deadline; //absolute deadline of current job
}Task;

//struct to hold header info
typedef struct{
    int numTasks;
    int simLength; //how long the system runs (s)
    int activePwr[NUM_FREQS]; //active power (mW) at each freq index
    int idlePwr; //idle power (mW) at lowest freq
    Task task[NUM_TASKS];
}System;


//reads the input file into sys, returns 0 on success and -1 on error
static int parseInput(const char *file, System *sys){
    FILE *f = fopen(file, "r");
    if(!f){
        perror("fopen");
        return -1;
    }

    //read header line, 7 numbers
    if(fscanf(f, "%d %d %d %d %d %d %d", &sys->numTasks, &sys->simLength, &sys->activePwr[0], &sys->activePwr[1], &sys->activePwr[2], &sys->activePwr[3], &sys->idlePwr) != 7){
        fprintf(stderr, "bad header line\n");
        fclose(f);
        return -1;
    }

    //make sure the task count from the file fits in the task array
    if(sys->numTasks < 1 || sys->numTasks > NUM_TASKS){
        fprintf(stderr, "number of tasks must be between 1 and %d (file says %d)\n", NUM_TASKS, sys->numTasks);
        fclose(f);
        return -1;
    }

    //read one task line per task: name, period, 4 wcets
    for(int i = 0; i < sys->numTasks; i++){
        Task *t = &sys->task[i]; //pointer to the task being filled

        if(fscanf(f, "%15s %d %d %d %d %d", t->taskName, &t->period, &t->wcet[0], &t->wcet[1], &t->wcet[2], &t->wcet[3]) != 6){
            fprintf(stderr, "error: could not read task %d (line %d of %s): expected name, period and %d WCETs\n", i + 1, i + 2, file, NUM_FREQS);
            fclose(f);
            return -1;
        }
        //start at max freq with no job released yet
        t->freqIndex = 0;
        t->remainingTime = 0;
        t->deadline = 0;

    }
    fclose(f);
    return 0;
}


int main(int argc, char *argv[]){
    int isEDF, isRM, isEE;
    System sys; //holds everything read from the input file
    const char* file;
    //need 3 or 4 args: program, file, policy, optional EE
    if(argc < 3 || argc > 4){
        fprintf(stderr, "Usage %s <arg1> <arg2>\n", argv[0]);
        return EXIT_FAILURE;
    }

    //setting variables to know which scheduling type to use
    isEDF = strcmp(argv[2], "EDF") == 0;
    isRM = strcmp(argv[2], "RM") == 0;
    file = argv[1]; //pointer to file

    //policy has to be EDF or RM
    if(!isEDF && !isRM){
        fprintf(stderr, "Need parameters.\n");
        return EXIT_FAILURE;
    }

    //4th arg is only allowed to be EE
    if(argc >=4 && strcmp(argv[3], "EE") != 0){
        fprintf(stderr, "Wrong EE argument\n");
        return EXIT_FAILURE;
    }
    else
        isEE = (argc >= 4); //EE is on if there is a valid 4th arg

    //read the input file, quit if it fails
    if(parseInput(file, &sys) != 0){
        perror("parseInput");
        return EXIT_FAILURE;
    }

    //TEMPORARY: print parsed input to check it against the file (remove later)
    printf("tasks=%d simLength=%d activePwr=%d %d %d %d idlePwr=%d\n",
           sys.numTasks, sys.simLength, sys.activePwr[0], sys.activePwr[1],
           sys.activePwr[2], sys.activePwr[3], sys.idlePwr);
    for(int i = 0; i < sys.numTasks; i++){
        Task *t = &sys.task[i];
        printf("  %s period=%d wcet=%d %d %d %d freqIndex=%d (%d MHz)\n",
               t->taskName, t->period, t->wcet[0], t->wcet[1], t->wcet[2],
               t->wcet[3], t->freqIndex, FREQS[t->freqIndex]);
    }
    //END TEMPORARY


    return 0;

}
