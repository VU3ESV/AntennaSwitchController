import SwiftUI
import RadioPluginUI

/// Root UI: a sidebar of controllers (saved + discovered) and a detail pane for
/// the selected controller. Shared by the standalone app and the suite plugin.
struct ContentView: View {
    @EnvironmentObject var store: ControllersStore
    @State private var showAdd = false

    var body: some View {
        NavigationSplitView {
            SidebarView(showAdd: $showAdd)
                .navigationSplitViewColumnWidth(min: 240, ideal: 290)
        } detail: {
            if let c = store.controller(store.selection) {
                ControllerDetailView(controller: c)
                    .id(c.id)   // fresh view model per selected controller
            } else {
                EmptyStateView(systemImage: "antenna.radiowaves.left.and.right",
                               title: "No Controller Selected",
                               message: "Add a controller by IP, or pick one discovered on your network.",
                               actionTitle: "Add by IP",
                               action: { showAdd = true })
            }
        }
        .sheet(isPresented: $showAdd) { AddControllerView() }
    }
}

struct SidebarView: View {
    @EnvironmentObject var store: ControllersStore
    @Binding var showAdd: Bool

    var body: some View {
        List(selection: $store.selection) {
            Section("My Controllers") {
                if store.controllers.isEmpty {
                    Text("No controllers yet").foregroundStyle(.secondary).font(.callout)
                }
                ForEach(store.controllers) { c in
                    VStack(alignment: .leading, spacing: 2) {
                        Text(c.name)
                        Text(c.address).font(.caption).foregroundStyle(.secondary)
                    }
                    .tag(c.id)
                    .contextMenu {
                        Button("Remove", role: .destructive) { store.remove(c.id) }
                    }
                }
            }

            if !store.newlyDiscovered.isEmpty {
                Section("Discovered on Network") {
                    ForEach(store.newlyDiscovered) { d in
                        HStack {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(d.title)
                                Text(d.address).font(.caption).foregroundStyle(.secondary)
                            }
                            Spacer()
                            Button { store.addDiscovered(d) } label: {
                                Image(systemName: "plus.circle")
                            }
                            .buttonStyle(.borderless)
                            .help("Add this controller")
                        }
                    }
                }
            }
        }
        .listStyle(.sidebar)
        .navigationTitle("Controllers")
        .toolbar {
            ToolbarItem {
                Button { showAdd = true } label: { Label("Add by IP", systemImage: "plus") }
            }
            ToolbarItem {
                Button { store.rescan() } label: {
                    Label("Rescan", systemImage: "arrow.clockwise")
                }
                .help(store.isBrowsing ? "Discovering…" : "Rescan network")
            }
        }
    }
}

struct AddControllerView: View {
    @EnvironmentObject var store: ControllersStore
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var address = ""

    private var trimmed: String { address.trimmingCharacters(in: .whitespaces) }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Add Controller").font(.title2).bold()
            Text("Enter the controller's IP address or mDNS hostname.")
                .font(.caption).foregroundStyle(.secondary)
            TextField("Name (optional)", text: $name)
            TextField("e.g. 192.168.1.42 or ANT-SW-Controller-7A.local", text: $address)
            HStack {
                Spacer()
                Button("Cancel") { dismiss() }
                Button("Add") {
                    store.add(name: name, address: trimmed)
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(trimmed.isEmpty)
            }
        }
        .padding(20)
        .frame(width: 480)
    }
}
