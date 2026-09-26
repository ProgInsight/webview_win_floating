#include "webview_win_floating_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

// For getPlatformVersion; remove unless needed for your plugin implementation.
#include <VersionHelpers.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <sstream>

// Jacky {
#include "my_webview.h"
#include "additions/bridge.h"

// toWideString(): convert utf8 to utf16 via MultiByteToWideChar (safe, no C4244)
#define toWideString(str) utf8ToUtf16(str).c_str()

// wideToUtf8(): convert utf16 wstring to utf8 string via WideCharToMultiByte (safe, no C4244)
static std::string wideToUtf8(const std::wstring& wide) {
  if (wide.empty()) return std::string();
  int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (size <= 0) return std::string();
  std::string out(size - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &out[0], size, nullptr, nullptr);
  return out;
}

// utf8ToUtf16(): convert utf8 to utf16, with MultiByteToWideChar()
std::wstring utf8ToUtf16(const std::string& utf8Str) {
  if (utf8Str.empty()) return std::wstring();
    
  int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0);
  if (wideLen == 0) {
    std::cout << "[webview_win_floating][native] MultiByteToWideChar fail" << std::endl;
    return std::wstring();
  }
    
  std::vector<wchar_t> wideBuffer(wideLen);
  int result = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, wideBuffer.data(), wideLen);
  if (result == 0) return std::wstring();
    
  return std::wstring(wideBuffer.data(), wideLen - 1);
}

// Jacky }

namespace webview_win_floating {

void WebviewWinFloatingPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "webview_win_floating",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<WebviewWinFloatingPlugin>();

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  //Jacky {
  plugin->m_nativeHWND = registrar->GetView()->GetNativeWindow();
  plugin->m_MethodChannel = std::move(channel);
  // Jacky }

  registrar->AddPlugin(std::move(plugin));
}

WebviewWinFloatingPlugin::WebviewWinFloatingPlugin() {}

WebviewWinFloatingPlugin::~WebviewWinFloatingPlugin() {
  std::cout << "[webview_win_floating] ~WebviewWinFloatingPlugin(): plugin disposing now" << std::endl;
  destroyAllWebViews();
}

// ---------------------------------------------------------------------------
// Simple helpers to extract string values from a minimal JSON object.
// We only need to parse flat {"key":"value"} and {"key":["a","b"]} shapes
// produced by dart:convert — so we avoid pulling in a JSON library.
// ---------------------------------------------------------------------------
static std::string ccb_jsonStringValue(const std::string& json, const std::string& key) {
  // find "key":"value" — value ends at the next unescaped '"'
  std::string needle = std::string(1,'"') + key + std::string(1,'"') + ':' + std::string(1,'"');
  auto pos = json.find(needle);
  if (pos == std::string::npos) return "";
  pos += needle.size();
  std::string result;
  for (; pos < json.size(); ++pos) {
    char c = json[pos];
    if (c == '\\' && pos + 1 < json.size()) { result += json[++pos]; continue; }
    if (c == '"') break;
    result += c;
  }
  return result;
}

