import SwiftUI
import UniformTypeIdentifiers

@main
struct OldiOSApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}

struct ContentView: View {
    @State private var showingPicker = false
    @State private var isAnalyzing = false
    @State private var report: [String: Any]?
    @State private var errorText: String?

    private var faults: [String] {
        report?["faults"] as? [String] ?? []
    }

    var body: some View {
        NavigationView {
            ScrollView {
                VStack(spacing: 18) {
                    Image(systemName: "cpu")
                        .font(.system(size: 54))
                        .foregroundColor(.blue)
                    Text("OldiOS Emulator")
                        .font(.largeTitle.bold())
                    Text("IPA loader • Mach-O inspector • ARMv6 prototype")
                        .font(.footnote)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)

                    Button {
                        showingPicker = true
                    } label: {
                        Label("Выбрать IPA", systemImage: "doc.badge.plus")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(isAnalyzing)

                    if isAnalyzing {
                        ProgressView("Анализ и выполнение ARM-инструкций…")
                    }

                    if let errorText {
                        resultCard(title: "Ошибка", value: errorText, color: .red)
                    }

                    if let report, (report["ok"] as? Bool) == true {
                        resultCard(title: "Результат", value: "Анализ завершён", color: .green)
                        infoRow("Приложение", report["bundle"] as? String ?? "—")
                        infoRow("Исполняемый файл", report["executable"] as? String ?? "—")
                        infoRow("Размер Mach-O", "\(report["bytes"] ?? 0) байт")
                        infoRow("Сегменты", "\(report["segments"] ?? 0)")
                        infoRow("Проверено инструкций", "\(report["attempted"] ?? 0)")
                        infoRow("Выполнено", "\(report["completed"] ?? 0)")
                        infoRow("Ошибок инструкций", "\(report["faultCount"] ?? 0)")
                        infoRow("Итоговый PC", report["pc"] as? String ?? "—")
                        infoRow("Остановлен штатно", (report["halted"] as? Bool ?? false) ? "Да" : "Нет")
                        if report["fetchFault"] as? Bool == true {
                            Text("Остановка: следующую инструкцию нельзя было прочитать из памяти. Повторять её бесконечно приложение не будет.")
                                .font(.footnote)
                                .foregroundColor(.orange)
                        }
                        if !faults.isEmpty {
                            VStack(alignment: .leading, spacing: 8) {
                                Text("Ошибки выполнения")
                                    .font(.headline)
                                ForEach(Array(faults.enumerated()), id: \.offset) { _, fault in
                                    Text(fault)
                                        .font(.system(.caption, design: .monospaced))
                                        .frame(maxWidth: .infinity, alignment: .leading)
                                        .textSelection(.enabled)
                                }
                                if report["faultsTruncated"] as? Bool == true {
                                    Text("Показаны первые 100 ошибок.")
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                            }
                            .padding()
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(Color.orange.opacity(0.10))
                            .clipShape(RoundedRectangle(cornerRadius: 12))
                        } else {
                            resultCard(title: "Ошибки", value: "Не обнаружены", color: .green)
                        }
                    }

                    Text("Важно: сейчас приложение извлекает и проверяет совместимый 32-битный ARM Mach-O и запускает ограниченный прототип ARMv6. Это ещё не полноценная среда iOS и не запуск UIKit-приложений.")
                        .font(.footnote)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.leading)
                        .padding(.top, 8)
                }
                .padding()
            }
            .navigationTitle("OldiOS")
            .navigationBarTitleDisplayMode(.inline)
            .fileImporter(isPresented: $showingPicker, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
                switch result {
                case .success(let urls):
                    guard let url = urls.first else { return }
                    analyze(url)
                case .failure(let error):
                    errorText = error.localizedDescription
                }
            }
        }
    }

    private func analyze(_ url: URL) {
        isAnalyzing = true
        errorText = nil
        report = nil
        let didAccess = url.startAccessingSecurityScopedResource()
        defer {
            if didAccess { url.stopAccessingSecurityScopedResource() }
        }
        DispatchQueue.global(qos: .userInitiated).async {
            let result = OldIOSBridge.analyzeIPA(atPath: url.path) as? [String: Any] ?? [:]
            DispatchQueue.main.async {
                self.report = result
                if result["ok"] as? Bool != true {
                    self.errorText = result["error"] as? String ?? "Не удалось прочитать IPA."
                }
                self.isAnalyzing = false
            }
        }
    }

    private func infoRow(_ title: String, _ value: String) -> some View {
        HStack(alignment: .top) {
            Text(title).foregroundColor(.secondary)
            Spacer(minLength: 12)
            Text(value).multilineTextAlignment(.trailing).textSelection(.enabled)
        }
        .font(.subheadline)
        .padding(.vertical, 3)
    }

    private func resultCard(title: String, value: String, color: Color) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title).font(.headline)
            Text(value).font(.subheadline)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
        .background(color.opacity(0.12))
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }
}
