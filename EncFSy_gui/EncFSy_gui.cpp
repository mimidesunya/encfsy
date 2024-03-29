// EncFSy_gui.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "EncFSy_gui.h"

#include <crtdbg.h>
#include <stdio.h>
#include <shlobj.h>

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text

BOOL                InitInstance(HINSTANCE, int);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ENCFSYGUI));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}


// Message handler for about box.
INT_PTR CALLBACK AboutDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

static TCHAR dirname[MAX_PATH];
static int driveIx;

// Message handler for about box.
INT_PTR CALLBACK PasswordDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
		{
			WCHAR password[256];
			GetDlgItemText(hDlg, IDC_PASSWORD, password, sizeof password);

			EndDialog(hDlg, LOWORD(wParam));

			WCHAR drive[2];
			drive[0] = driveIx + L'A';
			drive[1] = L'\0';
			_RPTWN(_CRT_WARN, L"DDP %s %s %s\n", drive, dirname, password);

			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	if (uMsg == BFFM_INITIALIZED) {
		SendMessage(hwnd, BFFM_SETSELECTION, (WPARAM)TRUE, lpData);
	}
	return 0;
}

BOOL ChooseDirectory(HWND hDlg)
{
	HWND hList;
	hList = GetDlgItem(hDlg, IDC_LIST);
	driveIx = (int)(DWORD)SendMessage(hList, LB_GETCURSEL, 0L, 0L);

	dirname[0] = TEXT('\0');
	/*

	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrInitialDir = _TEXT("C:\\");
	ofn.lpstrFile = dirname;
	ofn.nMaxFile = sizeof dirname;
	ofn.lpstrFilter = _TEXT("TXTファイル(*.TXT)\0*.TXT\0") _TEXT("全てのファイル(*.*)\0*.*\0");
	ofn.lpstrDefExt = _TEXT("TXT");
	ofn.lpstrTitle = _TEXT("Select a EncFS directory");
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
	BOOL ret = GetOpenFileNameW(&ofn);
	*/

	BROWSEINFO bInfo;
	LPITEMIDLIST pIDList;

	memset(&bInfo, 0, sizeof(bInfo));
	bInfo.hwndOwner = hDlg;
	bInfo.pidlRoot = NULL;
	bInfo.pszDisplayName = dirname;
	bInfo.lpszTitle = TEXT("Select a EncFS directory");
	bInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX | BIF_VALIDATE | BIF_NEWDIALOGSTYLE;
	bInfo.lpfn = BrowseCallbackProc;
	bInfo.lParam = (LPARAM)dirname;
	pIDList = SHBrowseForFolder(&bInfo);
	SHGetPathFromIDList(pIDList, dirname);

	DialogBoxW(hInst, MAKEINTRESOURCE(IDD_PASSWORD), hDlg, PasswordDialogProc);

	CoTaskMemFree(pIDList);

	return TRUE;
}

INT_PTR CALLBACK MainDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	int wmId = LOWORD(wParam);

	switch (message)
	{
	case WM_INITDIALOG:
		SetMenu(hDlg, LoadMenu(hInst, MAKEINTRESOURCEW(IDC_ENCFSYGUI)));

		HWND hList;
		hList = GetDlgItem(hDlg, IDC_LIST);
		WCHAR drive[3];
		drive[1] = ':';
		drive[2] = '\0';
		WCHAR letter;
		for (letter = 'A'; letter <= 'Z'; ++letter) {
			drive[0] = letter;
			SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)drive);
		}

		return (INT_PTR)TRUE;
		break;
	case WM_COMMAND:
		switch (wmId) {
		case ID_SELECT_FOLDER:
		case IDM_MOUNT:
			if (!ChooseDirectory(hDlg)) {
				DestroyWindow(hDlg);
			}
			break;
		case IDM_ABOUT:
			DialogBoxW(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, AboutDialogProc);
			break;
		case IDM_EXIT:
			DestroyWindow(hDlg);
			break;
		default:
			return DefWindowProc(hDlg, message, wParam, lParam);
		}
		break;
	case WM_CLOSE:
		EndDialog(hDlg, LOWORD(wParam));
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}
	return (INT_PTR)FALSE;
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   DialogBoxW(hInst, MAKEINTRESOURCE(IDD_MAIN), nullptr, MainDialogProc);

   /*
   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);
   */

   return TRUE;
}

