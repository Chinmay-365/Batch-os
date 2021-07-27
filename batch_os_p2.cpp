/*Note:
1. If a (non void) function returns a value '0', that means it executed properly, otherwise there was an error
*/
#include <iostream>
#include <fstream>
#include <string>
#include <stdlib.h>
#include <time.h>
#include <set>
using namespace std;

//defining data structures
char ir[4];  //Instruction register (4 Bytes)
int ic;      //This is the virtual address ?how to limit it to 99
char reg[4]; //General Purpose Register
bool C;      //toggle
int SI;      //for service interrupt
int PI;      //for Page Interrupt
int TI;      //for Timer interrupt
int ptr;     //page table entry

//process control block
typedef struct pcb
{
    int jobid, ttl, tll, ttc, llc;
} pcb;

//initializing a global process
pcb gpro; //PCB creation

//global constants
const int locs = 300;    //number of words (locations) //phase 2 has 300 word memory
const int word_size = 4; //number of characters/bytes in a word

//defining memory
char mem[locs][word_size];
//defining frame allocation hash table
bool framealc[30];

//Function declarations
void load();
void init_sys();
void init_pcb(string);
int alc_frame();
void init_pagetable();
void upd_pagetable(int, int);
void start_exec(ifstream &);
int address_map(int);
void exec_user_prog(ifstream &);
int mos(int, ifstream &);
int read(int, ifstream &);
int write(int);
void tmnt(int, int = 0);

///////////////////////////////////////////////////////////
//MOS (Master Mode) //should return 0 if no problems, else return -1
int mos(int mem_loc, ifstream &fin)
{
    bool endflag = true; //errflag indicates whether to terminate the execution of job
    if (TI == 0)
    {
        if (PI == 1) //TERMINATE(4)
        {
            tmnt(4);
        }
        else if (PI == 2) //TERMINATE(5)
        {
            tmnt(5);
        }
        else if (PI == 3) //check for valid page fault else TERMINATE(6)
        {
            if (ir[0] == 'G' || ir[0] == 'S')   //if it is a GD or SR instruction
            {
                //mem_loc will be last used real address, but we need virtual address of current operand
                int virt = ((ir[2] - '0') * 10) + (ir[3] - '0'); //converting char to int (from IR[3,4])
                int temp_frame = alc_frame();                    //allocate frame for loading data card/page
                if (temp_frame == -1)
                {
                    cout << "\nNo free frame found! Memory is out of space!" << endl;
                }
                else
                {
                    virt /= 10;                      //converting to page no
                    upd_pagetable(temp_frame, virt);

                    ic--;            //for repeating same instr again
                    endflag = false; //job can continue
                }
            }
            else //TERMINATE(6)
            {
                tmnt(6);
            }
        }

        else if (SI == 1) //READ from data card
        {
            if (!read(mem_loc, fin))
            {
                endflag = false; //job can continue
            }                    //if read returns error (-1), then return error from mos
        }
        else if (SI == 2) //WRITE
        {
            if (!write(mem_loc))
            {
                endflag = false; //job can continue
            }                    //if write returns error (-1), then return error from mos
        }
        else if (SI == 3) //TERMINATE(0)
        {
            tmnt(0);
        }

        else //this should not happen
        {
            cout << "\nUnusual Error: (~TI) SI:" << SI << " PI:" << PI << endl;
        }
    }
    else if (TI == 2)
    {                //all cases lead to termination
        if (PI == 1) //TERMINATE(3,4)
        {
            tmnt(3, 4);
        }
        else if (PI == 2) //TERMINATE(3,5)
        {
            tmnt(3, 5);
        }
        else if (PI == 3) //TERMINATE(3)
        {
            tmnt(3);
        }

        else if (SI == 1) //TERMINATE(3)
        {
            tmnt(3);
        }
        else if (SI == 2) //WRITE THEN TERMINATE(3)
        {
            write(mem_loc);
            tmnt(3);
        }
        else if (SI == 3) //TERMINATE(0)
        {
            tmnt(0);
        }

        else if (PI == 0 && SI == 0) //TERMINATE(3)
        {
            tmnt(3); //only time limit exceeded error
        }

        else //this should not happen
        {
            cout << "\nUnusual Error: (TI) SI:" << SI << " " << PI << endl;
        }
    }
    else //this should not happen
    {
        cout << "\nUnusual Error SI:" << SI << " PI:" << PI << " TI:" << TI << endl;
    }

    //reinitialization
    SI = 0;
    PI = 0;

    if (endflag == true)
    {
        return -1; //MOS executed with error/s or Halt instr
    }
    //else
    return 0; //MOS executed normally
}

