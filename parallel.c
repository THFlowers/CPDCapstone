#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/sysinfo.h> // for get_nprocs_conf() GNU Extension
#include <sys/wait.h> // for wait()/waitpid() POSIX

#define FALSE 0
#define TRUE  1
#define MAX_COMMAND_SIZE 100
#define MAX_SOURCES 8 // TODO: Allow significantly higher, but this should suffice for now
#define MAX_DATA_SIZE 50

// TODO: When MAX_SOURCES increased, delete this and use dynamic memory (need to handle that as well)
static char* sub_constants[] = {NULL, "{1}", "{2}", "{3}", "{4}", "{5}", "{6}", "{7}", "{8}", "{9}"}; 
static char* valid_sub_ops[] = {".", "#", "%", "/", "//", "/."};
enum valid_sub_opcodes {PATHNOEXT_OP=0, SEQNUM_OP, JOBSLOT_OP, BASEXT_OP, PATHNOBASE_OP, BASE_OP, num_valid_sub_ops};
static char* default_command = "echo";

// For this temporary version assume command is all args between options and first ":::"
// This is exec with "bash -c <command>" and with maximum size 100 strings
// We'll count one input stream from command line after a ":::"
// -j # or --jobs # to manually specifiy number of jobs (must be before ":::")

// Set-up sub-programs
void process_command_line(int argc, char* argv[]);
void sanity_check_subs();
void check_valid_sub_pattern(char* str, int *sources);

// Main-loop procedures
void increment_data_sources();
int  generate_concrete_command(char** concrete_command, int job_number, int slot_number);
void free_concrete_command(char** concrete_command);
pid_t spawn_process(char** concrete_command);

struct command_line_data
{
	char* strings[MAX_DATA_SIZE];
	int size;
	int pos;
	int loop;
};

struct process
{
	pid_t pid;
	int host_index;
};

unsigned int job_number   =  1;  // current num of tasks issued (count starts at 1, ends when == max_job)
unsigned int max_job      = -1;  // total number of tasks/jobs
unsigned int num_procs    =  0;  // total num of simultaneous tasks/jobs
unsigned int command_size =  0;  // Number of strings in abstract command
char* command[MAX_COMMAND_SIZE]; // Abstract command, substitutions not performed, array of strings

unsigned int num_command_line_sources = 0;
struct command_line_data data_sources[MAX_SOURCES];

struct process *pool = NULL;
unsigned int dryrun = FALSE;

int
main(int argc, char* argv[])
{
	process_command_line(argc, argv);

	// Note: Jobs can be bigger than cores.  Don't dictate to user.
	if (num_procs == 0)
		num_procs = get_nprocs_conf(); // func is why GNU_SOURCE, more mature code would ifdef for each *NIX

	if (num_command_line_sources == 0)
	{
		fprintf(stderr, "No input sources provided");
		exit(EXIT_FAILURE);
	}

	sanity_check_subs();

	// In this version, max_job is simply the size of the smallest (non-looping) source
	for (int i=0; i<num_command_line_sources; i++)
	{
		if (!data_sources[i].loop && data_sources[i].size < max_job)
			max_job = data_sources[i].size;
	}
	// Recall, max_job is unsigned so IDK if <= 0 works here
	if (max_job == -1 || max_job==0)
	{
		fprintf(stderr, "Empty inputs, or all loop\n");
		exit(EXIT_FAILURE);
	}

	printf("Number of processes: %d\n", num_procs);
	printf("Command: ");
	for (int i=0; i<command_size; i++)
		printf("%s ", command[i]);
	printf("\nNumber of command line sources %d\n", num_command_line_sources);

	if (!dryrun)
	{
		pool = (struct process*)malloc(sizeof(struct process)*num_procs);
		for (int i=0; i<num_procs; i++)
			pool[i].pid = -1;
	}

	int status = 0;
	char* concrete_command[MAX_COMMAND_SIZE+1]; // here we will actually perform substitutions (COMPLETELY DYNAMIC, must malloc/free)
	for (int i=0; i<=MAX_COMMAND_SIZE; i++)
		concrete_command[i]=NULL;

	// TODO: Use a table to keep track of which parts are dynamic? Eliminate useless mallocs
	while (job_number < max_job)
	{
		for (int i=0; i<num_procs && job_number <= max_job; job_number++)
		{
			if (!dryrun && pool[i].pid != -1)
			{
				waitpid(pool[i].pid, &status, WNOHANG);
				if (!WIFEXITED(status))
					continue;
			}

			if (!generate_concrete_command(concrete_command, job_number, i+1))
				fprintf(stderr, "Parsing error\n");
			else
			{
				if (!dryrun)
					pool[i].pid = spawn_process(concrete_command);
				else
				{
					// Offsets are due to prefix stuff in generate_concrete_command
					for (int i=0; i<command_size+1; i++)
						printf("%s ", concrete_command[i]);
					printf("\n");
				}
			}
			free_concrete_command(concrete_command);
			increment_data_sources();
		}
	}

	if (!dryrun)
	{
		while(wait(NULL) > 0);
		free(pool);
	}

	return EXIT_SUCCESS;
}

