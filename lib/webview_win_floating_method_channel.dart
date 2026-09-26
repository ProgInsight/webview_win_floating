import 'dart:developer';
import 'dart:convert';
import 'dart:ui';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:webview_flutter_platform_interface/webview_flutter_platform_interface.dart';

import 'webview.dart';
import 'webview_win_floating_platform_interface.dart';

/// An implementation of [WebviewWinFloatingPlatform] that uses method channels.
class MethodChannelWebviewWinFloating extends WebviewWinFloatingPlatform {
  @visibleForTesting
  final methodChannel = const MethodChannel('webview_win_floating');

  final webviewMap = <int, WeakReference<WinWebViewController>>{};

  MethodChannelWebviewWinFloating() {
    methodChannel.invokeMethod<bool>('init');
    assert(() { return true; }());
    initMethodCallHandler();
  }

  // ── CCBrowser additions ───────────────────────────────────────────────────

  @override
  Future<bool> ccbSetVirtualHost(
      int webviewId, String hostname, String folderPath) async {
    final result = await methodChannel.invokeMethod<bool>(
        'ccb_setVirtualHost', {
      'webviewId': webviewId,
      'hostname': hostname,
      'folderPath': folderPath,
    });
    return result ?? false;
  }

  @override
  Future<bool> ccbRemoveVirtualHost(int webviewId, String hostname) async {
    final result = await methodChannel.invokeMethod<bool>(
        'ccb_removeVirtualHost', {
      'webviewId': webviewId,
      'hostname': hostname,
    });
    return result ?? false;
  }

  @override
  Future<String?> ccbAddScript(int webviewId, String script) async {
    return methodChannel.invokeMethod<String>('ccb_addScript', {
      'webviewId': webviewId,
      'script': script,
    });
  }

  @override
  Future<bool> ccbRemoveScript(int webviewId, String scriptId) async {
    final result = await methodChannel.invokeMethod<bool>(
        'ccb_removeScript', {
      'webviewId': webviewId,
      'scriptId': scriptId,
    });
    return result ?? false;
  }

  // ── Native → Dart ─────────────────────────────────────────────────────────

  void initMethodCallHandler() {
    methodChannel.setMethodCallHandler((call) async {
      int? webviewId = call.arguments['webviewId'];
      assert(webviewId != null);
      final ref = webviewMap[webviewId];
      if (ref == null) {
        log('webview not found: id = $webviewId');
        return;
      }
      final controller = ref.target;
      if (controller == null) {
        webviewMap.remove(webviewId);
        log('webview is alive but not referenced anymore: id = $webviewId');
        return;
      }

      if (call.method == 'OnWebMessageReceived') {
        String? channelName = call.arguments['JkChannelName'];
        String message = call.arguments['message']!;
        var jobj = json.decode(message);
        if (channelName != null) {
          jobj = {'JkChannelName': channelName, 'msg': jobj};
        }
        controller.notifyMessageReceived_(jobj);
      } else if (call.method == 'onNavigationRequest') {
        int requestId = call.arguments['requestId']!;
        String url = call.arguments['url']!;
        bool isNewWindow = call.arguments['isNewWindow']!;
        controller.notifyOnNavigationRequest_(requestId, url, isNewWindow);
      } else if (call.method == 'onPageStarted') {
        controller.notifyOnPageStarted_(call.arguments['url']!);
      } else if (call.method == 'onPageFinished') {
        controller.notifyOnPageFinished_(call.arguments['url']!);
      } else if (call.method == 'onHttpError') {
        controller.notifyOnHttpError_(
            call.arguments['url']!, call.arguments['errCode']!);
      } else if (call.method == 'onSslAuthError') {
        controller.notifyOnSslAuthError_(call.arguments['url']!);
      } else if (call.method == 'onWebResourceError') {
        String url = call.arguments['url']!;
        int errCode = call.arguments['errCode']!;
        String errType = call.arguments['errType']!;
        var error = WebResourceError(
          url: url,
          errorCode: errCode,
          description: errType,
          errorType: WebResourceErrorType.values.byName(errType),
        );
        controller.notifyOnWebResourceError_(error);
      } else if (call.method == 'onUrlChange') {
        controller.notifyOnUrlChange_(call.arguments['url']!);
      } else if (call.method == 'onPageTitleChanged') {
        controller.notifyOnPageTitleChanged_(call.arguments['title']!);
      } else if (call.method == 'onMoveFocusRequest') {
        controller.notifyOnFocusRequest_(call.arguments['isNext']!);
      } else if (call.method == 'OnFullScreenChanged') {
        bool? isFullScreen = call.arguments['isFullScreen'];
        assert(isFullScreen != null);
        controller.notifyFullScreenChanged_(isFullScreen!);
      } else if (call.method == 'onHistoryChanged') {
        controller.notifyHistoryChanged_();
      } else if (call.method == 'onAskPermission') {
        String url = call.arguments['url']!;
        int kind = call.arguments['kind']!;
        int deferralId = call.arguments['deferralId']!;
        controller.notifyAskPermission_(
          url,
          WinWebViewPermissionResourceType.values[kind],
          deferralId,
        );
      } else {
        assert(false, 'unknown call from native: ${call.method}');
      }
    });
  }

