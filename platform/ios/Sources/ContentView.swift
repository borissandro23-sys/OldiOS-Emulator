import SwiftUI

struct ContentView: View {
    @State private var coreStatus = "Проверяем C++-ядро…"

    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: "iphone")
                .font(.system(size: 48))
            Text("OldiOS")
                .font(.largeTitle.bold())
            Text("ARMv6 compatibility runtime")
                .font(.headline)
            Text("C++ core")
                .font(.subheadline.bold())
            Text(coreStatus)
                .font(.footnote.monospaced())
                .multilineTextAlignment(.center)
                .foregroundStyle(.secondary)
                .padding(.horizontal)
            Text("Проверка ядра не означает, что запуск старых iOS-приложений уже поддерживается.")
                .font(.footnote)
                .multilineTextAlignment(.center)
                .foregroundStyle(.orange)
                .padding(.horizontal)
        }
        .padding()
        .onAppear {
            coreStatus = OldiOSCoreBridge.runtimeStatus()
        }
    }
}
