// master.cpp : Defines the entry point for the console application.
//

#include <Windows.h>
#include <Psapi.h>
#include <string.h>
#include <stdio.h>

enum EExitFlag
{
	Normal = 0,
	TimeLimit,
	MemLimit,
	Runtime,
	Launch,
};

bool PrepareJobIOCPLimit(long long TL, SIZE_T ML, HANDLE &hJob, HANDLE &hIOCP)
{
	hJob = CreateJobObject(NULL, NULL);
	if (hJob == NULL)
	{
		hJob = INVALID_HANDLE_VALUE;
		return false;
	}

	hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (hIOCP == NULL)
	{
		hIOCP = INVALID_HANDLE_VALUE;
		return false;
	}

	JOBOBJECT_ASSOCIATE_COMPLETION_PORT joacp = { (PVOID)1, hIOCP };
	BOOL bRes = SetInformationJobObject(hJob, JobObjectAssociateCompletionPortInformation, &joacp, sizeof(joacp));
	if (bRes == 0)
	{
		return false;
	}

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobli = { 0 };
	// процесс всегда выполняется с классом приоритета idle
	// jobli.PriorityClass = IDLE_PRIORITY_CLASS;
	// задание не может использовать более одной секунды процессорного времени
	if (TL)
	{
		jobli.BasicLimitInformation.PerJobUserTimeLimit.QuadPart = TL; // 1 секунда, выраженная в 100-наносекундных интервалах
		jobli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_TIME;
	}
	// два ограничения, которые я налагаю на задание (процесс)
	// jobli.LimitFlags = JOB_OBJECT_LIMIT_PRIORITY_CLASS
	//	| JOB_OBJECT_LIMIT_JOB_TIME;
	if (ML)
	{
		jobli.JobMemoryLimit = ML; // 8M
		jobli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_MEMORY;
	}

	jobli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;

	bRes = SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jobli, sizeof(jobli));
	if (bRes == 0)
	{
		return false;
	}

	return true;
}

bool InitRestrictedProcess(char *pchCmd, char *pchIn, char *pchOut, HANDLE hJob, PROCESS_INFORMATION &pi)
{
	HANDLE hFileIn = INVALID_HANDLE_VALUE;
	HANDLE hFileOut = INVALID_HANDLE_VALUE;

	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	hFileIn = CreateFileA(pchIn, GENERIC_READ, 0, /*NULL*/ &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE == hFileIn)
	{
		return false;
	}

	hFileOut = CreateFileA(pchOut, GENERIC_WRITE, 0, /*NULL*/ &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE == hFileOut)
	{
		CloseHandle(hFileIn);
		return false;
	}

	STARTUPINFOA si = { sizeof(si) };
	// SetHandleInformation(hFile, HANDLE_FLAG_INHERIT, 0);
	si.hStdInput = hFileIn;
	si.hStdOutput = hFileOut;
	si.hStdError = hFileOut;
	si.dwFlags = STARTF_USESTDHANDLES;

	pi.hThread = INVALID_HANDLE_VALUE;
	pi.hProcess = INVALID_HANDLE_VALUE;

	BOOL bRes = CreateProcessA(NULL, pchCmd/*"CMD"*/, NULL, NULL, /*FALSE*/ TRUE,
		CREATE_SUSPENDED, NULL, NULL, &si, &pi);
	CloseHandle(hFileIn);
	CloseHandle(hFileOut);
	if (bRes == NULL)
	{
		return false;
	}

	// Включаем процесс в задание.
	// ПРИМЕЧАНИЕ: дочерние процессы, порождаемые этим процессом,
	// автоматически становятся частью того же задания.
	bRes = AssignProcessToJobObject(hJob, pi.hProcess);
	if (bRes == NULL)
	{
		return false;
	}

	// теперь потоки дочерних процессов могут выполнять код
	DWORD dwRes = ResumeThread(pi.hThread);
	if (dwRes == (DWORD)-1)
	{
		return false;
	}
	return true;
}

void GetJobIOCPVal(HANDLE hProcess, HANDLE hIOCP, DWORD &dwJob, DWORD &dwExitCode)
{
	ULONG_PTR ulFlag;
	LPOVERLAPPED po;
	while (dwJob == 0 || dwJob == JOB_OBJECT_MSG_NEW_PROCESS || dwJob == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO)
	{
		GetQueuedCompletionStatus(hIOCP, &dwJob, &ulFlag, &po, INFINITE);
	}
	GetExitCodeProcess(hProcess, &dwExitCode);
}