///////////////////////////////////////////////////////////
//Read subroutine
int read(int mem_loc, ifstream &fin)
{                                       //cout << "reading.."<<endl;
    mem_loc = mem_loc - (mem_loc % 10); //multiple of 10 required
    string buffer;

    //read a Line from File
    if (getline(fin, buffer))
    {

        if (buffer[0] == '$' && buffer[1] == 'E') //this card will be the $END card (out of data)
        {
            tmnt(1); //TERMINATE(1)
            return -1;
        }
        else //it is data card
        {
            int j = 0, i = 0; //counter for data card length

            while (buffer[j] != '\0' && j < 40)
            { //buffer should not be a null char and data card length should be under 40 char
                mem[mem_loc][i] = buffer[j];
                i = (i + 1) % 4;
                if (!i) //if i becomes 0 again, then increment mem_loc
                {
                    mem_loc++; //go to next location
                }
                j++;
            }
        }
    }
    return 0; //read executed normally
}

///////////////////////////////////////////////////////////
//Write subroutine
int write(int mem_loc)
{
    gpro.llc++;              //increment line count
    if (gpro.llc > gpro.tll) //line limit exceeded
    {
        tmnt(2); //TERMINATE(2)
        return -1;
    }

    mem_loc = mem_loc - (mem_loc % 10); //multiple of 10 required
    string buffer = "";
    ofstream fout;
    // by default ios::out mode, automatically deletes
    // the content of file. To append the content, open in ios:app
    // fout.open("sample.txt", ios::app)
    fout.open("output.txt", ios::app);

    //copying the page into the buffer
    for (int i = mem_loc; i < (mem_loc + 10); i++) //for each word in page
    {
        for (int j = 0; j < word_size; j++) //for each byte in word
        {
            buffer += mem[i][j];
        }
    }

    //write buffer to output file
    fout << buffer;
    fout << endl;
    fout.close();

    return 0; //write executed normally
}

///////////////////////////////////////////////////////////
//Terminate subroutine
void tmnt(int em1, int em2)
{
    string buffer = "\n\n"; //this will output two blank lines
    ofstream fout;
    // by default ios::out mode, automatically deletes
    // the content of file. To append the content, open in ios:app
    fout.open("output.txt", ios::app);
    //write buffer to output file
    fout << buffer;

    fout << em1 << ": ";
    if (em1 == 0) //terminate normally
    {
        fout << "No Error" << endl;
    }
    //rest all are error msgs
    else if (em1 == 1)
    {
        fout << "Out of Data" << endl;
    }
    else if (em1 == 2)
    {
        fout << "Line Limit Exceeded" << endl;
    }
    else if (em1 == 3)
    {
        fout << "Time Limit Exceeded";
        if (em2 == 4)
        {
            fout << em2 << ": Operation Code Error" << endl;
        }
        else if (em1 == 5)
        {
            fout << em2 << ": Operand Error" << endl;
        }
        else
        {
            fout << endl;
        }
    }
    else if (em1 == 4)
    {
        fout << "Operation Code Error" << endl;
    }
    else if (em1 == 5)
    {
        fout << "Operand Error" << endl;
    }
    else if (em1 == 6)
    {
        fout << "Invalid Page Fault" << endl;
    }

    //output system state
    fout << "IC:" << ic << " ;; IR:";
    for (int i = 0; i < 4; i++)
    {
        fout << ir[i];
    }
    fout << " ;; Reg:";
    for (int i = 0; i < 4; i++)
    {
        fout << reg[i];
    }
    fout << " ;; C:" << C;
    fout << " ;; JobID:" << gpro.jobid << " ;; TTC:" << gpro.ttc << " ;; TTL:" << gpro.ttl << " ;; LLC:" << gpro.llc << " ;; TLL:" << gpro.tll << endl;

    fout.close();
}

