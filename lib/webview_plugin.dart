// lib/webview_plugin.dart  (CCBrowser fork)
// ignore_for_file: avoid_unused_constructor_parameters

import 'package:flutter/foundation.dart';
import 'package:webview_flutter_platform_interface/webview_flutter_platform_interface.dart';
import 'webview_win_floating_method_channel.dart';
import 'webview_win_floating_platform_interface.dart';

export 'package:webview_flutter_platform_interface/webview_flutter_platform_interface.dart'
    show
        JavaScriptMessage,
        NavigationDecision,
        NavigationRequest,
        PlatformWebViewControllerCreationParams,
        UrlChange;

// --------------------------------------------------------------------------
// callback types
// --------------------------------------------------------------------------

typedef JavaScriptMessageCallback = void Function(JavaScriptMessage message);
typedef WinWebViewPermissionRequest = ({
  String uri,
  int kind,
  int deferralId
});

// --------------------------------------------------------------------------
// navigation delegate
// --------------------------------------------------------------------------

class WinNavigationDelegate {
  WinNavigationDelegate({
    this.onPageStarted,
    this.onPageFinished,
    this.onHttpError,
    this.onSslAuthError,
    this.onWebResourceError,
    this.onNavigationRequest,
    this.onUrlChange,
    this.onPageTitleChanged,
    this.onHistoryChanged,
    this.onFullScreenChanged,
    this.onFaviconChanged,
  });

  final void Function(String url)? onPageStarted;
  final void Function(String url)? onPageFinished;
  final void Function(String url, int errCode)? onHttpError;
  final void Function(String url)? onSslAuthError;
  final void Function(String url, int errCode, String errType)?
      onWebResourceError;
  final NavigationDecision Function(NavigationRequest request)?
      onNavigationRequest;
  final void Function(UrlChange change)? onUrlChange;
  final void Function(String title)? onPageTitleChanged;
  final void Function()? onHistoryChanged;
  final void Function(bool isFullScreen)? onFullScreenChanged;
  final void Function(String? faviconUrl)? onFaviconChanged;
}

// --------------------------------------------------------------------------
// controller creation params
// --------------------------------------------------------------------------

typedef WindowsPlatformWebViewControllerCreationParams // legacy name
    = WindowsWebViewControllerCreationParams;

@immutable
class WindowsWebViewControllerCreationParams
    extends PlatformWebViewControllerCreationParams {
  final String? userDataFolder;

  /// Optional profile name for session isolation.
  ///
  /// When specified, WebView2 will use a named profile to isolate cookies,
  /// cache, and storage from other WebView instances, while sharing the same
  /// underlying browser process (more memory-efficient than separate
  /// environments). This works like Chrome's user profiles.
  ///
  /// See: https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/multi-profile-support
  final String? profileName;

  final bool suspendDuringDeactive;

  // ── CCBrowser: lazy-loaded config ─────────────────────────────────────────
  //
  // These are passed to the native `create` call as a JSON blob.
  // The C++ plugin applies them synchronously inside `onCreated`, after
  // the WebView2 object exists — so no async Dart→native round-trips are
  // needed after creation.
  //
  // virtualHostname + virtualFolder:  maps https://<virtualHostname>/ to the
  //   local folder at <virtualFolder> via SetVirtualHostNameToFolderMapping.
  //
  // contentScripts: JS strings injected before every page's own scripts via
  //   AddScriptToExecuteOnDocumentCreated.  The C++ side returns the script
  //   IDs via a new "onScriptsAdded" method-channel event if you ever need
  //   to remove them later; for now they persist for the webview lifetime.

  /// The https:// hostname to map to [virtualFolder] (e.g. "app.data").
  final String? virtualHostname;

  /// Absolute Windows path served under [virtualHostname] (e.g.
  /// r"C:\Users\X\AppData\Local\ProgInsight\shared\web").
  final String? virtualFolder;

  /// JS strings to inject before every page's own scripts.
  final List<String> contentScripts;

  /// Creates a new [WindowsPlatformWebViewControllerCreationParams] instance.
  const WindowsWebViewControllerCreationParams({
    this.userDataFolder,
    this.profileName,
    this.suspendDuringDeactive = true,
    this.virtualHostname,
    this.virtualFolder,
    this.contentScripts = const [],
  }) : super();

  /// Creates a [WindowsPlatformWebViewControllerCreationParams] instance based on [PlatformWebViewControllerCreationParams].
  factory WindowsWebViewControllerCreationParams.fromPlatformWebViewControllerCreationParams(
    // Recommended placeholder to prevent being broken by platform interface.
    // ignore: avoid_unused_constructor_parameters
    PlatformWebViewControllerCreationParams params,
  ) {
    return params as WindowsWebViewControllerCreationParams;
  }
}

class WindowsPlatformWebViewController extends PlatformWebViewController {
  WindowsPlatformWebViewController(
      PlatformWebViewControllerCreationParams params)
      : super.implementation(params);

  @override
  Future<void> setJavaScriptMode(JavaScriptMode javaScriptMode) async {}
}
