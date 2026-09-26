#pragma once

// =============================================================================
// CCBrowser — Extension Bridge
// =============================================================================
// Injects JavaScript before any page script runs, using WebView2's
// AddScriptToExecuteOnDocumentCreated API. This is more powerful than
// onPageFinished injection — the script runs on every page including iframes,
// before the page's own scripts execute.
//
// Each injected script gets a string ID back. Store that ID to remove it later.
//
// Dart side calls down via method channel:
//   "ccb_addScript"    → returns scriptId (string)
//   "ccb_removeScript" → takes scriptId (string)
//
// This is the foundation for the Chrome extension translation layer.
// The Dart side handles: crx download, manifest parsing, JS transformation.
// This C++ side handles: actually injecting the transformed JS into every page.
// =============================================================================

#include <WebView2.h>
#include <wrl.h>
#include <wil/com.h>
#include <windows.h>
#include <string>
#include <functional>

using namespace Microsoft::WRL;

// Inject JS that runs before every page's own scripts.
// onComplete is called with the scriptId string (or empty string on failure).
inline HRESULT CCBridge_AddScript(
    wil::com_ptr<ICoreWebView2> pWebview,
    const std::wstring& scriptCode,
    std::function<void(std::wstring scriptId)> onComplete)
{
    HRESULT hr = pWebview->AddScriptToExecuteOnDocumentCreated(
        scriptCode.c_str(),
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [onComplete](HRESULT innerHr, LPCWSTR scriptId) -> HRESULT {
                if (SUCCEEDED(innerHr) && scriptId != nullptr) {
                    std::wstring msg = L"[CCBridge][extension] script added, id=";
                    msg += scriptId;
                    msg += L"\n";
                    OutputDebugStringW(msg.c_str());
                    onComplete(std::wstring(scriptId));
                } else {
                    wchar_t buf[128];
                    swprintf_s(buf, L"[CCBridge][extension] AddScript failed: 0x%08X\n",
                               static_cast<unsigned>(innerHr));
                    OutputDebugStringW(buf);
                    onComplete(L"");
                }
                return S_OK;
            }
        ).Get()
    );

    return hr;
}

// Remove a previously injected script by its ID.
inline HRESULT CCBridge_RemoveScript(
    wil::com_ptr<ICoreWebView2> pWebview,
    const std::wstring& scriptId)
{
    if (scriptId.empty()) return E_INVALIDARG;

    HRESULT hr = pWebview->RemoveScriptToExecuteOnDocumentCreated(scriptId.c_str());

    if (SUCCEEDED(hr)) {
        std::wstring msg = L"[CCBridge][extension] script removed, id=";
        msg += scriptId;
        msg += L"\n";
        OutputDebugStringW(msg.c_str());
    } else {
        wchar_t buf[128];
        swprintf_s(buf, L"[CCBridge][extension] RemoveScript failed: 0x%08X\n",
                   static_cast<unsigned>(hr));
        OutputDebugStringW(buf);
    }

    return hr;
}
