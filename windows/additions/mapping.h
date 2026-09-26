#pragma once

// =============================================================================
// CCBrowser — Virtual Host Mapping
// =============================================================================
// Maps a local folder to a virtual https:// hostname inside WebView2.
//
// Example:
//   CCBridge_SetVirtualHost(webview, L"app.data", L"C:\\Users\\X\\AppData\\Local\\ProgInsight\\shared\\web");
//   → https://app.data/notepad/index.html serves that folder's notepad\index.html
//
// Called from my_webview.cpp after WebView2 environment is ready.
// Dart side calls down via method channel "ccb_setVirtualHost" / "ccb_removeVirtualHost".
// =============================================================================

#include <WebView2.h>
#include <wrl.h>
#include <wil/com.h>
#include <windows.h>
#include <string>

inline HRESULT CCBridge_SetVirtualHost(
    wil::com_ptr<ICoreWebView2> pWebview,
    const std::wstring& hostname,
    const std::wstring& folderPath)
{
    auto wv3 = pWebview.try_query<ICoreWebView2_3>();
    if (wv3 == nullptr) {
        OutputDebugStringW(L"[CCBridge][mapping] ICoreWebView2_3 not available\n");
        return E_NOINTERFACE;
    }

    HRESULT hr = wv3->SetVirtualHostNameToFolderMapping(
        hostname.c_str(),
        folderPath.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
    );

    if (SUCCEEDED(hr)) {
        std::wstring msg = L"[CCBridge][mapping] mapped https://";
        msg += hostname;
        msg += L" -> ";
        msg += folderPath;
        msg += L"\n";
        OutputDebugStringW(msg.c_str());
    } else {
        wchar_t buf[128];
        swprintf_s(buf, L"[CCBridge][mapping] SetVirtualHostNameToFolderMapping failed: 0x%08X\n",
                   static_cast<unsigned>(hr));
        OutputDebugStringW(buf);
    }

    return hr;
}

inline HRESULT CCBridge_RemoveVirtualHost(
    wil::com_ptr<ICoreWebView2> pWebview,
    const std::wstring& hostname)
{
    auto wv3 = pWebview.try_query<ICoreWebView2_3>();
    if (wv3 == nullptr) return E_NOINTERFACE;

    HRESULT hr = wv3->ClearVirtualHostNameToFolderMapping(hostname.c_str());

    if (SUCCEEDED(hr)) {
        std::wstring msg = L"[CCBridge][mapping] removed https://";
        msg += hostname;
        msg += L"\n";
        OutputDebugStringW(msg.c_str());
    } else {
        wchar_t buf[128];
        swprintf_s(buf, L"[CCBridge][mapping] ClearVirtualHostNameToFolderMapping failed: 0x%08X\n",
                   static_cast<unsigned>(hr));
        OutputDebugStringW(buf);
    }

    return hr;
}