// Just have everything loop by default, have size of inputs control exit main loop
// Also, when files are involved just skip those sources: getline() increments for us
// (If we switch to all file input (redirect stdin & command_line to files) this will be deleted
void increment_data_sources()
{
	for (int i=0; i<num_command_line_sources; i++)
	{
		data_sources[i].pos++;
		if (data_sources[i].pos >= data_sources[i].size)
			data_sources[i].pos = 0;
	}
}

// TODO: Finish copying methods from sanity_check_subs / check_valid_sub_pattern
// TODO: Big switch expression over sub_opt types
// perform substitutions and make concrete_command[0]=$SHELL, and rest like "$SHELL -c <user_input>"
int generate_concrete_command(char** concrete_command, int job_number, int slot_number)
{
	int prefix=0;
	//concrete_command[0]=strdup("-c");
	char *pattern=NULL, *start=NULL, *end=NULL;
	char *operation=NULL, *srcpos=NULL;

	for (int i=0; i<command_size; i++)
	{
		int concrete_index = i+prefix;
		int copy_len = strlen(command[i]);
		int res = -1, source_number = -1;
		int pat_len = 0;

		concrete_command[concrete_index]=(char*)calloc('\0', sizeof(char)*(copy_len+1));

		srcpos = command[i];
		start  = command[i];
		while ((start=strchr(start, '{')) != NULL)
		{
			if ((end=strchr(start, '}')) == NULL)
				break;

			pat_len = (end-start)+1;
			pattern = (char*)calloc('\0', sizeof(char)*(pat_len+1));
			strncpy(pattern, start, pat_len);

			// accumulating the original unsubstituted part
			strncat(concrete_command[concrete_index], srcpos, start-srcpos);

			res = sscanf(pattern, "{%d%m[^}]}", &source_number, &operation);
			// first field failed, try again without it
			if (res == 0)
				res = sscanf(pattern, "{%m[^}]}", &operation);

			// extracted all the parts, can now dispose of pattern
			free(pattern);

			// get here when string is "{}" or numberless op
			if (source_number==-1)
				source_number = 1;

			// assume due to sanity check means we must have a valid number here
			// 0 index representation internally
			// If op doesn't use this data then no trouble, only reads occur
			int  cur_data_pos = data_sources[source_number-1].pos;
			char *paste_item  = data_sources[source_number-1].strings[cur_data_pos];

			// literal substitution (both {} and {1} etc. handled)
			if (operation==NULL)
			{
				int  paste_len = strlen(paste_item);
				if (pat_len < paste_len)
				{
					copy_len += paste_len;
					concrete_command[concrete_index] = (char*)realloc(concrete_command[concrete_index], sizeof(char)*(copy_len+1));
				}
				strcat(concrete_command[concrete_index], paste_item);
			}
			// we are using a more complex substitution
			else
			{
				int op = 0;
				for (op=0; op<num_valid_sub_ops; op++)
					if (strcmp(operation, valid_sub_ops[op])==0)
						break;

				// TODO: replace naive versions of path operations with unix library function calls
				switch (op)
				{
					// Path+File without extension, semi-naieve
					case PATHNOEXT_OP:
					{
						char *dot_pos = strrchr(paste_item, '.');
						int  paste_len = dot_pos-paste_item;

						// search failed or hidden-file dot, return whole string
						if (dot_pos==paste_item)
							paste_len = strlen(paste_item);

						if (pat_len < paste_len)
						{
							copy_len += paste_len;
							concrete_command[concrete_index] =
								(char*)realloc(concrete_command[concrete_index], sizeof(char)*(copy_len+1));
						}
						strncat(concrete_command[concrete_index], paste_item, paste_len);
						break;
					}
					// Current job # out of total jobs
					case SEQNUM_OP:
					{
						int paste_len = snprintf(NULL, 0, "%d", job_number) + 1;
						if (pat_len < paste_len)
						{
							copy_len += paste_len;
							concrete_command[concrete_index] =
								(char*)realloc(concrete_command[concrete_index], sizeof(char)*(copy_len+1));
						}
						char *pos = concrete_command[concrete_index];
						int  size = strlen(pos);
						pos+=size;
						sprintf(pos, "%d", job_number);
						break;
					}
					case JOBSLOT_OP:
					{
						int paste_len = snprintf(NULL, 0, "%d", slot_number) + 1;
						if (pat_len < paste_len)
						{
							copy_len += paste_len;
							concrete_command[concrete_index] =
								(char*)realloc(concrete_command[concrete_index], sizeof(char)*(copy_len+1));
						}
						char *pos = concrete_command[concrete_index];
						int  size = strlen(pos);
						pos+=size;
						sprintf(pos, "%d", slot_number);
						break;
					}
					case BASEXT_OP:
					{
						char *slash_pos = strrchr(paste_item, '/');
						if (slash_pos != NULL)
							slash_pos++;
						else
							slash_pos = paste_item;
						int paste_len = strlen(slash_pos);

						if (pat_len < paste_len)
						{
							copy_len += paste_len;
							concrete_command[concrete_index] =
								(char*)realloc(concrete_command[concrete_index], sizeof(char)*(copy_len+1));
						}
						strncat(concrete_command[concrete_index], slash_pos, paste_len);
						break;
					}
					// Path without the base (takes BASE_OP and reverses the logic)
					// What do we do when there is no path, just base? Print a dot, path must be relative
					case PATHNOBASE_OP:
					{
						char *slash_pos = strrchr(paste_item, '/');
						if (slash_pos==NULL)
						{
							strncat(concrete_command[concrete_index], ".", 2);
							break;
						}

						int paste_len;
						paste_len = strlen(paste_item)-strlen(slash_pos);

						if (pat_len < paste_len)
						{
							copy_len += paste_len;
							concrete_command[concrete_index] =
								(char*)realloc(concrete_command[concrete_index], sizeof(char)*(copy_len+1));
						}
						strncat(concrete_command[concrete_index], paste_item, paste_len);
						break;
					}
					// TODO: Less brain-dead version
					// assumes last slash works (backwards to slash, forwards to dot if present)
					case BASE_OP:
					{
						char *slash_pos = strrchr(paste_item, '/');
						if (slash_pos==NULL)
							slash_pos = paste_item;
						else
							slash_pos++;
						char *dot_pos = strchr(slash_pos, '.');

						int paste_len;
						if (dot_pos == NULL)
							paste_len = strlen(slash_pos);
						else
							paste_len = dot_pos - slash_pos;

						if (pat_len < paste_len)
						{
							copy_len += paste_len;
							concrete_command[concrete_index] =
								(char*)realloc(concrete_command[concrete_index], sizeof(char)*(copy_len+1));
						}
						strncat(concrete_command[concrete_index], slash_pos, paste_len);
						break;
					}
					default:
						fprintf(stderr, "We shouldn't get here, impossible opcode\n");
						return FALSE;
				}
			}

			start  = end+1;
			srcpos = start;
		}

		// copy trailing part of current command part
		if (srcpos != NULL)
			strcat(concrete_command[concrete_index], srcpos);
	}

	// NULL terminate the vector
	concrete_command[command_size+prefix] = strdup("\0");
	return TRUE;
}