///////////////////////////////////////////////////////////
//function for initializing memeory locations to null value
void init_sys()
{
    //initializing memory and frame allc hashtable
    for (int i = 0; i < locs; i++)
    {
        for (int j = 0; j < word_size; j++)
        {
            mem[i][j] = '\0'; //initializing byte to NULL character
            if (i % 10 == 0)  //for each start of block
            {
                framealc[i / 10] = false;
            }
        }
    }
    //initializing registers
    for (int i = 0; i < word_size; i++)
    {
        reg[i] = '\0'; //initializing byte to NULL character
        ir[i] = '\0';
    }
    //resetting interrupts
    SI = 0;
    TI = 0;
    PI = 0;
}

///////////////////////////////////////////////////////////
//initializing process control block
void init_pcb(string buffer)
{
    //in case of single process exec, this pcb will be reinitiazlized after every job starts
    gpro = {0, 0, 0, 0, 0};
    for (int i = 0; i < 4; i++)
    {
        gpro.jobid = (gpro.jobid * 10) + (buffer[i + 4] - '0');
        gpro.ttl = (gpro.ttl * 10) + (buffer[i + 8] - '0');
        gpro.tll = (gpro.tll * 10) + (buffer[i + 12] - '0');
        gpro.ttc = 0;
        gpro.llc = 0;
    }
}

///////////////////////////////////////////////////////////
//free frame allocation function //returns -1 if no free frame found
int alc_frame()
{
    // cout << "alc_frame\n";
    set<int> visited; //for checking unique frames searched
    int check_frame;
    do
    {
        check_frame = (rand() % (locs / 10));
        visited.insert(check_frame);
    } while ((framealc[check_frame] == true) && (visited.size() < (locs / 10))); //loop if frame already allocated

    if (visited.size() == 30)
    {
        return -1; //no free frame found
    }
    framealc[check_frame] = true;
    return check_frame; //return the free frame number
}

///////////////////////////////////////////////////////////
//initializing page table
void init_pagetable()
{
    // cout << "init pgt\n";
    //we assume no previous data exist in page table block and ptr has been assigned a frame number
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < word_size; j++)
        {
            mem[ptr + i][j] = '*'; //initializing page table entries with '*' char
        }
    }
}

///////////////////////////////////////////////////////////
//function to update page table for new program/data page
void upd_pagetable(int temp_frame, int pti)
{
    // cout << "upd pgt\n";
    int i = 0;
    if (mem[ptr + pti][3] == '*') //if free word in page table for that index
    {
        mem[ptr + pti][3] = (temp_frame % 10) + 48;
        mem[ptr + pti][2] = ((temp_frame / 10) % 10) + 48;
    }
    else
    {
        //there already exists a pte at this address, so the contents maybe overwritten
    }
}

