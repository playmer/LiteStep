/*
This is a part of the LiteStep Shell Source code.

Copyright (C) 1997-2002 The LiteStep Development Team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/ 
/****************************************************************************
****************************************************************************/

#include "litestep.h"

// Misc Helpers
#include "RecoveryMenu.h"
#include "StartupRunner.h"
#include "../lsapi/ThreadedBangCommand.h"
#include "../utility/macros.h"

// Services
#include "DDEService.h"
#include "DDEStub.h"
#include "TrayService.h"

// Managers
#include "HookManager.h"
#include "MessageManager.h"
#include "ModuleManager.h"

// Misc Helpers
#include "DataStore.h"

// STL headers
#include <algorithm>

#include "../utility/core.hpp"


// namespace stuff
using std::for_each;
using std::mem_fun;


// Parse the command line
static bool ParseCmdLine(LPCSTR pszCmdLine);
static void ExecuteCmdLineBang(LPCSTR pszCommand, LPCSTR pszArgs);

CLiteStep gLiteStep;
CHAR szAppPath[MAX_PATH];
CHAR szRcPath[MAX_PATH];

enum StartupMode
{
    STARTUP_DONT_RUN  = -1,
    STARTUP_DEFAULT   = 0,  // run only if first time
    STARTUP_FORCE_RUN = TRUE
};

int g_nStartupMode = STARTUP_DEFAULT;


//
// ExecuteCmdLineBang
//
static void ExecuteCmdLineBang(LPCSTR pszCommand, LPCSTR pszArgs)
{
    if (IsValidStringPtr(pszCommand))
	{
		HWND hWnd = FindWindow(szMainWindowClass, szMainWindowTitle);
		
        if (IsWindow(hWnd))
		{
            std::auto_ptr<LMBANGCOMMAND> pBangCommand(new LMBANGCOMMAND);
			if (pBangCommand.get() != NULL)
			{
				pBangCommand->cbSize = sizeof(LMBANGCOMMAND);
				pBangCommand->hWnd = NULL;

				StringCchCopy(pBangCommand->szCommand, MAX_BANGCOMMAND, pszCommand);

				pBangCommand->szArgs[0] = '\0';
				if (IsValidStringPtr(pszArgs))
				{
					StringCchCopy(pBangCommand->szArgs, MAX_BANGARGS, pszArgs);
				}

				COPYDATASTRUCT cds =
                {
                    LM_BANGCOMMAND,
                    sizeof(LMBANGCOMMAND),
                    (LPVOID)pBangCommand.get()
                };

				SendMessage(hWnd, WM_COPYDATA, 0, (LPARAM)&cds);
			}
		}
	}
}


//
// ParseCmdLine(LPCSTR pszCmdLine)
//
static bool ParseCmdLine(LPCSTR pszCmdLine)
{
	if (IsValidStringPtr(pszCmdLine))
	{
        char szToken[MAX_LINE_LENGTH];
        LPCSTR pszNextToken = pszCmdLine;

        while (GetToken(pszNextToken, szToken, &pszNextToken, false))
		{
			switch (szToken[0])
			{
				case '-':
				{
					if (!stricmp(szToken, "-nostartup"))
					{
						g_nStartupMode = STARTUP_DONT_RUN;
					}
                    else if (!stricmp(szToken, "-startup"))
                    {
                        g_nStartupMode = STARTUP_FORCE_RUN;
                    }                    
				}
				break;

				case '!':
				{
					ExecuteCmdLineBang(szToken, pszNextToken);
                    return false;
				}
				break;

				default:
				{
					if (PathFileExists(szToken))
					{
						if (strchr(szToken, '\\'))
						{
							StringCchCopy(szRcPath, MAX_PATH, szToken);
						}
						else
						{
							StringCchPrintfEx(szRcPath, MAX_PATH, NULL, NULL, STRSAFE_NULL_ON_FAILURE, "%s\\%s", szAppPath, szToken);
						}
					}
				}
				break;
			}
		}
	}

    return true;
}


