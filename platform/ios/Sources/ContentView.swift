import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @State private var coreStatus = "Проверяем C++-ядро…"
    @State private var ipaStatus = "IPA ещё не выбран."
    @State private var isImporterPresented = false
    @State private var isInspecting = false

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                Image(systemName: "iphone")
                    .font(.system(size: 48))
                Text("OldiOS")
                    .font(.largeTitle.bold())
                Text("ARMv6 compatibility runtime")
                    .font(.headline)

                Group {
                    Text("C++ core")
                        .font(.subheadline.bold())
                    Text(coreStatus)
                        .font(.footnote.monospaced())
                        .multilineTextAlignment(.center)
                        .foregroundStyle(.secondary)
                }

                Divider()

                Text("Проверка IPA")
                    .font(.headline)
                Button {
                    isImporterPresented = true
                } label: {
                    Label("Выбрать IPA", systemImage: "doc.badge.plus")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(isInspecting)

                if isInspecting {
                    ProgressView("Анализируем IPA…")
                }

                Text(ipaStatus)
                    .font(.footnote.monospaced())
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .textSelection(.enabled)
                    .padding(12)
                    .background(.secondary.opacity(0.10), in: RoundedRectangle(cornerRadius: 10))

                Text("Сейчас приложение извлекает и анализирует Mach-O. Выполнение старого iOS-приложения пока не поддерживается.")
                    .font(.footnote)
                    .multilineTextAlignment(.center)
                    .foregroundStyle(.orange)
            }
            .padding()
        }
        .fileImporter(
            isPresented: $isImporterPresented,
            allowedContentTypes: [.data],
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else {
                    ipaStatus = "Файл не выбран."
                    return
                }
                isInspecting = true
                let hasAccess = url.startAccessingSecurityScopedResource()
                ipaStatus = OldiOSCoreBridge.inspectIPA(atPath: url.path)
                if hasAccess {
                    url.stopAccessingSecurityScopedResource()
                }
                isInspecting = false
            case .failure(let error):
                ipaStatus = "Не удалось открыть IPA: \(error.localizedDescription)"
            }
        }
        .onAppear {
            coreStatus = OldiOSCoreBridge.runtimeStatus()
        }
    }
}
