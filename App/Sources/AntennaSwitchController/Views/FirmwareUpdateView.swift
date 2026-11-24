import SwiftUI
import UniformTypeIdentifiers
import RadioPluginUI

/// Sheet that updates a controller's firmware over the air (ArduinoOTA), from the
/// latest GitHub release or a local `.bin`.
struct FirmwareUpdateView: View {
    @StateObject private var model: FirmwareUpdateModel
    @Environment(\.dismiss) private var dismiss
    @State private var importing = false

    private static let binTypes = [UTType(filenameExtension: "bin", conformingTo: .data) ?? .data]

    init(controller: ControllerViewModel) {
        _model = StateObject(wrappedValue: FirmwareUpdateModel(controller: controller))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            header
            Form {
                Section("Firmware image") {
                    Picker("Source", selection: $model.source) {
                        ForEach(FirmwareUpdateModel.Source.allCases) { Text($0.rawValue).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    sourceDetail
                }
                Section("OTA password") {
                    SecureField("Password", text: $model.password, prompt: Text("Leave blank if none is set"))
                    Text("The one set under Settings → Device. The controller never sends it back, so type it here.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            .formStyle(.grouped)
            .disabled(model.isUpdating)

            statusArea
            buttons
        }
        .padding(20)
        .frame(width: 540)
        .fileImporter(isPresented: $importing, allowedContentTypes: Self.binTypes) { result in
            switch result {
            case .success(let url): model.loadFile(url)
            case .failure(let error): model.fileImportFailed(error)
            }
        }
        .interactiveDismissDisabled(model.isUpdating)
        .onAppear { model.prepare() }
        .onChange(of: model.source) { _, _ in model.prepare() }
    }

    // MARK: - Sections

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Update Firmware").font(.title2.bold())
            Text(subtitle).font(.callout).foregroundStyle(.secondary)
        }
    }

    private var subtitle: String {
        let name = model.controller.displayName
        let parts = [name, "firmware \(model.deviceVersion ?? "unknown")"]
            + (name == model.target ? [] : [model.target])
        return parts.joined(separator: " · ")
    }

    @ViewBuilder private var sourceDetail: some View {
        switch model.source {
        case .release:
            if case .loadingImage(let what) = model.stage {
                HStack(spacing: 8) { ProgressView().controlSize(.small); Text(what) }
            } else if let image = model.image, let release = model.release {
                imageSummary(image, title: "\(release.tag) — \(image.name)")
                Button("Check Again") { model.fetchLatestRelease() }
            } else {
                Button("Check GitHub for the Latest Release") { model.fetchLatestRelease() }
            }
        case .file:
            if let image = model.image { imageSummary(image, title: image.name) }
            Button(model.image == nil ? "Choose .bin File…" : "Choose a Different File…") { importing = true }
        }
        if let err = model.sourceError {
            Banner(level: .error, title: "Can’t use this firmware", message: err)
        }
    }

    private func imageSummary(_ image: FirmwareImage, title: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title).font(.body.weight(.medium))
            Text("Firmware \(image.version ?? "?") · \(Self.bytes(image.size)) · MD5 \(image.md5.prefix(8))…")
                .font(.caption).foregroundStyle(.secondary)
            if let note = versionNote(image) {
                Text(note).font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    private func versionNote(_ image: FirmwareImage) -> String? {
        guard let current = model.deviceVersion, let new = image.version else { return nil }
        switch FirmwareVersion.compare(new, current) {
        case .orderedDescending?: return "Newer than the controller’s \(current)."
        case .orderedSame?:       return "Same version the controller runs — this reinstalls it."
        case .orderedAscending?:  return "Older than the controller’s \(current) — this downgrades it."
        case nil:                 return nil
        }
    }

    @ViewBuilder private var statusArea: some View {
        switch model.stage {
        case .uploading(let phase):
            progress(for: phase)
        case .rebooting:
            HStack(spacing: 8) {
                ProgressView().controlSize(.small)
                Text("Firmware written. Waiting for the controller to reboot…")
            }
        case .succeeded(let message):
            Banner(level: .success, title: "Update complete", message: message)
        case .failed(let message):
            Banner(level: .error, title: "Update failed", message: message)
            if model.needsReboot {
                Banner(level: .warning, title: "All relays are off",
                       message: "The controller entered update mode, which switches every relay off and drops the radio link. Reboot it to resume normal switching.")
            }
        case .idle, .loadingImage:
            if let reason = model.blockReason, model.image != nil {
                Banner(level: .warning, title: "Can’t update right now", message: reason)
            } else {
                Text("During the update every relay switches off and the radio link drops. The controller reboots when it’s done — about 30 seconds in all.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    private func progress(for phase: EspOTAUploader.Phase) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            switch phase {
            case .inviting:
                Text("Contacting the controller…")
                ProgressView().progressViewStyle(.linear)
            case .authenticating:
                Text("Checking the OTA password…")
                ProgressView().progressViewStyle(.linear)
            case .waitingForDevice:
                Text("Controller is in update mode (relays off). Waiting for it to connect…")
                ProgressView().progressViewStyle(.linear)
            case .uploading(let sent, let total):
                Text("Uploading \(Self.bytes(sent)) of \(Self.bytes(total))…")
                ProgressView(value: Double(sent), total: Double(max(total, 1)))
            case .verifying:
                Text("Verifying and writing the image…")
                ProgressView(value: 1)
            }
        }
    }

    private var buttons: some View {
        HStack {
            if case .failed = model.stage, model.needsReboot {
                Button("Reboot Controller") { Task { await model.rebootController() } }
            }
            Spacer()
            if model.isUpdating {
                Button("Cancel", role: .cancel) { model.cancel() }
            } else {
                Button(isFinished ? "Done" : "Close") { dismiss() }
                    .keyboardShortcut(isFinished ? .defaultAction : .cancelAction)
                if !isFinished {
                    Button("Update") { model.startUpdate() }
                        .keyboardShortcut(.defaultAction)
                        .disabled(model.blockReason != nil || model.isBusy)
                }
            }
        }
    }

    private var isFinished: Bool {
        if case .succeeded = model.stage { return true }
        return false
    }

    private static func bytes(_ n: Int) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(n), countStyle: .file)
    }
}
