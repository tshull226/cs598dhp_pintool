
/*! @file
 *  This is an example of the PIN tool that demonstrates some basic PIN APIs 
 *  and could serve as the starting point for developing your first PIN tool
 */

#include "pin.H"
//#include "control_manager.H"
#include "portability.H"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <stack>
#include <vector>
#include <algorithm>
#include <string.h>
#include <stdlib.h>
#include "cache.H"

/* ================================================================== */
// Global variables 
/* ================================================================== */



//I want this near the bottom so i have to declare it up here...
VOID failurePrintout(const char *message);


static std::ostream * out = &cerr;
static PIN_LOCK lock;
static int NumArgs = 0;
static char **Args;

// used for keeping track of the state
static BOOL withinGC = false;
//static BOOL withinGC = true;

static THREADID gcThreadid = 0;
static BOOL somethingFailed = false;




/* ===================================================================== */
// Helper Classes
/* ===================================================================== */

typedef enum{
    LOAD_OP=0,
    STORE_OP,
    INSTRUCTION_OP
}mem_operations;

struct MEM_INFO{
    ADDRINT address;
    mem_operations mem_op_type;
    UINT32 access_size;
    UINT64 cycle_num;
    MEM_INFO(ADDRINT addr, mem_operations mem_op, UINT32 size, UINT64 cycle = 0): address(addr), mem_op_type(mem_op), 
        access_size(size), cycle_num(cycle) {}
    MEM_INFO(): address(0), mem_op_type(LOAD_OP), access_size(0), cycle_num(0) {}

};

#define LOG_SIZE 1000000
//size in bytes of a dram access
#define ACCESS_SIZE 8
static UINT64 arrayOffset = 0;
static MEM_INFO memValues[LOG_SIZE];

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,  "pintool",
        "o", "", "specify file name for AtomicRegion output");



/* ===================================================================== */
// Utilities
/* ===================================================================== */

VOID printArguments(){
    cerr << "Arguments for said program\n";
    for(int i = 0; i < NumArgs; i++){
        cerr << Args[i] << endl;
    }
    cerr << endl;
}



/*!
 *  Print out help message.
 */
INT32 Usage()
{
    cerr << "This tool prints out the number of dynamically executed " << endl <<
        "instructions, basic blocks and threads in the application." << endl << endl;

    cerr << KNOB_BASE::StringKnobSummary() << endl;

    return -1;
}

VOID initializeVariables(){
    //don't actually have to do this...
}

inline ADDRINT mask(ADDRINT ea)  {
    const ADDRINT mask = ~static_cast<ADDRINT>(ACCESS_SIZE-1);
    return ea & mask;
}

const char * memOpToString(mem_operations mem_op){
    switch(mem_op){
        case LOAD_OP:
            return "READ   ";
            break;
        case STORE_OP:
            return "WRITE  ";
            break;
        case INSTRUCTION_OP:
            return "IFETCH ";
            break;
        default:
            cerr << "didn't recognize mem op type: " << mem_op << endl;
            ASSERTX(false && "couldn't recognize mem op type");
    }
    return "";
}

VOID writeOutMemLog(){
    for(UINT64 i = 0; i < arrayOffset; i++){
        MEM_INFO &data = memValues[i];
        //splitting it up into as many addresses as necessary
        ADDRINT start = mask(data.address);
        ADDRINT end   = mask(data.address + data.access_size - 1);
        for(ADDRINT addr = start ; addr <= end ; addr += ACCESS_SIZE) {
            //printing here
            const char * access_type = memOpToString(data.mem_op_type);
            *out << "0x" << std::hex << std::uppercase << addr << " " << access_type << std::nouppercase << std::dec << data.cycle_num << endl;
        }
    }
}

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

/*!
 * Increase counter of the executed basic blocks and instructions.
 * This function is called for every basic block when it is about to be executed.
 * @param[in]   numInstInBbl    number of instructions in the basic block
 * @note use atomic operations for multi-threaded applications
 */

// This function is called before every block
//
//
VOID PIN_FAST_ANALYSIS_CALL record_mem(THREADID threadid, ADDRINT memea, UINT32 length, UINT32 mem_type) {
    if(threadid != gcThreadid){
        return;
    }
    memValues[arrayOffset++] = MEM_INFO(memea, (mem_operations)mem_type, length);
    if(arrayOffset == LOG_SIZE){
        cerr << "this happened" << endl;
        //at this point need to write it out
        writeOutMemLog();
        arrayOffset = 0;
    }
}


/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

