/*This code is part of the Epoch Boot System.
* The Epoch Boot System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**This file handles the parsing of epoch.conf, our configuration file.
 * It adds everything into the object table.**/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include "epoch.h"

/*We want the only interface for this to be LookupObjectInTable().*/
ObjTable *ObjectTable = NULL;

/*Function forward declarations for all the statics.*/
static ObjTable *AddObjectToTable(const char *ObjectID);
static char *NextLine(const char *InStream);
static char *NextSpace(const char *InStream);
static rStatus GetLineDelim(const char *InStream, char *OutStream);
static rStatus ScanConfigIntegrity(void);

/*Actual functions.*/
static char *NextLine(const char *InStream)
{
	if (!(InStream = strstr(InStream, "\n")))
	{
		return NULL;
	}

	if (*(InStream + 1) == '\0')
	{ /*End of file.*/
		return NULL;
	}

	++InStream; /*Plus one for the newline. We want to skip past it.*/

	return (char*)InStream;
}

static char *NextSpace(const char *InStream)
{  /*This is used for parsing lines that need to be divided by spaces.*/
	if (!(InStream = strstr(InStream, " ")))
	{
		return NULL;
	}

	if (*(InStream + 1) == '\0')
	{
		return NULL;
	}

	++InStream;

	return (char*)InStream;
}

