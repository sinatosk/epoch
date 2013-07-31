/*This code is part of Epoch. Epoch is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include "epoch.h"

/*We store the current runlevel here.*/
char CurRunlevel[MAX_LINE_SIZE] = "default";

static Bool FileUsable(const char *FileName)
{
	FILE *TS = fopen(FileName, "r");
	
	if (TS)
	{
		fclose(TS);
		return true;
	}
	else
	{
		fclose(TS);
		return false;
	}
}

rStatus ExecuteConfigObject(ObjTable *InObj, Bool IsStartingMode)
{ /*Not making static because this is probably going to be useful for other stuff.*/
	pid_t LaunchPID;
	const char *CurCmd, *ShellPath = "sh"; /*We try not to use absolute paths here, because some distros don't have normal layouts,
											*And I'm sure they would rather see a warning than have it just botch up.*/
	rStatus ExitStatus = 0;
	Bool ShellDissolves;
	int RawExitStatus;
	
	CurCmd = (IsStartingMode ? InObj->ObjectStartCommand : InObj->ObjectStopCommand);
	
	/*Check how we should handle PIDs for each shell. In order to get the PID, exit status,
	* and support shell commands, we need to jump through a bunch of hoops.
	* PID killing support in this manner, is buggy. That's why we support PIDFILE.*/
	if (FileUsable("/bin/bash"))
	{
		ShellDissolves = true;
		ShellPath = "bash";
	}
	else if (FileUsable("/bin/dash"))
	{
		ShellPath = "dash";
		ShellDissolves = true;
	}
	else if (FileUsable("/bin/zsh"))
	{
		ShellPath = "zsh";
		ShellDissolves = true;
	}
	else if (FileUsable("/bin/csh"))
	{
		ShellPath = "csh";
		ShellDissolves = true;
	}
	else if (FileUsable("/bin/busybox"))
	{ /*This is one of those weird shells that still does the old practice of creating a child for -c.
		* We can deal with the likes of them. Small chance that for shells like this, another PID could jump in front
		* and we could end up storing the wrong one. Very small, but possible.*/
		ShellPath = "busybox";
		ShellDissolves = false;
	}
#ifndef WEIRDSHELLPERMITTED
	else /*Found no other shells. Assume fossil, spit warning.*/
	{
		static Bool DidWarn = false; /*Don't spam this warning.*/
		
		ShellDissolves = false;
		if (!DidWarn)
		{	 
			DidWarn = true;
			SpitWarning("No known shell found. Using /bin/sh.\n"
			"Best if you install one of these: bash, dash, csh, zsh, or busybox.\n"
			"This matters because PID detection is affected by the way shells handle sh -c.");
		}
	}
#endif
	
	/**Here be where we execute commands.---------------**/
	LaunchPID = fork();
	
	if (LaunchPID < 0)
	{
		SpitError("Failed to call fork(). This is a critical error.");
		exit(1);
	}
	
	if (LaunchPID == 0) /**Child process code.**/
	{ /*Child does all this.*/
		char TmpBuf[1024];
		execlp(ShellPath, "sh", "-c", CurCmd, NULL); /*I bet you think that this is going to return the PID of sh. No.*/
		/*We still around to talk about it? We were supposed to be imaged with the new command!*/
		
		snprintf(TmpBuf, 1024, "Failed to execute %s: execlp() failure.", InObj->ObjectID);
		SpitError(TmpBuf);
		exit(1);
	}
	
	/*Get PID*/ /**Parent code resumes.**/
	InObj->ObjectPID = waitpid(LaunchPID, &RawExitStatus, 0); /*Wait for the process to exit.*/
	
	if (!ShellDissolves)
	{
		++InObj->ObjectPID; /*This probably won't always work, but 99.9999999% of the time, yes, it will.*/
	}
	
	/**And back to normalcy after this.------------------**/
	
	switch (WEXITSTATUS(RawExitStatus))
	{ /*FIXME: Make this do more later.*/
		case 128: /*Bad exit parameter*/
		case -1: /*Out of range for exit status. Probably shows as an unsigned value on some machines anyways.*/
			ExitStatus = WARNING;
			break;
		case 0:
			ExitStatus = SUCCESS;
			break;
		default:
			ExitStatus = FAILURE;
			break;
	}
	
	return ExitStatus;
}

