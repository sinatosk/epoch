/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file UNLICENSE.TXT for more information.*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <time.h>
#include "epoch.h"

/**Globals**/

/*We store the current runlevel here.*/
char CurRunlevel[MAX_DESCRIPT_SIZE];
struct _CTask CurrentTask; /*We save this for each linear task, so we can kill the process if it becomes unresponsive.*/
BootMode CurrentBootMode;

/**Function forward declarations.**/

static rStatus ExecuteConfigObject(ObjTable *InObj, const char *CurCmd);

/**Actual functions.**/

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
		return false;
	}
}	

static rStatus ExecuteConfigObject(ObjTable *InObj, const char *CurCmd)
{ /*Not making static because this is probably going to be useful for other stuff.*/
#ifdef NOMMU
#define ForkFunc() vfork()
#else
#define ForkFunc() fork()
#endif

	pid_t LaunchPID;
	rStatus ExitStatus = FAILURE; /*We failed unless we succeeded.*/
	int RawExitStatus, Inc = 0;
	sigset_t SigMaker[2];	
#ifndef NOSHELL
	Bool ShellEnabled = true; /*If we use shells.*/
	Bool ShellDissolves = SHELLDISSOLVES;
	Bool ForceShell = InObj->Opts.ForceShell;
	static Bool DidWarn = false;
	const char *ShellPath = "/bin/sh";
	
	if (CurCmd == NULL)
	{
		const char *ErrMsg = "NULL value passed to ExecuteConfigObject()! This is likely a bug.";
		SpitError(ErrMsg);
		WriteLogLine(ErrMsg, true);
		
		return FAILURE;
	}
	
	/*Check how we should handle PIDs for each shell. In order to get the PID, exit status,
	* and support shell commands, we need to jump through a bunch of hoops.*/
	if (ShellEnabled)
	{
		if (FileUsable(SHELLPATH))
		{ /*Try our specified shell first.*/
			ShellPath = SHELLPATH;
		}
		else if (FileUsable("/bin/bash"))
		{
			ShellDissolves = true;
			ShellPath = "/bin/bash";
		}
		else if (FileUsable("/bin/dash"))
		{
			ShellPath = "/bin/dash";
			ShellDissolves = true;
		}
		else if (FileUsable("/bin/zsh"))
		{
			ShellPath = "/bin/zsh";
			ShellDissolves = true;
		}
		else if (FileUsable("/bin/csh"))
		{
			ShellPath = "/bin/csh";
			ShellDissolves = true;
		}
		else if (FileUsable("/bin/tcsh"))
		{
			ShellPath = "/bin/tcsh";
			ShellDissolves = true;
		}
		else if (FileUsable("/bin/ksh"))
		{
			ShellPath = "/bin/ksh";
			ShellDissolves = true;
		}
		else if (FileUsable("/bin/busybox"))
		{ /*This is one of those weird shells that still does the old practice of creating a child for -c.
			* We can deal with the likes of them. Small chance that for shells like this, another PID could jump in front
			* and we could end up storing the wrong one. Very small, but possible.*/
			ShellPath = "/bin/busybox";
			ShellDissolves = false;
		}
		else /*Found no other shells. Assume fossil, spit warning.*/
		{
			const char *Errs[2] = { ("Cannot find any functioning shell. /bin/sh is not available.\n"
									 CONSOLE_COLOR_YELLOW "** Disabling shell support! **" CONSOLE_ENDCOLOR),
									("No known shell found. Using \"/bin/sh\".\n"
									"Best if you install one of these: bash, dash, csh, zsh, or busybox.\n") };
			if (!DidWarn)
			{
				
				if (!FileUsable("/bin/sh"))
				{
					SpitWarning(Errs[0]);
					WriteLogLine(Errs[0], true);
					
					ShellEnabled = false; /*Disable shell support.*/
				}
				else
				{
					ShellDissolves = true; /*Most do.*/
					SpitWarning(Errs[1]);
					WriteLogLine(Errs[1], true);
				}
				
				DidWarn = true;
			}
		}
		
		if (!DidWarn && strcmp(ShellPath, ENVVAR_SHELL) != 0)
		{ /*Only happens if we are using a known shell, even if it's not ours.*/
			char ErrBuf[MAX_LINE_SIZE];
			
			snprintf(ErrBuf, MAX_LINE_SIZE, "\"" ENVVAR_SHELL "\" cannot be read. Using \"%s\" instead.", ShellPath);
			
			/*Just write to log, because this happens.*/
			WriteLogLine(ErrBuf, true);
			SpitWarning(ErrBuf);
			DidWarn = true;
		}
	}
#endif /*NOSHELL*/
	/**Here be where we execute commands.---------------**/
	
	/*We need to block all signals until we have executed the process.*/
	sigemptyset(&SigMaker[0]);
	
	for (; Inc < NSIG; ++Inc)
	{
		sigaddset(&SigMaker[0], Inc);
	}
	SigMaker[1] = SigMaker[0];
	
	sigprocmask(SIG_BLOCK, &SigMaker[0], NULL);
	
	/**Actually do the (v)fork().**/
	LaunchPID = ForkFunc();
	
	if (LaunchPID < 0)
	{
		SpitError("Failed to call vfork(). This is a critical error.");
		EmergencyShell();
	}
	
	if (LaunchPID > 0)
	{
			CurrentTask.Node = InObj;
			CurrentTask.TaskName = InObj->ObjectID;
			CurrentTask.PID = LaunchPID;
			CurrentTask.Set = true;
			
			sigprocmask(SIG_UNBLOCK, &SigMaker[1], NULL); /*Unblock now that (v)fork() is complete.*/
	}
	
	if (LaunchPID == 0) /**Child process code.**/
	{ /*Child does all this.*/
		char TmpBuf[1024];
		int Inc = 0;
		sigset_t Sig2;
		
		sigemptyset(&Sig2);
		
		for (; Inc < NSIG; ++Inc)
		{
			sigaddset(&Sig2, Inc);
			signal(Inc, SIG_DFL); /*Set all the signal handlers to default while we're at it.*/
		}
		
		sigprocmask(SIG_UNBLOCK, &Sig2, NULL); /*Unblock signals.*/
		
#ifndef NOMMU /*Can't do this because vfork() blocks the parent.*/
		/*If we are supposed to spawn off as a daemon, do this.*/
		if (InObj->Opts.Fork && CurCmd == InObj->ObjectStartCommand)
		{
			pid_t Subchild = 0;
			
			signal(SIGCHLD, SIG_IGN); /*We don't care about the child's exit code.*/
			
			/*Failure.*/
			if ((Subchild = fork()) == -1) _exit(1);
				
			if (Subchild == 0)
			{ /*Child of the child. PID 1 is now a grandfather.*/
				signal(SIGCHLD, SIG_DFL);
			}
			
			if (Subchild > 0) _exit(0); /*parent is not needed.*/
		}
#endif /*NOMMU*/

		/*Change our session id.*/
		setsid();
		
		if (InObj->ObjectWorkingDirectory != NULL && CurCmd == InObj->ObjectStartCommand)
		{ /*Switch directories if desired.*/
			if (chdir(InObj->ObjectWorkingDirectory) == -1)
			{ /*Failed to chdir.*/
				fprintf(stderr, "Epoch: Object %s " CONSOLE_COLOR_RED "failed" CONSOLE_ENDCOLOR " to chdir to \"%s\".\n",
						InObj->ObjectID, InObj->ObjectWorkingDirectory);
				_exit(1);
			}
		}
		
		
		/*stdout*/
		if (InObj->ObjectStdout != NULL)
		{
			freopen(InObj->ObjectStdout, "a", stdout); /*We don't deal with the return code.*/
		}
		
		/*stderr*/
		if (InObj->ObjectStderr != NULL)
		{
			freopen(InObj->ObjectStderr, "a", stderr);
		}
		
		/**The ordering of this is important to make the file descriptors work for an alternative stdout/stderr.**/
		if (CurCmd == InObj->ObjectStartCommand)
		{ /*Set user and group if desired.*/

			if (InObj->UserID != 0)
			{
				struct passwd *UserStruct = getpwuid(InObj->UserID);
				
				if (!UserStruct) _exit(1);
				
				initgroups(UserStruct->pw_name, UserStruct->pw_gid);
				endgrent();
				
				if (!InObj->GroupID) setgid(UserStruct->pw_gid);

				setuid(InObj->UserID);
				
				setenv("HOME", UserStruct->pw_dir, 1);
				setenv("USER", UserStruct->pw_name, 1);
				setenv("SHELL", UserStruct->pw_shell, 1);
				
				if (!InObj->ObjectWorkingDirectory) chdir(UserStruct->pw_dir);
				
				
			}
			
			if (InObj->GroupID != 0) setgid(InObj->GroupID);
		}
			
#ifndef NOSHELL
		if (ShellEnabled && (strpbrk(CurCmd, "&^$#@!()*%{}`~+|\\<>?;:'[]\"\t") != NULL || ForceShell))
		{
			execlp(ShellPath, "sh", "-c", CurCmd, NULL); /*I bet you think that this is going to return the PID of sh. No.*/
			
			snprintf(TmpBuf, 1024, "Failed to execute %s: execlp() failure launching \"" SHELLPATH "\".", InObj->ObjectID);
			SpitError(TmpBuf);
			_exit(1); /*Makes sure that we report failure. This is a problem.*/
		}
		else
#endif
		{ /*don't worry about the heap stuff, exec() takes care of it you know.*/
			char **ArgV = NULL;
			unsigned long NumSpaces = 1, Inc = 0, cOffset = 0, Inc2 = 0;
			char NCmd[MAX_LINE_SIZE], *Worker = NCmd;
			
			strncpy(NCmd, CurCmd, strlen(CurCmd) + 1);
			
			while ((Worker = WhitespaceArg(Worker))) ++NumSpaces;
			
			ArgV = malloc(sizeof(char*) * NumSpaces + 1);
			
			for (Worker = NCmd, Inc = 0; Inc < NumSpaces; ++Inc)
			{
				/*Count how much space we need first.*/
				for (Inc2 = 0; Worker[Inc2 + cOffset] != ' ' && Worker[Inc2 + cOffset] != '\t' && Worker[Inc2 + cOffset] != '\0'; ++Inc2);
				
				ArgV[Inc] = malloc(Inc2 + 1);
				
				for (Inc2 = 0; Worker[Inc2 + cOffset] != ' ' && Worker[Inc2 + cOffset] != '\t' && Worker[Inc2 + cOffset] != '\0'; ++Inc2)
				{
					ArgV[Inc][Inc2] = Worker[Inc2 + cOffset];
				}
				ArgV[Inc][Inc2] = '\0';
				
				cOffset += Inc2 + 1;
			}
			
			ArgV[NumSpaces] = NULL;
			
			execvp(ArgV[0], ArgV);
			
			/*In this case, it could be a file not found, in which case, just have the child, us, exit gracefully.*/
			_exit(1);
			
		}
		/*We still around to talk about it? We were supposed to be imaged with the new command!*/
	}
	
	/**Parent code resumes.**/
	waitpid(LaunchPID, &RawExitStatus, 0); /*Wait for the process to exit.*/

	CurrentTask.Set = false;
	CurrentTask.Node = NULL;
	CurrentTask.TaskName = NULL;
	CurrentTask.PID = 0; /*Set back to zero for the next one.*/
	
	if (CurCmd == InObj->ObjectStartCommand)
	{
		InObj->ObjectPID = LaunchPID; /*Save our PID.*/
#ifndef NOSHELL		
		if (!ShellDissolves)
		{
			++InObj->ObjectPID; /*This probably won't always work, but 99.9999999% of the time, yes, it will.*/
		}
#endif
		if (InObj->Opts.IsService)
		{ /*If we specify that this is a service, one up the PID again.*/
			++InObj->ObjectPID;
		}
#ifndef NOMMU		
		/*The PID is obviously going to be one greater.*/
		if (InObj->Opts.Fork) ++InObj->ObjectPID;
#endif		
		/*Check if the PID we found is accurate and update it if not. This method is very,
		 * very accurate compared to the buggy morass above.*/
		AdvancedPIDFind(InObj, true);
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

rStatus ProcessConfigObject(ObjTable *CurObj, Bool IsStartingMode, Bool PrintStatus)
{
	char PrintOutStream[1024];
	rStatus ExitStatus = FAILURE;
	
	if (IsStartingMode && CurObj->ObjectStartCommand == NULL)
	{ /*Don't bother with it, if it has no command.
		For starting objects, this should not happen unless we set the option HALTONLY.*/
		return SUCCESS;
	}
	
	if (!IsStartingMode && CurObj->Opts.HaltCmdOnly &&
		!CurObj->ObjectStopCommand && CurObj->Opts.StopMode == STOP_COMMAND)
	{ /*This can happen too.*/
		return SUCCESS;
	}
	
	if (PrintStatus)
	{/*Copy in the description to be printed to the console.*/
		if (CurObj->Opts.RawDescription)
		{
			snprintf(PrintOutStream, 1024, "%s", CurObj->ObjectDescription);
		}
		else if (!IsStartingMode && CurObj->Opts.HaltCmdOnly)
		{
			snprintf(PrintOutStream, 1024, "%s %s", "Starting", CurObj->ObjectDescription);
		}
		else
		{
			snprintf(PrintOutStream, 1024, "%s %s", (IsStartingMode ? "Starting" : "Stopping"), CurObj->ObjectDescription);
		}
		
		if (IsStartingMode && CurObj->Opts.HaltCmdOnly)
		{
			RenderStatusReport(PrintOutStream);
			CompleteStatusReport(PrintOutStream, FAILURE, true);
		}
	}
	
	if (IsStartingMode && CurObj->Opts.HaltCmdOnly) return FAILURE;
	
	if (IsStartingMode)
	{		
		rStatus PrestartExitStatus = SUCCESS;
		unsigned long Counter = 0;
		
		if (PrintStatus)
		{
			RenderStatusReport(PrintOutStream);
		}
		
		/*fflush(NULL); *//*Things tend to get clogged up when we don't flush.*/
		
		
		/*This means we are doing an equivalent pivot_root...*/
		if (CurObj->Opts.PivotRoot)
		{
			unsigned long Inc = 0;
			char NewRoot[MAX_LINE_SIZE], OldRootDir[MAX_LINE_SIZE], *Worker = CurObj->ObjectStartCommand;
				
			for (; *Worker != ' ' && *Worker != '\t' && *Worker != '\0' && Inc < MAX_LINE_SIZE - 1; ++Inc, ++Worker)
			{
				NewRoot[Inc] = *Worker;
			}
			NewRoot[Inc] = '\0';
			
			Worker = WhitespaceArg(Worker);
			
			snprintf(OldRootDir, sizeof OldRootDir, "%s", Worker);
			
			PerformPivotRoot(NewRoot, OldRootDir);
			
			CompleteStatusReport(PrintOutStream, SUCCESS, true);
			return SUCCESS;
		}
		else if (CurObj->Opts.Exec)
		{ /*We are supposed to replace ourselves with this.*/
			
			PerformExec(CurObj->ObjectStartCommand);
			
			CompleteStatusReport(PrintOutStream, FAILURE, true);
			return FAILURE;
		}
			
			
		if (CurObj->ObjectPrestartCommand != NULL)
		{
			PrestartExitStatus = ExecuteConfigObject(CurObj, CurObj->ObjectPrestartCommand);
		}
		
		ExitStatus = ExecuteConfigObject(CurObj, CurObj->ObjectStartCommand);
		
		if (PrestartExitStatus != SUCCESS && ExitStatus)
		{
			char TBuf[MAX_LINE_SIZE];
			
			snprintf(TBuf, MAX_LINE_SIZE, "Prestart command %s for object \"%s\".",
					PrestartExitStatus ? "returned a warning" : "failed", CurObj->ObjectID);
			WriteLogLine(TBuf, true);
			ExitStatus = WARNING;
		}
		
		/*Wait for a PID file to appear if we specified one. This prevents autorestart hell.*/
		if (ExitStatus && CurObj->Opts.HasPIDFile)
		{
			Bool Abort = false;
			
			CurrentTask.Node = (void*)&Abort;
			CurrentTask.TaskName = CurObj->ObjectID;
			CurrentTask.PID = 0;
			CurrentTask.Set = true;
			
			for (Counter = 0; !FileUsable(CurObj->ObjectPIDFile) && Counter < 10000 && !Abort; ++Counter)
			{
				usleep(100);
			}
			
			if (Counter == 10000 && !Abort)
			{
				char OutBuf[MAX_LINE_SIZE];
				
				snprintf(OutBuf, sizeof OutBuf,CONSOLE_COLOR_YELLOW "WARNING: " CONSOLE_ENDCOLOR
						"Object %s was successfully started%s,\n"
						"but it's PID file did not appear within ten seconds of start.\n"
						"Please verify that \"%s\" exists and whether this object is starting properly.",
						CurObj->ObjectID, (ExitStatus == WARNING ? ", but with a warning" : ""),
						CurObj->ObjectPIDFile);
					
				WriteLogLine(OutBuf, true);
				ExitStatus = WARNING;
			}
			
			CurrentTask.Set = false;
			CurrentTask.Node = NULL;
			CurrentTask.TaskName = NULL;
			CurrentTask.PID = 0;
		}
		
		CurObj->Started = (ExitStatus ? true : false); /*Mark the process dead or alive.*/
		
		if (ExitStatus)
		{
			CurObj->StartedSince = time(NULL);
		}
		
		if (PrintStatus)
		{
			CompleteStatusReport(PrintOutStream, ExitStatus, true);
		}
	}
	else
	{	
		Bool LastAutoRestartState = CurObj->Opts.AutoRestart;
		/*We need to do this so objects that are stopped have no chance of restarting themselves.*/
		CurObj->Opts.AutoRestart = false;
		
		switch (CurObj->Opts.StopMode)
		{
			case STOP_COMMAND:
			{
				unsigned long Inc = 0;
				
				if (PrintStatus)
				{
					RenderStatusReport(PrintOutStream);
				}
				
				ExitStatus = ExecuteConfigObject(CurObj, CurObj->ObjectStopCommand);
				
				if (!CurObj->Opts.NoStopWait)
				{
					unsigned long CurPID = 0;
					Bool Abort = false;
					
					CurrentTask.Node = (void*)&Abort;
					CurrentTask.PID = 0;
					CurrentTask.TaskName = CurObj->ObjectID;
					CurrentTask.Set = true;
					
					for (; ObjectProcessRunning(CurObj) && Inc < CurObj->Opts.StopTimeout * 10000 && !Abort; ++Inc)
					{
						CurPID = CurObj->Opts.HasPIDFile ? ReadPIDFile(CurObj) : CurObj->ObjectPID;
						
						if (!CurPID) break; /*No PID? No point.*/
						
						waitpid(CurPID, NULL, WNOHANG);
						
						usleep(100);
					}
					
					if (Inc == CurObj->Opts.StopTimeout * 10000 || Abort)
					{ /*We timed out or something.*/
						ExitStatus = WARNING;
					}
					
					CurrentTask.Set = false;
					CurrentTask.Node = NULL;
					CurrentTask.TaskName = NULL;
					CurrentTask.PID = 0;
				}
					
				if (ExitStatus)
				{
					CurObj->ObjectPID = 0;
					CurObj->Started = false;
					CurObj->StartedSince = 0;
				}
				
				if (PrintStatus)
				{
					CompleteStatusReport(PrintOutStream, ExitStatus, true);
				}
				break;
			}
			case STOP_INVALID:
				break;
			case STOP_NONE:
				CurObj->Started = false; /*Just say we did it even if nothing to do.*/
				CurObj->StartedSince = 0;
				ExitStatus = SUCCESS;
				CurObj->ObjectPID = 0;
				break;
			case STOP_PID:
			{
				if (PrintStatus)
				{
					RenderStatusReport(PrintOutStream);
				}
				
				if (!CurObj->ObjectPID)
				{
					ExitStatus = FAILURE;
					if (PrintStatus)
					{
						CompleteStatusReport(PrintOutStream, ExitStatus, true);
					}
					break;
				}
				
				if (kill(CurObj->ObjectPID, CurObj->TermSignal) == 0)
				{ /*Just send SIGTERM.*/
					if (!CurObj->Opts.NoStopWait)
					{
						unsigned long TInc = 0;
						Bool Abort = false;
						
						CurrentTask.Node = (void*)&Abort;
						CurrentTask.PID = 0;
						CurrentTask.TaskName = CurObj->ObjectID;
						CurrentTask.Set = true;
								
						/*Give it ten seconds to terminate on it's own.*/
						for (; kill(CurObj->ObjectPID, 0) == 0 && TInc < CurObj->Opts.StopTimeout * 200 && !Abort; ++TInc)
						{
							
							waitpid(CurObj->ObjectPID, NULL, WNOHANG); /*We must harvest the PID since we have occupied the primary loop.*/
							
							usleep(50000);
						}
						
						if (TInc == CurObj->Opts.StopTimeout * 200)
						{
							ExitStatus = FAILURE;
						}
						else if (Abort)
						{
							ExitStatus = WARNING;
						}
						else
						{
							ExitStatus = SUCCESS;
						}
						
						CurrentTask.Set = false;
						CurrentTask.Node = NULL;
						CurrentTask.TaskName = NULL;
						CurrentTask.PID = 0;
					}
					else
					{ /*Just quit and say everything's fine.*/
						ExitStatus = SUCCESS;
					}
				}
				else
				{
					ExitStatus = FAILURE;
				}
				
				if (ExitStatus)
				{
					CurObj->ObjectPID = 0;
					CurObj->StartedSince = 0;
					CurObj->Started = false;
				}
				
				if (PrintStatus)
				{
					CompleteStatusReport(PrintOutStream, ExitStatus, true);
				}
				
				break;
			}
			case STOP_PIDFILE:
			{
				unsigned long TruePID = 0;
				
				if (PrintStatus)
				{
					RenderStatusReport(PrintOutStream);
				}
				
				if (!(TruePID = ReadPIDFile(CurObj)))
				{ /*This might be good with goto, but... no.*/
					ExitStatus = FAILURE;
					if (PrintStatus)
					{
						CompleteStatusReport(PrintOutStream, ExitStatus, true);
					}
					break;
				}
				
				/*Now we can actually kill the process ID.*/
				if (kill(TruePID, CurObj->TermSignal) == 0)
				{
					if (!CurObj->Opts.NoStopWait)
					{ /*If we're free to wait for a PID to stop, do so.*/
						unsigned long TInc = 0;			
						Bool Abort = false;
						
						CurrentTask.Node = (void*)&Abort;
						CurrentTask.PID = 0;
						CurrentTask.TaskName = CurObj->ObjectID;
						CurrentTask.Set = true;
						
						/*Give it ten seconds to terminate on it's own.*/
						for (; kill(TruePID, 0) == 0 && TInc < CurObj->Opts.StopTimeout * 200 && !Abort; ++TInc)
						{ /*Two hundred is ten seconds here.*/
							
							waitpid(TruePID, NULL, WNOHANG); /*We must harvest the PID since we have occupied the primary loop.*/
							
							usleep(50000);
						}
						
						if (TInc == CurObj->Opts.StopTimeout * 200)
						{
							ExitStatus = FAILURE;
						}
						else if (Abort)
						{ /*Means we were killed via CTRL-ALT-DEL.*/
							ExitStatus = WARNING;
						}
						else
						{
							ExitStatus = SUCCESS;
						}
						
						CurrentTask.Set = false;
						CurrentTask.Node = NULL;
						CurrentTask.TaskName = NULL;
						CurrentTask.PID = 0;
					}
					else
					{
						ExitStatus = SUCCESS;
					}
				}
				else
				{
					ExitStatus = FAILURE;
				}
				
				if (ExitStatus)
				{
					CurObj->Started = false;
					CurObj->StartedSince = 0;
					CurObj->ObjectPID = 0;
				}
				
				if (PrintStatus)
				{
					CompleteStatusReport(PrintOutStream, ExitStatus, true);
				}
				
				break;
			}
		}
		
		/*Now that the object is stopped, we should reset the autorestart to it's previous state.*/
		CurObj->Opts.AutoRestart = LastAutoRestartState;
	}
	
	return ExitStatus;
}

/*This function does what it sounds like. It's not the entire boot sequence, we gotta display a message and stuff.*/
rStatus RunAllObjects(Bool IsStartingMode)
{
	unsigned long MaxPriority = GetHighestPriority(IsStartingMode);
	unsigned long Inc = 1; /*One to skip zero.*/
	ObjTable *CurObj = NULL;
	ObjTable *LastNode = NULL;
	
	if (!MaxPriority && IsStartingMode)
	{
		SpitError("All objects have a priority of zero!");
		return FAILURE;
	}
	
	CurrentBootMode = (IsStartingMode ? BOOT_BOOTUP : BOOT_SHUTDOWN);
	
	for (; Inc <= MaxPriority; ++Inc)
	{
		for (LastNode = NULL;
			(CurObj = GetObjectByPriority((IsStartingMode ? CurRunlevel : NULL), LastNode, IsStartingMode, Inc));
			LastNode = CurObj)
		{ /*Probably set to zero or something, but we don't care if we have a gap in the priority system.*/
		
			if (CurObj == (void*)-1)
			{
				return FAILURE;
			}
			
			if (!CurObj->Enabled && (IsStartingMode || CurObj->Opts.HaltCmdOnly))
			{ /*Stop even disabled objects, but not disabled HALTONLY objects.*/
				continue;
			}
		
			if (IsStartingMode && CurObj->Opts.HaltCmdOnly)
			{
				continue;
			}
			
			if ((IsStartingMode ? !CurObj->Started : CurObj->Started))
			{
				ProcessConfigObject(CurObj, IsStartingMode, true);
			}
		}
	}
	
	CurrentBootMode = BOOT_NEUTRAL;
	
	return SUCCESS;
}

rStatus ProcessReloadCommand(ObjTable *CurObj, Bool PrintStatus)
{
	rStatus RetVal = FAILURE;
	char StatusReportBuf[MAX_DESCRIPT_SIZE];
	
	if (!CurObj->ObjectReloadCommand && !CurObj->ReloadCommandSignal)
	{
		return FAILURE;
	}
	
	if (PrintStatus)
	{
		snprintf(StatusReportBuf, MAX_DESCRIPT_SIZE, "Reloading %s", CurObj->ObjectID);
		RenderStatusReport(StatusReportBuf);
	}
	
	if (CurObj->ReloadCommandSignal != 0)
	{
		const unsigned long PID = CurObj->Opts.HasPIDFile ? ReadPIDFile(CurObj) : CurObj->ObjectPID;

		if (!PID) return FAILURE;
		
		RetVal = !kill(PID, CurObj->ReloadCommandSignal);
	}
	else
	{
		RetVal = ExecuteConfigObject(CurObj, CurObj->ObjectReloadCommand);
	}
	
	if (PrintStatus)
	{
		CompleteStatusReport(StatusReportBuf, RetVal, true);
	}
	
	return RetVal;
}

rStatus SwitchRunlevels(const char *Runlevel)
{
	unsigned long NumInRunlevel = 0, CurPriority = 1, MaxPriority;
	ObjTable *TObj = ObjectTable;
	ObjTable *LastNode = NULL;
	/*Check the runlevel has objects first.*/
	
	for (; TObj->Next != NULL; TObj = TObj->Next)
	{ /*I think a while loop would look much better, but if I did that,
		* I'd get folks asking "why didn't you use a for loop?", so here!*/
		if (!TObj->Opts.HaltCmdOnly && ObjRL_CheckRunlevel(Runlevel, TObj, true) &&
			TObj->Enabled && TObj->ObjectStartPriority > 0)
		{
			++NumInRunlevel;
		}
	}
	
	if (NumInRunlevel == 0)
	{
		return FAILURE;
	}
	
	/*Stop everything not meant for this runlevel.*/
	for (MaxPriority = GetHighestPriority(false); CurPriority <= MaxPriority; ++CurPriority)
	{
		for (LastNode = NULL; (TObj = GetObjectByPriority(CurRunlevel, LastNode, false, CurPriority)); LastNode = TObj)
		{
			if (TObj == (void*)-1)
			{
				return FAILURE;
			}
			
			if (TObj->Started && !TObj->Opts.Persistent && !TObj->Opts.HaltCmdOnly &&
				!ObjRL_CheckRunlevel(Runlevel, TObj, true))
			{
				ProcessConfigObject(TObj, false, true);
			}
		}
	}
	
	/*Good to go, so change us to the new runlevel.*/
	snprintf(CurRunlevel, MAX_DESCRIPT_SIZE, "%s", Runlevel);
	MaxPriority = GetHighestPriority(true);
	
	/*Now start the things that ARE meant for our runlevel.*/
	for (CurPriority = 1; CurPriority <= MaxPriority; ++CurPriority)
	{
		for (LastNode = NULL; (TObj = GetObjectByPriority(CurRunlevel, LastNode, true, CurPriority)); LastNode = TObj)
		{
			if (TObj == (void*)-1)
			{
				return FAILURE;
			}
			
			if (TObj->Enabled && !TObj->Started)
			{
				ProcessConfigObject(TObj, true, true);
			}
		}
	}
	
	return SUCCESS;
}
