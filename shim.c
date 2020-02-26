#include <Windows.h>

HANDLE process_heap;

#define ALLOC(T, size) \
	(T*) HeapAlloc(process_heap, HEAP_GENERATE_EXCEPTIONS, (size) * sizeof(T))
#define FREE(p) HeapFree(process_heap, 0, p);

#ifdef USE_FREE_OPT
#define FREE_OPT FREE
#else
#define FREE_OPT(p)
#endif

void* memset(void* target, int value, size_t size) { // used by clang
	unsigned char* p = (unsigned char*) target;
	unsigned char c = (unsigned char) value;
	while (size--)
		*p++ = c;
	return target;
}

// if str starts with pattern, return the pointer after pattern
char* found(char* str, char* pattern) {
	for (; *pattern; pattern++, str++)
		if (*str != *pattern)
			return NULL;
	return str;
}

char* find_eol(char* p) {
	while (*p && *p != '\r' && *p != '\n')
		p++;
	return p;
}

wchar_t* find_nul_wide(wchar_t* p) {
	while (*p)
		p++;
	return p;
}

void copy_wide(wchar_t* dest, wchar_t* src, size_t n) {
	for (; n; dest++, src++, n--)
		*dest = *src;
}

wchar_t* utf8_to_utf16(char* str, char* str_end, int* size) {
	*size = MultiByteToWideChar(CP_UTF8, 0, str, str_end - str, NULL, 0);
	if (!*size)
		return NULL;
	wchar_t* buffer = ALLOC(wchar_t, *size);
	if (!MultiByteToWideChar(CP_UTF8, 0, str, str_end - str, buffer, *size))
		return NULL;
	return buffer;
}

BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
	switch (ctrl_type) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		return TRUE;
	default:
		return FALSE;
	}
}

int process(void) {
	process_heap = GetProcessHeap();

	// get current exe file name
	wchar_t* filename;
	DWORD filename_len;
	for (DWORD size = 256;; size *= 2) {
		filename = ALLOC(wchar_t, size);
		filename_len = GetModuleFileNameW(NULL, filename, size);
		if (!filename_len)
			return GetLastError();
		if (filename_len + 1 < size) // make space for .shim
			break;
		FREE(filename);
	}

	// change extension to shim
	wchar_t shim_ext[] = L"shim";
	for (int i = 0; i < sizeof(shim_ext) / sizeof(shim_ext[0]); ++i) {
		filename[filename_len - 3 + i] = shim_ext[i];
		// max index == filename_len + 1 < size
	}

	// read entire shim file and append '\0'
	HANDLE file =
	    CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return GetLastError();
	FREE_OPT(filename);
	LARGE_INTEGER file_size;
	if (!GetFileSizeEx(file, &file_size))
		return GetLastError();
	if (file_size.HighPart)
		return ERROR_FILE_TOO_LARGE;
	char* content = ALLOC(char, file_size.LowPart + 1);
	content[file_size.LowPart] = '\0';
	DWORD read_file_size_out;
	if (!ReadFile(file, content, file_size.LowPart, &read_file_size_out, NULL))
		return GetLastError();

	// parse path and args
	char* p = content;
	char* q;
	if ((q = found(p, "\xEF\xBB\xBF")))
		p = q; // UTF-8 BOM
	char* path = NULL;
	char* path_end = NULL;
	char* args = NULL;
	char* args_end = NULL;
	while (*p) { // immediately break on EOF
		if ((q = found(p, "path = "))) {
			path = q;
			p = path_end = find_eol(path);
		} else if ((q = found(p, "args = "))) {
			args = q;
			p = args_end = find_eol(path);
		} else
			break;
		if (*p == '\r')
			++p;
		if (*p == '\n')
			++p;
	}
	if (!path)
		return ERROR_BAD_ARGUMENTS;

	// convert to UTF-16
	int path_len = 0, args_len = 0;
	wchar_t* path_wide = utf8_to_utf16(path, path_end, &path_len);
	wchar_t* args_wide = NULL;
	if (args)
		args_wide = utf8_to_utf16(args, args_end, &args_len);

	// get current cmd line
	wchar_t* cmd_args = GetCommandLineW();
	if (*cmd_args == '"') {
		++cmd_args;
		while (*cmd_args && *cmd_args != '"')
			++cmd_args;
		if (*cmd_args)
			++cmd_args;
	} else if (*cmd_args && *cmd_args > ' ') {
		while (*cmd_args && *cmd_args > ' ')
			++cmd_args;
	}
	DWORD cmd_args_len = find_nul_wide(cmd_args) - cmd_args;

	// concat cmd line
	wchar_t* new_cmd =
	    ALLOC(wchar_t, 2 + path_len + !!args_len + args_len + cmd_args_len + 1);
	wchar_t* r = new_cmd;
	*r++ = L'"';
	copy_wide(r, path_wide, path_len);
	r += path_len;
	*r++ = L'"';
	if (args_len) {
		*r++ = L' ';
		copy_wide(r, args_wide, args_len);
		r += args_len;
	}
	copy_wide(r, cmd_args, cmd_args_len);
	r += cmd_args_len;
	*r = L'\0';
	FREE_OPT(content);
	FREE_OPT(path_wide);
	FREE_OPT(args_wide);

	// create job
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info = {};
	HANDLE job = CreateJobObject(NULL, NULL);
	job_info.BasicLimitInformation.LimitFlags =
	    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
	    JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
	SetInformationJobObject(job, JobObjectExtendedLimitInformation, &job_info,
	                        sizeof(job_info));

	// create process
	STARTUPINFOW startup_info = {sizeof(startup_info)};
	PROCESS_INFORMATION process_info = {};
	if (!CreateProcessW(NULL, new_cmd, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL,
	                    NULL, &startup_info, &process_info))
		return GetLastError();
	FREE_OPT(new_cmd);

	// configurate and wait for process
	AssignProcessToJobObject(job, process_info.hProcess);
	if (ResumeThread(process_info.hThread) == (DWORD) -1)
		return GetLastError();
	SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
	WaitForSingleObject(process_info.hProcess, INFINITE);
	DWORD exit_code;
	GetExitCodeProcess(process_info.hProcess, &exit_code);
	return exit_code;
}

int entry(void) { return ExitProcess(process()), 0; }