//
//
//
BOOL WINAPI FileIconInit(BOOL bFullInit);
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int /* nCmdShow */)
{
    HRESULT hr = S_OK;
    
    // Determine our application's path
	if (GetModuleFileName (hInstance, szAppPath, sizeof (szAppPath)) > 0)
	{
		PathRemoveFileSpec(szAppPath);
		PathAddBackslash(szAppPath);
	}
	PathCombine(szRcPath, szAppPath, "step.rc");

	// Parse command line, setting appropriate variables
	if (!ParseCmdLine(lpCmdLine))
    {
        return 1;
    }

	// If we can't find "step.rc", there's no point in proceeding
	if (!PathFileExists(szRcPath))
	{
		RESOURCE_STREX(hInstance, IDS_LITESTEP_ERROR2, resourceTextBuffer, MAX_LINE_LENGTH,
		               "Unable to find the file \"%s\".\nPlease verify the location of the file, and try again.", szRcPath);
		MessageBox(NULL, resourceTextBuffer, "LiteStep", MB_TOPMOST | MB_ICONEXCLAMATION);
		return 2;
	}

	// Check for previous instance
	HANDLE hMutex = CreateMutex(NULL, FALSE, "LiteStep");
    
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		// Prevent multiple instances of LiteStep
		RESOURCE_STR(hInstance, IDS_LITESTEP_ERROR1,
		             "A previous instance of LiteStep was detected.\nAre you sure you want to continue?");
		if (IDNO == MessageBox(NULL, resourceTextBuffer, "LiteStep", MB_TOPMOST | MB_ICONINFORMATION | MB_YESNO | MB_DEFBUTTON2))
		{
			hr = E_ABORT;
		}
	}

	if (SUCCEEDED(hr))
    {
        hr = gLiteStep.Start(szAppPath, szRcPath, hInstance, g_nStartupMode);
    }

    CloseHandle(hMutex);

	FreeLibrary(hShell32);
    return HRESULT_CODE(hr);
}


//
// CLiteStep()
//
CLiteStep::CLiteStep()
{
	m_hInstance = NULL;
    m_bAutoHideModules = false;
    m_bAppIsFullScreen = false;
	m_hMainWindow = NULL;
	WM_ShellHook = 0;
	m_pModuleManager = NULL;
	m_pDataStoreManager = NULL;
	m_pMessageManager = NULL;
	m_bHookManagerStarted = false;
	m_pTrayService = NULL;
}


//
//
//
CLiteStep::~CLiteStep()
{
}