rStatus InitConfig(void)
{ /*Set aside storage for the table.*/
	FILE *Descriptor = NULL;
	struct stat FileStat;
	char *ConfigStream = NULL, *Worker = NULL;
	ObjTable *CurObj = NULL;
	char DelimCurr[MAX_LINE_SIZE];
	unsigned long LineNum = 1;
	
	/*Get the file size of the config file.*/
	if (stat(CONFIGDIR CONF_NAME, &FileStat) != 0)
	{ /*Failure?*/
		SpitError("Failed to obtain information about configuration file epoch.conf.\nDoes it exist?");
		return FAILURE;
	}
	else
	{ /*No? Use the file size to allocate space in memory, since a char is a byte big.
	* If it's not a byte on your platform, your OS is not UNIX, and Epoch was not designed for you.*/
		ConfigStream = malloc(FileStat.st_size + 1);
	}

	Descriptor = fopen(CONFIGDIR CONF_NAME, "r"); /*Open the configuration file.*/

	/*Read the file into memory. I don't really trust fread(), but oh well.
	 * People will whine if I use a loop instead.*/
	fread(ConfigStream, 1, FileStat.st_size, Descriptor);
	
	ConfigStream[FileStat.st_size] = '\0'; /*Null terminate.*/
	
	fclose(Descriptor); /*Close the file.*/

	Worker = ConfigStream;

	/*Empty file?*/
	if ((*Worker == '\n' && *(Worker + 1) == '\0') || *Worker == '\0')
	{
		SpitError("Seems that epoch.conf is empty or corrupted.");
		return FAILURE;
	}

	do /*This loop does most of the parsing.*/
	{
		if (*Worker == '\n')
		{ /*Empty line.*/
			continue;
		}
		else if (*Worker == '#')
		{ /*Line is just a comment.*/
			continue;
		}
		else if (!strncmp(Worker, "DisableCAD", strlen("DisableCAD")))
		{ /*Should we disable instant reboots on CTRL-ALT-DEL?*/

			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute DisableCAD in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}

			if (!strcmp(DelimCurr, "true"))
			{
				DisableCAD = true;
			}
			else if (!strcmp(DelimCurr, "false"))
			{
				DisableCAD = false;
			}
			else
			{
				char TmpBuf[1024];
				
				DisableCAD = true;
				
				snprintf(TmpBuf, 1024, "Bad value %s for attribute DisableCAD at line %lu.\n"
						"Valid values are true and false. Assuming yes.",
						DelimCurr, LineNum);
						
				SpitWarning(TmpBuf);
			}

			continue;
		}
		/*Now we get into the actual attribute tags.*/
		else if (!strncmp(Worker, "BootBannerText", strlen("BootBannerText")))
		{ /*The text shown at boot up as a kind of greeter, before we start executing objects. Can be disabled, off by default.*/
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute BootBannerText in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			if (!strcmp(DelimCurr, "NONE")) /*So, they decided to explicitly opt out of banner display. Ok.*/
			{
				BootBanner.BannerText[0] = '\0';
				BootBanner.BannerColor[0] = '\0';
				BootBanner.ShowBanner = false; /*Should already be false, but to prevent possible bugs...*/
				continue;
			}
			strncat(BootBanner.BannerText, DelimCurr, 512);
			
			BootBanner.ShowBanner = true;
			continue;
		}
		else if (!strncmp(Worker, "BootBannerColor", strlen("BootBannerColor")))
		{ /*Color for boot banner.*/
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute BootBannerColor in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			if (!strcmp(DelimCurr, "NONE")) /*They don't want a color.*/
			{
				BootBanner.BannerColor[0] = '\0';
				continue;
			}
			
			SetBannerColor(DelimCurr); /*Function to be found elsewhere will do this for us, otherwise this loop would be even bigger.*/
			continue;
		}
		else if (!strncmp(Worker, "DefaultRunlevel", strlen("DefaultRunlevel")))
		{
			if (CurObj != NULL)
			{ /*What the warning says. It'd get all weird if we allowed that.*/
				char TmpBuf[1024];
				
				snprintf(TmpBuf, 1024, "Attribute DefaultRunlevel cannot be set after an ObjectID attribute; epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute DefaultRunlevel in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}	
			
			strncpy(CurRunlevel, DelimCurr, MAX_DESCRIPT_SIZE);
			
			continue;
		}
		else if (!strncmp(Worker, "ObjectID", strlen("ObjectID")))
		{ /*ASCII value used to identify this object internally, and also a kind of short name for it.*/

			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectID in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}

			CurObj = AddObjectToTable(DelimCurr); /*Sets this as our current object.*/

			continue;
		}
		else if (!strncmp(Worker, "ObjectEnabled", strlen("ObjectEnabled")))
		{
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectEnabled comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				
				SpitError(TmpBuf);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectEnabled in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			if (!strcmp(DelimCurr, "true"))
			{
				CurObj->Enabled = true;
			}
			else if (!strcmp(DelimCurr, "false"))
			{
				CurObj->Enabled = false;
			}
			else
			{
				char TmpBuf[1024];
				
				CurObj->Enabled = true;
				
				snprintf(TmpBuf, 1024, "Bad value %s for attribute ObjectEnabled for object %s at line %lu.\n"
						"Valid values are true and false.",
						DelimCurr, CurObj->ObjectID, LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			continue;
		}
		else if (!strncmp(Worker, "ObjectPersistent", strlen("ObjectPersistent")))
		{
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectPersistent comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				
				SpitError(TmpBuf);
				return FAILURE;
			}
			if (!GetLineDelim(Worker, DelimCurr))
			{ /*It's not just a word.*/
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectPersistent in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			if (!strcmp("true", DelimCurr))
			{
				CurObj->CanStop = false;
			}
			else if (!strcmp("false", DelimCurr))
			{
				CurObj->CanStop = true;
			}
			else
			{
				char TmpBuf[1024];
				
				snprintf(TmpBuf, 1024, "Bad value for attribute ObjectPersistent in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				return FAILURE;
			}
			
			continue;
		}
		else if (!strncmp(Worker, "ObjectName", strlen("ObjectName")))
		{ /*It's description.*/
			
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectName comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				
				SpitError(TmpBuf);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectName in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			strncpy(CurObj->ObjectName, DelimCurr, MAX_DESCRIPT_SIZE);

			continue;
		}
		else if (!strncmp(Worker, "ObjectStartCommand", strlen("ObjectStartCommand")))
		{ /*What we execute to start it.*/
			
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStartCommand comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				
				SpitError(TmpBuf);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStartCommand in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			strncpy(CurObj->ObjectStartCommand, DelimCurr, MAX_LINE_SIZE);
			continue;
		}
		else if (!strncmp(Worker, "ObjectStopCommand", strlen("ObjectStopCommand")))
		{ /*If it's "PID", then we know that we need to kill the process ID only. If it's "NONE", well, self explanitory.*/
			
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStopCommand comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				
				SpitError(TmpBuf);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStopCommand in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}

			if (!strncmp(DelimCurr, "PID", strlen("PID")))
			{
				CurObj->StopMode = STOP_PID;
			}
			else if (!strncmp(DelimCurr, "PIDFILE", strlen("PIDFILE")))
			{ /*They want us to kill a PID file on exit.*/
				char *Worker = DelimCurr;
				
				Worker += strlen("PIDFILE");
				
				while (*Worker == ' ')
				{ /*Skip past all spaces.*/
					++Worker;
				}
				
				strncpy(CurObj->ObjectPIDFile, Worker, MAX_LINE_SIZE);
				
				CurObj->StopMode = STOP_PIDFILE;
			}
			else if (!strncmp(DelimCurr, "NONE", strlen("NONE")))
			{
				CurObj->StopMode = STOP_NONE;
			}
			else
			{
				CurObj->StopMode = STOP_COMMAND;
				strncpy(CurObj->ObjectStopCommand, DelimCurr, MAX_LINE_SIZE);
			}
			continue;
		}
		else if (!strncmp(Worker, "ObjectStartPriority", strlen("ObjectStartPriority")))
		{
			/*The order in which this item is started. If it is disabled in this runlevel, the next object in line is executed, IF
			 * and only IF it is enabled. If not, the one after that and so on.*/
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStartPriority comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				
				SpitError(TmpBuf);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStartPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			if (!isdigit(DelimCurr[0])) /*Make sure we are getting a number, not Shakespeare.*/
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Bad non-integer value for attribute ObjectStartPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			CurObj->ObjectStartPriority = atoi(DelimCurr);
			continue;
		}
		else if (!strncmp(Worker, "ObjectStopPriority", strlen("ObjectStopPriority")))
		{
			/*Same as above, but used for when the object is being shut down.*/
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStopPriority comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				
				SpitError(TmpBuf);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStopPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			if (!isdigit(DelimCurr[0]))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Bad non-integer value for attribute ObjectStopPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			CurObj->ObjectStopPriority = atoi(DelimCurr);
			continue;
		}
		else if (!strncmp(Worker, "ObjectRunlevels", strlen("ObjectRunlevels")))
		{ /*Runlevel.*/
			char *TWorker;
			char TRL[MAX_DESCRIPT_SIZE], *TRL2;
			
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectRunlevels comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				
				SpitError(TmpBuf);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectRunlevels in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			TWorker = DelimCurr;
			
			do
			{
				for (TRL2 = TRL; *TWorker != ' ' && *TWorker != '\t' && *TWorker != '\n' && *TWorker != '\0'; ++TWorker, ++TRL2)
				{
					*TRL2 = *TWorker;
				}
				*TRL2 = '\0';
				
				ObjRL_AddRunlevel(TRL, CurObj);
				
			} while ((TWorker = NextSpace(TWorker)));
			
			continue;

		}
		else
		{ /*No big deal.*/
			char TmpBuf[1024];
			snprintf(TmpBuf, 1024, "Unidentified attribute in epoch.conf on line %lu.", LineNum);
			SpitWarning(TmpBuf);
			
			continue;
		}
	} while (++LineNum, (Worker = NextLine(Worker)));
	
	if (!ScanConfigIntegrity())
	{ /*We failed integrity checking.*/
		fprintf(stderr, CONSOLE_COLOR_MAGENTA "Beginning dump of epoch.conf to console.\n" CONSOLE_ENDCOLOR);
		fprintf(stderr, "%s", ConfigStream);
		fflush(NULL);
		
		return FAILURE;
	}
		
	free(ConfigStream); /*Release ConfigStream, since we only use the object table now.*/

	return SUCCESS;
}

static rStatus GetLineDelim(const char *InStream, char *OutStream)
{
	unsigned long cOffset, Inc = 0;

	/*Jump to the first tab or space. If we get a newline or null, problem.*/
	while (InStream[Inc] != '\t' && InStream[Inc] != ' ' && InStream[Inc] != '\n' && InStream[Inc] != '\0') ++Inc;

	/*Hit a null or newline before tab or space. ***BAD!!!*** */
	if (InStream[Inc] == '\0' || InStream[Inc] == '\n')
	{
		char TmpBuf[1024];
		char ObjectInQuestion[1024];
		unsigned long IncT = 0;

		for (; InStream[IncT] != '\0' && InStream[IncT] != '\n'; ++IncT)
		{
			ObjectInQuestion[IncT] = InStream[IncT];
		}
		ObjectInQuestion[IncT] = '\0';

		snprintf(TmpBuf, 1024, "No parameter for attribute \"%s\" in epoch.conf.", ObjectInQuestion);

		SpitError(TmpBuf);

		return FAILURE;
	}

	/*Continue until we are past all tabs and spaces.*/
	while (InStream[Inc] == ' ' || InStream[Inc] == '\t') ++Inc;

	cOffset = Inc; /*Store this offset.*/

	/*Copy over the argument to the parameter. Quit whining about the loop copy.*/
	for (Inc = 0; InStream[Inc + cOffset] != '\n' && InStream[Inc + cOffset] != '\0' && Inc < MAX_LINE_SIZE - 1; ++Inc)
	{
		OutStream[Inc] = InStream[Inc + cOffset];
	}
	OutStream[Inc] = '\0';

	return SUCCESS;
}

rStatus EditConfigValue(const char *ObjectID, const char *Attribute, const char *Value)
{ /*Looks up the attribute for the passed ID and replaces the value for that attribute.*/
	
	/**Have fun reading this one, boys! Behehehehehh!!!
	 * I'm going to submit this to one of those bad code
	 * archive sites!**/
	char *Worker1, *Worker2, *Worker3;
	char *MasterStream, *HalfTwo;
	char LineWorker[2][MAX_LINE_SIZE];
	FILE *Descriptor;
	struct stat FileStat;
	unsigned long TempVal = 0;
	
	if (stat(CONFIGDIR CONF_NAME, &FileStat) != 0)
	{
		SpitError("EditConfigValue(): Failed to stat " CONFIGDIR CONF_NAME ". Does the file exist?");
		return FAILURE;
	}
	
	if ((Descriptor = fopen(CONFIGDIR CONF_NAME, "r")) == NULL)
	{
		SpitError("EditConfigValue(): Failed to open " CONFIGDIR CONF_NAME ". Are permissions correct?");
		return FAILURE;
	}
	
	MasterStream = malloc(FileStat.st_size + 1);
	
	fread(MasterStream, 1, FileStat.st_size, Descriptor);
	MasterStream[FileStat.st_size] = '\0';
	
	fclose(Descriptor);
	
	Worker1 = MasterStream;
	
	if (!(Worker1 = strstr(Worker1, ObjectID)))
	{
		snprintf(LineWorker[0], MAX_LINE_SIZE, "EditConfigValue(): No ObjectID %s present in epoch.conf.", ObjectID);
		SpitError(LineWorker[0]);
		free(MasterStream);
		return FAILURE;
	}
	
	Worker2 = Worker1;
	
	if ((Worker2 = strstr(Worker2, "ObjectID")))
	{
		*Worker2 = '\0';
	}

	if (!(Worker1 = strstr(Worker1, Attribute)))
	{
		snprintf(LineWorker[0], MAX_LINE_SIZE, "EditConfigValue(): Object %s specifies no %s attribute.", ObjectID, Attribute);
		SpitError(LineWorker[0]);
		free(MasterStream);
		return FAILURE;
	}
	
	if (Worker2) *Worker2 = 'O'; /*Letter O.*/
	
	/*Now copy in the line with our value.*/
	Worker2 = Worker1;
	Worker3 = LineWorker[1];
	
	
	for (; *Worker2 != '\n' && *Worker2 != '\0'; ++Worker2, ++Worker3)
	{
		*Worker3 = *Worker2;
	}
	*Worker3 = '\0';
	
	/*Now, terminate MasterStream at the beginning of our attribute, to keep it as a HalfOne for us.*/
	*Worker1 = '\0';
	
	/*Allocate and copy in HalfTwo, which is everything beyond our line.*/
	HalfTwo = malloc(strlen(Worker2) + 1);
	snprintf(HalfTwo, strlen(Worker2) + 1, Worker2);
	
	/*Edit the value.*/
	Worker3 = LineWorker[1];
	
	if (!strstr(Worker3, " "))
	{
		if (strlen(Worker3) < (MAX_LINE_SIZE - 1))
		{
			TempVal = strlen(Worker3);
			Worker3[TempVal++] = ' ';
			Worker3[TempVal] = '\0';
		}
		else
		{
			snprintf(LineWorker[0], MAX_LINE_SIZE, "EditConfigValue(): Malformed attribute %s for object %s: No value.",
					Attribute, ObjectID);
			SpitError(LineWorker[0]);
			
			free(HalfTwo);
			free(MasterStream);
			return FAILURE;
		}
		
	}
	
	for (; *Worker3 != ' ' && *Worker3 != '\n' &&
		*Worker3 != '\0'; ++Worker3) ++TempVal; /*We have to get to the spaces anyways. Harvest string length up until a space.*/
	for (; *Worker3 == ' '; ++Worker3) ++TempVal;
	
	strncpy(Worker3, Value, MAX_LINE_SIZE - TempVal);
	
	/*Now record it back to disk.*/
	if ((Descriptor = fopen(CONFIGDIR CONF_NAME, "w")))
	{
		MasterStream = realloc(MasterStream, (TempVal = strlen(MasterStream) + strlen(LineWorker[1]) + strlen(HalfTwo) + 1));
		
		/*We do a really ugly hack here. See first argument to snprintf().*/
		snprintf(&MasterStream[strlen(MasterStream)], TempVal, "%s%s", LineWorker[1], HalfTwo);
		
		fwrite(MasterStream, 1, strlen(MasterStream), Descriptor);
		fclose(Descriptor);
	}
	else
	{
		SpitError("EditConfigValue(): Unable to open " CONFIGDIR CONF_NAME " for writing. No write permission?");
	}
	
	free(MasterStream);
	free(HalfTwo);
	
	return SUCCESS;
}

/*Adds an object to the table and, if the first run, sets up the table.*/
static ObjTable *AddObjectToTable(const char *ObjectID)
{
	ObjTable *Worker = ObjectTable;
	
	/*See, we actually allocate two cells initially. The base and it's node.
	 * We always keep a free one open. This is just more convenient.*/
	if (ObjectTable == NULL)
	{
		ObjectTable = malloc(sizeof(ObjTable));
		ObjectTable->Next = malloc(sizeof(ObjTable));
		ObjectTable->Next->Next = NULL;
		ObjectTable->Next->Prev = ObjectTable;
		ObjectTable->Prev = NULL;

		Worker = ObjectTable;
	}
	else
	{
		while (Worker->Next)
		{
			Worker = Worker->Next;
		}

		Worker->Next = malloc(sizeof(ObjTable));
		Worker->Next->Next = NULL;
		Worker->Next->Prev = Worker;
	}

	/*This is the first thing that must ever be initialized, because it's how we tell objects apart.*/
	strncpy(Worker->ObjectID, ObjectID, MAX_DESCRIPT_SIZE);
	
	/*Initialize these to their default values. Used to test integrity before execution begins.*/
	Worker->Started = false;
	Worker->ObjectName[0] = '\0';
	Worker->ObjectStartCommand[0] = '\0';
	Worker->ObjectStopCommand[0] = '\0';
	Worker->ObjectPIDFile[0] = '\0';
	Worker->ObjectStartPriority = 0;
	Worker->ObjectStopPriority = 0;
	Worker->StopMode = STOP_INVALID;
	Worker->CanStop = true;
	Worker->ObjectPID = 0;
	Worker->ObjectRunlevels = malloc(sizeof(struct _RLTree));
	Worker->ObjectRunlevels->Next = NULL;
	Worker->Enabled = 2; /*We can indeed store this in a bool you know. There's no 1 bit datatype.*/
	
	return Worker;
}

static rStatus ScanConfigIntegrity(void)
{ /*Here we check common mistakes and problems.*/
	ObjTable *Worker = ObjectTable, *TOffender;
	char TmpBuf[1024];
	
	for (; Worker->Next != NULL; Worker = Worker->Next)
	{
		if (*Worker->ObjectName == '\0')
		{
			snprintf(TmpBuf, 1024, "Object %s has no attribute ObjectName.", Worker->ObjectID);
			SpitError(TmpBuf);
			return FAILURE;
		}
		else if (*Worker->ObjectStartCommand == '\0' && *Worker->ObjectStopCommand == '\0')
		{
			snprintf(TmpBuf, 1024, "Object %s has neither ObjectStopCommand nor ObjectStartCommand attributes.", Worker->ObjectID);
			SpitError(TmpBuf);
			return FAILURE;
		}
		else if (Worker->StopMode == STOP_INVALID)
		{
			snprintf(TmpBuf, 1024, "Internal error when loading StopMode for Object \"%s\".", Worker->ObjectID);
			SpitError(TmpBuf);
			return FAILURE;
		}
		else if (Worker->ObjectRunlevels == NULL)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" has no attribute ObjectRunlevels.", Worker->ObjectID);
			SpitError(TmpBuf);
			return FAILURE;
		}
		else if (Worker->Enabled == 2)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" has no attribute ObjectEnabled.", Worker->ObjectID);
			SpitError(TmpBuf);
			return FAILURE;
		}
		
		/*Check for duplicate ObjectIDs.*/
		for (TOffender = ObjectTable; TOffender->Next != NULL; TOffender = TOffender->Next)
		{
			if (!strcmp(Worker->ObjectID, TOffender->ObjectID) && Worker != TOffender)
			{
				snprintf(TmpBuf, 1024, "Two objects in configuration with ObjectID \"%s\".", Worker->ObjectID);
				SpitError(TmpBuf);
				return FAILURE;
			}			
		}
	}
	
	for (Worker = ObjectTable; Worker->Next != NULL; Worker = Worker->Next)
	{
		if (((TOffender = GetObjectByPriority(NULL, true, Worker->ObjectStartPriority)) != NULL ||
			(TOffender = GetObjectByPriority(NULL, false, Worker->ObjectStopPriority)) != NULL) &&
			strcmp(TOffender->ObjectID, Worker->ObjectID) != 0 && TOffender->Enabled && Worker->Enabled)
		{ /*We got a priority collision.*/
			snprintf(TmpBuf, 1024, "Two objects in configuration with the same priority.\n"
			"They are \"%s\" and \"%s\". This could lead to strange behaviour.", Worker->ObjectID, TOffender->ObjectID);
			SpitWarning(TmpBuf);
			return WARNING;
		}
	}
	return SUCCESS;
}
	
