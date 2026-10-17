// swift-tools-version: 5.9
import PackageDescription

let llamaLinker: [LinkerSetting] = [
    .unsafeFlags(["-L/opt/homebrew/lib", "-Xlinker", "-rpath", "-Xlinker", "/opt/homebrew/lib"]),
]

let package = Package(
    name: "ZhuyinLLM",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "ZhuyinLLM", targets: ["ZhuyinLLMApp"]),
        .executable(name: "imecli", targets: ["imecli"]),
    ],
    targets: [
        .systemLibrary(name: "CLlama", path: "Sources/CLlama"),
        .target(name: "IMECore", path: "Sources/IMECore"),
        .target(name: "LLMScorer", dependencies: ["CLlama"], path: "Sources/LLMScorer",
                linkerSettings: llamaLinker + [.linkedFramework("Accelerate")]),
        .executableTarget(name: "imecli", dependencies: ["IMECore", "LLMScorer"], path: "Sources/imecli",
                          linkerSettings: llamaLinker),
        .executableTarget(name: "ZhuyinLLMApp", dependencies: ["IMECore", "LLMScorer"], path: "Sources/ZhuyinLLMApp",
                          linkerSettings: llamaLinker + [.linkedFramework("InputMethodKit"), .linkedFramework("Carbon")]),
    ]
)
