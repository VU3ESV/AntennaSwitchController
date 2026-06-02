import AntennaSwitchController
import Foundation

// Standalone entry point. `App.main()` is the same call `@main` synthesizes.
// `--snapshots <dir>` renders documentation images offscreen and exits.
if let i = CommandLine.arguments.firstIndex(of: "--snapshots"), i + 1 < CommandLine.arguments.count {
    MainActor.assumeIsolated { SnapshotTool.render(to: CommandLine.arguments[i + 1]) }
} else {
    AntennaSwitchStandaloneApp.main()
}