/*Find an object in the table and return a pointer to it. This function is public
 * because while we don't want other places adding to the table, we do want read
 * access to the table.*/
ObjTable *LookupObjectInTable(const char *ObjectID)
{
	ObjTable *Worker = ObjectTable;

	if (!ObjectTable)
	{
		return NULL;
	}
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		if (!strcmp(Worker->ObjectID, ObjectID))
		{
			return Worker;
		}
	}

	return NULL;
}

/*Get the max priority number we need to scan.*/
unsigned long GetHighestPriority(Bool WantStartPriority)
{
	ObjTable *Worker = ObjectTable;
	unsigned long CurHighest = 0;
	unsigned long TempNum;
	
	if (!ObjectTable)
	{
		return 0;
	}
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		TempNum = (WantStartPriority ? Worker->ObjectStartPriority : Worker->ObjectStopPriority);
		
		if (TempNum > CurHighest)
		{
			CurHighest = TempNum;
		}
		else if (TempNum == 0)
		{ /*We always skip anything with a priority of zero. That's like saying "DISABLED".*/
			continue;
		}
	}
	
	return CurHighest;
}

/*Functions for runlevel management.*/
Bool ObjRL_CheckRunlevel(const char *InRL, ObjTable *InObj)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels;
	
	for (; Worker->Next != NULL; Worker = Worker->Next)
	{
		if (!strcmp(Worker->RL, InRL))
		{
			return true;
		}
	}
	
	return false;
}
	