///////////////////////////////////////////////////////////
//LOAD function
void load()
{
    srand((unsigned)time(NULL)); //seeding random gen

    string buffer;   //buffer to read from file
    int mem_loc = 0; //store location of mem (index)
    int pti;         //page table index

    ifstream fin;
    //by default open mode = ios::in mode///////////////////////need to change to get commandline input
    fin.open("errorjobs.txt"); //first entry into file

    //execute a loop until EOF (End of File)
    while (fin)
    {
        //read a Line from File
        getline(fin, buffer);

        if (buffer[0] == '$') //if it is a control card (starts with '$')
        {
            if (buffer[1] == 'A' && buffer[2] == 'M' && buffer[3] == 'J') //for $AMJ
            {
                init_sys();  //initialization of memory and regs (not specified in doc but required)
                mem_loc = 0; //resetting local memory reference variable
                pti = 0;     //resetting page table index

                //initialization of pcb
                init_pcb(buffer);

                //ALLOCATE frame for page table
                ptr = alc_frame() * 10; //frame number x 10 (max p.t. entries)

                //initialize page table block
                init_pagetable();

                continue;
            }
            else if (buffer[1] == 'D' && buffer[2] == 'T' && buffer[3] == 'A') //for $DTA
            {
                start_exec(fin); //mos/start execution
            }
            else if (buffer[1] == 'E' && buffer[2] == 'N' && buffer[3] == 'D') //for $END
            {
                // break; //for one job
                continue;
            }
            else
            {
                cout << "\nInvalid control card" << endl;
            }
        }

        else //it is a program card
        {
            if (pti == 10) //if page table entries are full
            {
                cout << "\nMemory exceeded!\n";
                continue;
            }

            // This part stores program data into memory one card at a time
            else //memory in bounds
            {
                int temp_frame = alc_frame(); //allocate frame for loading program card/page
                if (temp_frame == -1)
                {
                    cout << "\nNo free frame found! Memory is out of space!" << endl;
                    continue;
                }
                upd_pagetable(temp_frame, pti);
                mem_loc = temp_frame * 10;

                int j = 0; //counter for program card length

                while (buffer[j] != '\0' && j < 40)
                { //buffer char should not be null and program card length should be under 40 char
                    int i = 0;
                    if (buffer[j] == 'H') //if the instruction is Halt (1 byte)
                    {
                        mem[mem_loc][i] = buffer[j];
                        j++;
                    }
                    else //for other instructions because they are 4 bytes long
                    {
                        for (i = 0; i < word_size; i++)
                        {
                            mem[mem_loc][i] = buffer[j];
                            j++;
                        }
                    }
                    mem_loc++; //go to next location

                    /////////////////////////////////
                    //testing - printing program card
                    // cout << mem_loc << ": ";
                    // for (i = 0; i < word_size; i++)
                    // {
                    //     cout << mem[mem_loc][i];
                    // }
                    // cout << endl;
                }
            }

            pti++; //incrementing pt index for storing next page
        }
    }
    fin.close(); //close the input file stream
}

///////////////////////////////////////////////////////////
//STARTEXECUTION function
void start_exec(ifstream &fin)
{
    ic = 0;              //setting counter to zero
    exec_user_prog(fin); //return to start_exec() after exec_user_prog() completes
}

///////////////////////////////////////////////////////////
//ADDRESS MAP function  //returns -1 if error/fault else returns real address
int address_map(int virt)
{
    if (virt == -1) //-1 stands for VA of operand in IR
    {
        if ((ir[2] >= 48 && ir[2] <= 57) && (ir[3] >= 48 && ir[3] <= 57)) //valid operand (operand is a number)
        {
            virt = ((ir[2] - '0') * 10) + (ir[3] - '0'); //converting char to int
        }
        else
        {
            PI = 2; //operand error
            return -1;
        }
    }

    int pte = ptr + (virt / 10); //getting page table entry

    if (mem[pte][3] == '*') //if page table entry is not assigned   0th *   *   0   1
    {
        PI = 3; //page fault
        return -1;
    }
    else //page table entry exists
    {    //getting real address of that virtual address
        int real = ((((mem[pte][2] - '0') * 10) + (mem[pte][3] - '0')) * 10) + (virt % 10);
        //returning real address
        return real;
    }

    return -1; //inturrupt raised
}