// Frees and NULL's out the strings in concrete_command
void free_concrete_command(char** concrete_command)
{
	for (int i=0; i<MAX_COMMAND_SIZE; i++)
	{
		if (concrete_command[i])
			free(concrete_command[i]);
		concrete_command[i] = NULL;
	}
}

// TODO
// We assume that concrete_command has been correctly constructed
// That is concrete_command[0]=$SHELL and command is "$SHELL -c <processed_command>
pid_t spawn_process(char** concrete_command)
{
	pid_t pid = fork();
	if (pid == 0)
	{
		execvp(concrete_command[0], concrete_command);
		exit(EXIT_FAILURE);
	}
	return pid;
}

void process_command_line(int argc, char* argv[])
{
	char* arg = NULL;
	int i=1; // skip 0th argument, the name of this process
	for (; i<argc; i++)
	{
		arg = argv[i];
		if (strcmp(":::", arg)==0)
		{
			if (command_size==0)
			{
				command[0] = default_command;
				command_size=1;
			}
			break;
		}
		// Not an option, begin command processing
		else if (*(arg++) != '-')
			break;
		else
		{
			switch (*arg)
			{
				case '-':
				{
					++arg;
					if (strcmp(arg, "jobs")==0)
					{
						if (num_procs != 0)
						{
							fprintf(stderr, "Number of jobs given twice");
							exit(EXIT_FAILURE);
						}

						if (++i >= argc)
						{
							fprintf(stderr, "Must specify number of jobs\n");
						}
						else
						{
							num_procs = atoi(argv[i]);
							if (num_procs <= 0)
							{
								fprintf(stderr, "Invalid number of jobs");
								exit(EXIT_FAILURE);
							}
						}
					}
					else if (strcmp(arg, "dryrun")==0)
					{
						dryrun = TRUE;
					}
					else
					{
						fprintf(stderr, "Invalid Long Argument: %s\n", arg);
					}
					break;
				}
				case 'j':
				{
					if (num_procs != 0)
					{
						fprintf(stderr, "Number of jobs given twice");
						exit(EXIT_FAILURE);
					}
					
					if (!isdigit(*(++arg)))
					{
						if (++i >= argc)
						{
							fprintf(stderr, "Must specify number of jobs\n");
							break;
						}
						else
							arg = argv[i];
					}

					num_procs = atoi(arg);
					if (num_procs <= 0)
					{
						fprintf(stderr, "Invalid number of jobs");
						exit(EXIT_FAILURE);
					}
					break;
				}
			}
		}
	}

	for (; i<argc; i++)
	{
		arg = argv[i];
		if (strcmp(":::", arg)==0)
			break;
		else
		{
			if (command_size>=MAX_COMMAND_SIZE)
			{
				fprintf(stderr, "Command too long\n");
				exit(EXIT_FAILURE);
			}
			
			command[command_size] = argv[i];
			command_size++;
		}
	}

	// If ":::" found above then code should fall through to this, initialize struct here
	for (; i<argc; i++)
	{
		arg = argv[i];
		if (strcmp(":::", arg)==0)
		{
			num_command_line_sources++;
			data_sources[num_command_line_sources-1].size = 0;
			data_sources[num_command_line_sources-1].pos = 0;
			data_sources[num_command_line_sources-1].loop = FALSE;
		}
		else
		{
			int pos = data_sources[num_command_line_sources-1].size++;
			data_sources[num_command_line_sources-1].strings[pos] = argv[i];
		}
	}
}

