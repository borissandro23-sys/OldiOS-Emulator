import SwiftUI
import UniformTypeIdentifiers
import ZIPFoundation

struct InstalledApp: Identifiable {
    let id: String
    let name: String
    let bundleIdentifier: String
    let folderURL: URL
}

@MainActor
final class AppLibrary: ObservableObject {
    @Published private(set) var apps: [InstalledApp] = []
    @Published var status = "Выберите IPA-файл, чтобы добавить его в библиотеку."

    private let fileManager = FileManager.default

    private var libraryURL: URL {
        let documents = fileManager.urls(for: .documentDirectory, in: .userDomainMask)[0]
        return documents.appendingPathComponent("InstalledApps", isDirectory: true)
    }

    init() {
        do {
            try fileManager.createDirectory(at: libraryURL, withIntermediateDirectories: true)
            reload()
        } catch {
            status = "Не удалось создать библиотеку: \(error.localizedDescription)"
        }
    }

    func reload() {
        do {
            try fileManager.createDirectory(at: libraryURL, withIntermediateDirectories: true)
            let folders = try fileManager.contentsOfDirectory(
                at: libraryURL,
                includingPropertiesForKeys: [.isDirectoryKey],
                options: [.skipsHiddenFiles]
            )
            apps = folders.compactMap { folder in
                let plistURL = folder.appendingPathComponent("Info.plist")
                guard let data = try? Data(contentsOf: plistURL),
                      let plist = try? PropertyListSerialization.propertyList(
                        from: data, options: [], format: nil
                      ) as? [String: Any] else { return nil }
                let name = (plist["CFBundleDisplayName"] as? String)
                    ?? (plist["CFBundleName"] as? String)
                    ?? folder.deletingPathExtension().lastPathComponent
                let bundleID = (plist["CFBundleIdentifier"] as? String) ?? "Не указан"
                return InstalledApp(
                    id: folder.lastPathComponent,
                    name: name,
                    bundleIdentifier: bundleID,
                    folderURL: folder
                )
            }.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        } catch {
            status = "Ошибка чтения библиотеки: \(error.localizedDescription)"
        }
    }

    func importIPA(from sourceURL: URL) {
        let hasAccess = sourceURL.startAccessingSecurityScopedResource()
        defer {
            if hasAccess { sourceURL.stopAccessingSecurityScopedResource() }
        }

        let stagingRoot = fileManager.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        do {
            try fileManager.createDirectory(at: stagingRoot, withIntermediateDirectories: true)
            defer { try? fileManager.removeItem(at: stagingRoot) }

            guard let archive = Archive(url: sourceURL, accessMode: .read) else {
                throw ImportError.invalidArchive
            }

            guard let infoEntry = archive.first(where: {
                $0.path.hasPrefix("Payload/") &&
                $0.path.contains(".app/") &&
                $0.path.hasSuffix(".app/Info.plist")
            }) else {
                throw ImportError.appBundleNotFound
            }

            let appPath = String(infoEntry.path.dropLast("Info.plist".count))
                .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            let appFolderName = URL(fileURLWithPath: appPath).lastPathComponent
            guard appFolderName.hasSuffix(".app"), !appFolderName.contains("..") else {
                throw ImportError.unsafeBundleName
            }

            let destination = stagingRoot.appendingPathComponent(appFolderName, isDirectory: true)
            try fileManager.createDirectory(at: destination, withIntermediateDirectories: true)

            for entry in archive where entry.path == appPath || entry.path.hasPrefix(appPath + "/") {
                let relativePath = String(entry.path.dropFirst(appPath.count))
                    .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
                if relativePath.isEmpty { continue }

                let components = relativePath.split(separator: "/")
                guard !components.isEmpty,
                      components.allSatisfy({ $0 != "." && $0 != ".." }) else {
                    throw ImportError.unsafeArchivePath
                }

                let outputURL = destination.appendingPathComponent(relativePath).standardizedFileURL
                let rootPath = destination.standardizedFileURL.path + "/"
                guard outputURL.path.hasPrefix(rootPath) else {
                    throw ImportError.unsafeArchivePath
                }

                if entry.type == .directory {
                    try fileManager.createDirectory(at: outputURL, withIntermediateDirectories: true)
                } else {
                    try fileManager.createDirectory(
                        at: outputURL.deletingLastPathComponent(),
                        withIntermediateDirectories: true
                    )
                    _ = try archive.extract(entry, to: outputURL, skipCRC32: false)
                }
            }

            let plistURL = destination.appendingPathComponent("Info.plist")
            guard fileManager.fileExists(atPath: plistURL.path) else {
                throw ImportError.appInfoMissing
            }

            let finalURL = libraryURL.appendingPathComponent(appFolderName, isDirectory: true)
            guard !fileManager.fileExists(atPath: finalURL.path) else {
                throw ImportError.alreadyImported
            }
            try fileManager.moveItem(at: destination, to: finalURL)
            status = "Добавлено в библиотеку: \(appFolderName)"
            reload()
        } catch {
            status = "Не удалось импортировать IPA: \(error.localizedDescription)"
        }
    }

    func remove(_ app: InstalledApp) {
        do {
            try fileManager.removeItem(at: app.folderURL)
            status = "Удалено: \(app.name)"
            reload()
        } catch {
            status = "Ошибка удаления: \(error.localizedDescription)"
        }
    }
}

