import Carbon
import Foundation

// Registers an input-method bundle in ~/Library/Input Methods with Text Input Sources and lists
// the input sources it provides. (Enabling it is left to the user in System Settings.)
let path = CommandLine.arguments[1]
let status = TISRegisterInputSource(URL(fileURLWithPath: path) as CFURL)
print("TISRegisterInputSource →", status)
let filter = [kTISPropertyBundleID as String: "com.minix.inputmethod.ZhuyinLLM"] as CFDictionary
let list = (TISCreateInputSourceList(filter, true)?.takeRetainedValue() as? [TISInputSource]) ?? []
for src in list {
    func prop(_ key: CFString) -> Any? {
        guard let p = TISGetInputSourceProperty(src, key) else { return nil }
        return Unmanaged<AnyObject>.fromOpaque(p).takeUnretainedValue()
    }
    print(" -", prop(kTISPropertyInputSourceID) ?? "?", "| name:", prop(kTISPropertyLocalizedName) ?? "?",
          "| enabled:", (prop(kTISPropertyInputSourceIsEnabled) as? Bool) ?? false,
          "| selectable:", (prop(kTISPropertyInputSourceIsSelectCapable) as? Bool) ?? false)
}
if list.isEmpty { print("(no input sources registered for this bundle)") }