static std::vector<std::string> ccb_jsonStringArray(const std::string& json, const std::string& key) {
  std::vector<std::string> out;
  std::string needle = std::string(1,'"') + key + std::string(1,'"') + ":[";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return out;
  pos += needle.size();
  while (pos < json.size()) {
    // skip whitespace and commas
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n')) ++pos;
    if (pos >= json.size() || json[pos] == ']') break;
    if (json[pos] != '"') { ++pos; continue; }
    ++pos; // skip opening quote
    std::string item;
    for (; pos < json.size(); ++pos) {
      char c = json[pos];
      if (c == '\\' && pos + 1 < json.size()) { item += json[++pos]; continue; }
      if (c == '"') { ++pos; break; }
      item += c;
    }
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

void WebviewWinFloatingPlugin::createWebview(const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> &result,
    int webviewId, std::string url, std::string userDataFolder, std::string profileName,
    std::string ccbConfig) {

  std::shared_ptr<flutter::MethodResult<flutter::EncodableValue>> shared_result = std::move(result);
  MyWebViewCreateParams params;

  params.onCreated = [=](HRESULT hr, MyWebView *webview) -> void {
    if (webview != NULL) {
      m_webviewMap[webviewId] = webview;
      std::cout << "[webview] native create: id = " << webviewId << std::endl;

      // ── CCBrowser lazy-config: apply mapping + scripts inside onCreated ──
      // The WebView2 object exists here, so no async timing race is possible.
      if (!ccbConfig.empty()) {
        // Virtual-host mapping
        auto vhost  = ccb_jsonStringValue(ccbConfig, "virtualHostname");
        auto vfolder = ccb_jsonStringValue(ccbConfig, "virtualFolder");
        if (!vhost.empty() && !vfolder.empty()) {
          std::wstring whostname = utf8ToUtf16(vhost);
          std::wstring wfolder   = utf8ToUtf16(vfolder);
          // setVirtualHost is synchronous — it wraps SetVirtualHostNameToFolderMapping
          webview->setVirtualHost(whostname.c_str(), wfolder.c_str());
          std::cout << "[CCBrowser] mapped https://" << vhost << " -> " << vfolder << std::endl;
        }
        // Content-script injection
        auto scripts = ccb_jsonStringArray(ccbConfig, "contentScripts");
        std::cout << "[CCBrowser] injecting " << scripts.size() << " script(s) for id=" << webviewId << std::endl;
        for (const auto& script : scripts) {
          webview->addScript(utf8ToUtf16(script).c_str(), [webviewId](std::wstring scriptId) {
            std::cout << "[CCBrowser] script added id=" << webviewId << " scriptId=" << wideToUtf8(scriptId) << std::endl;
          });
        }
      }
      // ─────────────────────────────────────────────────────────────────────

      if (!url.empty()) webview->loadUrl(toWideString(url));
      shared_result->Success(flutter::EncodableValue(true));
    } else {
      std::cout << "[webview] native create failed. result = " << static_cast<int>(hr) << std::endl;
      shared_result->Error("[webview] native create failed.");
    }
  };

  params.onNavigationRequest = [=](int requestId, std::string url, bool isNewWindow) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("requestId")] = flutter::EncodableValue(requestId);
    arguments[flutter::EncodableValue("url")] = flutter::EncodableValue(url);
    arguments[flutter::EncodableValue("isNewWindow")] = flutter::EncodableValue(isNewWindow);
    m_MethodChannel->InvokeMethod("onNavigationRequest", std::make_unique<flutter::EncodableValue>(arguments));
  };
  
  params.onPageStarted = [=](std::string url) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("url")] = flutter::EncodableValue(url);
    m_MethodChannel->InvokeMethod("onPageStarted", std::make_unique<flutter::EncodableValue>(arguments));
  };
  
  params.onPageFinished = [=](std::string url) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("url")] = flutter::EncodableValue(url);
    m_MethodChannel->InvokeMethod("onPageFinished", std::make_unique<flutter::EncodableValue>(arguments));
  };
  
  params.onHttpError = [=](std::string url, int errCode) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("url")] = flutter::EncodableValue(url);
    arguments[flutter::EncodableValue("errCode")] = flutter::EncodableValue(errCode);
    m_MethodChannel->InvokeMethod("onHttpError", std::make_unique<flutter::EncodableValue>(arguments));
  };
  
  params.onSslAuthError = [=](std::string url) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("url")] = flutter::EncodableValue(url);
    m_MethodChannel->InvokeMethod("onSslAuthError", std::make_unique<flutter::EncodableValue>(arguments));
  };

  params.onWebResourceError = [=](std::string url, int errCode, std::string errType) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("url")] = flutter::EncodableValue(url);
    arguments[flutter::EncodableValue("errCode")] = flutter::EncodableValue(errCode);
    arguments[flutter::EncodableValue("errType")] = flutter::EncodableValue(errType);
    m_MethodChannel->InvokeMethod("onWebResourceError", std::make_unique<flutter::EncodableValue>(arguments));
  };

  params.onUrlChange = [=](std::string url) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("url")] = flutter::EncodableValue(url);
    m_MethodChannel->InvokeMethod("onUrlChange", std::make_unique<flutter::EncodableValue>(arguments));
  };

  params.onPageTitleChanged = [=](std::string title) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("title")] = flutter::EncodableValue(title);
    m_MethodChannel->InvokeMethod("onPageTitleChanged", std::make_unique<flutter::EncodableValue>(arguments));
  };
  
  params.onWebMessageReceived = [=](std::string message) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("message")] = flutter::EncodableValue(message);
    m_MethodChannel->InvokeMethod("OnWebMessageReceived", std::make_unique<flutter::EncodableValue>(arguments));
  };
  
  params.onMoveFocusRequest = [=](bool isNext) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("isNext")] = flutter::EncodableValue(isNext);
    m_MethodChannel->InvokeMethod("onMoveFocusRequest", std::make_unique<flutter::EncodableValue>(arguments));
  };
  
  params.onFullScreenChanged = [=](BOOL isFullScreen) -> void {
    // TODO: Android webview does'n support fullscreen listener... should we support ONLY in windows ???
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("isFullScreen")] = flutter::EncodableValue(isFullScreen ? true : false);
    m_MethodChannel->InvokeMethod("OnFullScreenChanged", std::make_unique<flutter::EncodableValue>(arguments));
  };
  
  params.onHistoryChanged = [=]() -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    m_MethodChannel->InvokeMethod("onHistoryChanged", std::make_unique<flutter::EncodableValue>(arguments));
  };
  
  params.onAskPermission = [=](std::string url, int kind, int deferralId) -> void {
    flutter::EncodableMap arguments;
    arguments[flutter::EncodableValue("webviewId")] = flutter::EncodableValue(webviewId);
    arguments[flutter::EncodableValue("url")] = flutter::EncodableValue(url);
    arguments[flutter::EncodableValue("kind")] = flutter::EncodableValue(kind);
    arguments[flutter::EncodableValue("deferralId")] = flutter::EncodableValue(deferralId);
    m_MethodChannel->InvokeMethod("onAskPermission", std::make_unique<flutter::EncodableValue>(arguments));
  };

  MyWebView::Create(m_nativeHWND, params, utf8ToUtf16(userDataFolder).c_str(), toWideString(profileName));
}