void ConvertJobExit(DWORD dwJob, DWORD dwExitCode, EExitFlag &ret, const char * &pchError)
{
	switch (dwJob)
	{
		case 0:											ret = Runtime;   pchError = "Unknown"; break;
		case JOB_OBJECT_MSG_END_OF_JOB_TIME:			ret = TimeLimit; pchError = "Job time limit exceeded"; break;
		case JOB_OBJECT_MSG_END_OF_PROCESS_TIME:		ret = TimeLimit; pchError = "Process time limit exceeded"; break;
		case JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT:		ret = Runtime;   pchError = "Number of active processes is in limit"; break;
		case JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO:		ret = Runtime;   pchError = "No active processes"; break; // Never
		case JOB_OBJECT_MSG_NEW_PROCESS:				ret = Runtime;   pchError = "New process created"; break; // Never
		case JOB_OBJECT_MSG_EXIT_PROCESS:				ret = Normal;    pchError = "Process exited"; break;
		case JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS:		ret = Runtime;   pchError = "Process crashed"; break;
		case JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT:		ret = MemLimit;  pchError = "Process memory limit exceeded"; break;
		case JOB_OBJECT_MSG_JOB_MEMORY_LIMIT:			ret = MemLimit;  pchError = "Job memory limit exceeded"; break;
		//case JOB_OBJECT_MSG_NOTIFICATION_LIMIT:			ret = Runtime;   pchError = "Notification limit"; break;
		//case JOB_OBJECT_MSG_JOB_CYCLE_TIME_LIMIT:		ret = Runtime;   pchError = "CPU cycle limit exceeded"; break;
		//case JOB_OBJECT_MSG_SILO_TERMINATED:			ret = Runtime;   pchError = "SILO terminated"; break;
		default:										ret = Runtime;   pchError = "Unknown 2"; break;
	}
}

EExitFlag CreateRestrictedProcess(char *pchCmd, long long TL, SIZE_T ML, char *pchIn, char *pchOut)
{
	EExitFlag ret = Launch;

	HANDLE hJob = INVALID_HANDLE_VALUE;
	HANDLE hIOCP = INVALID_HANDLE_VALUE;

	bool bOk = PrepareJobIOCPLimit(TL, ML, hJob, hIOCP);
	if (bOk)
	{
		PROCESS_INFORMATION pi;
		bOk = InitRestrictedProcess(pchCmd, pchIn, pchOut, hJob, pi);
		if (bOk)
		{
			DWORD dwJob = 0;
			DWORD dwExitCode = 0;
			GetJobIOCPVal(pi.hProcess, hIOCP, dwJob, dwExitCode);

			JOBOBJECT_BASIC_ACCOUNTING_INFORMATION jobai;
			QueryInformationJobObject(hJob, JobObjectBasicAccountingInformation, &jobai, sizeof(jobai), NULL);
			long long userTime_100ns = jobai.TotalUserTime.QuadPart;
			long long
				t_s = userTime_100ns / 10000000,
				t_ms = userTime_100ns / 10000 % 1000,
				t_us = userTime_100ns / 10 % 1000,
				t_100ns = userTime_100ns % 10;

			const char *pchError;
			ConvertJobExit(dwJob, dwExitCode, ret, pchError);
			//::MessageBoxA(0, pchError, "Create Restricted Process", MB_ICONINFORMATION);
		}

		// проводим очистку процессов
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}

	// проводим очистку
	CloseHandle(hIOCP);
	CloseHandle(hJob);

	return ret;
}

int main(int argc, char **argv)
{
	// argv
	// 0 exe
	// 1 pchCmd
	// 2 TL (ms)
	// 3 ML (KiBytes) >= 512
	// 4 pchIn
	// 5 pchOut
	const char *pchCmd = "cmd", *pchIn = "con", *pchOut = "con";
	long long TL = 0;
	SIZE_T ML = 0;
	EExitFlag ef = Normal;
	if (argc > 5)
	{
		// parse
		pchCmd = argv[1];
		sscanf(argv[2], "%lli", &TL);
		TL *= 10000;
		long long _ml;
		sscanf(argv[3], "%lli", &_ml);
		ML = (SIZE_T)_ml;
		if (ML < 512 && ML != 0)
		{
			ML = 512;
		}
		ML *= 1024;
		pchIn = argv[4];
		pchOut = argv[5];

		// Create Restricted Process
		ef = CreateRestrictedProcess((char*)pchCmd, TL, ML, (char*)pchIn, (char*)pchOut);
	}
	else
	{
		system("type readme.md");
	}

	return ef;
}