///////////////////////////////////////////////////////////
//EXECUTEUSERPROGRAM (SLAVE MODE) function
void exec_user_prog(ifstream &fin) //execute user program
{
    int i, ic_real, opr_real;

    while (true) //loop
    {
        if (ic < 0 || ic > 99)
        {
            cout << "\nIC exceeded limit" << endl;
            break;
        }

        //to convert IC to real address (VA -> RA)
        ic_real = address_map(ic); //ic_real stores real address of IC

        if (ic_real != -1) //interrupt raised in address map
        {
            for (i = 0; i < 4; i++) //fetching instruction from memory location 'IC'
            {
                ir[i] = mem[ic_real][i];
            }
            cout << endl;

            // #testing
            cout << ic << ": ";
            for (i = 0; i < 4; i++)
            {
                cout << ir[i];
            }
            cout << endl;

            ic++;

            //If the instr is Halt, then no need for address_map
            if (ir[0] == 'H' && ir[1] == '\0' && ir[2] == '\0' && ir[3] == '\0')
            {
                // cout << "test h" << endl;
                SI = 3;
                opr_real = -1; //to avoid examine IR[1,2]
            }
            else
            {
                //to convert operand to real address
                opr_real = address_map(-1); //opr_real stores real address of operand //address_map(VA -> RA)
            }
            //if no errors then execute instruction normally //if x == -1, this could also mean valid page fault
            if (opr_real != -1)
            {
                //instructions
                if (ir[0] == 'L' && ir[1] == 'R') //for LR
                {
                    // cout << "\ntest lr";
                    for (i = 0; i < 4; i++) //loading data to general register
                    {
                        reg[i] = mem[opr_real][i];
                    }
                }

                else if (ir[0] == 'S' && ir[1] == 'R') //for SR
                {
                    // cout << "\ntest sr";
                    for (i = 0; i < 4; i++) //storing data from general register
                    {
                        mem[opr_real][i] = reg[i];
                    }
                    //gpro.ttc++;
                }

                else if (ir[0] == 'C' && ir[1] == 'R') //for CR
                {
                    // cout << "\ntest cr";
                    bool flag = true;
                    for (i = 0; i < 4; i++) //loading data to general register
                    {
                        if (reg[i] != mem[opr_real][i])
                        {
                            flag = false;
                        }
                    }

                    //setting 'C' toggle
                    if (flag)
                    {
                        C = true;
                    }
                    else
                    {
                        C = false;
                    }
                }

                else if (ir[0] == 'B' && ir[1] == 'T') //for BT
                {
                    // cout << "\ntest bt";
                    if (C)
                    {
                        ic = ((ir[2] - '0') * 10) + (ir[3] - '0'); //converting char to int
                    }
                }

                else if (ir[0] == 'G' && ir[1] == 'D')
                { //in this case mem_loc passed in SI will be VA, because no frame is allocated to this pte
                    // cout << "\ntest gd";
                    SI = 1;
                    //gpro.ttc++;
                }

                else if (ir[0] == 'P' && ir[1] == 'D')
                {
                    // cout << "\ntest pd";
                    SI = 2;
                }

                else
                {
                    PI = 1; //opcode/operation error
                    // break;
                }
            }
        }
        //increment TTC
        gpro.ttc++;

        if (gpro.ttc > gpro.ttl) //time counter exceeded limit
        {
            TI = 2; //this means that job had not yet halt but the timer exceeded TTL
            // break;
        }

        if (SI || PI || TI) //if any interrupt was raised
        {
            if (mos(opr_real, fin)) //if mos returns error, break from loop
            {
                break;
            }
        }

    } //end-loop
}

//driver or starter program
int main()
{
    load();

    ///////////////////////////////////
    //testing - for checking memory alc
    // cout << "Memory:" << endl;
    // for (int i = 0; i < 300; i++)
    // {

    //     cout << i << ": ";
    //     for (int j = 0; j < 4; j++)
    //     {
    //         cout << mem[i][j];
    //     }
    //     cout << endl;
    // }
    return 0;
}