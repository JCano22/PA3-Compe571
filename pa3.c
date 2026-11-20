#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_FREQS 4 //number of cpu frequency settings
#define NUM_TASKS 5 //max tasks the task array can hold
#define IDLE -1 //idle value
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
    if(fscanf(f, "%d %d %d %d %d %d %d", 
        &sys->numTasks, &sys->simLength, 
        &sys->activePwr[0], &sys->activePwr[1], 
        &sys->activePwr[2], &sys->activePwr[3], 
        &sys->idlePwr) != 7){
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

        if(fscanf(f, "%15s %d %d %d %d %d", 
            t->taskName, &t->period, 
            &t->wcet[0], &t->wcet[1], 
            &t->wcet[2], &t->wcet[3]) != 6){
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

//function to decide which task gets to run next
int pickNext(System* sys, int isEDF){
    int bsf = IDLE; //best so far variable
    
    for(int i = 0; i < sys->numTasks; i++){
        Task *t = &sys->task[i];

        if(t->remainingTime == 0) 
            continue; //not ready, no work left on its job

        if(bsf == IDLE){ //first ready task found, nothing to compare to yet
            bsf = i;
            continue;
        }

        Task *best = &sys->task[bsf]; //current best task
        if(isEDF){
            if(t->deadline < best->deadline) 
                bsf = i; //EDF: earlier deadline wins, ties keep the first task
        }
        else{
            if(t->period < best->period) 
                bsf = i; //RM: shorter period wins, ties keep the first task
        }
    }
    return bsf; //index of task to run, or IDLE if nothing is ready
}

//prints one schedule line: start, task, freq, length, energy, and returns the energy
double printSegment(System* sys, int start, int who, int length){
    double energy; //energy used by this segment in joules

    if(who == IDLE){
        energy = sys->idlePwr * length / 1000.0; //mW * s / 1000 = J
        printf("%d IDLE IDLE %d %.3fJ\n", start + 1, length, energy); //+1 to match the 1-indexed sample output
    }
    else{
        Task *t = &sys->task[who]; //task that ran in this segment
        energy = sys->activePwr[t->freqIndex] * length / 1000.0; //power at this task's freq
        printf("%d %s %d %d %.3fJ\n", start + 1, t->taskName, FREQS[t->freqIndex], length, energy);
    }
    return energy; //so main can add it to the total
}

//checking feasibility - if given task can meet every deadline with the freq
int isFeasible(System* sys, int isEDF){
    double u = 0; //total utilization

    for(int i = 0; i < sys->numTasks; i++){
        Task *t = &sys->task[i];
        u += (double)t->wcet[t->freqIndex] / t->period; //wcet at its freq / period
    }

    if(isEDF)
        return u <= 1.0 + 1e-9; //EDF is U <= 1, tiny margin for rounding

    //RM
    for(int i = 0; i < sys->numTasks; i++){
        Task *ti = &sys->task[i]; //task being checked
        int c = ti->wcet[ti->freqIndex]; //its execution time
        int r = c; //response time guess, starts at its own wcet
        int prev = 0; //response time from the last round

        while(r != prev && r <= ti->period){ //repeat until r stops changing or passes the deadline
            prev = r;
            r = c;
            for(int j = 0; j < sys->numTasks; j++){
                Task *tj = &sys->task[j];
                //higher priority = shorter period, ties go to the task listed first (same as pickNext)
                if(tj->period < ti->period || (tj->period == ti->period && j < i)){
                    int releases = (prev + tj->period - 1) / tj->period; //ceiling of prev / period
                    r += releases * tj->wcet[tj->freqIndex]; //each release of tj preempts ti once
                }
            }
        }
        if(r > ti->period)
            return 0; //this task finishes after its deadline
    }
    return 1; //every task finished in time
}

//runs the schedule without printing, returns the total energy (J)
//sys is passed as a copy so the real tasks are not changed
double simulateEnergy(System sys, int isEDF){
    double energy = 0;

    for(int k = 0; k < sys.simLength; k++){
        //release new jobs, same as the main loop
        for(int i = 0; i < sys.numTasks; i++){
            Task *t = &sys.task[i];
            if(k % t->period == 0){
                t->remainingTime = t->wcet[t->freqIndex];
                t->deadline = t->period + k;
            }
        }

        int taskToRun = pickNext(&sys, isEDF);
        if(taskToRun == IDLE)
            energy += sys.idlePwr / 1000.0; //1 second of idle power
        else{
            energy += sys.activePwr[sys.task[taskToRun].freqIndex] / 1000.0; //1 second at this task's freq
            sys.task[taskToRun].remainingTime--;
        }
    }
    return energy;
}

//step 6: EE, tries every freq combination and keeps the feasible one with the lowest energy
void assignFrequencies(System* sys, int isEDF){
    int combos = 1; //number of freq combinations, NUM_FREQS ^ numTasks
    for(int i = 0; i < sys->numTasks; i++)
        combos *= NUM_FREQS;

    int bestCombo = -1; //combination with the lowest energy so far, -1 = none found yet
    double bestEnergy = 0;

    for(int c = 0; c < combos; c++){
        //turn combo number c into one freq index per task (c written in base 4)
        int digits = c;
        for(int i = 0; i < sys->numTasks; i++){
            sys->task[i].freqIndex = digits % NUM_FREQS;
            digits /= NUM_FREQS;
        }

        if(!isFeasible(sys, isEDF))
            continue; //would miss a deadline, skip it

        double energy = simulateEnergy(*sys, isEDF); //includes idle energy, so slower only wins if it really saves energy
        if(bestCombo == -1 || energy < bestEnergy){
            bestCombo = c;
            bestEnergy = energy;
        }
    }

    //set the tasks to the best combination found
    if(bestCombo == -1){
        fprintf(stderr, "EE: no feasible frequencies found, running everything at max freq\n");
        bestCombo = 0; //combo 0 = every task at index 0
    }
    int digits = bestCombo;
    for(int i = 0; i < sys->numTasks; i++){
        sys->task[i].freqIndex = digits % NUM_FREQS;
        digits /= NUM_FREQS;
    }
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

    //EE: pick the lowest energy freq for each task
    if(isEE)
        assignFrequencies(&sys, isEDF);

    //warn if the tasks can't all meet their deadlines, still run so the misses show up
    if(!isFeasible(&sys, isEDF))
        fprintf(stderr, "WARNING: task set is not schedulable under %s\n", isEDF ? "EDF" : "RM");

    int k; //loop variable
    int misses = 0; //counter for misses
    double totalEnergy = 0; //energy of all segments added up (J)
    int idleTime = 0; //total seconds spent idle
    int start = 0; //time the segment begins
    int who = IDLE; // task index running it, or IDLE
    int length = 0; //how many seconds it has lasted so far


    for(k = 0; k < sys.simLength; k++){
        //Release new jobs whose period starts at k
        for(int i=0; i < sys.numTasks; i++){
            Task *t = &sys.task[i]; //pointer to the task being checked

            if(k % t->period == 0){ //task starts a new period at k
                if(t->remainingTime > 0){ //old job still had work left at its deadline
                    printf("Missed deadline task %s at t=%d\n", t->taskName, k);
                    misses++;
                }
                t->remainingTime = t->wcet[t->freqIndex]; //new job needs wcet at this task's freq
                t->deadline = t->period + k; //new job must finish by the next release
            }

        }

        //pick which ready job runs this second, or idle
        int taskToRun = pickNext(&sys, isEDF);
        //Run it for one second, reducing its remaining work by 1
        if(taskToRun != IDLE){
            sys.task[taskToRun].remainingTime--; //decreasing remaining time of task by 1second
        }
        //Record what ran, so consecutive seconds of the same task merge into one output line
        if(taskToRun == who){
            length++;
        }
        else{
            if(length > 0){ //only print if the old segment actually ran
                totalEnergy += printSegment(&sys, start, who, length);
                if(who == IDLE)
                    idleTime += length; //count idle seconds for the idle %
            }
            //start a new segment with whoever runs this second
            start = k;
            who = taskToRun;
            length = 1;
        }

    }
    if(length > 0){ //print the last segment, the loop ends before it gets printed
        totalEnergy += printSegment(&sys, start, who, length);
        if(who == IDLE)
            idleTime += length;
    }

    //summary
    printf("\nTotal energy: %.3fJ\n", totalEnergy);
    printf("Idle time: %.2f%%\n", idleTime * 100.0 / sys.simLength); //100.0 avoids integer division
    printf("Total execution time: %ds\n", sys.simLength);
    printf("Deadline misses: %d\n", misses);

    return 0;

}