  @override
  void registerWebView(int webviewId, WinWebViewController webview) {
    webviewMap[webviewId] = WeakReference(webview);
  }

  @override
  void unregisterWebView(int webviewId) {
    webviewMap.remove(webviewId);
  }

  @override
  Future<bool> create(int webviewId,
      {String? initialUrl,
      String? userDataFolder,
      String? profileName,
      String? ccbConfig}) async {
    return await methodChannel.invokeMethod<bool>('create', {
          'webviewId': webviewId,
          'url': initialUrl ?? '',
          'userDataFolder': userDataFolder ?? '',
          'profileName': profileName ?? '',
          'ccbConfig': ccbConfig ?? '',
        }) ??
        false;
  }

  @override
  Future<void> setHasNavigationDecision(
      int webviewId, bool hasNavigationDecision) async {
    return methodChannel.invokeMethod<void>('setHasNavigationDecision', {
      'webviewId': webviewId,
      'hasNavigationDecision': hasNavigationDecision,
    });
  }

  @override
  Future<void> allowNavigationRequest(
      int webviewId, int requestId, bool isAllowed) async {
    return methodChannel.invokeMethod<void>('allowNavigationRequest', {
      'webviewId': webviewId,
      'requestId': requestId,
      'isAllowed': isAllowed,
    });
  }

  @override
  Future<void> updateBounds(int webviewId, Offset offset, Size size,
      double devicePixelRatio) async {
    await methodChannel.invokeMethod<bool>('updateBounds', {
      'webviewId': webviewId,
      'left': (offset.dx * devicePixelRatio).toInt(),
      'top': (offset.dy * devicePixelRatio).toInt(),
      'right': ((offset.dx + size.width) * devicePixelRatio).toInt(),
      'bottom': ((offset.dy + size.height) * devicePixelRatio).toInt(),
    });
  }

  @override
  Future<void> loadUrl(int webviewId, String url) async {
    await methodChannel
        .invokeMethod<bool>('loadUrl', {'webviewId': webviewId, 'url': url});
  }

  @override
  Future<void> loadHtmlString(
      int webviewId, String html, String? baseUrl) async {
    await methodChannel.invokeMethod<bool>('loadHtmlString', {
      'webviewId': webviewId,
      'html': html,
      'baseUrl': baseUrl,
    });
  }

  @override
  Future<void> runJavaScript(int webviewId, String javaScriptString) async {
    await methodChannel.invokeMethod<void>('runJavascript', {
      'webviewId': webviewId,
      'javaScriptString': javaScriptString,
      'ignoreResult': true,
    });
  }

  @override
  Future<Object> runJavaScriptReturningResult(
      int webviewId, String javaScriptString) async {
    var value = await methodChannel.invokeMethod<String?>('runJavascript', {
      'webviewId': webviewId,
      'javaScriptString': javaScriptString,
      'ignoreResult': false,
    });
    if (value == null) return 'null';
    var vi = int.tryParse(value);
    if (vi != null) return vi;
    var vd = double.tryParse(value);
    if (vd != null) return vd;
    var vb = bool.tryParse(value);
    if (vb != null) return vb;
    return value;
  }

  @override
  Future<void> addScriptChannelByName(int webviewId, String channelName) {
    return methodChannel.invokeMethod<void>('addScriptChannelByName', {
      'webviewId': webviewId,
      'channelName': channelName,
    });
  }

  @override
  Future<void> removeScriptChannelByName(int webviewId, String channelName) {
    return methodChannel.invokeMethod<void>('removeScriptChannelByName', {
      'webviewId': webviewId,
      'channelName': channelName,
    });
  }