//
// Start(LPCSTR pszAppPath, LPCSTR pszRcPath, HINSTANCE hInstance, int nStartupMode)
//
HRESULT CLiteStep::Start(LPCSTR pszAppPath, LPCSTR pszRcPath, HINSTANCE hInstance, int nStartupMode)
{
	HRESULT hr;
	bool bUnderExplorer = false;

	m_sAppPath.assign(pszAppPath);  // could throw length_error
	m_sConfigFile.assign(pszRcPath);
	m_hInstance = hInstance;

	// Initialize OLE/COM
	OleInitialize(NULL);

	// before anything else, start the recovery menu thread
	DWORD dwRecoveryThreadID;
	HANDLE hRecoveryThread = CreateThread(NULL, 0, RecoveryThreadProc,
        (void*)m_hInstance, 0, &dwRecoveryThreadID);

	// configure the Win32 window manager to hide windows when they are minimized
    MINIMIZEDMETRICS mm = { 0 };
	mm.cbSize = sizeof(MINIMIZEDMETRICS);

	SystemParametersInfo(SPI_GETMINIMIZEDMETRICS, mm.cbSize, &mm, 0);

	if (!(mm.iArrange & ARW_HIDE))
	{
		mm.iArrange |= ARW_HIDE;
		SystemParametersInfo(SPI_SETMINIMIZEDMETRICS, mm.cbSize, &mm, 0);
	}

	SetupSettingsManager(m_sAppPath.c_str(), m_sConfigFile.c_str());

    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
        (nStartupMode != STARTUP_FORCE_RUN && !GetRCBool("LSNoStartup", FALSE)))
    {
        nStartupMode = STARTUP_DONT_RUN;
    }

    m_bAutoHideModules = GetRCBool("LSAutoHideModules", TRUE) ? true : false;

	// Check for explorer
	if (FindWindow("Shell_TrayWnd", NULL)) // Running under Exploder
	{
		if (GetRCBool("LSNoShellWarning", FALSE))
		{
			RESOURCE_STR(hInstance, IDS_LITESTEP_ERROR3,
			             "You are currently running another shell, while Litestep b24 allows you\012to run under Explorer, we don't advise it for inexperienced users, and we\012will not support it, so do so at your own risk.\012\012If you continue, some of the advanced features of Litestep will be disabled\012such as the desktop. The wharf, hotkeys, and shortcuts will still work.\012\012To get rid of this message next time, put LSNoShellWarning in your step.rc\012\012Continue?")
			RESOURCE_TITLE(hInstance, IDS_LITESTEP_TITLE_WARNING, "Warning")
			if (MessageBox(0, resourceTextBuffer, resourceTitleBuffer, MB_YESNO | MB_ICONEXCLAMATION | MB_TOPMOST) == IDNO)
			{
				return E_ABORT;
			}
		}
		bUnderExplorer = true;
	}

	// Register Window Class
    WNDCLASSEX wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.cbWndExtra = sizeof(CLiteStep*);
	wc.lpfnWndProc = CLiteStep::InternalWndProc;
	wc.hInstance = m_hInstance;
	wc.lpszClassName = szMainWindowClass;

    if (!RegisterClassEx(&wc))
	{
		RESOURCE_MSGBOX_T(hInstance, IDS_LITESTEP_ERROR4,
		                  "Error registering main Litestep window class.",
		                  IDS_LITESTEP_TITLE_ERROR, "Error")

		return E_FAIL;
	}

	// Create our main window
	m_hMainWindow = CreateWindowEx(WS_EX_TOOLWINDOW,
        szMainWindowClass, szMainWindowTitle,
        0, 0, 0, 0,
        0, NULL, NULL,
        m_hInstance,
        (void*)this);

	// Start up everything
	if (m_hMainWindow)
	{
		// Set magic DWORD to prevent VWM from seeing main window
		SetWindowLong (m_hMainWindow, GWL_USERDATA, magicDWord);

        FARPROC (__stdcall * RegisterShellHook)(HWND, DWORD) =
            (FARPROC (__stdcall *)(HWND, DWORD))GetProcAddress(
            GetModuleHandle("SHELL32.DLL"), (LPCSTR)((long)0xB5));

        WM_ShellHook = RegisterWindowMessage("SHELLHOOK");
		
        if (RegisterShellHook)
        {
            RegisterShellHook(NULL, RSH_REGISTER);

            OSVERSIONINFO verInfo = { 0 };
            verInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
            GetVersionEx(&verInfo);

            if (verInfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
            {
                // c0atzin's fix for 9x
                RegisterShellHook(m_hMainWindow, RSH_REGISTER);
            }
            else
            {
                RegisterShellHook(m_hMainWindow, RSH_TASKMAN);
            }
        }

		// Set Shell Window
		if (!bUnderExplorer && (GetRCBool("LSSetAsShell", TRUE)))
		{
			FARPROC (__stdcall * SetShellWindow)(HWND) = NULL;
			SetShellWindow = (FARPROC (__stdcall *)(HWND))GetProcAddress(GetModuleHandle("USER32.DLL"), "SetShellWindow");
			if (SetShellWindow)
			{
				SetShellWindow(m_hMainWindow);
			}
		}

		hr = _InitServices();
		if (SUCCEEDED(hr))
		{
			hr = _StartServices();
			// Quietly swallow service errors... in the future.. do something
		}

		// Run startup items if the SHIFT key is not down
		if (nStartupMode != STARTUP_DONT_RUN)
		{
			DWORD dwThread;
			
            CloseHandle(CreateThread(NULL, 0, StartupRunner::Run,
                (void*)nStartupMode, 0, &dwThread));
		}

		hr = _InitManagers();
		if (SUCCEEDED(hr))
		{
            hr = _StartManagers();
            // Quietly swallow manager errors... in the future.. do something
        }
        
        // Undocumented call: Shell Loading Finished
        SendMessage(GetDesktopWindow(), WM_USER, 0, 0);
        
        // Main message pump
        MSG message;
        while (GetMessage(&message, 0, 0, 0) > 0)
        {
            try
            {
                if (message.hwnd == NULL)
		        {
                    if (message.message == NULL)
                    {
					    //something's wacked, break out of this
					    break;
				    }
				    // Thread message
				    switch (message.message)
				    {
					    case LM_THREAD_BANGCOMMAND:
					    {
						    ThreadedBangCommand * pInfo = (ThreadedBangCommand*)message.wParam;

						    if (pInfo != NULL)
						    {
							    pInfo->Execute();
							    pInfo->Release(); //check BangCommand.cpp for the reason
						    }
					    }
					    break;
				    }
			    }
			    else
			    {
				    TranslateMessage(&message);
				    DispatchMessage (&message);
			    }
			}
			catch(...)
			{}
		}

		if (RegisterShellHook)
		{
			RegisterShellHook(m_hMainWindow, RSH_UNREGISTER);
		}

		_StopManagers();
		_CleanupManagers();

		_StopServices();
		_CleanupServices();

		// Destroy _main window
		DestroyWindow(m_hMainWindow);
		m_hMainWindow = NULL;
	}
	else
	{
		RESOURCE_MSGBOX_T(hInstance, IDS_LITESTEP_ERROR5,
		                  "Error creating Litestep main application window.",
		                  IDS_LITESTEP_TITLE_ERROR, "Error")
	}

	// Unreg class
	UnregisterClass(szMainWindowClass, m_hInstance);

	// deinitialize stepsettings
	DeleteSettingsManager();

	// Uninitialize OLE/COM
	OleUninitialize();

	// close the recovery thread: tell the thread to quit
    PostThreadMessage(dwRecoveryThreadID, WM_QUIT, 0, 0);
    // wait until the thread is done quitting, at most three seconds though
    if (WaitForSingleObject(hRecoveryThread, 3000) == WAIT_TIMEOUT)
    {
        TerminateThread(hRecoveryThread, 0);
    }
    // close the thread handle
    CloseHandle(hRecoveryThread);

	return S_OK;
}