/*This function does what it sounds like. It's not the entire boot sequence, we gotta display a message and stuff.*/
rStatus RunAllObjects(Bool IsStartingMode)
{
	unsigned long MaxPriority = GetHighestPriority(IsStartingMode);
	unsigned long Inc = 1; /*One to skip zero.*/
	ObjTable *CurObj = NULL;
	char PrintOutStream[1024];
	rStatus ExitStatus;
	
	if (!MaxPriority && IsStartingMode)
	{
		SpitError("All objects have a priority of zero!");
		return FAILURE;
	}
	
	for (; Inc <= MaxPriority; ++Inc)
	{
		if (!(CurObj = GetObjectByPriority(CurRunlevel, IsStartingMode, Inc)))
		{ /*Probably set to zero or something, but we don't care if we have a gap in the priority system.*/
			continue;
		}
		
		if (!CurObj->Enabled)
		{
			continue;
		}
		
		/*Copy in the description to be printed to the console.*/
		snprintf(PrintOutStream, 1024, "%s %s ", (IsStartingMode ? "Starting" : "Stopping"), CurObj->ObjectName);
		
		if (IsStartingMode)
		{
			printf(PrintOutStream);
			fflush(NULL); /*Things tend to get clogged up when we don't flush.*/
			
			ExitStatus = ExecuteConfigObject(CurObj, IsStartingMode); /*Don't bother with return value here.*/
			PrintStatusReport(PrintOutStream, ExitStatus);
		}
		else
		{
			switch (CurObj->StopMode)
			{
				case STOP_COMMAND:
					printf(PrintOutStream);
					fflush(NULL);
					
					ExitStatus = ExecuteConfigObject(CurObj, IsStartingMode);
					
					PrintStatusReport(PrintOutStream, ExitStatus);
					break;
				case STOP_INVALID:
				case STOP_NONE:
					break;
				case STOP_PID:
					printf(PrintOutStream);
					fflush(NULL);
					
					if (kill(CurObj->ObjectPID, OSCTL_SIGNAL_TERM) == 0)
					{ /*Just send SIGTERM.*/
						ExitStatus = SUCCESS;
					}
					else
					{
						ExitStatus = FAILURE;
					}
					PrintStatusReport(PrintOutStream, ExitStatus);
					
					break;
				case STOP_PIDFILE:
				{
					FILE *Tdesc = fopen(CurObj->ObjectPIDFile, "r");
					unsigned long Inc = 0, TruePID = 0;
					char Buf[MAX_LINE_SIZE], WChar, *TWorker;
					
					printf(PrintOutStream);
					fflush(NULL);
					
					for (; (WChar = getc(Tdesc)) != EOF && Inc < MAX_LINE_SIZE; ++Inc)
					{
						Buf[Inc] = WChar;
					}
					Buf[Inc] = '\0'; /*Stop. Whining. About. The. Loops. I don't want to use stat() here!*/
					
					fclose(Tdesc);
					
					if ((TWorker = strstr(Buf, "\n")) != NULL) /*Nuke the newlines.*/ 
					{
						*TWorker = '\0';
					}
					
					if (isdigit(Buf[0]))
					{
						TruePID = atoi(Buf);
					}
					else
					{
						char TmpBuf[1024];
						
						snprintf(TmpBuf, 1024, "Cannot kill %s: The PID file does not contain purely numeric values.", CurObj->ObjectID);
						SpitError(TmpBuf);
						PrintStatusReport(PrintOutStream, FAILURE);
						continue;
					}
					
					/*Now we can actually kill the process ID.*/
					
					if (kill(TruePID, OSCTL_SIGNAL_TERM) == 0)
					{
						ExitStatus = SUCCESS;
					}
					else
					{
						ExitStatus = FAILURE;
					}
					
					PrintStatusReport(PrintOutStream, ExitStatus);
					
					break;
				}
			}
		}
	}
	
	return SUCCESS;
}