  @override
  Future<void> setFullScreen(int webviewId, bool isFullScreen) async {
    await methodChannel.invokeMethod<bool>(
        'setFullScreen', {'webviewId': webviewId, 'isFullScreen': isFullScreen});
  }

  @override
  Future<void> setVisibility(int webviewId, bool isVisible) async {
    await methodChannel.invokeMethod<bool>(
        'setVisibility', {'webviewId': webviewId, 'isVisible': isVisible});
  }

  @override
  Future<void> enableJavascript(int webviewId, bool isEnable) async {
    await methodChannel.invokeMethod<bool>(
        'enableJavascript', {'webviewId': webviewId, 'isEnable': isEnable});
  }

  @override
  Future<bool> setUserAgent(int webviewId, String userAgent) async {
    return (await methodChannel.invokeMethod<bool?>(
        'setUserAgent', {'webviewId': webviewId, 'userAgent': userAgent}))!;
  }

  @override
  Future<bool> canGoBack(int webviewId) async {
    return (await methodChannel
        .invokeMethod<bool?>('canGoBack', {'webviewId': webviewId}))!;
  }

  @override
  Future<bool> canGoForward(int webviewId) async {
    return (await methodChannel
        .invokeMethod<bool?>('canGoForward', {'webviewId': webviewId}))!;
  }

  @override
  Future<void> goBack(int webviewId) async {
    await methodChannel.invokeMethod<void>('goBack', {'webviewId': webviewId});
  }

  @override
  Future<void> goForward(int webviewId) async {
    await methodChannel
        .invokeMethod<void>('goForward', {'webviewId': webviewId});
  }

  @override
  Future<void> reload(int webviewId) async {
    await methodChannel.invokeMethod<void>('reload', {'webviewId': webviewId});
  }

  @override
  Future<void> cancelNavigate(int webviewId) async {
    await methodChannel
        .invokeMethod<void>('cancelNavigate', {'webviewId': webviewId});
  }

  @override
  Future<void> clearCache(int webviewId) async {
    await methodChannel
        .invokeMethod<void>('clearCache', {'webviewId': webviewId});
  }

  @override
  Future<bool> clearCookies(int webviewId) async {
    return (await methodChannel
        .invokeMethod<bool?>('clearCookies', {'webviewId': webviewId}))!;
  }

  @override
  Future<void> requestFocus(int webviewId) async {
    await methodChannel
        .invokeMethod<void>('requestFocus', {'webviewId': webviewId});
  }

  @override
  Future<void> setBackgroundColor(int webviewId, Color color) async {
    await methodChannel.invokeMethod<void>('setBackgroundColor', {
      'webviewId': webviewId,
      'color': color.value,
    });
  }

  @override
  Future<void> suspend(int webviewId) async {
    await methodChannel
        .invokeMethod<bool>('suspend', {'webviewId': webviewId});
  }

  @override
  Future<void> resume(int webviewId) async {
    await methodChannel.invokeMethod<bool>('resume', {'webviewId': webviewId});
  }

  @override
  Future<void> dispose(int webviewId) async {
    await methodChannel
        .invokeMethod<bool>('dispose', {'webviewId': webviewId});
  }

  @override
  Future<void> grantPermission(
      int webviewId, int deferralId, bool isGranted) {
    return methodChannel.invokeMethod<void>('grantPermission', {
      'webviewId': webviewId,
      'deferralId': deferralId,
      'isGranted': isGranted,
    });
  }

  @override
  Future<void> enableZoom(int webviewId, bool isEnable) async {
    await methodChannel.invokeMethod<bool>('enableIsZoomControl', {
      'webviewId': webviewId,
      'isEnable': isEnable,
    });
  }

  @override
  Future<void> openDevTools(int webviewId) {
    return methodChannel
        .invokeMethod<void>('openDevTools', {'webviewId': webviewId});
  }

  @override
  Future<void> enableStatusBar(int webviewId, bool isEnable) async {
    await methodChannel.invokeMethod<bool>('enableStatusBar', {
      'webviewId': webviewId,
      'isEnable': isEnable,
    });
  }

  @override
  Future<void> hideTab(int webviewId) async {
    await methodChannel
        .invokeMethod<void>('hideTab', {'webviewId': webviewId});
  }

  @override
  Future<void> showTab(int webviewId) async {
    await methodChannel
        .invokeMethod<void>('showTab', {'webviewId': webviewId});
  }
}