//
//
//
LRESULT CALLBACK CLiteStep::InternalWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static CLiteStep* pLiteStep = NULL;

	if (uMsg == WM_CREATE)
	{
        pLiteStep = static_cast<CLiteStep*>(
            reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);

        ASSERT_ISWRITEPTR(pLiteStep);
	}

	if (pLiteStep)
	{
        return pLiteStep->ExternalWndProc(hWnd, uMsg, wParam, lParam);
	}

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


//
//
//
LRESULT CLiteStep::ExternalWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LRESULT lReturn = FALSE;
    
    switch (uMsg)
	{
		case WM_KEYDOWN:
		case WM_SYSCOMMAND:
		{
			switch (wParam)
			{
				case LM_SHUTDOWN:
				case SC_CLOSE:
				{
					ParseBangCommand(hWnd, "!ShutDown", NULL);
				}
				break;

				default:
				{
					lReturn = DefWindowProc(hWnd, uMsg, wParam, lParam);
				}
				break;
			}
		}
		break;

		case WM_QUERYENDSESSION:
		case WM_ENDSESSION:
		{
			lReturn = TRUE;
		}
		break;

		case LM_SYSTRAYREADY:
		{
			if (m_pTrayService)
			{
				lReturn = (LRESULT)m_pTrayService->SendSystemTray();
			}
		}
		break;

		case LM_SAVEDATA:
		{
			WORD wIdent = HIWORD(wParam);
			WORD wLength = LOWORD(wParam);
			void *pvData = (void *)lParam;
			if ((pvData != NULL) && (wLength > 0))
			{
				if (m_pDataStoreManager == NULL)
				{
					m_pDataStoreManager = new DataStore();
				}
				if (m_pDataStoreManager)
				{
					lReturn = m_pDataStoreManager->StoreData(wIdent, pvData, wLength);
				}
			}
		}
		break;

		case LM_RESTOREDATA:
		{
			WORD wIdent = HIWORD(wParam);
			WORD wLength = LOWORD(wParam);
			void *pvData = (void *)lParam;
			if ((pvData != NULL) && (wLength > 0))
			{
				if (m_pDataStoreManager)
				{
					lReturn = m_pDataStoreManager->ReleaseData(wIdent, pvData, wLength);
					if (m_pDataStoreManager->Count() == 0)
					{
						delete m_pDataStoreManager;
						m_pDataStoreManager = NULL;
					}
				}
			}
		}
		break;

		case LM_GETLSOBJECT:
		case LM_WINDOWLIST:
		case LM_MESSAGEMANAGER:
		case LM_DATASTORE:
		{
			; // Obsolete Message, return 0
		}
		break;

		case LM_RECYCLE:
		{
			switch (wParam)
			{
				case LR_RECYCLE:
				{
					_Recycle();
				}
				break;

				case LR_LOGOFF:
				{
					if (ExitWindowsEx(EWX_LOGOFF, 0))
					{
						PostQuitMessage(0);
					}
				}
				break;

				case LR_QUIT:
				{
					PostQuitMessage(0);
				}
				break;

				default:  // wParam == LR_MSSHUTDOWN
				{
					FARPROC (__stdcall * MSWinShutdown)(HWND) = NULL;
					MSWinShutdown = (FARPROC (__stdcall *)(HWND))GetProcAddress(GetModuleHandle("SHELL32.DLL"), (LPSTR)((long)0x3C));
					if (MSWinShutdown)
					{
						MSWinShutdown(m_hMainWindow);
					}
				}
                break;
            }
        }
        break;
        
        case LM_RELOADMODULE:
        case LM_UNLOADMODULE:
        {
            if (m_pModuleManager)
            {
                if (lParam & LMM_HINSTANCE)
                {
                    if (uMsg == LM_UNLOADMODULE)
                    {
                        m_pModuleManager->QuitModule((HINSTANCE)wParam);
                    }                    
                    else
                    {
                        m_pModuleManager->ReloadModule((HINSTANCE)wParam);
                    }
                }
                else
                {
                    LPCSTR pszPath = (LPCSTR)wParam;
                    
                    if (IsValidStringPtr(pszPath))
                    {
                        HINSTANCE hInst = m_pModuleManager->GetModuleInstance(pszPath);
                        
                        if (hInst != NULL)
                        {
                            PostMessage(hWnd, uMsg, (WPARAM)hInst, lParam | LMM_HINSTANCE);
                        }
                        else if (uMsg == LM_RELOADMODULE)
                        {
                            m_pModuleManager->LoadModule(pszPath, (DWORD)lParam);
                        }
                    }
                }
            }
        }
		break;

        case LM_BANGCOMMAND:
		{
			PLMBANGCOMMAND plmbc = (PLMBANGCOMMAND)lParam;

			if (IsValidReadPtr(plmbc))
			{
				if (plmbc->cbSize == sizeof(LMBANGCOMMAND))
				{
					lReturn = ParseBangCommand(plmbc->hWnd, plmbc->szCommand, plmbc->szArgs);
				}
			}
		}
		break;

		case WM_COPYDATA:
		{
			PCOPYDATASTRUCT pcds = (PCOPYDATASTRUCT)lParam;

			switch (pcds->dwData)
			{
				case LM_BANGCOMMAND:
				{
					lReturn = SendMessage(hWnd, LM_BANGCOMMAND, 0, (LPARAM)pcds->lpData);
				}
				break;
			}
		}
		break;

		case LM_GETREVID:
		{
			char szBuffer[256];

/*			if (wParam == 1)
			{
				StringCchCopy(szBuffer, 256, &rcsId[1]);
				szBuffer[strlen(szBuffer) - 1] = '\0';
			}
			else
			{
				StringCchCopy(szBuffer, 256, "litestep.exe: ");
				StringCchCat(szBuffer, 256, (LPCSTR) & LSRev);
				szBuffer[strlen(szBuffer) - 1] = '\0';
			}*/
            #pragma COMPILE_TODO("Need to fix LM_GETREVID")
			SendMessage((HWND)lParam, LM_GETREVID, 0, (long)szBuffer);
			m_pMessageManager->GetRevID(LM_GETREVID, wParam, lParam);
		}
		break;

		case LM_REGISTERHOOKMESSAGE:
		{
			if (!m_bHookManagerStarted)
			{
                m_bHookManagerStarted = startHookManager(m_hInstance) ? true : false;
			}
			if (m_bHookManagerStarted)
			{
				lReturn = RegisterHookMessage(hWnd, wParam, (HookCallback*)lParam);
			}
		}
		break;

		case LM_UNREGISTERHOOKMESSAGE:
		{
			if (m_bHookManagerStarted)
			{
				if (UnregisterHookMessage(hWnd, wParam, (HookCallback*)lParam) == 0)
				{
					stopHookManager();
					m_bHookManagerStarted = false;
				}
			}
		}
		break;

		case LM_REGISTERMESSAGE:     // Message Handler Message
		{
			if (m_pMessageManager)
			{
				m_pMessageManager->AddMessages((HWND)wParam, (UINT *)lParam);
			}
		}
		break;

		case LM_UNREGISTERMESSAGE:     // Message Handler Message
		{
			if (m_pMessageManager)
			{
				m_pMessageManager->RemoveMessages((HWND)wParam, (UINT *)lParam);
			}
		}
		break;

		default:
		{
            if (uMsg == WM_ShellHook)
            {
                HWND hWndMessage = (HWND)lParam;
                uMsg = (LOWORD(wParam) & 0x00FF) + 9500;
                lParam = (LOWORD(wParam) & 0xFF00);
                wParam = (WPARAM)hWndMessage;
                
                if (uMsg == LM_WINDOWACTIVATED)
                {
                    if (m_bAutoHideModules)
					{
						if ((lParam > 0) && (m_bAppIsFullScreen == false))
						{
							m_bAppIsFullScreen = true;
							ParseBangCommand(m_hMainWindow, "!HIDEMODULES", NULL);
						}
						else if ((lParam <= 0) && (m_bAppIsFullScreen == true))
						{
							m_bAppIsFullScreen = false;
							ParseBangCommand(m_hMainWindow, "!SHOWMODULES", NULL);
						}
					}
                }
            }

            if (m_pMessageManager && m_pMessageManager->HandlerExists(uMsg))
			{
                lReturn = m_pMessageManager->SendMessage(uMsg, wParam, lParam);
                break;
			}
			lReturn = DefWindowProc (hWnd, uMsg, wParam, lParam);
		}
		break;
	}

    return lReturn;
}


