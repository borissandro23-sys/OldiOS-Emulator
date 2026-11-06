import SwiftUI
import UIKit
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
        .sheet(isPresented: $isImporterPresented) {
            IPAFilePicker(
                onPick: { url in
                    isImporterPresented = false
                    guard url.pathExtension.lowercased() == "ipa" else {
                        ipaStatus = "Выбран не IPA-файл. Выбери файл с расширением .ipa."
                        return
                    }
                    isInspecting = true
                    ipaStatus = OldiOSCoreBridge.inspectIPA(atPath: url.path)
                    isInspecting = false
                },
                onCancel: {
                    isImporterPresented = false
                },
                onError: { message in
                    isImporterPresented = false
                    ipaStatus = "Не удалось выбрать IPA: \\(message)"
                }
            )
            .ignoresSafeArea()
        }
        .onAppear {
            coreStatus = OldiOSCoreBridge.runtimeStatus()
        }
    }
}

/// Accepts any file in the system picker so iOS Files providers do not hide
/// IPA archives whose content type is not registered consistently.
/// The selected filename is checked for the .ipa extension in ContentView.
private struct IPAFilePicker: UIViewControllerRepresentable {
    let onPick: (URL) -> Void
    let onCancel: () -> Void
    let onError: (String) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(onPick: onPick, onCancel: onCancel, onError: onError)
    }

    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: [.item],
            asCopy: true
        )
        picker.delegate = context.coordinator
        picker.allowsMultipleSelection = false
        return picker
    }

    func updateUIViewController(_ controller: UIDocumentPickerViewController, context: Context) {}

    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        private let onPick: (URL) -> Void
        private let onCancel: () -> Void
        private let onError: (String) -> Void

        init(
            onPick: @escaping (URL) -> Void,
            onCancel: @escaping () -> Void,
            onError: @escaping (String) -> Void
        ) {
            self.onPick = onPick
            self.onCancel = onCancel
            self.onError = onError
        }

        func documentPicker(
            _ controller: UIDocumentPickerViewController,
            didPickDocumentsAt urls: [URL]
        ) {
            guard let url = urls.first else {
                onError("Системный проводник не вернул выбранный файл.")
                return
            }
            onPick(url)
        }

        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
            onCancel()
        }
    }
}
