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

                let outputURL = destination.appendingPathComponent(relativePath)
                    .standardizedFileURL
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
                    VStack(alignment: .leading, spacing: 8) {
                        HStack(spacing: 12) {
                            Image(systemName: "cube.transparent")
                                .font(.system(size: 34, weight: .medium))
                                .foregroundColor(.blue)
                            VStack(alignment: .leading, spacing: 3) {
                                Text("OldiOS Emulator")
                                    .font(.title2.bold())
                                Text("ARMv6 · ARMv7 · IPA Library")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }
                        }
                        Text("Импортируйте старые iOS-приложения в отдельную библиотеку.")
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                    }
                    .padding(.vertical, 8)
                }

                Section("Библиотека · \(library.apps.count)") {
                    if library.apps.isEmpty {
                        ContentUnavailableView(
                            "Пока пусто",
                            systemImage: "square.stack.3d.up",
                            description: Text("Нажмите «Добавить IPA», чтобы импортировать приложение.")
                        )
                    } else {
                        ForEach(library.apps) { app in
                            HStack(spacing: 12) {
                                Image(systemName: "app.fill")
                                    .font(.system(size: 28))
                                    .foregroundColor(.blue)
                                    .frame(width: 42, height: 42)
                                    .background(Color.blue.opacity(0.10))
                                    .clipShape(RoundedRectangle(cornerRadius: 10))
                                VStack(alignment: .leading, spacing: 4) {
                                    Text(app.name).font(.headline)
                                    Text(app.bundleIdentifier)
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                        .lineLimit(1)
                                }
                                Spacer()
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundColor(.green)
                            }
                            .swipeActions {
                                Button(role: .destructive) {
                                    appToDelete = app
                                } label: {
                                    Label("Удалить", systemImage: "trash")
                                }
                            }
                        }
                    }
                }

                Section {
                    Button {
                        showingImporter = true
                    } label: {
                        Label("Добавить IPA", systemImage: "plus.app.fill")
                            .frame(maxWidth: .infinity, alignment: .center)
                    }
                    .font(.headline)
                }

                Section("Статус") {
                    Text(library.status)
                        .font(.footnote)
                        .foregroundColor(.secondary)
                }

                Section {
                    Label("Импорт IPA не устанавливает приложение в iOS и не запускает его.", systemImage: "info.circle")
                        .font(.footnote)
                        .foregroundColor(.secondary)
                    Text("Это первая версия библиотеки. Для запуска старых приложений ещё нужен ARM-эмулятор и реализация системных API iOS.")
                        .font(.footnote)
                        .foregroundColor(.secondary)
                }
            }
            .navigationTitle("OldiOS")
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button {
                        showingImporter = true
                    } label: {
                        Image(systemName: "plus")
                    }
                    .accessibilityLabel("Добавить IPA")
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