void WebviewWinFloatingPlugin::destroyAllWebViews() {
  for(auto iter = m_webviewMap.begin(); iter != m_webviewMap.end(); iter++) {
    std::cout << "[webview_win_floating] old webview found, deleting id = " << iter->first << std::endl;
    delete iter->second;
  }
  m_webviewMap.clear();
}

void WebviewWinFloatingPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

  //std::cout << "native HandleMethodCall(): " << method_call.method_name() << std::endl;

  if (method_call.method_name().compare("init") == 0) {
    // called when hot-restart in debug mode, and clear all the old webviews which created before hot-restart
    destroyAllWebViews();
    result->Success();
    return;
  }

  flutter::EncodableMap arguments = std::get<flutter::EncodableMap>(*method_call.arguments());
  auto webviewId = std::get<int>(arguments[flutter::EncodableValue("webviewId")]);

  bool isCreateCall  = method_call.method_name().compare("create") == 0;
  bool isHideShowTab = method_call.method_name().compare("hideTab") == 0
                    || method_call.method_name().compare("showTab") == 0;
  auto webview = m_webviewMap[webviewId];
  if (webview == NULL && !isCreateCall && !isHideShowTab) {
    result->Error("webview hasn't created");
    return;
  }

  if (isCreateCall) {
    auto url = std::get<std::string>(arguments[flutter::EncodableValue("url")]);
    auto userDataFolder = std::get<std::string>(arguments[flutter::EncodableValue("userDataFolder")]);
    auto profileName = std::get<std::string>(arguments[flutter::EncodableValue("profileName")]);
    auto ccbConfig = std::get<std::string>(arguments[flutter::EncodableValue("ccbConfig")]);
    createWebview(method_call, result, webviewId, url, userDataFolder, profileName, ccbConfig);
  } else if (method_call.method_name().compare("setHasNavigationDecision") == 0) {
    auto hasNavigationDecision = std::get<bool>(arguments[flutter::EncodableValue("hasNavigationDecision")]);
    webview->setHasNavigationDecision(hasNavigationDecision);
    result->Success();
  } else if (method_call.method_name().compare("allowNavigationRequest") == 0) {
    auto requestId = std::get<int>(arguments[flutter::EncodableValue("requestId")]);
    auto isAllowed = std::get<bool>(arguments[flutter::EncodableValue("isAllowed")]);
    webview->allowNavigationRequest(requestId, isAllowed);
    result->Success();
  } else if (method_call.method_name().compare("updateBounds") == 0) {
    RECT bounds;
    bounds.left = std::get<int>(arguments[flutter::EncodableValue("left")]);
    bounds.top = std::get<int>(arguments[flutter::EncodableValue("top")]);
    bounds.right = std::get<int>(arguments[flutter::EncodableValue("right")]);
    bounds.bottom = std::get<int>(arguments[flutter::EncodableValue("bottom")]);
    webview->updateBounds(bounds);
    result->Success(flutter::EncodableValue(true));
  } else if (method_call.method_name().compare("loadUrl") == 0) {
    auto url = std::get<std::string>(arguments[flutter::EncodableValue("url")]);
    auto hr = webview->loadUrl(toWideString(url));
    result->Success(flutter::EncodableValue(SUCCEEDED(hr)));
  } else if (method_call.method_name().compare("loadHtmlString") == 0) {
    auto html = std::get<std::string>(arguments[flutter::EncodableValue("html")]);
    auto hr = webview->loadHtmlString(toWideString(html));
    result->Success(flutter::EncodableValue(SUCCEEDED(hr)));

    if (!arguments[flutter::EncodableValue("baseUrl")].IsNull()) {
      static bool g_isPrompted_baseUrl = false;
      if (!g_isPrompted_baseUrl) {
        std::cout << "[win_webview_floating] loadHtmlString() ignore 'baseUrl' parameter in Windows. WebView2 doesn't support. ref: https://github.com/MicrosoftEdge/WebView2Feedback/issues/530" << std::endl;
        g_isPrompted_baseUrl = true;
      }
    }

  } else if (method_call.method_name().compare("runJavascript") == 0) {
    std::shared_ptr<flutter::MethodResult<flutter::EncodableValue>> shared_result = std::move(result);
    auto javaScriptString = std::get<std::string>(arguments[flutter::EncodableValue("javaScriptString")]);
    auto ignoreResult = std::get<bool>(arguments[flutter::EncodableValue("ignoreResult")]);
    auto hr = webview->runJavascript(utf8ToUtf16(javaScriptString).c_str(), ignoreResult, [shared_result, ignoreResult](std::string result) -> void {
      if (ignoreResult) {
        shared_result->Success();
      } else {
        shared_result->Success(flutter::EncodableValue(result));
      }
    });
    if (FAILED(hr)) result->Error("runJavascript() error");
  } else if (method_call.method_name().compare("addScriptChannelByName") == 0) {
    auto channelName = std::get<std::string>(arguments[flutter::EncodableValue("channelName")]);
    webview->addScriptChannelByName(toWideString(channelName));
    result->Success();
  } else if (method_call.method_name().compare("removeScriptChannelByName") == 0) {
    auto channelName = std::get<std::string>(arguments[flutter::EncodableValue("channelName")]);
    webview->removeScriptChannelByName(toWideString(channelName));
    result->Success();
  } else if (method_call.method_name().compare("setFullScreen") == 0) {
    auto isFullScreen = std::get<bool>(arguments[flutter::EncodableValue("isFullScreen")]);
    if (isFullScreen) {
      RECT bounds;
      GetWindowRect(GetDesktopWindow(), &bounds);
      webview->updateBounds(bounds);
    }
    result->Success(flutter::EncodableValue(true));
  } else if (method_call.method_name().compare("setVisibility") == 0) {
    auto isVisible = std::get<bool>(arguments[flutter::EncodableValue("isVisible")]);
    webview->setVisible(isVisible);
    result->Success(flutter::EncodableValue(true));
  } else if (method_call.method_name().compare("enableJavascript") == 0) {
    auto isEnable = std::get<bool>(arguments[flutter::EncodableValue("isEnable")]);
    webview->enableJavascript(isEnable);
    result->Success(flutter::EncodableValue(true));
  } else if (method_call.method_name().compare("enableStatusBar") == 0) {
       auto isEnable = std::get<bool>(arguments[flutter::EncodableValue("isEnable")]);
       webview->enableStatusBar(isEnable);
       result->Success(flutter::EncodableValue(true));
  } else if (method_call.method_name().compare("enableIsZoomControl") == 0) {
         auto isEnable = std::get<bool>(arguments[flutter::EncodableValue("isEnable")]);
         webview->enableIsZoomControl(isEnable);
         result->Success(flutter::EncodableValue(true));
  } else if (method_call.method_name().compare("setUserAgent") == 0) {
    auto userAgent = std::get<std::string>(arguments[flutter::EncodableValue("userAgent")]);
    HRESULT hr = webview->setUserAgent(toWideString(userAgent));
    result->Success(flutter::EncodableValue(SUCCEEDED(hr) ? true : false));
  } else if (method_call.method_name().compare("canGoBack") == 0) {
    bool allow = webview->canGoBack();
    result->Success(flutter::EncodableValue(allow));
  } else if (method_call.method_name().compare("canGoForward") == 0) {
    bool allow = webview->canGoForward();
    result->Success(flutter::EncodableValue(allow));
  } else if (method_call.method_name().compare("goBack") == 0) {
    webview->goBack();
    result->Success();
  } else if (method_call.method_name().compare("goForward") == 0) {
    webview->goForward();
    result->Success();
  } else if (method_call.method_name().compare("reload") == 0) {
    webview->reload();
    result->Success();
  } else if (method_call.method_name().compare("cancelNavigate") == 0) {
    webview->cancelNavigate();
    result->Success();

  } else if (method_call.method_name().compare("clearCache") == 0) {
    webview->clearCache();
    result->Success();
  } else if (method_call.method_name().compare("clearCookies") == 0) {
    HRESULT hr = webview->clearCookies();
    result->Success(flutter::EncodableValue(SUCCEEDED(hr)));

  } else if (method_call.method_name().compare("requestFocus") == 0) {
    webview->requestFocus(true);
    result->Success();

  } else if (method_call.method_name().compare("setBackgroundColor") == 0) {
    auto color = std::get<int64_t>(arguments[flutter::EncodableValue("color")]);
    webview->setBackgroundColor((int32_t)color);
    result->Success();

  } else if (method_call.method_name().compare("suspend") == 0) {
    webview->setVisible(false);
    webview->suspend();
    result->Success();
  } else if (method_call.method_name().compare("resume") == 0) {
    webview->resume();
    webview->setVisible(true);
    result->Success();

  } else if (method_call.method_name().compare("dispose") == 0) {
    if (webview != NULL) {
      delete webview; //TODO:...
      m_webviewMap.erase(webviewId);
      std::cout << "[webview] native dispose: id = " << webviewId << std::endl;
    }
    result->Success(flutter::EncodableValue(true));
  } else if (method_call.method_name().compare("grantPermission") == 0) {
    auto deferralId = std::get<int>(arguments[flutter::EncodableValue("deferralId")]);
    auto isGranted = std::get<bool>(arguments[flutter::EncodableValue("isGranted")]);
    webview->grantPermission(deferralId, isGranted);
    result->Success();    
  } else if (method_call.method_name().compare("openDevTools") == 0) {
    webview->openDevTools();
    result->Success();

  // CCBrowser additions

  } else if (method_call.method_name().compare("ccb_setVirtualHost") == 0) {
    auto hostname   = std::get<std::string>(arguments[flutter::EncodableValue("hostname")]);
    auto folderPath = std::get<std::string>(arguments[flutter::EncodableValue("folderPath")]);
    HRESULT hr = webview->setVirtualHost(utf8ToUtf16(hostname).c_str(), utf8ToUtf16(folderPath).c_str());
    result->Success(flutter::EncodableValue(SUCCEEDED(hr)));

  } else if (method_call.method_name().compare("ccb_removeVirtualHost") == 0) {
    auto hostname = std::get<std::string>(arguments[flutter::EncodableValue("hostname")]);
    HRESULT hr = webview->removeVirtualHost(utf8ToUtf16(hostname).c_str());
    result->Success(flutter::EncodableValue(SUCCEEDED(hr)));

  } else if (method_call.method_name().compare("ccb_addScript") == 0) {
    std::shared_ptr<flutter::MethodResult<flutter::EncodableValue>> shared_result = std::move(result);
    auto script = std::get<std::string>(arguments[flutter::EncodableValue("script")]);
    webview->addScript(utf8ToUtf16(script).c_str(), [shared_result](std::wstring scriptId) {
      std::string id = wideToUtf8(scriptId);
      shared_result->Success(flutter::EncodableValue(id));
    });

  } else if (method_call.method_name().compare("ccb_removeScript") == 0) {
    auto scriptId = std::get<std::string>(arguments[flutter::EncodableValue("scriptId")]);
    HRESULT hr = webview->removeScript(utf8ToUtf16(scriptId).c_str());
    result->Success(flutter::EncodableValue(SUCCEEDED(hr)));

  } else if (method_call.method_name().compare("hideTab") == 0) {
    // Guard: deactivate() can fire after dispose() — silently succeed.
    if (webview != NULL) webview->hideTab();
    result->Success();

  } else if (method_call.method_name().compare("showTab") == 0) {
    // Guard: activate() can fire before native create completes — silently succeed.
    if (webview != NULL) webview->showTab();
    result->Success();

  } else {
    result->NotImplemented();
  }
}

}  // namespace webview_win_floating