void sanity_check_subs()
{
	// NOTE: This could be made strictly static, sources[MAX_SOURCES]
	// array stores if source is explicitly used
	int  *sources = calloc(FALSE, sizeof(int)*num_command_line_sources);
	char *buffer=NULL, *start=NULL, *end=NULL;
	int  len = -1;

	// Find {} pairs, copy them to buffer, then check correctness
	for (int i=0; i<command_size; i++)
	{
		start = command[i];
		while ((start=strchr(start, '{')) != NULL)
		{
			if ((end=strchr(start, '}')) == NULL)
				break;

			len = (end-start)+1;
			buffer = (char*)calloc('\0', sizeof(char)*(len+1));
			strncpy(buffer, start, len);

			check_valid_sub_pattern(buffer, sources);

			if (buffer != NULL)
			{
				free(buffer);
				buffer = NULL;
			}

			start+=len;
		}
	}

	// Append unused substitution numbers to end of command
	for (int i=0; i<num_command_line_sources; i++)
	{
		if (sources[i]==FALSE)
		{
			if (command_size>=MAX_COMMAND_SIZE)
			{
				fprintf(stderr, "Command too long\n");
				free(sources);
				exit(EXIT_FAILURE);
			}
			// TODO: replace sub_constants with malloced memory
			command[command_size] = sub_constants[i+1];
			command_size++;
		}
	}
	free(sources);
}

// Only called when str begins with { and ends with }
void check_valid_sub_pattern(char* str, int *sources)
{
	int   source_number = -1;
	char* operation = NULL;
	int   len = strlen(str);
	int   res = -1;

	// All patterns must have "{}" at very least, sanity check
	if (len < 2)
		return;
	
	res = sscanf(str, "{%d%m[^}]}", &source_number, &operation);
	// first field failed, try again without it
	if (res == 0)
		res = sscanf(str, "{%m[^}]}", &operation);

	// should only get here when string is "{}"
	if (res == 0)
	{
		// num sources == 1, mark it as used
		if (num_command_line_sources==1)
			sources[0] = TRUE;
		// num sources > 1, then which does this refer to?
		else
		{
			fprintf(stderr, "Must provide source number!\n");
			free(sources);
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		// check if we have prefix number
		if (source_number != -1)
		{
			if (source_number==0 || source_number > (num_command_line_sources+1))
			{
				fprintf(stderr, "There is no input source %d", source_number);
				free(sources);
				exit(EXIT_FAILURE);
			}
			sources[source_number-1] = TRUE;
		}

		if (operation != NULL)
		{
			for (int i=0; i<num_valid_sub_ops; i++)
			{
				if (strcmp(operation, valid_sub_ops[i])==0)
				{
					free(operation);
					return;
				}
			}

			fprintf(stderr, "Invalid/Unimplemented substitution operation: %s", operation);
			free(sources);
			free(operation);
			exit(EXIT_FAILURE);
		}
	}
}