STDMETHODIMP CLiteStep::get_Window(/*[out, retval]*/ long *phWnd)
{
    HRESULT hr;

    if (phWnd != NULL)
    {
        *phWnd = (LONG)m_hMainWindow;
        hr = S_OK;
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}


STDMETHODIMP CLiteStep::get_AppPath(/*[out, retval]*/ LPSTR pszPath, /*[in]*/ size_t cchPath)
{
    HRESULT hr;

    if (IsValidStringPtr(pszPath, cchPath))
    {
        hr = StringCchCopy(pszPath, cchPath, m_sAppPath.c_str());
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}


//
// _InitServies()
//
HRESULT CLiteStep::_InitServices()
{
    IService* pService;

	//
    // DDE Service
    //
    if (GetRCBool("LSUseSystemDDE", TRUE))
    {
        // M$ DDE
        pService = new DDEStub();
    }
    else
    {
        // liteman
        pService = new DDEService();
    }

    if (pService)
    {
        m_Services.push_back(pService);
    }
    else
    {
        return E_OUTOFMEMORY;
    }

    //
    // Tray Service
    //
    m_pTrayService = new TrayService();
    if (m_pTrayService)
    {
        m_Services.push_back(m_pTrayService);
    }
    else
    {
        return E_OUTOFMEMORY;
    }

	return S_OK;
}


//
// _StartServices()
//
HRESULT CLiteStep::_StartServices()
{
    // use std::transform to add error checking to this
    for_each(m_Services.begin(), m_Services.end(), mem_fun(&IService::Start));
	return S_OK;
}


//
// _StopServices()
//
HRESULT CLiteStep::_StopServices()
{
    for_each(m_Services.begin(), m_Services.end(), mem_fun(&IService::Stop));
	return S_OK;
}


//
// _CleanupServices()
//
void CLiteStep::_CleanupServices()
{
    std::for_each(m_Services.begin(), m_Services.end(),
        std::mem_fun(&IService::Release));
    
    m_Services.clear();
}


//
// _InitManagers()
//
HRESULT CLiteStep::_InitManagers()
{
	HRESULT hr = S_OK;

	//gDataStore = new DataStore();

	m_pMessageManager = new MessageManager();

	m_pModuleManager = new ModuleManager();

	//gBangManager = new BangManager();

	return hr;
}


//
// _StartManagers
//
HRESULT CLiteStep::_StartManagers()
{
	HRESULT hr = S_OK;

	//SetBangManager(gBangManager);

	// Setup bang commands in lsapi
	SetupBangs();

	// Load modules
	m_pModuleManager->Start(this);

	return hr;
}


//
// _StopManagers()
//
HRESULT CLiteStep::_StopManagers()
{
	HRESULT hr = S_OK;

	if (m_bHookManagerStarted)
	{
		stopHookManager();
		m_bHookManagerStarted = false;
	}

	m_pModuleManager->Stop();
	//gBangManager->ClearBangCommands();
	m_pMessageManager->ClearMessages();

	//ClearBangManager();
    ClearBangs();

	return hr;
}


//
// _CleanupManagers
//
void CLiteStep::_CleanupManagers()
{
	if (m_pModuleManager)
	{
		delete m_pModuleManager;
		m_pModuleManager = NULL;
	}

	/*if (gBangManager)
	{
		delete gBangManager;
		gBangManager = NULL;
	}*/

	if (m_pMessageManager)
	{
		delete m_pMessageManager;
		m_pMessageManager = NULL;
	}

	if (m_pDataStoreManager)
	{
		m_pDataStoreManager->Clear();
		delete m_pDataStoreManager;
		m_pDataStoreManager = NULL;
	}
}


//
// _Recycle
//
void CLiteStep::_Recycle()
{
	_StopManagers();

	DeleteSettingsManager();

	if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
	{
		RESOURCE_MSGBOX(m_hInstance, IDS_LITESTEP_ERROR6,
		                "Recycle has been paused, click OK to continue.", "LiteStep")
	}

	SetupSettingsManager(m_sAppPath.c_str(), m_sConfigFile.c_str());

	_StartManagers();
}




