enum ImportError: LocalizedError {
    case invalidArchive, appBundleNotFound, unsafeBundleName
    case unsafeArchivePath, appInfoMissing, alreadyImported

    var errorDescription: String? {
        switch self {
        case .invalidArchive: return "Файл не является корректным ZIP/IPA архивом."
        case .appBundleNotFound: return "В архиве не найден Payload/<App>.app/Info.plist."
        case .unsafeBundleName: return "Недопустимое имя приложения."
        case .unsafeArchivePath: return "Архив содержит небезопасный путь."
        case .appInfoMissing: return "В приложении отсутствует Info.plist."
        case .alreadyImported: return "Это приложение уже есть в библиотеке."
        }
    }
}

struct ContentView: View {
    @StateObject private var library = AppLibrary()
    @State private var showingImporter = false
    @State private var appToDelete: InstalledApp?

    var body: some View {
        NavigationView {
            List {
                Section {
                    if library.apps.isEmpty {
                        VStack(spacing: 12) {
                            Image(systemName: "square.stack.3d.up")
                                .font(.system(size: 42, weight: .light))
                                .foregroundColor(.secondary)
                            Text("Нет приложений")
                                .font(.headline)
                            Text("Импортированные IPA появятся здесь.")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                                .multilineTextAlignment(.center)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 30)
                        .listRowBackground(Color.clear)
                    } else {
                        ForEach(library.apps) { app in
                            HStack(spacing: 13) {
                                AppIconView(app: app)
                                VStack(alignment: .leading, spacing: 4) {
                                    Text(app.name)
                                        .font(.body)
                                        .foregroundColor(.primary)
                                    Text(app.bundleIdentifier)
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                        .lineLimit(1)
                                }
                                Spacer(minLength: 4)
                                Image(systemName: "checkmark.circle.fill")
                                    .font(.system(size: 17))
                                    .foregroundColor(.green)
                            }
                            .padding(.vertical, 4)
                            .swipeActions {
                                Button(role: .destructive) {
                                    appToDelete = app
                                } label: {
                                    Label("Удалить", systemImage: "trash")
                                }
                            }
                        }
                    }
                } header: {
                    HStack {
                        Text("Приложения")
                        Spacer()
                        Text("\(library.apps.count)")
                            .foregroundColor(.secondary)
                    }
                }

                Section {
                    Button {
                        showingImporter = true
                    } label: {
                        Label("Установить IPA", systemImage: "square.and.arrow.down")
                    }
                    .foregroundColor(.blue)
                } footer: {
                    Text("IPA добавляется только в библиотеку OldiOS. Это не устанавливает приложение в iOS и пока не позволяет запускать его.")
                }

                Section {
                    HStack(spacing: 8) {
                        Image(systemName: "info.circle")
                            .foregroundColor(.secondary)
                        Text(library.status)
                            .font(.footnote)
                            .foregroundColor(.secondary)
                    }
                } header: {
                    Text("Состояние")
                }
            }
            .listStyle(.insetGrouped)
            .navigationTitle("OldiOS")
            .navigationBarTitleDisplayMode(.large)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button {
                        showingImporter = true
                    } label: {
                        Image(systemName: "plus")
                            .font(.system(size: 17, weight: .semibold))
                    }
                    .accessibilityLabel("Импортировать IPA")
                }
            }
            .fileImporter(
                isPresented: $showingImporter,
                allowedContentTypes: [UTType(filenameExtension: "ipa") ?? .data],
                allowsMultipleSelection: false
            ) { result in
                switch result {
                case .success(let urls):
                    if let url = urls.first { library.importIPA(from: url) }
                case .failure(let error):
                    library.status = "Выбор файла отменён или завершился ошибкой: \(error.localizedDescription)"
                }
            }
            .confirmationDialog(
                "Удалить приложение из библиотеки?",
                isPresented: Binding(
                    get: { appToDelete != nil },
                    set: { if !$0 { appToDelete = nil } }
                ),
                titleVisibility: .visible
            ) {
                Button("Удалить", role: .destructive) {
                    if let app = appToDelete { library.remove(app) }
                    appToDelete = nil
                }
                Button("Отмена", role: .cancel) { appToDelete = nil }
            } message: {
                Text(appToDelete?.name ?? "")
            }
        }
        .navigationViewStyle(.stack)
    }
}

struct AppIconView: View {
    let app: InstalledApp

    var body: some View {
        Group {
            if let icon = loadIcon() {
                Image(uiImage: icon)
                    .resizable()
                    .scaledToFit()
            } else {
                Image(systemName: "app.fill")
                    .resizable()
                    .scaledToFit()
                    .padding(10)
                    .foregroundColor(.blue)
                    .background(Color.blue.opacity(0.12))
            }
        }
        .frame(width: 42, height: 42)
        .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))
    }

    private func loadIcon() -> UIImage? {
        guard let data = try? Data(contentsOf: app.folderURL.appendingPathComponent("Info.plist")),
              let plist = try? PropertyListSerialization.propertyList(from: data, options: [], format: nil) as? [String: Any],
              let icons = plist["CFBundleIcons"] as? [String: Any],
              let primary = icons["CFBundlePrimaryIcon"] as? [String: Any],
              let files = primary["CFBundleIconFiles"] as? [String],
              let iconName = files.last else { return nil }

        let iconURL = app.folderURL.appendingPathComponent(iconName)
        return UIImage(contentsOfFile: iconURL.path)
    }
}
