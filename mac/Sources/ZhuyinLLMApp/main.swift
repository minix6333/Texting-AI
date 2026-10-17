import Cocoa
import InputMethodKit

// Input-method entry point: register the IMK connection and start the run loop right away; the
// engine loads its dictionaries and model in the background (see Engine.init).
let launched = Date()
// `--preflight` (run by install.sh): exit at once. The first launch of a new build is held for
// seconds by the system's security scan; doing it at install time keeps first keys from being lost.
if CommandLine.arguments.contains("--preflight") { exit(0) }
let connectionName = Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String
    ?? "com.minix.inputmethod.ZhuyinLLM_Connection"
let server = IMKServer(name: connectionName, bundleIdentifier: Bundle.main.bundleIdentifier)
_ = Engine.shared
_ = Panels.candidates
FileLog.write(String(format: "started, IMK server up in %.2fs", Date().timeIntervalSince(launched)))
NSApplication.shared.run()
