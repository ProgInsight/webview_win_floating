// =============================================================================
// CCBrowser Bridge — Dart API
// =============================================================================
// Import this in CCBrowser's webview_service.dart to access all CCBrowser
// additions: virtual host mapping, extension script injection.
//
// Usage in CCBrowser:
//   import 'package:webview_win_floating/ccbrowser_bridge.dart';
//
//   final bridge = CCBrowserBridge(webviewId: ctrl.webviewId);
//   await bridge.setVirtualHost('app.data', r'C:\Users\X\AppData\Local\ProgInsight\shared\web');
//   final scriptId = await bridge.addScript('window.__ccbrowser = true;');
//   await bridge.removeScript(scriptId!);
// =============================================================================

import 'webview_win_floating_method_channel.dart';

class CCBrowserBridge {
  CCBrowserBridge({required this.webviewId});

  final int webviewId;
  final _channel = MethodChannelWebviewWinFloating();

  // ── Virtual host mapping ──────────────────────────────────────────────────

  /// Maps https://[hostname] to [folderPath] on disk.
  /// Example: setVirtualHost('app.data', r'C:\Users\X\AppData\Local\ProgInsight\shared\web')
  /// → https://app.data/notepad/index.html serves that folder.
  Future<bool> setVirtualHost(String hostname, String folderPath) {
    return _channel.ccbSetVirtualHost(webviewId, hostname, folderPath);
  }

  /// Removes a previously set virtual host mapping.
  Future<bool> removeVirtualHost(String hostname) {
    return _channel.ccbRemoveVirtualHost(webviewId, hostname);
  }

  // ── Extension script injection ────────────────────────────────────────────

  /// Injects [script] so it runs before every page's own scripts load.
  /// Returns a scriptId you must keep to remove the script later.
  /// Returns null on failure.
  Future<String?> addScript(String script) {
    return _channel.ccbAddScript(webviewId, script);
  }

  /// Removes a previously injected script by its [scriptId].
  Future<bool> removeScript(String scriptId) {
    return _channel.ccbRemoveScript(webviewId, scriptId);
  }
}