void ObjRL_AddRunlevel(const char *InRL, ObjTable *InObj)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels;
	
	while (Worker->Next != NULL) Worker = Worker->Next;
	
	Worker->Next = malloc(sizeof(struct _RLTree));
	Worker->Next->Next = NULL;
	
	strncpy(Worker->RL, InRL, MAX_DESCRIPT_SIZE);
}

void ObjRL_ShutdownRunlevels(ObjTable *InObj)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels, *NDel;
	
	for (; Worker != NULL; Worker = NDel)
	{
		NDel = Worker->Next;
		free(Worker);
	}
	
	InObj->ObjectRunlevels = NULL;
}
	

ObjTable *GetObjectByPriority(const char *ObjectRunlevel, Bool WantStartPriority, unsigned long ObjectPriority)
{ /*The primary lookup function to be used when executing commands.*/
	ObjTable *Worker = ObjectTable;
	
	if (!ObjectTable)
	{
		return NULL;
	}
	
	for (; Worker->Next != NULL; Worker = Worker->Next)
	{
		if ((ObjectRunlevel == NULL || ObjRL_CheckRunlevel(ObjectRunlevel, Worker)) && 
		/*As you can see by below, I obfuscate with efficiency!*/
		(WantStartPriority ? Worker->ObjectStartPriority : Worker->ObjectStopPriority) == ObjectPriority)
		{
			return Worker;
		}
	}
	
	return NULL;
}

void ShutdownConfig(void)
{
	ObjTable *Worker = ObjectTable, *Temp;

	for (; Worker != NULL; Worker = Temp)
	{
		if (Worker->Next)
		{
			ObjRL_ShutdownRunlevels(Worker);
		}
		
		Temp = Worker->Next;
		free(Worker);
	}
	
	ObjectTable = NULL;
}