/*!
 * Insert call to the CountBbl() analysis routine before every basic block 
 * of the trace.
 * This function is called every time a new trace is encountered.
 * @param[in]   trace    trace to be instrumented
 * @param[in]   v        value specified by the tool in the TRACE_AddInstrumentFunction
 *                       function call
 */
VOID Trace(TRACE trace, VOID *v)
{
    //do not need to do anything when not taking count
    if(!withinGC){
        return;
    }
    // Visit every basic block in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)){
            // instrument the load(s)
            if (INS_IsMemoryRead(ins)) {
                INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) record_mem,
                        IARG_FAST_ANALYSIS_CALL,
                        IARG_THREAD_ID,
                        IARG_MEMORYREAD_EA,
                        IARG_MEMORYREAD_SIZE,
                        IARG_UINT32, LOAD_OP,
                        IARG_END);

            }
            if (INS_HasMemoryRead2(ins)) {
                INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) record_mem,
                        IARG_FAST_ANALYSIS_CALL,
                        IARG_THREAD_ID,
                        IARG_MEMORYREAD2_EA,
                        IARG_MEMORYREAD_SIZE,
                        IARG_UINT32, LOAD_OP,
                        IARG_END);

            }
            // instrument the store
            if (INS_IsMemoryWrite(ins)) {
                INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) record_mem,
                        IARG_FAST_ANALYSIS_CALL,
                        IARG_THREAD_ID,
                        IARG_MEMORYWRITE_EA,
                        IARG_MEMORYWRITE_SIZE,
                        IARG_UINT32, STORE_OP,
                        IARG_END);

            }
        }
    }
}


/*!
 * Increase counter of threads in the application.
 * This function is called for every thread created by the application when it is
 * about to start running (including the root thread).
 * @param[in]   threadIndex     ID assigned by PIN to the new thread
 * @param[in]   ctxt            initial register state for the new thread
 * @param[in]   flags           thread creation flags (OS specific)
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddThreadStartFunction function call
 */


VOID CallSimulationBegin(THREADID threadid){
    gcThreadid = threadid;
    withinGC = true;
    PIN_RemoveInstrumentation();
}

VOID CallSimulationEnd(THREADID threadid){
    withinGC = false;
    PIN_RemoveInstrumentation();
}

VOID CallSimulationExit(THREADID threadid){
}

VOID Routine(RTN rtn, VOID* v)
{
    RTN_Open(rtn);
    if (RTN_Name(rtn).find("pin_simulation_begin") != string::npos)
    {
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)CallSimulationBegin, IARG_THREAD_ID, IARG_END);
    }
    else if (RTN_Name(rtn).find("pin_simulation_end") != string::npos)
    {
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)CallSimulationEnd, IARG_THREAD_ID, IARG_END);
    }
    else if (RTN_Name(rtn).find("pin_simulation_exit") != string::npos)
    {
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)CallSimulationExit, IARG_THREAD_ID, IARG_END);
    }

    RTN_Close(rtn);
}


//printing this out when a failure occurs
VOID failurePrintout(const char *message){
    *out << message << endl;
    cerr << message << endl;
    somethingFailed = true;
    ASSERTX(false && "");
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID *v)
{
    if(somethingFailed){
        *out << "something failed so I'm not printing out any results" << endl;
        cerr << "something failed so I'm not printing out any results" << endl;
    }
    writeOutMemLog();
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid 
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }
    PIN_InitSymbols();

    //copying arguments to heap
    NumArgs = argc;
    Args = (char **) malloc(sizeof(char *) * argc);
    for(int i = 0; i < argc; i++){
        char * arg = argv[i];
        Args[i] = (char *) malloc(sizeof(char) * (strlen(arg)+ 1));
        strcpy(Args[i], arg);
    }


    initializeVariables(); //setting up variables

    // Initialize the lock
    PIN_InitLock(&lock);



    string fileName = KnobOutputFile.Value();

    if (!fileName.empty()) { out = new std::ofstream(fileName.c_str());}

    //to identify calls in the code for starting/stoping instruction count
    RTN_AddInstrumentFunction(Routine, 0);


    // Register function to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Register function to be called to instrument traces
    TRACE_AddInstrumentFunction(Trace, 0);

    cerr <<  "===============================================" << endl;
    cerr <<  "This application is instrumented by MyPinTool" << endl;
    if (!KnobOutputFile.Value().empty()) 
    {
        cerr << "See file " << KnobOutputFile.Value() << " for analysis results" << endl;
    }
    printArguments();
    cerr <<  "===============================================" << endl;


    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
